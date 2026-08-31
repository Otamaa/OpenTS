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
// settings. video.cpp is the only caller.

#pragma once

#include <windows.h>


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


bool Backend_Init(HWND window, int windowwidth, int windowheight, BackendRenderer renderer, bool vsync);
void Backend_Shutdown(void);

bool Backend_Set_Frame_Size(int width, int height);
void Backend_On_Resize(int windowwidth, int windowheight);

// Uploads the frame and presents it. The pixels are 16 bit 565 and stay owned by the
// caller; they are consumed before this returns.
void Backend_Present(void const * pixels, int pitch, int destx, int desty, int destwidth, int destheight, BackendScaleMode mode);

char const * Backend_Renderer_Name(void);


// --- GPU sprite atlas / instanced draw support ------------------------------
// STAGE 1 SCAFFOLD. Additive to the existing Backend_Present() pixel-blit
// path; nothing below is called from anywhere yet except gpu_sprite.cpp.
// Kept behind this header (rather than exposing bgfx handles directly) so
// this remains the only translation unit that includes bgfx.

typedef unsigned int BackendTextureHandle;
static const BackendTextureHandle BACKEND_INVALID_TEXTURE = 0xFFFFFFFFu;

// Creates an empty RGBA8 atlas page of the given size. Returns
// BACKEND_INVALID_TEXTURE on failure (e.g. before Backend_Init()).
BackendTextureHandle Backend_Create_Atlas_Page(int width, int height);
void Backend_Destroy_Atlas_Page(BackendTextureHandle page);

// Uploads RGBA8 pixel data into a sub-rect of an existing atlas page.
// pixels must be width*height RGBA8 texels, tightly packed (pitch == width*4).
bool Backend_Update_Atlas_Region(BackendTextureHandle page, int x, int y, int width, int height, void const * pixels);

struct BackendSpriteInstance
{
	// Destination rect, in the same pixel space as the frame passed to
	// Backend_Present() (i.e. game/frame pixel coordinates, not window
	// coordinates -- the existing prescale/present views handle that
	// remaining transform).
	float DestX, DestY, DestWidth, DestHeight;

	// Normalized [0,1] UV rect within the atlas page.
	float U0, V0, U1, V1;

	unsigned int TintRGBA;
};

// Brackets sprite submission for one frame. Must be called before any
// Backend_Submit_Sprites() this frame, and Backend_End_Sprite_Frame() before
// Backend_Present() runs for that same frame.
//
// MISSING: ordering/compositing between this GPU sprite pass and the CPU
// Backend_Present() pixel upload is not decided yet -- today they'd just be
// two separate draws into the same backbuffer with no defined z-order
// against each other. Do not enable both for the same shapes until that's
// resolved.
void Backend_Begin_Sprite_Frame(void);
void Backend_End_Sprite_Frame(void);

// Submits one batch of instances, all sampling the same atlas page. Safe to
// call multiple times per frame with different pages.
void Backend_Submit_Sprites(BackendTextureHandle page, BackendSpriteInstance const * instances, int count);
