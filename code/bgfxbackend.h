/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The renderer's private interface. Only bgfxbackend.cpp includes bgfx, so no bgfx type
// appears here and no other translation unit needs the library's headers or its build
// settings. video.cpp drives the presenter; anim.cpp additionally queues GPU overlay
// quads (see Backend_Queue_Overlay_Quad below) for anims marked GPUOverlay in art.ini.

#pragma once

#include "nativewindow.hh"


enum BackendRenderer {
	BACKEND_RENDERER_AUTO,
	BACKEND_RENDERER_D3D11,
	BACKEND_RENDERER_D3D12,
	BACKEND_RENDERER_VULKAN,
	BACKEND_RENDERER_OPENGL,
};


enum BackendScaleMode {
	BACKEND_SCALE_NEAREST,
	BACKEND_SCALE_LINEAR,
	BACKEND_SCALE_PIXELART,
};


// EXTENSION: full-screen GPU post effect applied to the frame before it is scaled and
// presented. None costs the extra passes nothing; they are simply skipped.
enum BackendPostFX {
	BACKEND_POSTFX_NONE,
	BACKEND_POSTFX_BLOOM,
};


// Drawable sizes are physical pixel dimensions supplied by the application shell.
bool Backend_Init(NativeWindow const & window, int drawablewidth, int drawableheight, BackendRenderer renderer, bool vsync);
void Backend_Shutdown(void);

bool Backend_Set_Frame_Size(int width, int height);
void Backend_On_Resize(int drawablewidth, int drawableheight);

// Uploads the frame and presents it. The pixels are 16 bit 565 and stay owned by the
// caller; they are consumed before this returns.
//
// EXTENSION: postfx selects a full-screen GPU effect run on the frame before it is
// scaled into the window. bloomthreshold is the luma above which a pixel contributes to
// the glow (0..1); bloomintensity scales how strongly the blurred glow is added back in.
// Both are ignored when postfx is BACKEND_POSTFX_NONE.
void Backend_Present(void const * pixels, int pitch, int destx, int desty, int destwidth, int destheight, BackendScaleMode mode,
	BackendPostFX postfx, float bloomthreshold, float bloomintensity);

char const * Backend_Renderer_Name(void);


// EXTENSION: a sentinel 565 color a caller of Backend_Queue_Overlay_Quad fills its source
// surface's background with before drawing into it. Pure magenta, since real sprite art
// essentially never uses it; any source pixel that still holds this exact value when the
// quad is queued is treated as fully transparent rather than an actual color.
inline constexpr unsigned short BACKEND_OVERLAY_KEY_COLOR_565 = 0xF81F;

// Queues one hardware-composited quad, drawn in the frame's own pixel space after the
// whole software-rendered frame and after every full-screen post effect. Meant for anims
// marked GPUOverlay (see AnimTypeClass::IsGPUOverlay), which always composite after every
// software-drawn object regardless of draw order -- but the quad is still depth- and
// ambient-tested against the same snapshots a GPU beam's own color pass uses, so it is
// occluded by terrain and units the same way a software-drawn sprite already would be;
// "GPUOverlay" only changes when in the draw order this quad gets composited in, not
// whether it can be hidden behind something.
//
// pixels/pitch describe a 16 bit 565 image, width x height in size, whose background was
// filled with keycolor565 before anything was drawn into it. The source is copied to the
// GPU before this returns, so the caller does not need to keep it alive afterward. destx/
// desty/destwidth/destheight place and (if they differ from width/height) scale it within
// the frame. depth is the anim's own depth in the same raw units Backend_Queue_GPU_Beam's
// start/end depth use (before that function's own BEAM_DEPTH_SCALE normalization) -- the
// quad is dropped, not drawn unoccluded, on a frame with no depth snapshot available; see
// Backend_Has_Queued_Overlay_Quads for how a caller can make sure one gets taken. The
// whole queue is drawn once and cleared at the end of each Backend_Present.
void Backend_Queue_Overlay_Quad(void const * pixels, int pitch, int width, int height, int destx, int desty, int destwidth, int destheight, unsigned short keycolor565, float depth);

