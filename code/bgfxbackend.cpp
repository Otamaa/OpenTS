/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// The bgfx side of the presenter. This is the only translation unit that includes bgfx,
// which keeps the library's headers and build settings away from the rest of the engine.

#include "bgfxbackend.h"

#include "dbgprint.h"
#include "except.h"

#include <vector>
#include <unordered_map>
#include <string>

#include <bx/allocator.h>
#include <bgfx/bgfx.h>
// EXTENSION: this project's CMakeLists.txt only compiles s_6_0 (Shader Model 6, DXIL) for
// a 64-bit build (see OPENTS_SHADER_PROFILES there for why); 32-bit stays on s_5_0 (DXBC)
// alone. The embedded-shader macro below must be told to match, since
// BGFX_PLATFORM_SUPPORTS_DXIL otherwise defaults to true on Windows regardless of
// architecture. WGSL (WebGPU) is unrelated to either -- nothing here targets WebGPU on
// any architecture -- so it stays off unconditionally. Both #ifndef guards live in
// embedded_shader.h itself and must be overridden before that header is included, which
// is why this is all the way up here rather than down by the profile-specific #include
// block below.
#ifndef OPENTS_BUILD_64BIT
#define BGFX_PLATFORM_SUPPORTS_DXIL 0
#endif
#define BGFX_PLATFORM_SUPPORTS_WGSL 0

#include <bgfx/embedded_shader.h>
#include <bimg/bimg.h>
#include <bx/error.h>
#include <bx/readerwriter.h>

// EXTENSION: stb_image is the fallback for the plain image formats bimg's own container
// parser doesn't handle (PNG, TGA, BMP, JPG, etc.) -- see Backend_Load_Texture. Vendored
// under thirdparty/stb rather than pulled in through bgfx.cmake, since it has nothing to
// do with bgfx itself.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include <stb_image.h>

#include <vs_ocornut_imgui.bin.h>
#include <fs_ocornut_imgui.bin.h>

// EXTENSION: the postfx shaders are compiled by bgfx_compile_shaders() in CMakeLists.txt,
// which emits one header per graphics profile rather than the single multi-profile file
// bgfx ships prebuilt for the two imgui shaders above, so each profile needs its own
// #include here. dxil only exists as a build output at all on a 64-bit build -- see
// OPENTS_SHADER_PROFILES in CMakeLists.txt -- so its #include is conditional on the same
// OPENTS_BUILD_64BIT that turned BGFX_PLATFORM_SUPPORTS_DXIL back on above; every other
// profile here is unconditional since it's compiled either way.
#include <glsl/vs_postfx.sc.bin.h>
#include <essl/vs_postfx.sc.bin.h>
#include <spirv/vs_postfx.sc.bin.h>
#include <dxbc/vs_postfx.sc.bin.h>
#ifdef OPENTS_BUILD_64BIT
#include <dxil/vs_postfx.sc.bin.h>
#endif
#include <glsl/fs_bloom_bright.sc.bin.h>
#include <essl/fs_bloom_bright.sc.bin.h>
#include <spirv/fs_bloom_bright.sc.bin.h>
#include <dxbc/fs_bloom_bright.sc.bin.h>
#ifdef OPENTS_BUILD_64BIT
#include <dxil/fs_bloom_bright.sc.bin.h>
#endif
#include <glsl/fs_bloom_blur.sc.bin.h>
#include <essl/fs_bloom_blur.sc.bin.h>
#include <spirv/fs_bloom_blur.sc.bin.h>
#include <dxbc/fs_bloom_blur.sc.bin.h>
#ifdef OPENTS_BUILD_64BIT
#include <dxil/fs_bloom_blur.sc.bin.h>
#endif
#include <glsl/fs_bloom_combine.sc.bin.h>
#include <essl/fs_bloom_combine.sc.bin.h>
#include <spirv/fs_bloom_combine.sc.bin.h>
#include <dxbc/fs_bloom_combine.sc.bin.h>
#ifdef OPENTS_BUILD_64BIT
#include <dxil/fs_bloom_combine.sc.bin.h>
#endif

// EXTENSION: the GPU beam shaders (see code/shaders/gpubeam), compiled the same way.
#include <glsl/vs_gpubeam.sc.bin.h>
#include <essl/vs_gpubeam.sc.bin.h>
#include <spirv/vs_gpubeam.sc.bin.h>
#include <dxbc/vs_gpubeam.sc.bin.h>
#ifdef OPENTS_BUILD_64BIT
#include <dxil/vs_gpubeam.sc.bin.h>
#endif
#include <glsl/fs_gpubeam.sc.bin.h>
#include <essl/fs_gpubeam.sc.bin.h>
#include <spirv/fs_gpubeam.sc.bin.h>
#include <dxbc/fs_gpubeam.sc.bin.h>
#ifdef OPENTS_BUILD_64BIT
#include <dxil/fs_gpubeam.sc.bin.h>
#endif
#include <glsl/fs_gpubeam_distort.sc.bin.h>
#include <essl/fs_gpubeam_distort.sc.bin.h>
#include <spirv/fs_gpubeam_distort.sc.bin.h>
#include <dxbc/fs_gpubeam_distort.sc.bin.h>
#ifdef OPENTS_BUILD_64BIT
#include <dxil/fs_gpubeam_distort.sc.bin.h>
#endif
#include <glsl/fs_anim_overlay.sc.bin.h>
#include <essl/fs_anim_overlay.sc.bin.h>
#include <spirv/fs_anim_overlay.sc.bin.h>
#include <dxbc/fs_anim_overlay.sc.bin.h>
#ifdef OPENTS_BUILD_64BIT
#include <dxil/fs_anim_overlay.sc.bin.h>
#endif
#include <glsl/fs_particle.sc.bin.h>
#include <essl/fs_particle.sc.bin.h>
#include <spirv/fs_particle.sc.bin.h>
#include <dxbc/fs_particle.sc.bin.h>
#ifdef OPENTS_BUILD_64BIT
#include <dxil/fs_particle.sc.bin.h>
#endif
#include <glsl/fs_distortwarp.sc.bin.h>
#include <essl/fs_distortwarp.sc.bin.h>
#include <spirv/fs_distortwarp.sc.bin.h>
#include <dxbc/fs_distortwarp.sc.bin.h>
#ifdef OPENTS_BUILD_64BIT
#include <dxil/fs_distortwarp.sc.bin.h>
#endif
#include <glsl/fs_atmosphere.sc.bin.h>
#include <essl/fs_atmosphere.sc.bin.h>
#include <spirv/fs_atmosphere.sc.bin.h>
#include <dxbc/fs_atmosphere.sc.bin.h>
#ifdef OPENTS_BUILD_64BIT
#include <dxil/fs_atmosphere.sc.bin.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>


static const bgfx::EmbeddedShader _EmbeddedShaders[] = {
	BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
	BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
	BGFX_EMBEDDED_SHADER(vs_postfx),
	BGFX_EMBEDDED_SHADER(fs_bloom_bright),
	BGFX_EMBEDDED_SHADER(fs_bloom_blur),
	BGFX_EMBEDDED_SHADER(fs_bloom_combine),
	BGFX_EMBEDDED_SHADER(vs_gpubeam),
	BGFX_EMBEDDED_SHADER(fs_gpubeam),
	BGFX_EMBEDDED_SHADER(fs_gpubeam_distort),
	BGFX_EMBEDDED_SHADER(fs_distortwarp),
	BGFX_EMBEDDED_SHADER(fs_atmosphere),
	BGFX_EMBEDDED_SHADER(fs_anim_overlay),
	BGFX_EMBEDDED_SHADER(fs_particle),
	BGFX_EMBEDDED_SHADER_END()
};


// Views render in ascending id order. GPU beams composite onto the base frame first,
// since they occlude against the software scene's own depth; any distortion-enabled beam
// also writes a warp vector in the same pass, consumed immediately after by the warp view;
// GPU overlay quads composite on top of that, since they draw over everything
// unconditionally; water/weather tint the whole scene after that, so their light affects
// beams, particles, and overlay anims too, not just the terrain underneath them; the
// bloom chain has to finish before the pixel art magnify pass samples its result, which in
// turn has to finish before the present pass samples whichever of the earlier stages ran
// last.
static const bgfx::ViewId VIEW_GPU_BEAM = 0;
static const bgfx::ViewId VIEW_DISTORTION = 1;
static const bgfx::ViewId VIEW_DISTORTION_WARP = 2;
static const bgfx::ViewId VIEW_ANIM_OVERLAY = 3;
static const bgfx::ViewId VIEW_ATMOSPHERE = 4;
static const bgfx::ViewId VIEW_BLOOM_BRIGHT = 5;
static const bgfx::ViewId VIEW_BLOOM_BLUR_H = 6;
static const bgfx::ViewId VIEW_BLOOM_BLUR_V = 7;
static const bgfx::ViewId VIEW_BLOOM_COMBINE = 8;
static const bgfx::ViewId VIEW_PRESCALE = 9;
static const bgfx::ViewId VIEW_PRESENT = 10;


static bool _Initialized = false;

static bgfx::TextureHandle _FrameTexture = BGFX_INVALID_HANDLE;
static bgfx::ProgramHandle _Program = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _TextureSampler = BGFX_INVALID_HANDLE;
static bgfx::FrameBufferHandle _PrescaleTarget = BGFX_INVALID_HANDLE;
static bgfx::VertexLayout _VertexLayout;

// EXTENSION: the bloom chain. Bright-pass and blur run at half the frame's resolution,
// which is the usual cost/quality tradeoff for a glow that is meant to look soft rather
// than crisp; the combine pass writes a full-resolution result the rest of the presenter
// treats exactly like the frame texture it replaces.
static bgfx::ProgramHandle _BloomBrightProgram = BGFX_INVALID_HANDLE;
static bgfx::ProgramHandle _BloomBlurProgram = BGFX_INVALID_HANDLE;
static bgfx::ProgramHandle _BloomCombineProgram = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _BloomSampler = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _BloomParamsUniform = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _BloomDirUniform = BGFX_INVALID_HANDLE;
static bgfx::FrameBufferHandle _BloomExtractTarget = BGFX_INVALID_HANDLE;
static bgfx::FrameBufferHandle _BloomBlurTargetA = BGFX_INVALID_HANDLE;
static bgfx::FrameBufferHandle _BloomBlurTargetB = BGFX_INVALID_HANDLE;
static bgfx::FrameBufferHandle _PostFXTarget = BGFX_INVALID_HANDLE;
static int _BloomWidth = 0;
static int _BloomHeight = 0;

// The frame size the bloom chain above was last built for. Compared against the current
// frame size rather than against _FrameWidth/_FrameHeight directly, because those are
// already updated to the new size by the time Ensure_Bloom_Targets runs.
static int _PostFXFrameWidth = 0;
static int _PostFXFrameHeight = 0;

// EXTENSION: GPU overlay quads (see Backend_Queue_Overlay_Quad). Each entry owns a
// texture created fresh for that one frame; both the entry and its texture are gone by
// the time the next Backend_Present call returns.
struct BackendOverlayQuad {
	bgfx::TextureHandle Texture;
	float DestX;
	float DestY;
	float DestWidth;
	float DestHeight;

	// EXTENSION: a single depth value for the whole quad -- unlike a beam, an overlay
	// anim's sprite sits at one depth, not a range across its length -- in the same
	// scroll-corrected units Backend_Queue_GPU_Beam's own start/end depth use.
	float Depth;
};
static std::vector<BackendOverlayQuad> _OverlayQueue;
static bgfx::FrameBufferHandle _AnimOverlayTarget = BGFX_INVALID_HANDLE;
static int _OverlayFrameWidth = 0;
static int _OverlayFrameHeight = 0;

// EXTENSION: weather/water. Config is set once a frame by Backend_Set_Atmosphere and
// consumed by Run_Atmosphere_Pass; the target is its own, separate from every other
// postfx target, since the pass both reads and writes a full-screen composite and can't
// do that to the same texture at once.
static BackendWeatherConfig _WeatherConfig;
static BackendWaterConfig _WaterConfig;
static bgfx::FrameBufferHandle _AtmosphereTarget = BGFX_INVALID_HANDLE;
static int _AtmosphereFrameWidth = 0;
static int _AtmosphereFrameHeight = 0;
static bgfx::ProgramHandle _AtmosphereProgram = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _WeatherSampler = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _WaterSampler = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _AtmosphereFlagsUniform = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _WeatherParamsUniform = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _WaterParamsUniform = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _WaterTilingUniform = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _WaterFogColorUniform = BGFX_INVALID_HANDLE;

// EXTENSION: depth-tested GPU beams (see Backend_Queue_GPU_Beam). Unlike overlay quads
// these carry no texture of their own; the beam's look is entirely procedural, driven by
// the vertex data below plus a snapshot of the software renderer's own depth buffer.
struct BackendGPUBeam {
	float StartX, StartY, StartDepth;
	float EndX, EndY, EndDepth;
	float Width;
	unsigned int Color;
	BackendGPUBeamStyle Style;
};
static std::vector<BackendGPUBeam> _BeamQueue;

// EXTENSION: GPU particles. Share the beam target/view (VIEW_GPU_BEAM) and this frame's
// depth/ambient/visibility snapshots rather than owning any of their own -- there's
// nothing about a particle's occlusion or lighting that differs from a beam's.
struct BackendGPUParticle {
	float X, Y, Depth;
	float Size;
	unsigned int Color;
	BackendTextureHandle Texture;
};
static std::vector<BackendGPUParticle> _ParticleQueue;
static bgfx::ProgramHandle _ParticleProgram = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _ParticleTextureSampler = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _ParticleParamsUniform = BGFX_INVALID_HANDLE;
static bgfx::FrameBufferHandle _GPUBeamTarget = BGFX_INVALID_HANDLE;
static int _BeamFrameWidth = 0;
static int _BeamFrameHeight = 0;
static bgfx::ProgramHandle _GPUBeamProgram = BGFX_INVALID_HANDLE;
static bgfx::ProgramHandle _AnimOverlayProgram = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _DepthSampler = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _AmbientSampler = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _VisibilitySampler = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _VisibilityParamsUniform = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _VisibilityFlagsUniform = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _DepthParamsUniform = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _BeamParamsUniform = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _BeamSheetParamsUniform = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _BeamTextureSampler = BGFX_INVALID_HANDLE;

