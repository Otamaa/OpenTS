/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 ******************************************************************************/

#pragma once

#include "listnode.h"

/// <summary>
/// A registered .zip archive that participates in the same name-based file
/// lookup CCFileClass already performs against the mixfile system. Loading a
/// TSArchClass parses the archive's central directory into a CRC-sorted
/// index; entry payloads are decompressed lazily, on first access, and then
/// cached for the lifetime of the archive object.
///
/// Only the common single-disk, non-ZIP64 subset of the format is
/// understood: archives with more than 65534 entries, or a central
/// directory / entries beyond the 4 GiB boundary, are rejected at load time.
/// This matches the scale of a mod asset archive and keeps the parser small.
/// Compression methods other than STORE (0) and DEFLATE (8) are rejected
/// per entry.
/// </summary>
class TSArchClass : public Node<TSArchClass *>
{
	public:
		char const * Filename;			// Filename of the archive, as given to the constructor.

		explicit TSArchClass(char const * filename);
		virtual ~TSArchClass(void) override;

		/// <summary>Loads and registers the named archive.</summary>
		/// <returns>Whether the archive parsed as a valid, supported zip file.</returns>
		static bool Load(char const * filename);

		/// <summary>Drops the decompressed payload cache for the named archive.</summary>
		static bool Free(char const * filename);
		void Free(void);

		/// <summary>
		/// Searches every registered archive for an entry matching the given name.
		/// </summary>
		/// <param name="filename">The entry name to search for.</param>
		/// <param name="archive">Receives the archive the entry was found in.</param>
		/// <param name="index">Receives the entry's index within that archive.</param>
		/// <param name="size">Receives the entry's uncompressed size.</param>
		/// <returns>Whether a matching entry was found.</returns>
		static bool Offset(char const * filename, TSArchClass ** archive = 0, int * index = 0, int * size = 0);

		/// <summary>Finds and decompresses (or returns the cached decompression of) an entry by name.</summary>
		static void const * Retrieve(char const * filename, int * size = 0);

		/// <summary>Decompresses (or returns the cached decompression of) an entry by index.</summary>
		void const * Retrieve(int index, int * size = 0);

		/// <summary>Returns the entry's packed MS-DOS date/time, compatible with wwfile.h's YEAR()/MONTH()/etc.</summary>
		unsigned int Get_Date_Time(int index) const;

		int Get_Count(void) const {return(Count);}

		enum {
			METHOD_STORE = 0,
			METHOD_DEFLATE = 8,
		};

		/// <summary>One central directory entry, indexed by the CRC of its upper-cased name.</summary>
		struct Entry {
			int CRC;					// CRCEngine() value of the upper-cased entry name.
			unsigned int ZipCRC32;				// Standard (ISO 3309) CRC-32 of the uncompressed data.
			int LocalHeaderOffset;				// Offset of the local file header within the archive.
			int DataOffset;					// Offset of the compressed payload; -1 until resolved.
			int CompressedSize;
			int UncompressedSize;
			unsigned short Method;
			unsigned int DosDateTime;

			int operator < (Entry const & other) const {return(CRC < other.CRC);};
			int operator > (Entry const & other) const {return(CRC > other.CRC);};
			int operator == (Entry const & other) const {return(CRC == other.CRC);};
		};

	private:
		static TSArchClass * Finder(char const * filename);
		int Find(char const * filename) const;
		bool Resolve_Data_Offset(Entry & entry) const;
		bool Decompress(Entry const & entry, void * dest, int destlen) const;

		/*
		**	The number of entries within the archive, and the CRC-sorted index
		**	describing them.
		*/
		int Count;
		Entry * Entries;

		/*
		**	Parallel array of decompressed payloads, one slot per entry. A slot is
		**	NULL until that entry has been retrieved for the first time.
		*/
		void ** Cache;

		static List<TSArchClass *> List;

		TSArchClass(TSArchClass const &) = delete;
		TSArchClass & operator = (TSArchClass const &) = delete;
};
