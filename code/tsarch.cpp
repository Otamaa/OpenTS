/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 ******************************************************************************/

#include "always.h"

#include "tsarch.h"

#include "bsearch.h"
#include "ccfile.h"
#include "crc.h"
#include "wwfile.h"

#include "puff.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>


/*
**	This is the list of every archive currently registered with the tsarch
**	system. TSArchClass::Offset() sweeps this list the same way
**	MixFileClass::Offset() sweeps the mixfile list.
*/
List<TSArchClass *> TSArchClass::List;


namespace {

	/*
	**	On-disk zip record layouts. These mirror APPNOTE.TXT exactly and are
	**	packed to match; they are read straight off disk into these structs.
	*/
	#pragma pack(push, 1)

	struct EOCDRecord {
		unsigned int Signature;			// 0x06054B50
		unsigned short DiskNumber;
		unsigned short CDStartDisk;
		unsigned short EntriesOnDisk;
		unsigned short TotalEntries;
		unsigned int CDSize;
		unsigned int CDOffset;
		unsigned short CommentLength;
	};

	struct CDFileHeader {
		unsigned int Signature;			// 0x02014B50
		unsigned short VersionMadeBy;
		unsigned short VersionNeeded;
		unsigned short Flags;
		unsigned short Method;
		unsigned short ModTime;
		unsigned short ModDate;
		unsigned int ZipCRC32;
		unsigned int CompressedSize;
		unsigned int UncompressedSize;
		unsigned short NameLength;
		unsigned short ExtraLength;
		unsigned short CommentLength;
		unsigned short DiskStart;
		unsigned short InternalAttrs;
		unsigned int ExternalAttrs;
		unsigned int LocalHeaderOffset;
	};

	struct LocalFileHeader {
		unsigned int Signature;			// 0x04034B50
		unsigned short VersionNeeded;
		unsigned short Flags;
		unsigned short Method;
		unsigned short ModTime;
		unsigned short ModDate;
		unsigned int ZipCRC32;
		unsigned int CompressedSize;
		unsigned int UncompressedSize;
		unsigned short NameLength;
		unsigned short ExtraLength;
	};

	#pragma pack(pop)

	constexpr unsigned int SIG_EOCD = 0x06054B50u;
	constexpr unsigned int SIG_CENTRAL = 0x02014B50u;
	constexpr unsigned int SIG_LOCAL = 0x04034B50u;
	constexpr int EOCD_FIXED_SIZE = 22;
	constexpr int LOCAL_FIXED_SIZE = 30;
	constexpr int MAX_COMMENT = 65535;


	/// <summary>
	/// Computes the CRCEngine() index key for a zip entry name. Names are
	/// matched case-insensitively, the same way mixfile entries are, so this
	/// hashes an upper-cased copy of the name rather than mutating the
	/// caller's string in place.
	/// </summary>
	int Name_CRC(char const * name)
	{
		char upper[_MAX_PATH];
		size_t len = strlen(name);
		if (len >= sizeof(upper)) {
			len = sizeof(upper) - 1;
		}
		memcpy(upper, name, len);
		upper[len] = '\0';
		strupr(upper);
		return CRCEngine()(upper, (int)len);
	}


	/// <summary>
	/// Standard (ISO 3309 / PKZIP) CRC-32, used only to verify a decompressed
	/// entry against the checksum stored in the archive. This is unrelated to
	/// the engine's own CRCEngine, which uses a different, faster algorithm
	/// and is not compatible with zip's checksum.
	/// </summary>
	unsigned int Zip_CRC32(void const * data, int length, unsigned int crc = 0)
	{
		static unsigned int table[256];
		static bool built = false;

		if (!built) {
			for (unsigned int i = 0; i < 256; i++) {
				unsigned int c = i;
				for (int bit = 0; bit < 8; bit++) {
					c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
				}
				table[i] = c;
			}
			built = true;
		}

		unsigned char const * bytes = (unsigned char const *)data;
		crc = crc ^ 0xFFFFFFFFu;
		for (int i = 0; i < length; i++) {
			crc = table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
		}
		return crc ^ 0xFFFFFFFFu;
	}


	/// <summary>
	/// Locates the end-of-central-directory record by scanning backward from
	/// the end of the file for its signature, since a trailing archive
	/// comment of unknown length can precede it.
	/// </summary>
	/// <returns>Whether a valid, non-ZIP64 EOCD record was found.</returns>
	bool Find_EOCD(FileClass & file, EOCDRecord & eocd, int & eocd_offset)
	{
		int const filesize = file.Size();
		if (filesize < EOCD_FIXED_SIZE) {
			return false;
		}

		int const window = std::min(filesize, EOCD_FIXED_SIZE + MAX_COMMENT);
		int const start = filesize - window;

		char * buffer = new char [window];
		file.Seek(start, SEEK_SET);
		int got = file.Read(buffer, window);

		bool found = false;
		if (got == window) {
			for (int i = window - EOCD_FIXED_SIZE; i >= 0; i--) {
				unsigned int const sig = *(unsigned int const *)(buffer + i);
				if (sig == SIG_EOCD) {
					memcpy(&eocd, buffer + i, sizeof(eocd));
					eocd_offset = start + i;
					found = true;
					break;
				}
			}
		}

		delete [] buffer;
		return found;
	}

}	// namespace