// EXTENSION: lets a caller decide, before it knows anything about GPU beams itself,
// whether a depth/ambient snapshot is worth taking this frame -- true once at least one
// overlay quad has been queued (which happens well before LaserDrawClass::Draw_All runs,
// since anims draw as part of Draw_Objects), even if no GPU beam exists this frame.
bool Backend_Has_Queued_Overlay_Quads(void);


// EXTENSION: uploads a snapshot of the software renderer's own depth buffer, taken at the
// same point in the frame the depth-tested software line drawer already reads it from, so
// a GPU beam queued this frame (Backend_Queue_GPU_Beam) can be occluded the same way a
// software-drawn one already is. depths/pitch describe a 16 bit source in the same units
// and comparison sense DSurface's own depth buffer uses (smaller is nearer); originx/
// originy are where the snapshot's own pixel (0,0) sits in the frame's pixel space. Safe
// to call every frame; skip it entirely on a frame with no GPU beams queued.
void Backend_Upload_Depth_Snapshot(void const * depths, int pitch, int width, int height, int originx, int originy);

// EXTENSION: uploads a snapshot of the software renderer's own ambient/shroud lighting
// buffer (AlphaBuffer), the same way and from the same point in the frame as the depth
// snapshot above, so a GPU beam can be darkened by shroud and ambient light the same way a
// software-drawn one already is. values/pitch describe a 16 bit source in the same units
// DSurface's own darkening already uses (raw value / 128, with 0 meaning fully shrouded);
// originx/originy work the same as Backend_Upload_Depth_Snapshot's own. Safe to call every
// frame; skip it on a frame with no GPU beams queued, same as the depth snapshot.
void Backend_Upload_Ambient_Snapshot(void const * values, int pitch, int width, int height, int originx, int originy);


// EXTENSION: an opaque handle to a texture Backend_Load_Texture returns. Deliberately not
// a bgfx type, so this header still exposes no bgfx type to the rest of the codebase.
// BACKEND_INVALID_TEXTURE means "no texture" -- either none was requested, or loading it
// failed.
typedef unsigned short BackendTextureHandle;
inline constexpr BackendTextureHandle BACKEND_INVALID_TEXTURE = 0xFFFF;

// Loads (and caches by cachekey) a GPU texture from a whole file's bytes already read into
// memory -- e.g. via CCFileClass + Load_Alloc_Data, which is how this should be sourced so
// the file can come from a mix, matching LaserTexture='s own documented behavior. Chosen
// over taking a filename directly so this file never needs to know how to open one.
//
// Auto-detects DDS, KTX, and PVR3 container formats and decodes to BGRA8 regardless of the
// source's own internal format (including block-compressed ones). Plain image formats --
// BMP, JPG, PNG, TGA, etc. -- are not supported yet; this returns BACKEND_INVALID_TEXTURE
// for them, same as for a file that fails to parse at all. DDS is what the reference this
// feature is modeled on recommends using, for what that is worth.
//
// A texture is decoded once per distinct cachekey and kept for the renderer's lifetime;
// passing the same cachekey again is a cache hit, not a re-decode. There is currently no
// way to evict one, unlike the reference's AllowTextureCache=, since nothing here shares
// its reason to default caching off.
BackendTextureHandle Backend_Load_Texture(char const * cachekey, void const * filedata, unsigned int filesize);