// EXTENSION: the distortion pass (LaserDistortion=). A second draw per distortion-enabled
// beam writes a warp vector into _DistortionTarget instead of a color; one full-screen pass
// afterward re-samples the scene through that warp. Neutral (0.5, 0.5, 1.0, 0.0) means "no
// displacement here" -- the fourth channel doubles as the warp pass's own opacity, so an
// area nothing wrote to contributes nothing to the final warp.
static bgfx::FrameBufferHandle _DistortionTarget = BGFX_INVALID_HANDLE;

// The warp pass reads _GPUBeamTarget (or whatever ran before it) and _DistortionTarget at
// once, so it cannot write back into either of them -- that would be reading and writing
// the same texture in the same draw. This is its own output, sharing the other two
// targets' lifecycle since all three always need to match the current frame size together.
static bgfx::FrameBufferHandle _DistortionWarpOutputTarget = BGFX_INVALID_HANDLE;
static bgfx::ProgramHandle _GPUBeamDistortProgram = BGFX_INVALID_HANDLE;
static bgfx::ProgramHandle _DistortWarpProgram = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _DistortSampler = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle _DistortParamsUniform = BGFX_INVALID_HANDLE;
static bool _DistortionQueuedThisFrame = false;

// EXTENSION: textures loaded through Backend_Load_Texture, keyed by the caller's own
// cachekey (see that function's own doc comment). Never evicted; see BackendTextureHandle.
static std::vector<bgfx::TextureHandle> _LoadedTextures;
static std::unordered_map<std::string, BackendTextureHandle> _LoadedTextureCache;

// The depth snapshot Backend_Upload_Depth_Snapshot last uploaded. Valid for one
// Backend_Present call only; consumed and invalidated there whether or not any beam was
// actually queued against it.
static bgfx::TextureHandle _DepthSnapshotTexture = BGFX_INVALID_HANDLE;
static int _DepthSnapshotWidth = 0;
static int _DepthSnapshotHeight = 0;
static int _DepthSnapshotOriginX = 0;
static int _DepthSnapshotOriginY = 0;
static bool _HasDepthSnapshot = false;

// EXTENSION: the ambient/shroud lighting snapshot (AlphaBuffer). Shares the depth
// snapshot's own origin uniform in the shaders -- DepthBuffer and AlphaBuffer are always
// constructed covering the same Rect -- so only the texture itself needs its own handle.
static bgfx::TextureHandle _AmbientSnapshotTexture = BGFX_INVALID_HANDLE;
static int _AmbientSnapshotWidth = 0;
static int _AmbientSnapshotHeight = 0;
static bool _HasAmbientSnapshot = false;

// EXTENSION: the shroud/fog visibility snapshot. Built cell-by-cell (see
// Tactical::Capture_GPU_Visibility_Mask), not copied from a ring-buffered software
// surface like the two above, so it carries its own origin rather than sharing the depth
// snapshot's -- there's no guarantee it covers exactly the same Rect.
static bgfx::TextureHandle _VisibilitySnapshotTexture = BGFX_INVALID_HANDLE;
static int _VisibilitySnapshotWidth = 0;
static int _VisibilitySnapshotHeight = 0;
static int _VisibilitySnapshotOriginX = 0;
static int _VisibilitySnapshotOriginY = 0;
static bool _HasVisibilitySnapshot = false;

// DSurface's own depth buffer is 16 bit with a ZBUFFER_MAX of 0x8000; both the snapshot
// and every beam's own endpoint depths are normalized by this before either reaches the
// GPU, so the shader's comparison never needs to know the raw format.
static const float BEAM_DEPTH_SCALE = 1.0f / 32768.0f;

// A dedicated vertex layout for beam quads: BackendVertex has no room for the per-vertex
// depth a beam needs to carry into its fragment shader's occlusion test.
struct BackendBeamVertex {
	float X, Y;
	float U, V;
	unsigned int Color;
	float Depth;
};
static bgfx::VertexLayout _BeamVertexLayout;

static int _FrameWidth = 0;
static int _FrameHeight = 0;
static int _PrescaleWidth = 0;
static int _PrescaleHeight = 0;
static int _DrawableWidth = 0;
static int _DrawableHeight = 0;
static unsigned int _ResetFlags = BGFX_RESET_FLIP_AFTER_RENDER;

// True while the frame texture holds the game's own 565 layout. When the hardware cannot
// sample that format the frame is widened to 32 bits on the way in instead.
static bool _FrameIs565 = false;
static unsigned int * _ConvertBuffer = NULL;
static unsigned int _ConvertTable[65536];


struct BackendVertex
{
	float X;
	float Y;
	float U;
	float V;
	unsigned int Color;
};


// bgfx reports lost devices and shader failures through this rather than a return code,
// so the engine would otherwise present to a black window with no explanation.
class BackendCallback : public bgfx::CallbackI
{
	public:
		virtual ~BackendCallback(void) override {}

		virtual void fatal(const char * filepath, uint16_t line, bgfx::Fatal::Enum code, const char * str) override
		{
			// A debug check is the library's own assertion, not a renderer failure. The ones it
			// runs while shutting down compare reference counts on interfaces that an overlay
			// or the Direct3D debug layer is free to hold, so ending the process over one would
			// report somebody else's reference as a crash.
			if (code == bgfx::Fatal::DebugCheck) {
				DebugString("Renderer check failed at %s(%u): %s\n",
							filepath != NULL ? filepath : "", (unsigned)line, str != NULL ? str : "");
				return;
			}

			Fatal("Renderer error %d at %s(%u): %s", (int)code,
						filepath != NULL ? filepath : "", (unsigned)line, str != NULL ? str : "");
		}

		virtual void traceVargs(const char * filepath, uint16_t line, const char * format, va_list argList) override
		{
			char message[1024];
			vsnprintf(message, sizeof(message), format, argList);
			OutputDebugString(message);
		}

		virtual void profilerBegin(const char *, uint32_t, const char *, uint16_t) override {}
		virtual void profilerBeginLiteral(const char *, uint32_t, const char *, uint16_t) override {}
		virtual void profilerEnd(void) override {}
		virtual uint32_t cacheReadSize(uint64_t) override { return(0); }
		virtual bool cacheRead(uint64_t, void *, uint32_t) override { return(false); }
		virtual void cacheWrite(uint64_t, const void *, uint32_t) override {}
		virtual void screenShot(const char *, uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, const void *, uint32_t, bool) override {}
		virtual void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
		virtual void captureEnd(void) override {}
		virtual void captureFrame(const void *, uint32_t) override {}
};

static BackendCallback _Callback;


// bgfx contains cache-line-aligned render records but requests their backing arrays with
// the allocator's default alignment. The Win32 CRT only guarantees eight-byte alignment,
// which is insufficient when clang-cl copies those records with aligned SSE instructions.
class BackendAllocator : public bx::AllocatorI
{
	public:
		virtual ~BackendAllocator(void) override {}

		virtual void * realloc(void * ptr, size_t size, size_t alignment, const char *, uint32_t) override
		{
			if (size == 0) {
				_aligned_free(ptr);
				return(NULL);
			}

			const size_t cachelinealignment = BX_CACHE_LINE_SIZE;
			alignment = std::max(alignment, cachelinealignment);
			return(_aligned_realloc(ptr, size, alignment));
		}
};

static BackendAllocator _Allocator;


/// <summary>
/// Builds the table that widens a 565 pixel to the 32 bit color the fallback path uploads.
/// </summary>
static void Build_Convert_Table(void)
{
	for (int pixel = 0; pixel < 65536; pixel++) {
		unsigned int red = (unsigned int)(((pixel >> 11) & 0x1F) * 255 / 31);
		unsigned int green = (unsigned int)(((pixel >> 5) & 0x3F) * 255 / 63);
		unsigned int blue = (unsigned int)((pixel & 0x1F) * 255 / 31);

		_ConvertTable[pixel] = 0xFF000000 | (red << 16) | (green << 8) | blue;
	}
}


/// <summary>
/// Submits one rectangle covering the given destination, drawn with the given program.
/// Any textures or uniforms the program needs are the caller's responsibility to bind
/// beforehand, since bgfx keeps them set until the next submit on this view.
/// </summary>
static void Submit_Quad(bgfx::ViewId view, bgfx::ProgramHandle program, float x, float y, float width, float height, bool flipv = false, bool blend = false)
{
	bgfx::TransientVertexBuffer buffer;

	if (bgfx::getAvailTransientVertexBuffer(6, _VertexLayout) < 6) {
		return;
	}

	bgfx::allocTransientVertexBuffer(&buffer, 6, _VertexLayout);

	BackendVertex * vertex = (BackendVertex *)buffer.data;
	const unsigned int white = 0xFFFFFFFF;

	const float vtop = flipv ? 1.0f : 0.0f;
	const float vbottom = flipv ? 0.0f : 1.0f;

	vertex[0] = { x, y, 0.0f, vtop, white };
	vertex[1] = { x + width, y, 1.0f, vtop, white };
	vertex[2] = { x + width, y + height, 1.0f, vbottom, white };
	vertex[3] = { x, y, 0.0f, vtop, white };
	vertex[4] = { x + width, y + height, 1.0f, vbottom, white };
	vertex[5] = { x, y + height, 0.0f, vbottom, white };

	bgfx::setVertexBuffer(0, &buffer);

	// EXTENSION: blend is true for GPU overlay quads, which are drawn on top of an
	// already-composited base frame and must let its color show through wherever the
	// source's alpha (set by Backend_Queue_Overlay_Quad's key color) says to.
	uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
	if (blend) {
		state |= BGFX_STATE_BLEND_ALPHA;
	}
	bgfx::setState(state);
	bgfx::submit(view, program);
}


/// <summary>
/// Builds an orthographic projection over a target measured in pixels, with the origin in
/// its top left corner.
/// </summary>
static void Build_Ortho_Projection(float * result, int width, int height)
{
	const float depthnear = 0.0f;
	const float depthfar = 1000.0f;
	const bool homogeneous = bgfx::getCaps()->homogeneousDepth;

	memset(result, 0, sizeof(float) * 16);

	result[0] = 2.0f / (float)width;
	result[5] = -2.0f / (float)height;
	result[10] = homogeneous ? 2.0f / (depthfar - depthnear) : 1.0f / (depthfar - depthnear);
	result[12] = -1.0f;
	result[13] = 1.0f;
	result[14] = homogeneous ? -(depthfar + depthnear) / (depthfar - depthnear) : -depthnear / (depthfar - depthnear);
	result[15] = 1.0f;
}


/// <summary>
/// Sets a view to draw into a target of the given size using pixel coordinates.
/// </summary>
static void Set_View_Transform(bgfx::ViewId view, int width, int height)
{
	float projection[16];
	bgfx::setViewRect(view, 0, 0, (uint16_t)width, (uint16_t)height);
	Build_Ortho_Projection(projection, width, height);
	bgfx::setViewTransform(view, NULL, projection);
}


/// <summary>
/// Discards the intermediate target the pixel art filter magnifies through.
/// </summary>
static void Destroy_Prescale_Target(void)
{
	if (bgfx::isValid(_PrescaleTarget)) {
		bgfx::destroy(_PrescaleTarget);
		_PrescaleTarget = BGFX_INVALID_HANDLE;
	}
	_PrescaleWidth = 0;
	_PrescaleHeight = 0;
}


/// <summary>
/// Makes sure the pixel art filter has an intermediate target of the requested size.
/// </summary>
/// <returns>bool; Is a target of that size ready to render into?</returns>
static bool Ensure_Prescale_Target(int width, int height)
{
	if (bgfx::isValid(_PrescaleTarget) && _PrescaleWidth == width && _PrescaleHeight == height) {
		return(true);
	}

	Destroy_Prescale_Target();

	const bgfx::Caps * caps = bgfx::getCaps();
	if (width <= 0 || height <= 0 || width > caps->limits.maxTextureSize || height > caps->limits.maxTextureSize) {
		return(false);
	}

	_PrescaleTarget = bgfx::createFrameBuffer((uint16_t)width, (uint16_t)height, bgfx::TextureFormat::BGRA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
	if (!bgfx::isValid(_PrescaleTarget)) {
		return(false);
	}

	_PrescaleWidth = width;
	_PrescaleHeight = height;
	return(true);
}


/// <summary>
/// Discards the bloom chain's intermediate targets.
/// </summary>
static void Destroy_Bloom_Targets(void)
{
	if (bgfx::isValid(_BloomExtractTarget)) {
		bgfx::destroy(_BloomExtractTarget);
		_BloomExtractTarget = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_BloomBlurTargetA)) {
		bgfx::destroy(_BloomBlurTargetA);
		_BloomBlurTargetA = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_BloomBlurTargetB)) {
		bgfx::destroy(_BloomBlurTargetB);
		_BloomBlurTargetB = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_PostFXTarget)) {
		bgfx::destroy(_PostFXTarget);
		_PostFXTarget = BGFX_INVALID_HANDLE;
	}
	_BloomWidth = 0;
	_BloomHeight = 0;
	_PostFXFrameWidth = 0;
	_PostFXFrameHeight = 0;
}