/// <summary>
/// Constructs an archive object, immediately parsing the named zip file's
/// central directory into a CRC-sorted index. If parsing fails for any
/// reason -- the file is missing, is not a supported zip, or uses a feature
/// this reader does not understand -- the object is left with zero entries
/// and is not registered; TSArchClass::Load() reports this as failure.
/// </summary>
TSArchClass::TSArchClass(char const * filename) :
	Filename(NULL),
	Count(0),
	Entries(NULL),
	Cache(NULL)
{
	CCFileClass file(filename);
	Filename = strdup(file.File_Name());

	if (!file.Is_Available()) {
		return;
	}
	file.Open(FileClass::READ);

	EOCDRecord eocd;
	int eocd_offset = 0;
	if (!Find_EOCD(file, eocd, eocd_offset)) {
		file.Close();
		return;
	}

	/*
	**	Reject ZIP64 archives (more entries or a larger central directory than
	**	the classic record can express). A modding archive has no reason to
	**	approach either limit; supporting the ZIP64 extension records is not
	**	worth the added parser complexity.
	*/
	if (eocd.TotalEntries == 0xFFFF || eocd.CDOffset == 0xFFFFFFFFu) {
		file.Close();
		return;
	}

	Count = eocd.TotalEntries;
	if (Count == 0) {
		file.Close();
		return;
	}

	Entries = new Entry [Count];
	Cache = new void * [Count];
	memset(Cache, 0, sizeof(void *) * Count);

	file.Seek(eocd.CDOffset, SEEK_SET);

	int valid = 0;
	for (int i = 0; i < Count; i++) {
		CDFileHeader header;
		if (file.Read(&header, sizeof(header)) != sizeof(header) || header.Signature != SIG_CENTRAL) {
			break;
		}

		char name[_MAX_PATH];
		int namelen = header.NameLength;
		if (namelen >= (int)sizeof(name)) {
			namelen = sizeof(name) - 1;
		}
		if (namelen > 0) {
			file.Read(name, namelen);
		}
		name[namelen] = '\0';

		/*
		**	Skip whatever of the name this reader didn't have room for, plus
		**	the extra field and comment; none of these are needed further.
		*/
		int skip = (int)header.NameLength - namelen + header.ExtraLength + header.CommentLength;
		if (skip > 0) {
			file.Seek(skip, SEEK_CUR);
		}

		/*
		**	Directory entries carry no data of their own; a trailing '/' (or
		**	zero size with the directory attribute bit) marks one.
		*/
		bool const is_directory = (namelen > 0 && name[namelen - 1] == '/');
		if (is_directory || namelen == 0) {
			continue;
		}

		if (header.Method != TSArchClass::METHOD_STORE && header.Method != TSArchClass::METHOD_DEFLATE) {

			/*
			**	EXTENSION: an entry compressed with an unsupported method
			**	(anything but store or deflate) is simply omitted from the
			**	index rather than failing the whole archive.
			*/
			continue;
		}

		Entry & entry = Entries[valid];
		entry.CRC = Name_CRC(name);
		entry.ZipCRC32 = header.ZipCRC32;
		entry.LocalHeaderOffset = (int)header.LocalHeaderOffset;
		entry.DataOffset = -1;
		entry.CompressedSize = (int)header.CompressedSize;
		entry.UncompressedSize = (int)header.UncompressedSize;
		entry.Method = header.Method;
		entry.DosDateTime = ((unsigned int)header.ModDate << 16) | (unsigned int)header.ModTime;
		valid++;
	}
	file.Close();

	if (valid == 0) {
		delete [] Entries;
		delete [] Cache;
		Entries = NULL;
		Cache = NULL;
		Count = 0;
		return;
	}
	Count = valid;

	std::sort(Entries, Entries + Count, [](Entry const & a, Entry const & b) {return(a.CRC < b.CRC);});

	List.Add_Tail(this);
}


TSArchClass::~TSArchClass(void)
{
	if (Filename) {
		free((char *)Filename);
	}
	Free();
	if (Entries != NULL) {
		delete [] Entries;
		Entries = NULL;
	}
	if (Cache != NULL) {
		delete [] Cache;
		Cache = NULL;
	}
	Unlink();
}


/// <summary>Uncaches every decompressed payload belonging to this archive; the index itself is kept.</summary>
void TSArchClass::Free(void)
{
	if (Cache == NULL) {
		return;
	}
	for (int i = 0; i < Count; i++) {
		if (Cache[i] != NULL) {
			delete [] (char *)Cache[i];
			Cache[i] = NULL;
		}
	}
}


bool TSArchClass::Free(char const * filename)
{
	TSArchClass * ptr = Finder(filename);
	if (ptr) {
		ptr->Free();
		return true;
	}
	return false;
}


bool TSArchClass::Load(char const * filename)
{
	TSArchClass * archive = new TSArchClass(filename);
	if (archive->Get_Count() > 0) {
		return true;
	}
	delete archive;
	return false;
}


