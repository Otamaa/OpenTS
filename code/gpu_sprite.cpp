/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// STAGE 1 -- see gpu_sprite.h for what this does and does not cover.

#include "gpu_sprite.h"

#include "bgfxbackend.h"
#include "palette.h"
#include "rgb.h"
#include "shapeset.h"

#include <map>
#include <vector>


void GPUSprite_Build_Palette(PaletteClass const & source, GPUSpritePalette & out_palette)
{
	for (int i = 0; i < 256; i++) {
		RGBClass const & c = source.Get_Color(i);
		out_palette.Entries[i] = 0xFF000000u | ((unsigned int)c.Blue << 16) | ((unsigned int)c.Green << 8) | (unsigned int)c.Red;
	}
}


namespace {

	struct AtlasPage
	{
		BackendTextureHandle Handle = BACKEND_INVALID_TEXTURE;
		int Width = 0;
		int Height = 0;

		// Simple shelf packer: current shelf's baseline Y and height, and the
		// next free X on that shelf. Good enough for the many-small-similar-
		// sized-frames case a shape set typically is; not space-optimal for
		// wildly mixed sizes. Revisit with a real bin packer if that matters.
		int ShelfY = 0;
		int ShelfHeight = 0;
		int CursorX = 0;
	};

	const int ATLAS_PAGE_SIZE = 2048;

	std::vector<AtlasPage> _Pages;

	struct FrameKey
	{
		ShapeSet const * Shapefile;
		int ShapeNum;

		bool operator<(FrameKey const & rhs) const
		{
			if (Shapefile != rhs.Shapefile) {
				return(Shapefile < rhs.Shapefile);
			}
			return(ShapeNum < rhs.ShapeNum);
		}
	};

	std::map<FrameKey, GPUSpriteHandle> _FrameLookup;

	// Shapesets already decided one way or the other, so repeat calls to
	// GPUSprite_Try_Atlas_Shapeset() are cheap and idempotent.
	enum class ShapesetStatus { Atlased, Refused };
	std::map<ShapeSet const *, ShapesetStatus> _ShapesetStatus;

	bool _Initialized = false;

	std::vector<BackendSpriteInstance> _PendingByPage[64];   // Indexed by page index; grown lazily below via Ensure_Page_Slot.


	// Finds space for a width x height region across existing pages, creating
	// a new page if none has room. Returns page index, or -1 on failure
	// (region larger than a page, or backend page creation failed).
	int Allocate_Region(int width, int height, int & out_x, int & out_y)
	{
		if (width > ATLAS_PAGE_SIZE || height > ATLAS_PAGE_SIZE) {
			return(-1);
		}

		for (size_t i = 0; i < _Pages.size(); i++) {
			AtlasPage & page = _Pages[i];

			if (page.CursorX + width > page.Width) {
				// This shelf is full; start a new shelf below the tallest frame
				// placed on the current one.
				page.ShelfY += page.ShelfHeight;
				page.CursorX = 0;
				page.ShelfHeight = 0;
			}

			if (page.ShelfY + height <= page.Height && page.CursorX + width <= page.Width) {
				out_x = page.CursorX;
				out_y = page.ShelfY;
				page.CursorX += width;
				if (height > page.ShelfHeight) {
					page.ShelfHeight = height;
				}
				return((int)i);
			}
		}

		// No existing page has room; create a new one.
		BackendTextureHandle handle = Backend_Create_Atlas_Page(ATLAS_PAGE_SIZE, ATLAS_PAGE_SIZE);
		if (handle == BACKEND_INVALID_TEXTURE) {
			return(-1);
		}

		if (_Pages.size() >= (sizeof(_PendingByPage) / sizeof(_PendingByPage[0]))) {
			// MISSING: raise this ceiling (or make _PendingByPage dynamic) if a
			// real workload needs more than 64 atlas pages of queued sprites
			// in a single frame. Fine for now -- refuse rather than overrun.
			Backend_Destroy_Atlas_Page(handle);
			return(-1);
		}

		AtlasPage page;
		page.Handle = handle;
		page.Width = ATLAS_PAGE_SIZE;
		page.Height = ATLAS_PAGE_SIZE;
		page.CursorX = width;
		page.ShelfY = 0;
		page.ShelfHeight = height;
		_Pages.push_back(page);

		out_x = 0;
		out_y = 0;
		return((int)_Pages.size() - 1);
	}