/// <summary>
/// Makes sure the bloom chain has targets sized for the given frame, allocating them on
/// first use or after a resize and reusing them otherwise.
/// </summary>
/// <returns>bool; Is the whole chain ready to render into?</returns>
static bool Ensure_Bloom_Targets(int framewidth, int frameheight)
{
	if (bgfx::isValid(_PostFXTarget) && bgfx::isValid(_BloomExtractTarget)
		&& _PostFXFrameWidth == framewidth && _PostFXFrameHeight == frameheight) {
		return(true);
	}

	Destroy_Bloom_Targets();

	const bgfx::Caps * caps = bgfx::getCaps();
	if (framewidth <= 0 || frameheight <= 0
		|| framewidth > caps->limits.maxTextureSize || frameheight > caps->limits.maxTextureSize) {
		return(false);
	}

	// Half resolution, but never zero for a source frame smaller than 2 pixels on a side.
	int bloomwidth = std::max(framewidth / 2, 1);
	int bloomheight = std::max(frameheight / 2, 1);

	_BloomExtractTarget = bgfx::createFrameBuffer((uint16_t)bloomwidth, (uint16_t)bloomheight, bgfx::TextureFormat::BGRA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
	_BloomBlurTargetA = bgfx::createFrameBuffer((uint16_t)bloomwidth, (uint16_t)bloomheight, bgfx::TextureFormat::BGRA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
	_BloomBlurTargetB = bgfx::createFrameBuffer((uint16_t)bloomwidth, (uint16_t)bloomheight, bgfx::TextureFormat::BGRA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
	_PostFXTarget = bgfx::createFrameBuffer((uint16_t)framewidth, (uint16_t)frameheight, bgfx::TextureFormat::BGRA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

	if (!bgfx::isValid(_BloomExtractTarget) || !bgfx::isValid(_BloomBlurTargetA)
		|| !bgfx::isValid(_BloomBlurTargetB) || !bgfx::isValid(_PostFXTarget)) {
		Destroy_Bloom_Targets();
		return(false);
	}

	_BloomWidth = bloomwidth;
	_BloomHeight = bloomheight;
	_PostFXFrameWidth = framewidth;
	_PostFXFrameHeight = frameheight;
	return(true);
}


/// <summary>
/// Discards the GPU overlay compositing target. The queue itself is discarded separately
/// by Discard_Overlay_Queue, once per frame, regardless of whether the target is kept.
/// </summary>
static void Destroy_Overlay_Target(void)
{
	if (bgfx::isValid(_AnimOverlayTarget)) {
		bgfx::destroy(_AnimOverlayTarget);
		_AnimOverlayTarget = BGFX_INVALID_HANDLE;
	}
	_OverlayFrameWidth = 0;
	_OverlayFrameHeight = 0;
}


/// <summary>
/// Makes sure the GPU overlay compositing target is sized for the given frame.
/// </summary>
/// <returns>bool; Is the target ready to render into?</returns>
static bool Ensure_Overlay_Target(int framewidth, int frameheight)
{
	if (bgfx::isValid(_AnimOverlayTarget) && _OverlayFrameWidth == framewidth && _OverlayFrameHeight == frameheight) {
		return(true);
	}

	Destroy_Overlay_Target();

	const bgfx::Caps * caps = bgfx::getCaps();
	if (framewidth <= 0 || frameheight <= 0
		|| framewidth > caps->limits.maxTextureSize || frameheight > caps->limits.maxTextureSize) {
		return(false);
	}

	_AnimOverlayTarget = bgfx::createFrameBuffer((uint16_t)framewidth, (uint16_t)frameheight, bgfx::TextureFormat::BGRA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
	if (!bgfx::isValid(_AnimOverlayTarget)) {
		return(false);
	}

	_OverlayFrameWidth = framewidth;
	_OverlayFrameHeight = frameheight;
	return(true);
}


/// <summary>
/// Destroys every queued overlay quad's one-frame texture and empties the queue. Called
/// once at the end of every Backend_Present, whether or not the queue was drawn from.
/// </summary>
static void Discard_Overlay_Queue(void)
{
	for (size_t index = 0; index < _OverlayQueue.size(); index++) {
		if (bgfx::isValid(_OverlayQueue[index].Texture)) {
			bgfx::destroy(_OverlayQueue[index].Texture);
		}
	}
	_OverlayQueue.clear();
}


/// <summary>
/// Discards the GPU beam compositing target and the distortion target alongside it.
/// </summary>
static void Destroy_Beam_Target(void)
{
	if (bgfx::isValid(_GPUBeamTarget)) {
		bgfx::destroy(_GPUBeamTarget);
		_GPUBeamTarget = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_DistortionTarget)) {
		bgfx::destroy(_DistortionTarget);
		_DistortionTarget = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_DistortionWarpOutputTarget)) {
		bgfx::destroy(_DistortionWarpOutputTarget);
		_DistortionWarpOutputTarget = BGFX_INVALID_HANDLE;
	}
	_BeamFrameWidth = 0;
	_BeamFrameHeight = 0;
}


/// <summary>
/// Makes sure the GPU beam compositing target, and the distortion target that shares its
/// lifetime, are sized for the given frame. The distortion target is allocated whether or
/// not any beam actually uses it this frame, since it costs little next to the beam target
/// it is always created alongside and it keeps this function's bookkeeping to one pair of
/// dimensions instead of two.
/// </summary>
/// <returns>bool; Are both targets ready to render into?</returns>
static bool Ensure_Beam_Target(int framewidth, int frameheight)
{
	if (bgfx::isValid(_GPUBeamTarget) && bgfx::isValid(_DistortionTarget) && bgfx::isValid(_DistortionWarpOutputTarget)
		&& _BeamFrameWidth == framewidth && _BeamFrameHeight == frameheight) {
		return(true);
	}

	Destroy_Beam_Target();

	const bgfx::Caps * caps = bgfx::getCaps();
	if (framewidth <= 0 || frameheight <= 0
		|| framewidth > caps->limits.maxTextureSize || frameheight > caps->limits.maxTextureSize) {
		return(false);
	}

	_GPUBeamTarget = bgfx::createFrameBuffer((uint16_t)framewidth, (uint16_t)frameheight, bgfx::TextureFormat::BGRA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
	_DistortionTarget = bgfx::createFrameBuffer((uint16_t)framewidth, (uint16_t)frameheight, bgfx::TextureFormat::BGRA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
	_DistortionWarpOutputTarget = bgfx::createFrameBuffer((uint16_t)framewidth, (uint16_t)frameheight, bgfx::TextureFormat::BGRA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

	if (!bgfx::isValid(_GPUBeamTarget) || !bgfx::isValid(_DistortionTarget) || !bgfx::isValid(_DistortionWarpOutputTarget)) {
		Destroy_Beam_Target();
		return(false);
	}

	_BeamFrameWidth = framewidth;
	_BeamFrameHeight = frameheight;
	return(true);
}


/// <summary>
/// Empties the beam queue and the distortion flag. Called once at the end of every
/// Backend_Present, whether or not the queue was drawn from. Beams own no per-instance GPU
/// resources of their own -- their textures come from the persistent Backend_Load_Texture
/// cache -- so this is nothing more than clearing the list.
/// </summary>
static void Discard_Beam_Queue(void)
{
	_BeamQueue.clear();
	_ParticleQueue.clear();
	_DistortionQueuedThisFrame = false;
}


/// <summary>
/// Builds and submits one beam's quad: a rectangle width pixels wide running from
/// (startx,starty) to (endx,endy), with each long edge's two vertices carrying the beam's
/// depth at that end for the fragment shader's own occlusion test. Used for both the color
/// pass and the distortion pass -- same geometry, different program and textures, which
/// the caller has already bound before calling this.
/// </summary>
static void Submit_Beam_Quad(bgfx::ViewId view, bgfx::ProgramHandle program, BackendGPUBeam const & beam)
{
	float dx = beam.EndX - beam.StartX;
	float dy = beam.EndY - beam.StartY;
	float length = sqrtf(dx * dx + dy * dy);
	if (length < 0.0001f) {
		return;
	}

	// The perpendicular offset that gives the quad its width, half to each side of the
	// beam's own centerline.
	float nx = -(dy / length) * (beam.Width * 0.5f);
	float ny = (dx / length) * (beam.Width * 0.5f);

	bgfx::TransientVertexBuffer buffer;
	if (bgfx::getAvailTransientVertexBuffer(6, _BeamVertexLayout) < 6) {
		return;
	}
	bgfx::allocTransientVertexBuffer(&buffer, 6, _BeamVertexLayout);

	BackendBeamVertex * vertex = (BackendBeamVertex *)buffer.data;
	vertex[0] = { beam.StartX + nx, beam.StartY + ny, 0.0f, 0.0f, beam.Color, beam.StartDepth };
	vertex[1] = { beam.EndX + nx, beam.EndY + ny, 1.0f, 0.0f, beam.Color, beam.EndDepth };
	vertex[2] = { beam.EndX - nx, beam.EndY - ny, 1.0f, 1.0f, beam.Color, beam.EndDepth };
	vertex[3] = { beam.StartX + nx, beam.StartY + ny, 0.0f, 0.0f, beam.Color, beam.StartDepth };
	vertex[4] = { beam.EndX - nx, beam.EndY - ny, 1.0f, 1.0f, beam.Color, beam.EndDepth };
	vertex[5] = { beam.StartX - nx, beam.StartY - ny, 0.0f, 1.0f, beam.Color, beam.StartDepth };

	bgfx::setVertexBuffer(0, &buffer);
	bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
	bgfx::submit(view, program);
}


/// <summary>
/// Draws every queued GPU particle that uses the given texture (BACKEND_INVALID_TEXTURE
/// meaning "the plain soft circular falloff", same as any other group) as one batched
/// draw call: all of them packed into a single transient vertex buffer rather than one
/// submit per particle, since there can be a lot of these in one frame. depthparams/
/// visibilityparams/visibilityflags/originflip are Run_GPU_Beam_Composite's own, passed
/// through unchanged -- a particle's occlusion and lighting test is identical to a
/// beam's.
/// </summary>
static void Submit_Particle_Batch(bgfx::ViewId view, BackendTextureHandle texture, float const * depthparams, float const * visibilityparams, float const * visibilityflags, float originflip)
{
	size_t count = 0;
	for (size_t index = 0; index < _ParticleQueue.size(); index++) {
		if (_ParticleQueue[index].Texture == texture) {
			count++;
		}
	}
	if (count == 0) {
		return;
	}

	// Capped to whatever the transient buffer pool actually has room for this frame;
	// particles beyond that are simply skipped rather than failing the whole batch, since
	// losing a few off-screen-adjacent sparks is far less noticeable than losing every
	// particle drawn this frame because one oversized batch failed to allocate.
	uint32_t available = bgfx::getAvailTransientVertexBuffer((uint32_t)(count * 6), _BeamVertexLayout);
	uint32_t quadstoallocate = available / 6;
	if (quadstoallocate == 0) {
		return;
	}

	bgfx::TransientVertexBuffer buffer;
	bgfx::allocTransientVertexBuffer(&buffer, quadstoallocate * 6, _BeamVertexLayout);
	BackendBeamVertex * vertex = (BackendBeamVertex *)buffer.data;

	uint32_t written = 0;
	for (size_t index = 0; index < _ParticleQueue.size() && written < quadstoallocate; index++) {
		BackendGPUParticle const & particle = _ParticleQueue[index];
		if (particle.Texture != texture) {
			continue;
		}

		float half = particle.Size * 0.5f;
		BackendBeamVertex * quad = vertex + (size_t)written * 6;
		quad[0] = { particle.X - half, particle.Y - half, 0.0f, 0.0f, particle.Color, particle.Depth };
		quad[1] = { particle.X + half, particle.Y - half, 1.0f, 0.0f, particle.Color, particle.Depth };
		quad[2] = { particle.X + half, particle.Y + half, 1.0f, 1.0f, particle.Color, particle.Depth };
		quad[3] = { particle.X - half, particle.Y - half, 0.0f, 0.0f, particle.Color, particle.Depth };
		quad[4] = { particle.X + half, particle.Y + half, 1.0f, 1.0f, particle.Color, particle.Depth };
		quad[5] = { particle.X - half, particle.Y + half, 0.0f, 1.0f, particle.Color, particle.Depth };
		written++;
	}

	const unsigned int linear = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
	bgfx::TextureHandle resolved = Resolve_Loaded_Texture(texture);
	float particleparams[4] = { originflip, bgfx::isValid(resolved) ? 1.0f : 0.0f, 0.0f, 0.0f };

	bgfx::setTexture(0, _DepthSampler, _DepthSnapshotTexture, linear);
	if (visibilityflags[0] > 0.5f) {
		bgfx::setTexture(1, _VisibilitySampler, _VisibilitySnapshotTexture, linear);
	}
	if (bgfx::isValid(resolved)) {
		bgfx::setTexture(2, _ParticleTextureSampler, resolved, linear);
	}
	bgfx::setUniform(_DepthParamsUniform, depthparams);
	bgfx::setUniform(_VisibilityParamsUniform, visibilityparams);
	bgfx::setUniform(_VisibilityFlagsUniform, visibilityflags);
	bgfx::setUniform(_ParticleParamsUniform, particleparams);

	bgfx::setVertexBuffer(0, &buffer, 0, written * 6);
	bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
	bgfx::submit(view, _ParticleProgram);
}


/// <summary>
/// Looks a texture handle up in the Backend_Load_Texture cache by its BackendTextureHandle
/// index, returning an invalid bgfx handle for BACKEND_INVALID_TEXTURE or an out-of-range
/// one rather than letting a stale handle from a different renderer instance read garbage.
/// </summary>
static bgfx::TextureHandle Resolve_Loaded_Texture(BackendTextureHandle handle)
{
	if (handle == BACKEND_INVALID_TEXTURE || (size_t)handle >= _LoadedTextures.size()) {
		return BGFX_INVALID_HANDLE;
	}
	return(_LoadedTextures[handle]);
}


/// <summary>
/// Draws the base frame followed by every queued GPU beam's color pass into the beam
/// compositing target, and every distortion-enabled beam's warp vector into the distortion
/// target, and returns the color composite's texture. Called only when the queue is
/// non-empty.
/// </summary>
/// <param name="base">Either the frame texture or an earlier stage's composite.</param>
/// <param name="baseistarget">See Run_Bloom_Chain; the same origin correction applies to
/// every base texture in this pipeline, not just the one bloom reads.</param>
static bgfx::TextureHandle Run_GPU_Beam_Composite(bgfx::TextureHandle base, bool baseistarget)
{
	const unsigned int linear = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
	const unsigned int pointfiltered = linear | BGFX_SAMPLER_POINT;
	const bool baseflip = baseistarget && bgfx::getCaps()->originBottomLeft;

	bgfx::setViewFrameBuffer(VIEW_GPU_BEAM, _GPUBeamTarget);
	bgfx::setViewClear(VIEW_GPU_BEAM, BGFX_CLEAR_COLOR, 0x000000FF);
	Set_View_Transform(VIEW_GPU_BEAM, _FrameWidth, _FrameHeight);

	bgfx::setTexture(0, _TextureSampler, base, linear);
	Submit_Quad(VIEW_GPU_BEAM, _Program, 0.0f, 0.0f, (float)_FrameWidth, (float)_FrameHeight, baseflip);

	bool any_distortion = false;
	for (size_t index = 0; index < _BeamQueue.size(); index++) {
		if (_BeamQueue[index].Style.DistortionTexture != BACKEND_INVALID_TEXTURE) {
			any_distortion = true;
			break;
		}
	}
	if (any_distortion) {
		// Cleared to "no displacement" -- see _DistortionTarget's own declaration for why
		// these particular four values -- so any pixel no beam's distortion pass reaches
		// leaves the final warp pass untouched.
		bgfx::setViewFrameBuffer(VIEW_DISTORTION, _DistortionTarget);
		bgfx::setViewClear(VIEW_DISTORTION, BGFX_CLEAR_COLOR, 0x00FFFF80);
		Set_View_Transform(VIEW_DISTORTION, _FrameWidth, _FrameHeight);
	}

	if (_HasDepthSnapshot && bgfx::isValid(_DepthSnapshotTexture)) {
		float depthparams[4] = {
			(float)_DepthSnapshotOriginX, (float)_DepthSnapshotOriginY,
			1.0f / (float)_DepthSnapshotWidth, 1.0f / (float)_DepthSnapshotHeight
		};
		float originflip = bgfx::getCaps()->originBottomLeft ? 1.0f : 0.0f;

		float visibilityparams[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		float visibilityflags[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		if (_HasVisibilitySnapshot && bgfx::isValid(_VisibilitySnapshotTexture)) {
			visibilityparams[0] = (float)_VisibilitySnapshotOriginX;
			visibilityparams[1] = (float)_VisibilitySnapshotOriginY;
			visibilityparams[2] = 1.0f / (float)_VisibilitySnapshotWidth;
			visibilityparams[3] = 1.0f / (float)_VisibilitySnapshotHeight;
			visibilityflags[0] = 1.0f;
		}

		for (size_t index = 0; index < _BeamQueue.size(); index++) {
			BackendGPUBeam const & beam = _BeamQueue[index];
			BackendGPUBeamStyle const & style = beam.Style;

			float beamparams[4] = { originflip, style.ScrollPhase, 0.0f, _HasAmbientSnapshot ? 1.0f : 0.0f };
			float sheetparams[4] = { style.SheetFrame, (float)style.SheetHorizontal, (float)style.SheetVertical, style.IsSheet ? 1.0f : 0.0f };

			bgfx::TextureHandle colortexture = Resolve_Loaded_Texture(style.Texture);
			unsigned int texfilter = style.PointFilter ? pointfiltered : linear;
			// LaserTextureNoStretch (bullet mapping) repeats the texture along the beam by
			// letting its own U coordinate wrap rather than clamp; direct mapping clamps so
			// one copy of the texture stretches across the whole length.
			unsigned int colorsamplerflags = texfilter | (style.NoStretch && !style.IsSheet ? 0 : BGFX_SAMPLER_U_CLAMP);
			beamparams[2] = bgfx::isValid(colortexture) ? 1.0f : 0.0f;

			bgfx::setTexture(0, _DepthSampler, _DepthSnapshotTexture, linear);
			if (bgfx::isValid(colortexture)) {
				bgfx::setTexture(1, _BeamTextureSampler, colortexture, colorsamplerflags);
			}
			if (_HasAmbientSnapshot && bgfx::isValid(_AmbientSnapshotTexture)) {
				bgfx::setTexture(2, _AmbientSampler, _AmbientSnapshotTexture, linear);
			}
			if (visibilityflags[0] > 0.5f) {
				bgfx::setTexture(3, _VisibilitySampler, _VisibilitySnapshotTexture, linear);
			}
			bgfx::setUniform(_DepthParamsUniform, depthparams);
			bgfx::setUniform(_BeamParamsUniform, beamparams);
			bgfx::setUniform(_BeamSheetParamsUniform, sheetparams);
			bgfx::setUniform(_VisibilityParamsUniform, visibilityparams);
			bgfx::setUniform(_VisibilityFlagsUniform, visibilityflags);
			Submit_Beam_Quad(VIEW_GPU_BEAM, _GPUBeamProgram, beam);

			bgfx::TextureHandle distorttexture = Resolve_Loaded_Texture(style.DistortionTexture);
			if (any_distortion && bgfx::isValid(distorttexture)) {
				float distortparams[4] = { style.DistortionDisplacement, 0.0f, 0.0f, 0.0f };
				bgfx::setTexture(0, _DepthSampler, _DepthSnapshotTexture, linear);
				bgfx::setTexture(1, _DistortSampler, distorttexture, texfilter | BGFX_SAMPLER_U_CLAMP);
				if (visibilityflags[0] > 0.5f) {
					bgfx::setTexture(3, _VisibilitySampler, _VisibilitySnapshotTexture, linear);
				}
				bgfx::setUniform(_DepthParamsUniform, depthparams);
				bgfx::setUniform(_BeamParamsUniform, beamparams);
				bgfx::setUniform(_BeamSheetParamsUniform, sheetparams);
				bgfx::setUniform(_DistortParamsUniform, distortparams);
				bgfx::setUniform(_VisibilityParamsUniform, visibilityparams);
				bgfx::setUniform(_VisibilityFlagsUniform, visibilityflags);
				Submit_Beam_Quad(VIEW_DISTORTION, _GPUBeamDistortProgram, beam);
			}
		}

		// EXTENSION: particles, batched per distinct texture (including
		// BACKEND_INVALID_TEXTURE, the plain soft-falloff group) rather than one submit
		// per particle -- see Submit_Particle_Batch's own doc comment.
		if (!_ParticleQueue.empty()) {
			std::vector<BackendTextureHandle> seen;
			for (size_t index = 0; index < _ParticleQueue.size(); index++) {
				BackendTextureHandle texture = _ParticleQueue[index].Texture;
				bool already = false;
				for (size_t seenindex = 0; seenindex < seen.size(); seenindex++) {
					if (seen[seenindex] == texture) {
						already = true;
						break;
					}
				}
				if (!already) {
					seen.push_back(texture);
				}
			}
			for (size_t index = 0; index < seen.size(); index++) {
				Submit_Particle_Batch(VIEW_GPU_BEAM, seen[index], depthparams, visibilityparams, visibilityflags, originflip);
			}
		}
	}
	// Without a depth snapshot there is nothing to occlude against; beams and particles
	// already queued this frame are simply dropped rather than drawn unoccluded on top of
	// everything, since that would be a more visible wrong answer than not drawing them
	// at all.

	_DistortionQueuedThisFrame = any_distortion && _HasDepthSnapshot;
	return(bgfx::getTexture(_GPUBeamTarget));
}


/// <summary>
/// Re-samples the given base texture through the distortion target's warp vectors and
/// returns the warped result. Called only when Run_GPU_Beam_Composite found at least one
/// distortion-enabled beam this frame.
/// </summary>
/// <param name="base">Whatever stage last produced -- the beam composite itself, most
/// likely, since this runs immediately after it.</param>
/// <param name="baseistarget">See Run_Bloom_Chain.</param>
static bgfx::TextureHandle Run_Distortion_Warp(bgfx::TextureHandle base, bool baseistarget)
{
	const unsigned int linear = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
	const bool baseflip = baseistarget && bgfx::getCaps()->originBottomLeft;

	bgfx::setViewFrameBuffer(VIEW_DISTORTION_WARP, _DistortionWarpOutputTarget);
	Set_View_Transform(VIEW_DISTORTION_WARP, _FrameWidth, _FrameHeight);

	bgfx::setTexture(0, _TextureSampler, base, linear);
	bgfx::setTexture(1, _DistortSampler, bgfx::getTexture(_DistortionTarget), linear);
	Submit_Quad(VIEW_DISTORTION_WARP, _DistortWarpProgram, 0.0f, 0.0f, (float)_FrameWidth, (float)_FrameHeight, baseflip);

	return(bgfx::getTexture(_DistortionWarpOutputTarget));
}


/// <summary>
/// Submits one overlay quad: an axis-aligned rectangle at a single depth (all four
/// corners share it, unlike a beam's per-vertex range), using the same vertex layout and
/// depth-tested program a beam's own color pass uses.
/// </summary>
static void Submit_Overlay_Quad(bgfx::ViewId view, float x, float y, float width, float height, float depth, unsigned int color)
{
	bgfx::TransientVertexBuffer buffer;
	if (bgfx::getAvailTransientVertexBuffer(6, _BeamVertexLayout) < 6) {
		return;
	}
	bgfx::allocTransientVertexBuffer(&buffer, 6, _BeamVertexLayout);

	BackendBeamVertex * vertex = (BackendBeamVertex *)buffer.data;
	vertex[0] = { x, y, 0.0f, 0.0f, color, depth };
	vertex[1] = { x + width, y, 1.0f, 0.0f, color, depth };
	vertex[2] = { x + width, y + height, 1.0f, 1.0f, color, depth };
	vertex[3] = { x, y, 0.0f, 0.0f, color, depth };
	vertex[4] = { x + width, y + height, 1.0f, 1.0f, color, depth };
	vertex[5] = { x, y + height, 0.0f, 1.0f, color, depth };

	bgfx::setVertexBuffer(0, &buffer);
	bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
	bgfx::submit(view, _AnimOverlayProgram);
}


/// <summary>
/// Draws the base frame followed by every queued GPU overlay quad into the overlay
/// compositing target, and returns that target's texture. Called only when the queue is
/// non-empty; Backend_Present reads straight from the frame texture otherwise. Each quad
/// is depth- and ambient-tested against the same snapshots a GPU beam's color pass uses,
/// rather than always drawing unconditionally on top of everything -- the "always draws
/// last" part of a GPU overlay anim is purely about when in the software draw order its
/// GPU quad gets composited in, not about skipping occlusion against the rest of the
/// scene.
/// </summary>
/// <param name="base">Either the frame texture or an earlier stage's composite.</param>
/// <param name="baseistarget">See Run_Bloom_Chain.</param>
static bgfx::TextureHandle Run_Overlay_Composite(bgfx::TextureHandle base, bool baseistarget)
{
	const unsigned int linear = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
	const bool baseflip = baseistarget && bgfx::getCaps()->originBottomLeft;

	bgfx::setViewFrameBuffer(VIEW_ANIM_OVERLAY, _AnimOverlayTarget);
	bgfx::setViewClear(VIEW_ANIM_OVERLAY, BGFX_CLEAR_COLOR, 0x000000FF);
	Set_View_Transform(VIEW_ANIM_OVERLAY, _FrameWidth, _FrameHeight);

	bgfx::setTexture(0, _TextureSampler, base, linear);
	Submit_Quad(VIEW_ANIM_OVERLAY, _Program, 0.0f, 0.0f, (float)_FrameWidth, (float)_FrameHeight, baseflip);

	if (_HasDepthSnapshot && bgfx::isValid(_DepthSnapshotTexture)) {
		float depthparams[4] = {
			(float)_DepthSnapshotOriginX, (float)_DepthSnapshotOriginY,
			1.0f / (float)_DepthSnapshotWidth, 1.0f / (float)_DepthSnapshotHeight
		};
		float originflip = bgfx::getCaps()->originBottomLeft ? 1.0f : 0.0f;
		float beamparams[4] = { originflip, 0.0f, 0.0f, _HasAmbientSnapshot ? 1.0f : 0.0f };

		float visibilityparams[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		float visibilityflags[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		if (_HasVisibilitySnapshot && bgfx::isValid(_VisibilitySnapshotTexture)) {
			visibilityparams[0] = (float)_VisibilitySnapshotOriginX;
			visibilityparams[1] = (float)_VisibilitySnapshotOriginY;
			visibilityparams[2] = 1.0f / (float)_VisibilitySnapshotWidth;
			visibilityparams[3] = 1.0f / (float)_VisibilitySnapshotHeight;
			visibilityflags[0] = 1.0f;
		}

		for (size_t index = 0; index < _OverlayQueue.size(); index++) {
			BackendOverlayQuad const & quad = _OverlayQueue[index];
			if (!bgfx::isValid(quad.Texture)) {
				continue;
			}
			bgfx::setTexture(0, _DepthSampler, _DepthSnapshotTexture, linear);
			bgfx::setTexture(1, _TextureSampler, quad.Texture, linear);
			if (_HasAmbientSnapshot && bgfx::isValid(_AmbientSnapshotTexture)) {
				bgfx::setTexture(2, _AmbientSampler, _AmbientSnapshotTexture, linear);
			}
			if (visibilityflags[0] > 0.5f) {
				bgfx::setTexture(3, _VisibilitySampler, _VisibilitySnapshotTexture, linear);
			}
			bgfx::setUniform(_DepthParamsUniform, depthparams);
			bgfx::setUniform(_BeamParamsUniform, beamparams);
			bgfx::setUniform(_VisibilityParamsUniform, visibilityparams);
			bgfx::setUniform(_VisibilityFlagsUniform, visibilityflags);
			Submit_Overlay_Quad(VIEW_ANIM_OVERLAY, quad.DestX, quad.DestY, quad.DestWidth, quad.DestHeight, quad.Depth, 0xFFFFFFFF);
		}
	}
	// Without a depth snapshot -- which Backend_Has_Queued_Overlay_Quads exists so a
	// caller can avoid this in the first place -- queued quads are dropped rather than
	// drawn unoccluded, the same choice Run_GPU_Beam_Composite makes for beams.

	return(bgfx::getTexture(_AnimOverlayTarget));
}


/// <summary>
/// Discards the atmosphere pass's output target.
/// </summary>
static void Destroy_Atmosphere_Target(void)
{
	if (bgfx::isValid(_AtmosphereTarget)) {
		bgfx::destroy(_AtmosphereTarget);
		_AtmosphereTarget = BGFX_INVALID_HANDLE;
	}
	_AtmosphereFrameWidth = 0;
	_AtmosphereFrameHeight = 0;
}


/// <summary>
/// Makes sure the atmosphere pass's output target is sized for the given frame.
/// </summary>
/// <returns>bool; Is the target ready to render into?</returns>
static bool Ensure_Atmosphere_Target(int framewidth, int frameheight)
{
	if (bgfx::isValid(_AtmosphereTarget) && _AtmosphereFrameWidth == framewidth && _AtmosphereFrameHeight == frameheight) {
		return(true);
	}

	Destroy_Atmosphere_Target();

	const bgfx::Caps * caps = bgfx::getCaps();
	if (framewidth <= 0 || frameheight <= 0
		|| framewidth > caps->limits.maxTextureSize || frameheight > caps->limits.maxTextureSize) {
		return(false);
	}

	_AtmosphereTarget = bgfx::createFrameBuffer((uint16_t)framewidth, (uint16_t)frameheight, bgfx::TextureFormat::BGRA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
	if (!bgfx::isValid(_AtmosphereTarget)) {
		return(false);
	}

	_AtmosphereFrameWidth = framewidth;
	_AtmosphereFrameHeight = frameheight;
	return(true);
}


/// <summary>
/// Tints the given base texture with this frame's weather/water configuration (see
/// Backend_Set_Atmosphere) and returns the result. Called only when at least one of the
/// two is actually enabled; Backend_Present reads straight from base otherwise.
/// </summary>
/// <param name="base">Whatever stage last produced -- the overlay composite, most
/// likely, since this runs immediately after it.</param>
/// <param name="baseistarget">See Run_Bloom_Chain.</param>
static bgfx::TextureHandle Run_Atmosphere_Pass(bgfx::TextureHandle base, bool baseistarget)
{
	const unsigned int linear = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
	const bool baseflip = baseistarget && bgfx::getCaps()->originBottomLeft;
	const bool originflip = bgfx::getCaps()->originBottomLeft;

	bgfx::setViewFrameBuffer(VIEW_ATMOSPHERE, _AtmosphereTarget);
	Set_View_Transform(VIEW_ATMOSPHERE, _FrameWidth, _FrameHeight);

	float visibilityparams[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	bool hasvisibility = _HasVisibilitySnapshot && bgfx::isValid(_VisibilitySnapshotTexture);
	if (hasvisibility) {
		visibilityparams[0] = (float)_VisibilitySnapshotOriginX;
		visibilityparams[1] = (float)_VisibilitySnapshotOriginY;
		visibilityparams[2] = 1.0f / (float)_VisibilitySnapshotWidth;
		visibilityparams[3] = 1.0f / (float)_VisibilitySnapshotHeight;
	}

	float atmosphereflags[4] = {
		originflip ? 1.0f : 0.0f,
		_WeatherConfig.Enabled ? 1.0f : 0.0f,
		_WaterConfig.Enabled ? 1.0f : 0.0f,
		hasvisibility ? 1.0f : 0.0f
	};
	float weatherparams[4] = { _WeatherConfig.ScrollX, _WeatherConfig.ScrollY, _WeatherConfig.Magnification, _WeatherConfig.Intensity };
	float waterparams[4] = { _WaterConfig.ScrollX, _WaterConfig.ScrollY, _WaterConfig.Frame, _WaterConfig.Intensity };
	float watertiling[4] = { _WaterConfig.TilingX, _WaterConfig.TilingY, 0.0f, 0.0f };
	float waterfog[4] = {
		(float)((_WaterConfig.FogColor >> 0) & 0xFF) / 255.0f,
		(float)((_WaterConfig.FogColor >> 8) & 0xFF) / 255.0f,
		(float)((_WaterConfig.FogColor >> 16) & 0xFF) / 255.0f,
		(float)((_WaterConfig.FogColor >> 24) & 0xFF) / 255.0f
	};

	bgfx::setTexture(0, _TextureSampler, base, linear);
	if (hasvisibility) {
		bgfx::setTexture(1, _VisibilitySampler, _VisibilitySnapshotTexture, linear);
	}
	if (_WeatherConfig.Enabled) {
		bgfx::TextureHandle weathertexture = Resolve_Loaded_Texture(_WeatherConfig.Texture);
		if (bgfx::isValid(weathertexture)) {
			bgfx::setTexture(2, _WeatherSampler, weathertexture, BGFX_SAMPLER_NONE);
		} else {
			atmosphereflags[1] = 0.0f;
		}
	}
	if (_WaterConfig.Enabled) {
		bgfx::TextureHandle watertexture = Resolve_Loaded_Texture(_WaterConfig.Texture);
		if (bgfx::isValid(watertexture)) {
			bgfx::setTexture(3, _WaterSampler, watertexture, BGFX_SAMPLER_NONE);
		} else {
			atmosphereflags[2] = 0.0f;
		}
	}

	bgfx::setUniform(_VisibilityParamsUniform, visibilityparams);
	bgfx::setUniform(_AtmosphereFlagsUniform, atmosphereflags);
	bgfx::setUniform(_WeatherParamsUniform, weatherparams);
	bgfx::setUniform(_WaterParamsUniform, waterparams);
	bgfx::setUniform(_WaterTilingUniform, watertiling);
	bgfx::setUniform(_WaterFogColorUniform, waterfog);

	Submit_Quad(VIEW_ATMOSPHERE, _AtmosphereProgram, 0.0f, 0.0f, (float)_FrameWidth, (float)_FrameHeight, baseflip);

	return(bgfx::getTexture(_AtmosphereTarget));
}


/// <summary>
/// Runs the bloom chain over the given base texture and returns the resulting texture.
/// </summary>
/// <param name="base">Either the frame texture or the overlay composite, whichever the
/// caller last produced.</param>
/// <param name="baseistarget">True when base is something bgfx rendered to (the overlay
/// composite) rather than the uploaded frame texture, so every read of it needs the same
/// origin correction the pixel art filter's own prescale target already needs when the
/// present pass samples it.</param>
static bgfx::TextureHandle Run_Bloom_Chain(bgfx::TextureHandle base, bool baseistarget, float threshold, float intensity)
{
	const bool flip = bgfx::getCaps()->originBottomLeft;
	const unsigned int linear = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
	const bool baseflip = baseistarget && flip;

	float bloomparams[4] = { threshold, intensity, 0.0f, 0.0f };

	bgfx::setViewFrameBuffer(VIEW_BLOOM_BRIGHT, _BloomExtractTarget);
	bgfx::setViewClear(VIEW_BLOOM_BRIGHT, BGFX_CLEAR_COLOR, 0x00000000);
	Set_View_Transform(VIEW_BLOOM_BRIGHT, _BloomWidth, _BloomHeight);
	bgfx::setUniform(_BloomParamsUniform, bloomparams);
	bgfx::setTexture(0, _TextureSampler, base, linear);
	Submit_Quad(VIEW_BLOOM_BRIGHT, _BloomBrightProgram, 0.0f, 0.0f, (float)_BloomWidth, (float)_BloomHeight, baseflip);

	float blurdirh[4] = { 1.0f / (float)_BloomWidth, 0.0f, 0.0f, 0.0f };
	bgfx::setViewFrameBuffer(VIEW_BLOOM_BLUR_H, _BloomBlurTargetA);
	Set_View_Transform(VIEW_BLOOM_BLUR_H, _BloomWidth, _BloomHeight);
	bgfx::setUniform(_BloomDirUniform, blurdirh);
	bgfx::setTexture(0, _TextureSampler, bgfx::getTexture(_BloomExtractTarget), linear);
	Submit_Quad(VIEW_BLOOM_BLUR_H, _BloomBlurProgram, 0.0f, 0.0f, (float)_BloomWidth, (float)_BloomHeight, flip);

	float blurdirv[4] = { 0.0f, 1.0f / (float)_BloomHeight, 0.0f, 0.0f };
	bgfx::setViewFrameBuffer(VIEW_BLOOM_BLUR_V, _BloomBlurTargetB);
	Set_View_Transform(VIEW_BLOOM_BLUR_V, _BloomWidth, _BloomHeight);
	bgfx::setUniform(_BloomDirUniform, blurdirv);
	bgfx::setTexture(0, _TextureSampler, bgfx::getTexture(_BloomBlurTargetA), linear);
	Submit_Quad(VIEW_BLOOM_BLUR_V, _BloomBlurProgram, 0.0f, 0.0f, (float)_BloomWidth, (float)_BloomHeight, flip);

	bgfx::setViewFrameBuffer(VIEW_BLOOM_COMBINE, _PostFXTarget);
	Set_View_Transform(VIEW_BLOOM_COMBINE, _FrameWidth, _FrameHeight);
	bgfx::setUniform(_BloomParamsUniform, bloomparams);
	bgfx::setTexture(0, _TextureSampler, base, linear);
	bgfx::setTexture(1, _BloomSampler, bgfx::getTexture(_BloomBlurTargetB), linear);
	Submit_Quad(VIEW_BLOOM_COMBINE, _BloomCombineProgram, 0.0f, 0.0f, (float)_FrameWidth, (float)_FrameHeight, baseflip);

	return(bgfx::getTexture(_PostFXTarget));
}


/// <summary>
/// Starts the renderer on an existing window.
/// </summary>
/// <param name="window">The window the frame is presented into.</param>
/// <param name="drawablewidth">The drawable area's width in physical pixels.</param>
/// <param name="drawableheight">The drawable area's height in physical pixels.</param>
/// <param name="renderer">Which graphics API to ask for, or auto to let bgfx decide.</param>
/// <param name="vsync">Should presents wait for the display's refresh?</param>
/// <returns>bool; Did the renderer start?</returns>
bool Backend_Init(NativeWindow const & window, int drawablewidth, int drawableheight, BackendRenderer renderer, bool vsync)
{
	if (_Initialized) {
		return(true);
	}

	// Presents happen at whatever depth the engine has reached, including from inside a
	// dialog's paint handler, so the renderer has to run on this thread. Calling
	// renderFrame before init is what selects that.
	bgfx::renderFrame();

	_DrawableWidth = drawablewidth;
	_DrawableHeight = drawableheight;
	_ResetFlags = BGFX_RESET_FLIP_AFTER_RENDER | (vsync ? BGFX_RESET_VSYNC : BGFX_RESET_NONE);

	bgfx::Init init;
	init.platformData.ndt = window.Display;
	init.platformData.nwh = window.Handle;
	init.platformData.type = window.Type == NATIVE_WINDOW_WAYLAND
		? bgfx::NativeWindowHandleType::Wayland
		: bgfx::NativeWindowHandleType::Default;
	init.resolution.width = (uint32_t)drawablewidth;
	init.resolution.height = (uint32_t)drawableheight;
	init.resolution.reset = _ResetFlags;
	init.callback = &_Callback;
	init.allocator = &_Allocator;

	switch (renderer) {
		case BACKEND_RENDERER_D3D11:
			init.type = bgfx::RendererType::Direct3D11;
			break;

		case BACKEND_RENDERER_D3D12:
			init.type = bgfx::RendererType::Direct3D12;
			break;

		case BACKEND_RENDERER_VULKAN:
			init.type = bgfx::RendererType::Vulkan;
			break;

		case BACKEND_RENDERER_OPENGL:
			init.type = bgfx::RendererType::OpenGL;
			break;

		default:
			init.type = bgfx::RendererType::Count;
			break;
	}

	if (!bgfx::init(init)) {
		return(false);
	}

	_VertexLayout.begin()
		.add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
		.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
		.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
		.end();

	// EXTENSION: matches BackendBeamVertex; TexCoord1 carries the per-vertex depth a beam
	// quad's fragment shader tests against the depth snapshot.
	_BeamVertexLayout.begin()
		.add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
		.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
		.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
		.add(bgfx::Attrib::TexCoord1, 1, bgfx::AttribType::Float)
		.end();

	bgfx::RendererType::Enum type = bgfx::getRendererType();
	bgfx::ShaderHandle vertexshader = bgfx::createEmbeddedShader(_EmbeddedShaders, type, "vs_ocornut_imgui");
	bgfx::ShaderHandle fragmentshader = bgfx::createEmbeddedShader(_EmbeddedShaders, type, "fs_ocornut_imgui");

	if (!bgfx::isValid(vertexshader) || !bgfx::isValid(fragmentshader)) {
		bgfx::shutdown();
		return(false);
	}

	_Program = bgfx::createProgram(vertexshader, fragmentshader, true);
	_TextureSampler = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);

	if (!bgfx::isValid(_Program) || !bgfx::isValid(_TextureSampler)) {
		bgfx::shutdown();
		return(false);
	}

	// EXTENSION: the bloom chain's own shaders. One vertex shader covers every pass; only
	// the fragment shader differs between the bright-pass, blur and combine steps.
	bgfx::ShaderHandle postfxvertex = bgfx::createEmbeddedShader(_EmbeddedShaders, type, "vs_postfx");
	bgfx::ShaderHandle brightfragment = bgfx::createEmbeddedShader(_EmbeddedShaders, type, "fs_bloom_bright");
	bgfx::ShaderHandle blurfragment = bgfx::createEmbeddedShader(_EmbeddedShaders, type, "fs_bloom_blur");
	bgfx::ShaderHandle combinefragment = bgfx::createEmbeddedShader(_EmbeddedShaders, type, "fs_bloom_combine");

	if (!bgfx::isValid(postfxvertex) || !bgfx::isValid(brightfragment)
		|| !bgfx::isValid(blurfragment) || !bgfx::isValid(combinefragment)) {
		bgfx::shutdown();
		return(false);
	}

	// The three programs each destroy their own fragment shader; the vertex shader is
	// shared, so only the last program created is allowed to take it with it.
	_BloomBrightProgram = bgfx::createProgram(postfxvertex, brightfragment, false);
	_BloomBlurProgram = bgfx::createProgram(postfxvertex, blurfragment, false);
	_BloomCombineProgram = bgfx::createProgram(postfxvertex, combinefragment, true);
	bgfx::destroy(brightfragment);
	bgfx::destroy(blurfragment);
	bgfx::destroy(combinefragment);

	_BloomSampler = bgfx::createUniform("s_bloom", bgfx::UniformType::Sampler);
	_BloomParamsUniform = bgfx::createUniform("u_bloomParams", bgfx::UniformType::Vec4);
	_BloomDirUniform = bgfx::createUniform("u_blurDir", bgfx::UniformType::Vec4);

	if (!bgfx::isValid(_BloomBrightProgram) || !bgfx::isValid(_BloomBlurProgram) || !bgfx::isValid(_BloomCombineProgram)
		|| !bgfx::isValid(_BloomSampler) || !bgfx::isValid(_BloomParamsUniform) || !bgfx::isValid(_BloomDirUniform)) {
		bgfx::shutdown();
		return(false);
	}

	// EXTENSION: the GPU beam's own shaders, plus the distortion pass, its warp, and the
	// anim overlay shader (which shares the beam's own vertex shader and depth test).
	bgfx::ShaderHandle beamvertex = bgfx::createEmbeddedShader(_EmbeddedShaders, type, "vs_gpubeam");
	bgfx::ShaderHandle beamfragment = bgfx::createEmbeddedShader(_EmbeddedShaders, type, "fs_gpubeam");
	bgfx::ShaderHandle beamdistortfragment = bgfx::createEmbeddedShader(_EmbeddedShaders, type, "fs_gpubeam_distort");
	bgfx::ShaderHandle distortwarpvertex = bgfx::createEmbeddedShader(_EmbeddedShaders, type, "vs_postfx");
	bgfx::ShaderHandle distortwarpfragment = bgfx::createEmbeddedShader(_EmbeddedShaders, type, "fs_distortwarp");
	bgfx::ShaderHandle animoverlayfragment = bgfx::createEmbeddedShader(_EmbeddedShaders, type, "fs_anim_overlay");

	// EXTENSION: particles reuse vs_gpubeam (a fresh handle, since beamvertex above is
	// consumed by _AnimOverlayProgram's own destroyShaders=true) and get their own small
	// fragment shader.
	bgfx::ShaderHandle particlevertex = bgfx::createEmbeddedShader(_EmbeddedShaders, type, "vs_gpubeam");
	bgfx::ShaderHandle particlefragment = bgfx::createEmbeddedShader(_EmbeddedShaders, type, "fs_particle");

	// EXTENSION: the atmosphere (weather/water) pass reuses vs_postfx, again via its own
	// fresh handle since distortwarpvertex above is consumed by _DistortWarpProgram's own
	// destroyShaders=true.
	bgfx::ShaderHandle atmospherevertex = bgfx::createEmbeddedShader(_EmbeddedShaders, type, "vs_postfx");
	bgfx::ShaderHandle atmospherefragment = bgfx::createEmbeddedShader(_EmbeddedShaders, type, "fs_atmosphere");

	if (!bgfx::isValid(beamvertex) || !bgfx::isValid(beamfragment) || !bgfx::isValid(beamdistortfragment)
		|| !bgfx::isValid(distortwarpvertex) || !bgfx::isValid(distortwarpfragment) || !bgfx::isValid(animoverlayfragment)
		|| !bgfx::isValid(particlevertex) || !bgfx::isValid(particlefragment)
		|| !bgfx::isValid(atmospherevertex) || !bgfx::isValid(atmospherefragment)) {
		bgfx::shutdown();
		return(false);
	}

	_GPUBeamProgram = bgfx::createProgram(beamvertex, beamfragment, false);
	_GPUBeamDistortProgram = bgfx::createProgram(beamvertex, beamdistortfragment, false);
	_AnimOverlayProgram = bgfx::createProgram(beamvertex, animoverlayfragment, true);
	_DistortWarpProgram = bgfx::createProgram(distortwarpvertex, distortwarpfragment, true);
	_ParticleProgram = bgfx::createProgram(particlevertex, particlefragment, true);
	_AtmosphereProgram = bgfx::createProgram(atmospherevertex, atmospherefragment, true);
	bgfx::destroy(beamfragment);
	bgfx::destroy(beamdistortfragment);

	_DepthSampler = bgfx::createUniform("s_depth", bgfx::UniformType::Sampler);
	_AmbientSampler = bgfx::createUniform("s_ambient", bgfx::UniformType::Sampler);
	_VisibilitySampler = bgfx::createUniform("s_visibility", bgfx::UniformType::Sampler);
	_VisibilityParamsUniform = bgfx::createUniform("u_visibilityParams", bgfx::UniformType::Vec4);
	_VisibilityFlagsUniform = bgfx::createUniform("u_visibilityFlags", bgfx::UniformType::Vec4);
	_DepthParamsUniform = bgfx::createUniform("u_depthParams", bgfx::UniformType::Vec4);
	_BeamParamsUniform = bgfx::createUniform("u_beamParams", bgfx::UniformType::Vec4);
	_BeamSheetParamsUniform = bgfx::createUniform("u_beamSheetParams", bgfx::UniformType::Vec4);
	_BeamTextureSampler = bgfx::createUniform("s_beamtex", bgfx::UniformType::Sampler);
	_DistortSampler = bgfx::createUniform("s_distorttex", bgfx::UniformType::Sampler);
	_DistortParamsUniform = bgfx::createUniform("u_distortParams", bgfx::UniformType::Vec4);
	_ParticleTextureSampler = bgfx::createUniform("s_particletex", bgfx::UniformType::Sampler);
	_ParticleParamsUniform = bgfx::createUniform("u_particleParams", bgfx::UniformType::Vec4);
	_WeatherSampler = bgfx::createUniform("s_weathertex", bgfx::UniformType::Sampler);
	_WaterSampler = bgfx::createUniform("s_watertex", bgfx::UniformType::Sampler);
	_AtmosphereFlagsUniform = bgfx::createUniform("u_atmosphereFlags", bgfx::UniformType::Vec4);
	_WeatherParamsUniform = bgfx::createUniform("u_weatherParams", bgfx::UniformType::Vec4);
	_WaterParamsUniform = bgfx::createUniform("u_waterParams", bgfx::UniformType::Vec4);
	_WaterTilingUniform = bgfx::createUniform("u_waterTiling", bgfx::UniformType::Vec4);
	_WaterFogColorUniform = bgfx::createUniform("u_waterFogColor", bgfx::UniformType::Vec4);

	if (!bgfx::isValid(_GPUBeamProgram) || !bgfx::isValid(_GPUBeamDistortProgram) || !bgfx::isValid(_AnimOverlayProgram) || !bgfx::isValid(_DistortWarpProgram)
		|| !bgfx::isValid(_ParticleProgram) || !bgfx::isValid(_ParticleTextureSampler) || !bgfx::isValid(_ParticleParamsUniform)
		|| !bgfx::isValid(_AtmosphereProgram) || !bgfx::isValid(_WeatherSampler) || !bgfx::isValid(_WaterSampler)
		|| !bgfx::isValid(_AtmosphereFlagsUniform) || !bgfx::isValid(_WeatherParamsUniform) || !bgfx::isValid(_WaterParamsUniform)
		|| !bgfx::isValid(_WaterTilingUniform) || !bgfx::isValid(_WaterFogColorUniform)
		|| !bgfx::isValid(_DepthSampler) || !bgfx::isValid(_AmbientSampler) || !bgfx::isValid(_VisibilitySampler) || !bgfx::isValid(_VisibilityParamsUniform) || !bgfx::isValid(_VisibilityFlagsUniform)
		|| !bgfx::isValid(_DepthParamsUniform) || !bgfx::isValid(_BeamParamsUniform)
		|| !bgfx::isValid(_BeamSheetParamsUniform) || !bgfx::isValid(_BeamTextureSampler)
		|| !bgfx::isValid(_DistortSampler) || !bgfx::isValid(_DistortParamsUniform)) {
		bgfx::shutdown();
		return(false);
	}

	_Initialized = true;
	return(true);
}


/// <summary>
/// Shuts the renderer down and releases everything it created.
/// </summary>
void Backend_Shutdown(void)
{
	if (!_Initialized) {
		return;
	}

	Destroy_Prescale_Target();
	Destroy_Bloom_Targets();
	Destroy_Overlay_Target();
	Discard_Overlay_Queue();
	Destroy_Beam_Target();
	Discard_Beam_Queue();

	if (bgfx::isValid(_DepthSnapshotTexture)) {
		bgfx::destroy(_DepthSnapshotTexture);
		_DepthSnapshotTexture = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_AmbientSnapshotTexture)) {
		bgfx::destroy(_AmbientSnapshotTexture);
		_AmbientSnapshotTexture = BGFX_INVALID_HANDLE;
	}

	if (bgfx::isValid(_FrameTexture)) {
		bgfx::destroy(_FrameTexture);
		_FrameTexture = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_TextureSampler)) {
		bgfx::destroy(_TextureSampler);
		_TextureSampler = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_Program)) {
		bgfx::destroy(_Program);
		_Program = BGFX_INVALID_HANDLE;
	}

	if (bgfx::isValid(_BloomBrightProgram)) {
		bgfx::destroy(_BloomBrightProgram);
		_BloomBrightProgram = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_BloomBlurProgram)) {
		bgfx::destroy(_BloomBlurProgram);
		_BloomBlurProgram = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_BloomCombineProgram)) {
		bgfx::destroy(_BloomCombineProgram);
		_BloomCombineProgram = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_BloomSampler)) {
		bgfx::destroy(_BloomSampler);
		_BloomSampler = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_BloomParamsUniform)) {
		bgfx::destroy(_BloomParamsUniform);
		_BloomParamsUniform = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_BloomDirUniform)) {
		bgfx::destroy(_BloomDirUniform);
		_BloomDirUniform = BGFX_INVALID_HANDLE;
	}

	if (bgfx::isValid(_GPUBeamProgram)) {
		bgfx::destroy(_GPUBeamProgram);
		_GPUBeamProgram = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_GPUBeamDistortProgram)) {
		bgfx::destroy(_GPUBeamDistortProgram);
		_GPUBeamDistortProgram = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_AnimOverlayProgram)) {
		bgfx::destroy(_AnimOverlayProgram);
		_AnimOverlayProgram = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_DistortWarpProgram)) {
		bgfx::destroy(_DistortWarpProgram);
		_DistortWarpProgram = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_ParticleProgram)) {
		bgfx::destroy(_ParticleProgram);
		_ParticleProgram = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_ParticleTextureSampler)) {
		bgfx::destroy(_ParticleTextureSampler);
		_ParticleTextureSampler = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_ParticleParamsUniform)) {
		bgfx::destroy(_ParticleParamsUniform);
		_ParticleParamsUniform = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_AtmosphereProgram)) {
		bgfx::destroy(_AtmosphereProgram);
		_AtmosphereProgram = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_WeatherSampler)) {
		bgfx::destroy(_WeatherSampler);
		_WeatherSampler = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_WaterSampler)) {
		bgfx::destroy(_WaterSampler);
		_WaterSampler = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_AtmosphereFlagsUniform)) {
		bgfx::destroy(_AtmosphereFlagsUniform);
		_AtmosphereFlagsUniform = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_WeatherParamsUniform)) {
		bgfx::destroy(_WeatherParamsUniform);
		_WeatherParamsUniform = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_WaterParamsUniform)) {
		bgfx::destroy(_WaterParamsUniform);
		_WaterParamsUniform = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_WaterTilingUniform)) {
		bgfx::destroy(_WaterTilingUniform);
		_WaterTilingUniform = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_WaterFogColorUniform)) {
		bgfx::destroy(_WaterFogColorUniform);
		_WaterFogColorUniform = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_AtmosphereTarget)) {
		bgfx::destroy(_AtmosphereTarget);
		_AtmosphereTarget = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_DepthSampler)) {
		bgfx::destroy(_DepthSampler);
		_DepthSampler = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_AmbientSampler)) {
		bgfx::destroy(_AmbientSampler);
		_AmbientSampler = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_VisibilitySampler)) {
		bgfx::destroy(_VisibilitySampler);
		_VisibilitySampler = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_VisibilityParamsUniform)) {
		bgfx::destroy(_VisibilityParamsUniform);
		_VisibilityParamsUniform = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_VisibilityFlagsUniform)) {
		bgfx::destroy(_VisibilityFlagsUniform);
		_VisibilityFlagsUniform = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_VisibilitySnapshotTexture)) {
		bgfx::destroy(_VisibilitySnapshotTexture);
		_VisibilitySnapshotTexture = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_DepthParamsUniform)) {
		bgfx::destroy(_DepthParamsUniform);
		_DepthParamsUniform = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_BeamParamsUniform)) {
		bgfx::destroy(_BeamParamsUniform);
		_BeamParamsUniform = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_BeamSheetParamsUniform)) {
		bgfx::destroy(_BeamSheetParamsUniform);
		_BeamSheetParamsUniform = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_BeamTextureSampler)) {
		bgfx::destroy(_BeamTextureSampler);
		_BeamTextureSampler = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_DistortSampler)) {
		bgfx::destroy(_DistortSampler);
		_DistortSampler = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_DistortParamsUniform)) {
		bgfx::destroy(_DistortParamsUniform);
		_DistortParamsUniform = BGFX_INVALID_HANDLE;
	}
	if (bgfx::isValid(_DistortionTarget)) {
		bgfx::destroy(_DistortionTarget);
		_DistortionTarget = BGFX_INVALID_HANDLE;
	}

	// EXTENSION: every texture Backend_Load_Texture ever cached.
	for (size_t index = 0; index < _LoadedTextures.size(); index++) {
		if (bgfx::isValid(_LoadedTextures[index])) {
			bgfx::destroy(_LoadedTextures[index]);
		}
	}
	_LoadedTextures.clear();
	_LoadedTextureCache.clear();

	delete [] _ConvertBuffer;
	_ConvertBuffer = NULL;

	bgfx::shutdown();

	_FrameWidth = 0;
	_FrameHeight = 0;
	_Initialized = false;
}