// EXTENSION: loads a sequence of separately-numbered frame files (LaserTexturePackageName=
// / TextureFormatExtension=) into one combined sheet texture, arranged in a roughly square
// grid (out_columns by out_rows), so the existing sheet-slicing path (LaserTextureIsSheet)
// in the beam and anim overlay shaders can show it without needing a separate rendering
// path for "many small files" versus "one sheet file".
//
// filedatas/filesizes are `count` whole files' worth of bytes, already read into memory in
// frame order (frame 0 first) -- the caller is the one that knows the naming convention
// (NAME 0000.EXT, NAME 0001.EXT, ...) and when to stop trying further numbers, since that
// requires opening files, which this renderer-side function does not do.
//
// Every frame after the first is expected to match the first one's own width and height;
// a frame that does not is dropped, along with every frame after it, rather than
// distorting the grid to fit a mismatched size. out_columns/out_rows describe the combined
// texture's own physical grid -- feed these straight to GPUBeamStyle's
// SheetHorizontal/SheetVertical, since the sheet-slicing shader math needs the texture's
// real layout, not how many frames are actually usable. out_loaded_count is that second
// number -- how many frames actually made it in, which may be fewer than `count` for the
// reason above, and which is what should drive animation cycling (GPUBeamSheetFrameCount)
// instead, so playback doesn't wrap into an empty cell the grid has room for but no frame
// ever filled.
BackendTextureHandle Backend_Load_Texture_Sequence(char const * cachekey, void const * const * filedatas, unsigned int const * filesizes, unsigned int count, int * out_columns, int * out_rows, unsigned int * out_loaded_count);


// EXTENSION: everything about a GPU beam's texture, sheet animation, and distortion pass
// that comes from the weapon's own tags rather than from where the beam currently is.
// Grouped into its own struct so adding a tag only touches this and whatever populates it,
// not Backend_Queue_GPU_Beam's own signature.
struct BackendGPUBeamStyle {
	// BACKEND_INVALID_TEXTURE falls back to the original procedural placeholder gradient
	// this beam drew before real texture support existed.
	BackendTextureHandle Texture = BACKEND_INVALID_TEXTURE;

	// BACKEND_INVALID_TEXTURE skips the distortion pass entirely, at zero extra cost.
	BackendTextureHandle DistortionTexture = BACKEND_INVALID_TEXTURE;

	// LaserTextureFilter: true for 1 (point), false for 2 (linear, the default).
	bool PointFilter = false;

	// LaserTextureIsSheet: true slices Texture into SheetHorizontal x SheetVertical cells
	// and shows SheetFrame of them; false uses Texture as a single image, either stretched
	// across the beam's length or scrolled, depending on NoStretch.
	bool IsSheet = false;
	int SheetHorizontal = 1;
	int SheetVertical = 1;
	float SheetFrame = 0.0f;

	// LaserTextureNoStretch: false (direct mapping) stretches Texture across the beam's
	// full length and scrolls it by ScrollPhase; true (bullet mapping) keeps Texture at
	// its own length and moves it by ScrollPhase instead. Ignored when IsSheet is true --
	// the two mapping styles are mutually exclusive per the weapon, same as the reference.
	bool NoStretch = false;
	float ScrollPhase = 0.0f;

	// LaserDistortionWidth/LaserDistortionDisplacement. Width is currently unused by the
	// renderer -- see Backend_Queue_GPU_Beam's own doc comment for why -- and is only here
	// so the caller has one struct to fill in rather than two.
	float DistortionWidth = 1.0f;
	float DistortionDisplacement = 1.0f;
};

// Queues one depth-tested GPU beam quad, drawn in the frame's own pixel space after the
// depth snapshot above (if any) is current for this frame. start/end are the beam's
// endpoints; startdepth/enddepth are its depth at each endpoint, in the depth snapshot's
// own units; width is the beam's on-screen thickness in pixels (LaserTextureThickness);
// color tints it, 0xAABBGGRR with the alpha channel scaling the whole beam's opacity. A
// beam queued with no depth snapshot uploaded this frame draws unoccluded.
//
// style.DistortionWidth is not applied: the reference widens the distortion pass's own
// geometry beyond the beam's visible quad so the warp reaches slightly past its edges.
// This renderer instead runs the distortion pass over the same quad as the color pass,
// which is simpler but means the warp cannot currently extend past where the beam itself
// is drawn.
void Backend_Queue_GPU_Beam(float startx, float starty, float startdepth, float endx, float endy, float enddepth, float width, unsigned int color, BackendGPUBeamStyle const & style);