	// Decodes one RLE-compressed frame into a flat rect.Width * rect.Height
	// RGBA8 buffer. This is a direct port of the scanline format read by
	// RLE_Blit (blit.cpp) and RLEBlitTransXlat (rlerle.h), verified against
	// both:
	//   - Each row is prefixed with a little-endian uint16 giving that row's
	//     total record length in bytes, INCLUDING the 2-byte prefix itself
	//     (blit.cpp advances `sbuffer += *(unsigned short*)sbuffer` between
	//     rows, and reads pixel data starting at `((unsigned short*)sbuffer) + 1`,
	//     i.e. immediately after that prefix).
	//   - Within a row, a 0x00 byte means "the next byte is a count of
	//     transparent pixels to skip"; any other byte is a literal palette
	//     index (rlerle.h, e.g. RLEBlitTransXlat::Blit).
	// No leading-pixel skip / clipping is needed here since the whole frame
	// is always decoded in full for the atlas.
	void Decode_RLE_Frame(unsigned char const * data, int width, int height, GPUSpritePalette const & palette, unsigned int * out_rgba)
	{
		unsigned char const * row = data;

		for (int y = 0; y < height; y++) {
			unsigned short recordlength = *(unsigned short const *)row;
			unsigned char const * pixels = row + sizeof(unsigned short);
			unsigned int * dstrow = out_rgba + (size_t)y * width;

			int x = 0;
			while (x < width) {
				unsigned char value = *pixels++;
				if (value == 0) {
					unsigned char skip = *pixels++;
					int count = skip;
					if (x + count > width) {
						count = width - x;   // Defensive clamp; a well-formed asset never needs this.
					}
					for (int i = 0; i < count; i++) {
						dstrow[x++] = 0u;
					}
				} else {
					dstrow[x++] = palette.Entries[value];
				}
			}

			row += recordlength;
		}
	}


	// Converts one frame's raw pixel data (RLE or plain 8bpp indexed) to
	// RGBA8 using the supplied palette, and uploads it into the atlas.
	// Returns false only on a real failure: no data/empty rect, or the atlas
	// backend couldn't allocate/upload the region.
	bool Atlas_Decode_Shape(ShapeSet const * shapefile, int shapenum, GPUSpritePalette const & palette, GPUSpriteHandle & out_handle)
	{
		Rect rect = shapefile->Get_Rect(shapenum);
		void const * data = shapefile->Get_Data(shapenum);
		if (data == NULL || rect.Width <= 0 || rect.Height <= 0) {
			return(false);
		}

		int x = 0, y = 0;
		int pageindex = Allocate_Region(rect.Width, rect.Height, x, y);
		if (pageindex < 0) {
			return(false);
		}

		std::vector<unsigned int> rgba((size_t)rect.Width * rect.Height);

		if (shapefile->Is_RLE_Compressed(shapenum)) {
			Decode_RLE_Frame((unsigned char const *)data, rect.Width, rect.Height, palette, rgba.data());
		} else {
			// Plain 8bpp indexed, tightly packed rows (BSurface::Stride() ==
			// Get_Width() * BytesPerPixel(), bsurface.h -- confirmed, see
			// gpu_sprite.h). Index 0 is transparent, same convention as the
			// RLE case (BlitTransXlat, blitblit.h).
			unsigned char const * source = (unsigned char const *)data;
			for (int row = 0; row < rect.Height; row++) {
				unsigned char const * srcrow = source + (size_t)row * rect.Width;
				unsigned int * dstrow = rgba.data() + (size_t)row * rect.Width;
				for (int col = 0; col < rect.Width; col++) {
					unsigned char index = srcrow[col];
					dstrow[col] = (index == 0) ? 0u : palette.Entries[index];
				}
			}
		}

		if (!Backend_Update_Atlas_Region(_Pages[pageindex].Handle, x, y, rect.Width, rect.Height, rgba.data())) {
			return(false);
		}

		out_handle.PageIndex = (unsigned int)pageindex;
		out_handle.UVRect = Rect(x, y, rect.Width, rect.Height);
		return(true);
	}

}   // namespace