/// <summary>
/// Points the renderer at a frame of the given size, replacing any earlier one.
/// </summary>
/// <returns>bool; Is a texture of that size ready to receive frames?</returns>
bool Backend_Set_Frame_Size(int width, int height)
{
	if (!_Initialized || width <= 0 || height <= 0) {
		return(false);
	}

	if (bgfx::isValid(_FrameTexture) && _FrameWidth == width && _FrameHeight == height) {
		return(true);
	}

	if (bgfx::isValid(_FrameTexture)) {
		bgfx::destroy(_FrameTexture);
		_FrameTexture = BGFX_INVALID_HANDLE;
	}

	// bgfx names packed formats from their low bits up, so its B5G6R5 is the layout the
	// game already draws in. Emulated support would convert every upload on the way
	// through, which is what the fallback below does more cheaply.
	const bgfx::Caps * caps = bgfx::getCaps();
	_FrameIs565 = (caps->formats[bgfx::TextureFormat::B5G6R5] & BGFX_CAPS_FORMAT_TEXTURE_2D) != 0;

	_FrameTexture = bgfx::createTexture2D((uint16_t)width, (uint16_t)height, false, 1, _FrameIs565 ? bgfx::TextureFormat::B5G6R5 : bgfx::TextureFormat::BGRA8);
	if (!bgfx::isValid(_FrameTexture)) {
		return(false);
	}

	delete [] _ConvertBuffer;
	_ConvertBuffer = NULL;

	// EXTENSION: the table is also what Backend_Queue_Overlay_Quad uses to widen its own
	// 565 sources to BGRA8, which it needs unconditionally since overlay quads carry an
	// alpha channel the frame texture itself does not, so this no longer builds only on
	// the emulated-565 path.
	if (_ConvertTable[0xFFFF] == 0) {
		Build_Convert_Table();
	}

	if (!_FrameIs565) {
		_ConvertBuffer = new unsigned int[width * height];
	}

	_FrameWidth = width;
	_FrameHeight = height;
	return(true);
}