/// <summary>Matches an archive in the registry by filename, ignoring any attached path.</summary>
TSArchClass * TSArchClass::Finder(char const * filename)
{
	TSArchClass * ptr = List.First();
	while (ptr->Is_Valid()) {
		char path[_MAX_PATH];
		char name[_MAX_FNAME];
		char ext[_MAX_EXT];

		_splitpath(ptr->Filename, NULL, NULL, name, ext);
		_makepath(path, NULL, NULL, name, ext);

		if (stricmp(path, filename) == 0) {
			return ptr;
		}
		ptr = ptr->Next();
	}
	return NULL;
}


/// <summary>Binary-searches this archive's own index for an entry name.</summary>
/// <returns>The entry's index, or -1 if not present in this archive.</returns>
int TSArchClass::Find(char const * filename) const
{
	Entry key;
	key.CRC = Name_CRC(filename);

	Entry * found = Binary_Search<Entry>(Entries, Count, key);
	if (found == NULL) {
		return -1;
	}
	return (int)(found - Entries);
}


bool TSArchClass::Offset(char const * filename, TSArchClass ** archive, int * index, int * size)
{
	if (filename == NULL) {
		assert(filename != NULL);
		return false;
	}

	TSArchClass * ptr = List.First();
	while (ptr->Is_Valid()) {
		int const found = ptr->Find(filename);
		if (found >= 0) {
			if (archive != NULL) *archive = ptr;
			if (index != NULL) *index = found;
			if (size != NULL) *size = ptr->Entries[found].UncompressedSize;
			return true;
		}
		ptr = ptr->Next();
	}
	return false;
}


void const * TSArchClass::Retrieve(char const * filename, int * size)
{
	TSArchClass * archive = NULL;
	int index = -1;
	if (!Offset(filename, &archive, &index)) {
		return NULL;
	}
	return archive->Retrieve(index, size);
}


/// <summary>
/// Reads the entry's local file header (whose name/extra field lengths can,
/// in principle, differ from the central directory's copy) to find where the
/// compressed payload actually starts, and caches that offset on the entry.
/// </summary>
bool TSArchClass::Resolve_Data_Offset(Entry & entry) const
{
	if (entry.DataOffset >= 0) {
		return true;
	}

	CCFileClass file(Filename);
	file.Open(FileClass::READ);
	file.Seek(entry.LocalHeaderOffset, SEEK_SET);

	LocalFileHeader local;
	bool ok = false;
	if (file.Read(&local, sizeof(local)) == sizeof(local) && local.Signature == SIG_LOCAL) {
		entry.DataOffset = entry.LocalHeaderOffset + LOCAL_FIXED_SIZE + local.NameLength + local.ExtraLength;
		ok = true;
	}
	file.Close();
	return ok;
}


/// <summary>Reads and, if necessary, inflates an entry's compressed payload into a freshly allocated buffer.</summary>
bool TSArchClass::Decompress(Entry const & entry, void * dest, int destlen) const
{
	if (entry.CompressedSize == 0 && destlen == 0) {
		return true;
	}

	char * compressed = new char [entry.CompressedSize];

	CCFileClass file(Filename);
	file.Open(FileClass::READ);
	file.Seek(entry.DataOffset, SEEK_SET);
	int const got = file.Read(compressed, entry.CompressedSize);
	file.Close();

	bool ok = false;
	if (got == entry.CompressedSize) {
		if (entry.Method == METHOD_STORE) {
			ok = (destlen == entry.CompressedSize);
			if (ok) {
				memcpy(dest, compressed, destlen);
			}
		} else {
			assert(entry.Method == METHOD_DEFLATE);
			unsigned long destlen_ul = (unsigned long)destlen;
			unsigned long srclen_ul = (unsigned long)entry.CompressedSize;
			int const result = puff((unsigned char *)dest, &destlen_ul, (unsigned char const *)compressed, &srclen_ul);
			ok = (result == 0 && (int)destlen_ul == destlen);
		}
	}
	delete [] compressed;

	if (ok && Zip_CRC32(dest, destlen) != entry.ZipCRC32) {

		/*
		**	a mismatch here means either a corrupt/truncated archive
		**	or a bug in this reader; treat it as failure rather than serving
		**	bad data.
		*/
		ok = false;
	}

	return ok;
}


void const * TSArchClass::Retrieve(int index, int * size)
{
	if (index < 0 || index >= Count) {
		return NULL;
	}

	Entry & entry = Entries[index];
	if (size != NULL) {
		*size = entry.UncompressedSize;
	}

	if (Cache[index] != NULL) {
		return Cache[index];
	}

	if (!Resolve_Data_Offset(entry)) {
		return NULL;
	}

	char * dest = new char [entry.UncompressedSize];
	if (!Decompress(entry, dest, entry.UncompressedSize)) {
		delete [] dest;
		return NULL;
	}

	Cache[index] = dest;
	return dest;
}


unsigned int TSArchClass::Get_Date_Time(int index) const
{
	if (index < 0 || index >= Count) {
		return 0;
	}
	return Entries[index].DosDateTime;
}