bool GPUSprite_Init(void)
{
	_Pages.clear();
	_FrameLookup.clear();
	_ShapesetStatus.clear();
	_Initialized = true;
	return(true);
}


void GPUSprite_Shutdown(void)
{
	for (AtlasPage & page : _Pages) {
		Backend_Destroy_Atlas_Page(page.Handle);
	}
	_Pages.clear();
	_FrameLookup.clear();
	_ShapesetStatus.clear();
	_Initialized = false;
}


bool GPUSprite_Try_Atlas_Shapeset(ShapeSet const * shapefile, GPUSpritePalette const & palette)
{
	if (!_Initialized || shapefile == NULL) {
		return(false);
	}

	auto known = _ShapesetStatus.find(shapefile);
	if (known != _ShapesetStatus.end()) {
		return(known->second == ShapesetStatus::Atlased);
	}

	int count = shapefile->Get_Count();
	std::vector<std::pair<int, GPUSpriteHandle>> decoded;
	decoded.reserve((size_t)count);

	for (int shapenum = 0; shapenum < count; shapenum++) {
		GPUSpriteHandle handle;
		if (!Atlas_Decode_Shape(shapefile, shapenum, palette, handle)) {
			// Real failure only now (bad frame data, or the atlas backend
			// couldn't allocate/upload) -- both RLE and non-RLE frames are
			// handled above. Refuse the whole shapeset rather than leaving it
			// half-atlased, so GPUSprite_Lookup's contract stays simple.
			_ShapesetStatus[shapefile] = ShapesetStatus::Refused;
			return(false);
		}
		decoded.emplace_back(shapenum, handle);
	}

	for (auto const & entry : decoded) {
		_FrameLookup[FrameKey{shapefile, entry.first}] = entry.second;
	}
	_ShapesetStatus[shapefile] = ShapesetStatus::Atlased;
	return(true);
}


GPUSpriteHandle GPUSprite_Lookup(ShapeSet const * shapefile, int shapenum)
{
	auto it = _FrameLookup.find(FrameKey{shapefile, shapenum});
	if (it != _FrameLookup.end()) {
		return(it->second);
	}
	return(GPUSpriteHandle{});
}


void GPUSprite_Submit(GPUSpriteHandle const & handle, Rect const & dest, unsigned int tint_rgba)
{
	if (!_Initialized || !handle.Is_Valid() || handle.PageIndex >= _Pages.size()) {
		return;
	}

	AtlasPage const & page = _Pages[handle.PageIndex];

	BackendSpriteInstance instance;
	instance.DestX = (float)dest.X;
	instance.DestY = (float)dest.Y;
	instance.DestWidth = (float)dest.Width;
	instance.DestHeight = (float)dest.Height;
	instance.U0 = (float)handle.UVRect.X / (float)page.Width;
	instance.V0 = (float)handle.UVRect.Y / (float)page.Height;
	instance.U1 = (float)(handle.UVRect.X + handle.UVRect.Width) / (float)page.Width;
	instance.V1 = (float)(handle.UVRect.Y + handle.UVRect.Height) / (float)page.Height;
	instance.TintRGBA = tint_rgba;

	_PendingByPage[handle.PageIndex].push_back(instance);
}


void GPUSprite_Flush(void)
{
	if (!_Initialized) {
		return;
	}

	Backend_Begin_Sprite_Frame();

	for (size_t i = 0; i < _Pages.size(); i++) {
		if (_PendingByPage[i].empty()) {
			continue;
		}
		Backend_Submit_Sprites(_Pages[i].Handle, _PendingByPage[i].data(), (int)_PendingByPage[i].size());
		_PendingByPage[i].clear();
	}

	Backend_End_Sprite_Frame();
}