/// <summary>
/// Tells the renderer the drawable area changed size.
/// </summary>
void Backend_On_Resize(int drawablewidth, int drawableheight)
{
	if (!_Initialized || drawablewidth <= 0 || drawableheight <= 0) {
		return;
	}

	if (_DrawableWidth == drawablewidth && _DrawableHeight == drawableheight) {
		return;
	}

	_DrawableWidth = drawablewidth;
	_DrawableHeight = drawableheight;
	bgfx::reset((uint32_t)drawablewidth, (uint32_t)drawableheight, _ResetFlags);
}


/// <summary>
/// Uploads the frame and puts it on the screen.
/// </summary>
/// <param name="pixels">The frame's top left pixel, in 16 bit 565.</param>
/// <param name="pitch">The bytes between one row of that frame and the next.</param>
/// <param name="destx">Where the left edge of the frame lands in the window.</param>
/// <param name="desty">Where the top edge of the frame lands in the window.</param>
/// <param name="destwidth">How wide the frame is drawn.</param>
/// <param name="destheight">How tall the frame is drawn.</param>
/// <param name="mode">How the frame is filtered when it is drawn larger than it is.</param>
/// <param name="postfx">Full-screen GPU effect to run on the frame before scaling it.</param>
/// <param name="bloomthreshold">Luma above which a pixel starts contributing to the glow.</param>
/// <param name="bloomintensity">How strongly the blurred glow is added back into the frame.</param>
void Backend_Present(void const * pixels, int pitch, int destx, int desty, int destwidth, int destheight, BackendScaleMode mode,
	BackendPostFX postfx, float bloomthreshold, float bloomintensity)
{
	if (!_Initialized || pixels == NULL || !bgfx::isValid(_FrameTexture)) {
		Discard_Overlay_Queue();
		Discard_Beam_Queue();
		_HasDepthSnapshot = false;
		_HasVisibilitySnapshot = false;
		return;
	}

	// A minimized window has no client area to present into.
	if (_DrawableWidth <= 0 || _DrawableHeight <= 0) {
		Discard_Overlay_Queue();
		Discard_Beam_Queue();
		_HasDepthSnapshot = false;
		_HasVisibilitySnapshot = false;
		return;
	}

	if (_FrameIs565) {
		bgfx::updateTexture2D(_FrameTexture, 0, 0, 0, 0, (uint16_t)_FrameWidth, (uint16_t)_FrameHeight, bgfx::copy(pixels, (uint32_t)(_FrameHeight * pitch)), (uint16_t)pitch);
	} else if (_ConvertBuffer != NULL) {
		for (int y = 0; y < _FrameHeight; y++) {
			unsigned short const * source = (unsigned short const *)((char const *)pixels + y * pitch);
			unsigned int * dest = _ConvertBuffer + y * _FrameWidth;
			for (int x = 0; x < _FrameWidth; x++) {
				dest[x] = _ConvertTable[source[x]];
			}
		}
		bgfx::updateTexture2D(_FrameTexture, 0, 0, 0, 0, (uint16_t)_FrameWidth, (uint16_t)_FrameHeight, bgfx::copy(_ConvertBuffer, (uint32_t)(_FrameWidth * _FrameHeight * 4)), (uint16_t)(_FrameWidth * 4));
	}

	bgfx::TextureHandle source = _FrameTexture;
	unsigned int samplerflags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
	const bool originflip = bgfx::getCaps()->originBottomLeft;

	// True once `source` is something bgfx rendered to rather than the uploaded frame
	// texture. Reading such a target as a texture needs a V flip on backends whose texture
	// space and render target space disagree about which edge is row zero; the uploaded
	// frame texture never needs it.
	bool source_from_target = false;

	if (mode == BACKEND_SCALE_NEAREST) {
		samplerflags |= BGFX_SAMPLER_POINT;
	}

	// EXTENSION: GPU beams composite onto the base frame first, since Run_GPU_Beam_Composite
	// needs to know what came before it was the plain frame texture rather than an earlier
	// stage's target, for its own origin correction.
	if ((!_BeamQueue.empty() || !_ParticleQueue.empty()) && Ensure_Beam_Target(_FrameWidth, _FrameHeight)) {
		source = Run_GPU_Beam_Composite(source, source_from_target);
		source_from_target = true;

		// The warp pass only runs when at least one beam actually had a distortion
		// texture and a depth snapshot was available to test it against; both are checked
		// inside Run_GPU_Beam_Composite, which sets this flag accordingly.
		if (_DistortionQueuedThisFrame) {
			source = Run_Distortion_Warp(source, source_from_target);
			source_from_target = true;
		}
	}

	// EXTENSION: GPU overlay quads composite onto the base frame before anything else
	// touches it, so bloom (and the scale filter after it) treat the composited result
	// exactly like the plain frame texture they replace. Skipped entirely, at zero cost,
	// when nothing queued a quad this frame.
	if (!_OverlayQueue.empty() && Ensure_Overlay_Target(_FrameWidth, _FrameHeight)) {
		source = Run_Overlay_Composite(source, source_from_target);
		source_from_target = true;
	}

	// EXTENSION: weather/water tint everything composited so far -- terrain, beams,
	// particles, and overlay anims alike -- before bloom picks up the result. Skipped
	// entirely, at zero cost, when neither is enabled.
	if ((_WeatherConfig.Enabled || _WaterConfig.Enabled) && Ensure_Atmosphere_Target(_FrameWidth, _FrameHeight)) {
		source = Run_Atmosphere_Pass(source, source_from_target);
		source_from_target = true;
	}

	// EXTENSION: bloom runs before the scale filter, on the frame at its native
	// resolution, so a later magnify pass enlarges the glow along with everything else
	// rather than sampling a lower-resolution glow into a sharper image.
	if (postfx == BACKEND_POSTFX_BLOOM && Ensure_Bloom_Targets(_FrameWidth, _FrameHeight)) {
		source = Run_Bloom_Chain(source, source_from_target, bloomthreshold, bloomintensity);
		source_from_target = true;
	}

	// The pixel art filter keeps whole pixels whole. An exact multiple needs nothing but
	// point sampling; anything else is magnified to the next whole multiple with point
	// sampling and then shrunk to the window smoothly, which keeps edges sharp without
	// the uneven pixel sizes that point sampling alone would give.
	if (mode == BACKEND_SCALE_PIXELART && destwidth > _FrameWidth && destheight > _FrameHeight) {
		if ((destwidth % _FrameWidth) == 0 && (destheight % _FrameHeight) == 0) {
			samplerflags |= BGFX_SAMPLER_POINT;
		} else {
			int scale = (destwidth + _FrameWidth - 1) / _FrameWidth;
			int scaley = (destheight + _FrameHeight - 1) / _FrameHeight;
			if (scaley > scale) {
				scale = scaley;
			}

			if (Ensure_Prescale_Target(_FrameWidth * scale, _FrameHeight * scale)) {
				bgfx::setViewFrameBuffer(VIEW_PRESCALE, _PrescaleTarget);
				bgfx::setViewClear(VIEW_PRESCALE, BGFX_CLEAR_COLOR, 0x000000FF);
				Set_View_Transform(VIEW_PRESCALE, _PrescaleWidth, _PrescaleHeight);
				bgfx::setTexture(0, _TextureSampler, source, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_POINT);
				Submit_Quad(VIEW_PRESCALE, _Program, 0.0f, 0.0f, (float)_PrescaleWidth, (float)_PrescaleHeight, source_from_target && originflip);
				source = bgfx::getTexture(_PrescaleTarget);
				source_from_target = true;
			}
		}
	}

	// Clearing the whole window is what paints the bars beside a frame that does not
	// share the window's shape.
	bgfx::setViewFrameBuffer(VIEW_PRESENT, BGFX_INVALID_HANDLE);
	bgfx::setViewClear(VIEW_PRESENT, BGFX_CLEAR_COLOR, 0x000000FF);
	Set_View_Transform(VIEW_PRESENT, _DrawableWidth, _DrawableHeight);

	bool flipv = source_from_target && originflip;
	bgfx::setTexture(0, _TextureSampler, source, samplerflags);
	Submit_Quad(VIEW_PRESENT, _Program, (float)destx, (float)desty, (float)destwidth, (float)destheight, flipv);

	bgfx::frame();

	// The queue's textures were only ever needed for the views submitted above; bgfx has
	// copied whatever it needed from them by the time frame() returns. The depth snapshot
	// is one-frame-lived too -- a later frame with no beams queued must not reuse stale
	// depth data from whenever it was last uploaded.
	Discard_Overlay_Queue();
	Discard_Beam_Queue();
	_HasDepthSnapshot = false;
	_HasVisibilitySnapshot = false;
}


