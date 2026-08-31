/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// STAGE 1 -- NOT WIRED TO GAMEPLAY YET.
//
// This is an *additive*, opt-in GPU sprite path that runs alongside the
// existing CPU Draw_Shape()/Bit_Blit()/RLE_Blit() pipeline (draw.cpp,
// blit.cpp). It does not replace anything and is inert unless explicitly
// enabled and fed shapes.
//
// WHAT THIS STAGE DOES:
//   - Builds a GPU texture atlas from ShapeSet frame data, decoded once at
//     load time (not per-draw), for BOTH RLE and non-RLE frames. The RLE
//     decode in gpu_sprite.cpp is a direct, verified port of the format
//     read by RLE_Blit (blit.cpp) / RLEBlitTransXlat (rlerle.h): each
//     scanline is prefixed with a little-endian uint16 record length, and
//     within a scanline a 0x00 byte means "skip N transparent pixels" (N =
//     next byte) while any other byte is a literal palette index.
//   - Submits instanced, alpha-blended textured quads through bgfx for any
//     shape that got atlased.
//
// WHAT THIS STAGE DELIBERATELY DOES NOT DO, and why that's a real
// architectural boundary rather than an oversight:
//   - No per-draw state is baked in: house-color remap, the alpha-lighting
//     LUT (RLEBlitTransXlatAlpha*), and z-buffer test/write
//     (RLEBlitTransXlatZRead/Write) are all supplied at *blit time* in the
//     CPU path, not stored in the shape asset -- see their `TranslateTable`
//     /`alpha_level`/`z_min`/`z_buff` parameters in rlerle.h. An atlas baked
//     once at load time structurally cannot carry per-draw dynamic state;
//     that requires either re-baking per remap/lighting combination (cheap
//     for remap, not for continuous lighting) or moving the translate-table
//     lookup into the shader (the real fix -- store raw palette indices in
//     the atlas instead of resolved RGBA, sample a small remap-LUT texture
//     in the fragment shader). This scaffold stores resolved RGBA, so it is
//     unlit, unremapped, and NOT what should back actual house-colored
//     unit sprites -- fine for a first correctness/perf check, not for
//     shipping gameplay use.
//   - No z-depth compositing against DepthBuffer: sprites drawn through
//     this path composite as one flat overlay on top of the whole CPU
//     frame (see bgfxbackend.cpp's VIEW_SPRITES target), not interleaved
//     by depth with CPU-drawn isometric objects. This needs a real GPU
//     depth buffer shared with (or reconciled against) DepthBuffer before
//     GPU sprites can correctly occlude/be-occluded-by CPU-drawn ones.
//   - Predator/ghost/cloak-style special blit flags are not handled.
//
// Given the above, GPUSprite_Try_Atlas_Shapeset() atlases a shapeset's
// frames unconditionally (RLE or not) but everything drawn through this
// path today is flat-lit and flat-composited. Treat this as validated
// plumbing for "can we get pixels from a ShapeSet onto the GPU and back on
// screen correctly", not as a drop-in replacement for Draw_Shape() yet.

#pragma once

#include "rect.h"

class ShapeSet;
class PaletteClass;
struct Point2D;


// The atlas decode step converts each shape's raw 8bpp indexed pixel data
// into RGBA8 for GPU upload. Two facts this relies on, both confirmed
// against source rather than assumed:
//
//   - BSurface::Stride() == Get_Width() * BytesPerPixel() unconditionally
//     (bsurface.h) -- shape pixel rows are always tightly packed, no
//     separate pitch exists.
//   - Palette index 0 is always the transparent sentinel: BlitTransXlat
//     (blitblit.h) treats `color == 0` as transparent for non-RLE frames,
//     and every RLEBlitTransXlat* variant (rlerle.h) treats a 0x00 byte in
//     the compressed stream as the run-length-skip escape, so index 0 can
//     never appear as a literal drawn pixel in either format.
struct GPUSpritePalette
{
	unsigned int Entries[256];   // RGBA8, index-matched to the shape's 8bpp data. Entries[0] is ignored (see above).
};

// Fills a GPUSpritePalette from the game's actual PaletteClass (palette.h),
// so callers don't have to hand-build the table themselves.
void GPUSprite_Build_Palette(PaletteClass const & source, GPUSpritePalette & out_palette);


// Opaque handle into the GPU atlas. INVALID if the shape has not been
// (or cannot yet be) atlased -- caller must fall back to CPU Draw_Shape().
struct GPUSpriteHandle
{
	unsigned int PageIndex = 0xFFFFFFFFu;
	Rect         UVRect;               // Pixel rect within the atlas page.

	bool Is_Valid(void) const { return PageIndex != 0xFFFFFFFFu; }
};


// Must be called once after Backend_Init() (video.cpp) before any atlasing
// or submission calls are made.
bool GPUSprite_Init(void);
void GPUSprite_Shutdown(void);

// Ensures every frame of the given ShapeSet is available in the atlas,
// decoding and uploading any frames not already present (RLE and non-RLE
// both handled -- see header comment above for what is NOT baked in).
// Safe to call repeatedly; already-atlased shapesets are a no-op lookup.
// Returns false only on a real failure (backend/atlas allocation failure),
// not because of frame format.
bool GPUSprite_Try_Atlas_Shapeset(ShapeSet const * shapefile, GPUSpritePalette const & palette);

// Looks up an already-atlased frame. Returns an invalid handle if the
// shapeset/frame was never atlased or atlasing was refused for it.
GPUSpriteHandle GPUSprite_Lookup(ShapeSet const * shapefile, int shapenum);

// Queues an instanced textured-quad draw for the current frame. Batches
// internally; nothing is actually submitted to bgfx until GPUSprite_Flush().
// dest is in the same destination-surface pixel space Draw_Shape() uses.
void GPUSprite_Submit(GPUSpriteHandle const & handle, Rect const & dest, unsigned int tint_rgba = 0xFFFFFFFFu);

// Flushes all queued instances for this frame as batched bgfx draw calls,
// grouped by atlas page to minimize state changes. Call once per frame,
// after all GPUSprite_Submit() calls, before the frame is presented via
// Backend_Present().
void GPUSprite_Flush(void);