/// <summary>
/// Queues one GPU overlay quad for the next Backend_Present call. See the declaration in
/// bgfxbackend.h for the contract pixels/pitch/keycolor565/depth have to satisfy.
/// </summary>
void Backend_Queue_Overlay_Quad(void const * pixels, int pitch, int width, int height, int destx, int desty, int destwidth, int destheight, unsigned short keycolor565, float depth)
{
	if (!_Initialized || pixels == NULL || width <= 0 || height <= 0 || destwidth <= 0 || destheight <= 0) {
		return;
	}

	// The table only exists once Backend_Set_Frame_Size has run at least once; before
	// that there is no frame to composite onto anyway.
	if (_ConvertTable[0xFFFF] == 0) {
		return;
	}

	unsigned int * converted = new unsigned int[(size_t)width * (size_t)height];

	for (int y = 0; y < height; y++) {
		unsigned short const * sourcerow = (unsigned short const *)((unsigned char const *)pixels + (size_t)y * pitch);
		unsigned int * destrow = converted + (size_t)y * width;

		for (int x = 0; x < width; x++) {
			unsigned short texel = sourcerow[x];
			// The key color becomes fully transparent black rather than whatever opaque
			// color _ConvertTable would otherwise widen it to.
			destrow[x] = (texel == keycolor565) ? 0x00000000 : _ConvertTable[texel];
		}
	}

	bgfx::TextureHandle texture = bgfx::createTexture2D((uint16_t)width, (uint16_t)height, false, 1, bgfx::TextureFormat::BGRA8,
		BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, bgfx::copy(converted, (uint32_t)((size_t)width * (size_t)height * 4)));

	delete [] converted;

	if (!bgfx::isValid(texture)) {
		return;
	}

	BackendOverlayQuad quad;
	quad.Texture = texture;
	quad.DestX = (float)destx;
	quad.DestY = (float)desty;
	quad.DestWidth = (float)destwidth;
	quad.DestHeight = (float)destheight;
	quad.Depth = depth * BEAM_DEPTH_SCALE;
	_OverlayQueue.push_back(quad);
}


/// <summary>
/// True once at least one overlay quad has been queued this frame. See the declaration in
/// bgfxbackend.h for why this exists.
/// </summary>
bool Backend_Has_Queued_Overlay_Quads(void)
{
	return(!_OverlayQueue.empty());
}


/// <summary>
/// Uploads a fresh depth snapshot for the next Backend_Present call. See the declaration
/// in bgfxbackend.h for the contract depths/pitch has to satisfy.
/// </summary>
void Backend_Upload_Depth_Snapshot(void const * depths, int pitch, int width, int height, int originx, int originy)
{
	if (!_Initialized || depths == NULL || width <= 0 || height <= 0) {
		_HasDepthSnapshot = false;
		_HasVisibilitySnapshot = false;
		return;
	}

	float * normalized = new float[(size_t)width * (size_t)height];

	for (int y = 0; y < height; y++) {
		unsigned short const * sourcerow = (unsigned short const *)((unsigned char const *)depths + (size_t)y * pitch);
		float * destrow = normalized + (size_t)y * width;

		for (int x = 0; x < width; x++) {
			destrow[x] = (float)sourcerow[x] * BEAM_DEPTH_SCALE;
		}
	}

	// A texture already sized correctly is updated in place rather than recreated, since
	// the snapshot is taken fresh every frame a GPU beam needs one.
	if (!bgfx::isValid(_DepthSnapshotTexture) || _DepthSnapshotWidth != width || _DepthSnapshotHeight != height) {
		if (bgfx::isValid(_DepthSnapshotTexture)) {
			bgfx::destroy(_DepthSnapshotTexture);
		}
		_DepthSnapshotTexture = bgfx::createTexture2D((uint16_t)width, (uint16_t)height, false, 1, bgfx::TextureFormat::R32F, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_POINT);
		_DepthSnapshotWidth = width;
		_DepthSnapshotHeight = height;
	}

	if (!bgfx::isValid(_DepthSnapshotTexture)) {
		delete [] normalized;
		_HasDepthSnapshot = false;
		_HasVisibilitySnapshot = false;
		return;
	}

	bgfx::updateTexture2D(_DepthSnapshotTexture, 0, 0, 0, 0, (uint16_t)width, (uint16_t)height,
		bgfx::copy(normalized, (uint32_t)((size_t)width * (size_t)height * 4)), (uint16_t)(width * 4));

	delete [] normalized;

	_DepthSnapshotOriginX = originx;
	_DepthSnapshotOriginY = originy;
	_HasDepthSnapshot = true;
}


/// <summary>
/// Uploads a fresh ambient/shroud lighting snapshot for the next Backend_Present call. See
/// the declaration in bgfxbackend.h for the contract values/pitch has to satisfy.
/// </summary>
void Backend_Upload_Ambient_Snapshot(void const * values, int pitch, int width, int height, int originx, int originy)
{
	(void)originx;
	(void)originy;

	if (!_Initialized || values == NULL || width <= 0 || height <= 0) {
		_HasAmbientSnapshot = false;
		return;
	}

	// DSurface::Draw_Depth_Shaded_Line's own darkening divides by 128 (a right shift of 7
	// bits), not the reference package's 127 -- this engine's actual convention, not the
	// one the feature is modeled on.
	static const float AMBIENT_SCALE = 1.0f / 128.0f;

	float * normalized = new float[(size_t)width * (size_t)height];

	for (int y = 0; y < height; y++) {
		unsigned short const * sourcerow = (unsigned short const *)((unsigned char const *)values + (size_t)y * pitch);
		float * destrow = normalized + (size_t)y * width;

		for (int x = 0; x < width; x++) {
			destrow[x] = (float)sourcerow[x] * AMBIENT_SCALE;
		}
	}

	if (!bgfx::isValid(_AmbientSnapshotTexture) || _AmbientSnapshotWidth != width || _AmbientSnapshotHeight != height) {
		if (bgfx::isValid(_AmbientSnapshotTexture)) {
			bgfx::destroy(_AmbientSnapshotTexture);
		}
		_AmbientSnapshotTexture = bgfx::createTexture2D((uint16_t)width, (uint16_t)height, false, 1, bgfx::TextureFormat::R32F, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_POINT);
		_AmbientSnapshotWidth = width;
		_AmbientSnapshotHeight = height;
	}

	if (!bgfx::isValid(_AmbientSnapshotTexture)) {
		delete [] normalized;
		_HasAmbientSnapshot = false;
		return;
	}

	bgfx::updateTexture2D(_AmbientSnapshotTexture, 0, 0, 0, 0, (uint16_t)width, (uint16_t)height,
		bgfx::copy(normalized, (uint32_t)((size_t)width * (size_t)height * 4)), (uint16_t)(width * 4));

	delete [] normalized;

	_HasAmbientSnapshot = true;
}


/// <summary>
/// Uploads a fresh shroud/fog visibility snapshot for the next Backend_Present call. See
/// the declaration in bgfxbackend.h for the contract values has to satisfy. Unlike the
/// depth and ambient snapshots, this one's source is already exactly width x height bytes
/// with no padding to account for, since Tactical::Capture_GPU_Visibility_Mask builds it
/// fresh each call rather than copying it out of an existing ring-buffered surface.
/// </summary>
void Backend_Upload_Visibility_Snapshot(void const * values, int width, int height, int originx, int originy)
{
	if (!_Initialized || values == NULL || width <= 0 || height <= 0) {
		_HasVisibilitySnapshot = false;
		return;
	}

	if (!bgfx::isValid(_VisibilitySnapshotTexture) || _VisibilitySnapshotWidth != width || _VisibilitySnapshotHeight != height) {
		if (bgfx::isValid(_VisibilitySnapshotTexture)) {
			bgfx::destroy(_VisibilitySnapshotTexture);
		}
		// Point-sampled: this is a coarse, cell-granularity mask, and linear filtering
		// would just blur its already-blocky boundaries rather than smoothing anything
		// meaningful.
		_VisibilitySnapshotTexture = bgfx::createTexture2D((uint16_t)width, (uint16_t)height, false, 1, bgfx::TextureFormat::R8,
			BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_POINT);
		_VisibilitySnapshotWidth = width;
		_VisibilitySnapshotHeight = height;
	}

	if (!bgfx::isValid(_VisibilitySnapshotTexture)) {
		_HasVisibilitySnapshot = false;
		return;
	}

	bgfx::updateTexture2D(_VisibilitySnapshotTexture, 0, 0, 0, 0, (uint16_t)width, (uint16_t)height,
		bgfx::copy(values, (uint32_t)((size_t)width * (size_t)height)), (uint16_t)width);

	_VisibilitySnapshotOriginX = originx;
	_VisibilitySnapshotOriginY = originy;
	_HasVisibilitySnapshot = true;
}


/// <summary>
/// Queues one depth-tested GPU beam for the next Backend_Present call. See the
/// declaration in bgfxbackend.h for what start/end depth mean.
/// </summary>
void Backend_Queue_GPU_Beam(float startx, float starty, float startdepth, float endx, float endy, float enddepth, float width, unsigned int color, BackendGPUBeamStyle const & style)
{
	if (!_Initialized || width <= 0.0f) {
		return;
	}

	BackendGPUBeam beam;
	beam.StartX = startx;
	beam.StartY = starty;
	beam.StartDepth = startdepth * BEAM_DEPTH_SCALE;
	beam.EndX = endx;
	beam.EndY = endy;
	beam.EndDepth = enddepth * BEAM_DEPTH_SCALE;
	beam.Width = width;
	beam.Color = color;
	beam.Style = style;
	_BeamQueue.push_back(beam);
}


/// <summary>
/// Queues one GPU particle for the next Backend_Present call. See the declaration in
/// bgfxbackend.h for what each parameter means.
/// </summary>
void Backend_Queue_GPU_Particle(float x, float y, float depth, float size, unsigned int color, BackendTextureHandle texture)
{
	if (!_Initialized || size <= 0.0f) {
		return;
	}

	BackendGPUParticle particle;
	particle.X = x;
	particle.Y = y;
	particle.Depth = depth * BEAM_DEPTH_SCALE;
	particle.Size = size;
	particle.Color = color;
	particle.Texture = texture;
	_ParticleQueue.push_back(particle);
}


/// <summary>
/// Sets this frame's weather/water configuration. See the declaration in bgfxbackend.h.
/// </summary>
void Backend_Set_Atmosphere(BackendWeatherConfig const & weather, BackendWaterConfig const & water)
{
	_WeatherConfig = weather;
	_WaterConfig = water;
}


/// <summary>
/// Decodes a whole image file's bytes to a freshly allocated BGRA8 buffer, trying bimg's
/// container parser first (DDS, KTX, PVR3, handling block-compressed formats too) and
/// falling back to stb_image (PNG, TGA, BMP, JPG, and the rest of its own format list) for
/// anything bimg doesn't recognize.
/// </summary>
/// <returns>bool; Did decoding succeed? outpixels is caller-owned (delete[]) on success,
/// untouched on failure.</returns>
static bool Decode_Image_To_Bgra8(void const * filedata, unsigned int filesize, int & outwidth, int & outheight, unsigned int * & outpixels)
{
	bimg::ImageContainer container;
	bx::Error error;
	if (bimg::imageParse(container, filedata, filesize, &error)) {
		bimg::ImageMip mip;
		if (bimg::imageGetRawData(container, 0, 0, filedata, filesize, mip)) {
			unsigned int * converted = new unsigned int[(size_t)mip.m_width * (size_t)mip.m_height];
			bimg::imageDecodeToBgra8(NULL, converted, mip.m_data, mip.m_width, mip.m_height, mip.m_width * 4, mip.m_format);
			outwidth = mip.m_width;
			outheight = mip.m_height;
			outpixels = converted;
			return(true);
		}
	}

	int width = 0, height = 0, sourcechannels = 0;
	stbi_uc * decoded = stbi_load_from_memory((stbi_uc const *)filedata, (int)filesize, &width, &height, &sourcechannels, 4);
	if (decoded == NULL) {
		return(false);
	}

	// stb_image's 4-channel request always returns RGBA byte order regardless of the
	// source format, which BGRA8 would read back to front; unlike bimg's own texture
	// format enum, there is no bgfx-side "this buffer happens to be RGBA" flag to set
	// short of just creating the texture as RGBA8 instead, so the swap happens here to
	// keep Backend_Load_Texture's own contract ("always BGRA8") true regardless of which
	// decoder handled a given file.
	unsigned int * converted = new unsigned int[(size_t)width * (size_t)height];
	for (size_t i = 0; i < (size_t)width * (size_t)height; i++) {
		unsigned char r = decoded[i * 4 + 0];
		unsigned char g = decoded[i * 4 + 1];
		unsigned char b = decoded[i * 4 + 2];
		unsigned char a = decoded[i * 4 + 3];
		converted[i] = ((unsigned int)a << 24) | ((unsigned int)r << 16) | ((unsigned int)g << 8) | (unsigned int)b;
	}
	stbi_image_free(decoded);

	outwidth = width;
	outheight = height;
	outpixels = converted;
	return(true);
}


/// <summary>
/// Registers an already-created texture under cachekey and returns its handle, or
/// destroys it and caches the failure if the handle table is somehow exhausted.
/// </summary>
static BackendTextureHandle Register_Loaded_Texture(char const * cachekey, bgfx::TextureHandle texture)
{
	if (!bgfx::isValid(texture)) {
		_LoadedTextureCache[cachekey] = BACKEND_INVALID_TEXTURE;
		return(BACKEND_INVALID_TEXTURE);
	}

	if (_LoadedTextures.size() >= BACKEND_INVALID_TEXTURE) {
		// In practice this is many thousands of distinct textures; reaching it means
		// something is generating cache keys instead of reusing them.
		bgfx::destroy(texture);
		_LoadedTextureCache[cachekey] = BACKEND_INVALID_TEXTURE;
		return(BACKEND_INVALID_TEXTURE);
	}

	BackendTextureHandle handle = (BackendTextureHandle)_LoadedTextures.size();
	_LoadedTextures.push_back(texture);
	_LoadedTextureCache[cachekey] = handle;
	return(handle);
}


/// <summary>
/// Loads (and caches by cachekey) a GPU texture from a whole file's bytes. See the
/// declaration in bgfxbackend.h for the format support and caching contract.
/// </summary>
BackendTextureHandle Backend_Load_Texture(char const * cachekey, void const * filedata, unsigned int filesize)
{
	if (!_Initialized || cachekey == NULL || filedata == NULL || filesize == 0) {
		return(BACKEND_INVALID_TEXTURE);
	}

	std::unordered_map<std::string, BackendTextureHandle>::iterator cached = _LoadedTextureCache.find(cachekey);
	if (cached != _LoadedTextureCache.end()) {
		return(cached->second);
	}

	int width = 0, height = 0;
	unsigned int * pixels = NULL;
	if (!Decode_Image_To_Bgra8(filedata, filesize, width, height, pixels)) {
		// Cached as a failure too, so a texture that fails to decode is not re-decoded on
		// every subsequent call for the same cachekey within this run.
		_LoadedTextureCache[cachekey] = BACKEND_INVALID_TEXTURE;
		return(BACKEND_INVALID_TEXTURE);
	}

	bgfx::TextureHandle texture = bgfx::createTexture2D((uint16_t)width, (uint16_t)height, false, 1, bgfx::TextureFormat::BGRA8,
		BGFX_SAMPLER_NONE, bgfx::copy(pixels, (uint32_t)((size_t)width * (size_t)height * 4)));

	delete [] pixels;

	return(Register_Loaded_Texture(cachekey, texture));
}


/// <summary>
/// Loads a numbered sequence of frame files into one combined sheet texture. See the
/// declaration in bgfxbackend.h for the grid layout and mismatched-frame-size contract.
/// </summary>
BackendTextureHandle Backend_Load_Texture_Sequence(char const * cachekey, void const * const * filedatas, unsigned int const * filesizes, unsigned int count, int * out_columns, int * out_rows, unsigned int * out_loaded_count)
{
	if (out_columns != NULL) {
		*out_columns = 0;
	}
	if (out_rows != NULL) {
		*out_rows = 0;
	}
	if (out_loaded_count != NULL) {
		*out_loaded_count = 0;
	}

	if (!_Initialized || cachekey == NULL || filedatas == NULL || filesizes == NULL || count == 0) {
		return(BACKEND_INVALID_TEXTURE);
	}

	std::unordered_map<std::string, BackendTextureHandle>::iterator cached = _LoadedTextureCache.find(cachekey);
	if (cached != _LoadedTextureCache.end()) {
		// A cache hit here only promises the texture, not out_columns/rows/loaded_count --
		// in practice every caller already knows its own frame count going in (it built
		// filedatas/filesizes from it), so this has never mattered.
		return(cached->second);
	}

	int cellwidth = 0, cellheight = 0;
	unsigned int * cellpixels = NULL;
	if (!Decode_Image_To_Bgra8(filedatas[0], filesizes[0], cellwidth, cellheight, cellpixels)) {
		_LoadedTextureCache[cachekey] = BACKEND_INVALID_TEXTURE;
		return(BACKEND_INVALID_TEXTURE);
	}

	// A roughly square grid keeps the combined texture's own dimensions from growing
	// lopsided for a large frame count; which exact rectangle the frames land in past
	// that doesn't matter, since the sheet-slicing shader math only cares about the
	// column/row count, not the pixel dimensions of the whole sheet.
	int columns = (int)ceilf(sqrtf((float)count));
	int rows = (int)ceilf((float)count / (float)columns);
	int atlaswidth = columns * cellwidth;

	unsigned int * atlas = new unsigned int[(size_t)atlaswidth * (size_t)rows * cellheight];
	memset(atlas, 0, (size_t)atlaswidth * (size_t)rows * cellheight * sizeof(unsigned int));

	unsigned int loaded = 0;
	for (unsigned int frame = 0; frame < count; frame++) {
		unsigned int * framepixels = cellpixels;
		bool owned = false;

		if (frame > 0) {
			int framewidth = 0, frameheight = 0;
			if (!Decode_Image_To_Bgra8(filedatas[frame], filesizes[frame], framewidth, frameheight, framepixels)) {
				break;
			}
			if (framewidth != cellwidth || frameheight != cellheight) {
				delete [] framepixels;
				break;
			}
			owned = true;
		}

		int cellx = (frame % columns) * cellwidth;
		int celly = (frame / columns) * cellheight;
		for (int y = 0; y < cellheight; y++) {
			memcpy(atlas + (size_t)(celly + y) * atlaswidth + cellx, framepixels + (size_t)y * cellwidth, (size_t)cellwidth * sizeof(unsigned int));
		}

		if (owned) {
			delete [] framepixels;
		}
		loaded++;
	}

	delete [] cellpixels;

	if (loaded == 0) {
		delete [] atlas;
		_LoadedTextureCache[cachekey] = BACKEND_INVALID_TEXTURE;
		return(BACKEND_INVALID_TEXTURE);
	}

	bgfx::TextureHandle texture = bgfx::createTexture2D((uint16_t)atlaswidth, (uint16_t)(rows * cellheight), false, 1, bgfx::TextureFormat::BGRA8,
		BGFX_SAMPLER_NONE, bgfx::copy(atlas, (uint32_t)((size_t)atlaswidth * rows * cellheight * sizeof(unsigned int))));

	delete [] atlas;

	BackendTextureHandle handle = Register_Loaded_Texture(cachekey, texture);
	if (handle != BACKEND_INVALID_TEXTURE) {
		// out_columns/out_rows describe the texture's actual grid -- what the sheet-uv
		// shader math needs -- while out_loaded_count is the smaller "how many of those
		// cells are real" number animation cycling should use instead.
		if (out_columns != NULL) {
			*out_columns = columns;
		}
		if (out_rows != NULL) {
			*out_rows = rows;
		}
		if (out_loaded_count != NULL) {
			*out_loaded_count = loaded;
		}
	}
	return(handle);
}


/// <summary>
/// Names the graphics API the renderer settled on.
/// </summary>
char const * Backend_Renderer_Name(void)
{
	if (!_Initialized) {
		return("none");
	}
	return(bgfx::getRendererName(bgfx::getRendererType()));
}
