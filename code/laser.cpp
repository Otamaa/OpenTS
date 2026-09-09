/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "laser.h"

#include "_map.h"
#include "_rect.h"
#include "_surface.h"
#include "_tactica.h"
#include "abuffer.h"
#include "bgfxbackend.h"
#include "bsurface.h"
#include "ccfile.h"
#include "ccrand.h"
#include "coord.h"
#include "data.h"
#include "dsurface.h"
#include "globals.h"
#include "goptions.h"
#include "inline.h"
#include "mouse.h"
#include "rgb.h"
#include "scenario.h"
#include "tactical.h"
#include "weapon.h"
#include "zbuffer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

DynamicVectorClass<LaserDrawClass *> LaserDrawClass::LaserDraws;


/// <summary>
/// EXTENSION: loads a laser's GPU beam texture by filename, the same way any other game
/// asset is loaded -- CCFileClass so it can come from a mix, Load_Alloc_Data to read the
/// whole file at once, then handed to the renderer's own cache keyed by that same
/// filename, so a second laser using the same texture is a cache hit rather than a second
/// decode. allowcache false forces a fresh decode even on a cache hit, for
/// AllowTextureCache=no; see Load_Laser_Texture_Sequence's own doc comment for why this
/// engine's default for that tag is the opposite of the reference's own.
/// </summary>
/// <returns>BACKEND_INVALID_TEXTURE for an empty filename or a file that fails to load.</returns>
static BackendTextureHandle Load_Laser_Texture(TStringID<64> const & filename, bool allowcache)
{
	if (filename.empty()) {
		return(BACKEND_INVALID_TEXTURE);
	}

	CCFileClass file((char const *)filename);
	if (!file.Is_Available()) {
		return(BACKEND_INVALID_TEXTURE);
	}

	int size = file.Size();
	void * data = Load_Alloc_Data(file);
	if (data == NULL || size <= 0) {
		delete [] (char *)data;
		return(BACKEND_INVALID_TEXTURE);
	}

	// AllowTextureCache=no gets a cache key nothing else will ever ask for again, which
	// is enough on its own to force a fresh decode without needing a second code path
	// through Backend_Load_Texture for "don't actually cache this".
	char cachekey[80];
	if (allowcache) {
		strncpy(cachekey, (char const *)filename, sizeof(cachekey) - 1);
		cachekey[sizeof(cachekey) - 1] = '\0';
	} else {
		static unsigned int uncached_counter = 0;
		sprintf(cachekey, "%s#%u", (char const *)filename, uncached_counter++);
	}

	BackendTextureHandle texture = Backend_Load_Texture(cachekey, data, (unsigned int)size);
	delete [] (char *)data;
	return(texture);
}


/// <summary>
/// EXTENSION: loads a TexturePackageName sequence -- NAME 0000.EXT, NAME 0001.EXT, and so
/// on (mind the space before the number) -- into one combined sheet texture. Stops at the
/// first number that fails to open, same as the reference this is modeled on documents.
/// out_columns/out_rows/out_framecount take Backend_Load_Texture_Sequence's own meanings.
/// AllowTextureCache=no's own reasoning is the same as Load_Laser_Texture's own; the
/// reference recommends against caching specifically for DDS, whose decode is the more
/// expensive of the two paths this engine supports, but that recommendation is tied to a
/// D3D9 texture pool quirk this renderer doesn't have, so nothing here treats it specially.
/// </summary>
static BackendTextureHandle Load_Laser_Texture_Sequence(TStringID<64> const & packagename, TStringID<8> const & extension, bool allowcache, int & out_columns, int & out_rows, unsigned int & out_framecount)
{
	out_columns = 0;
	out_rows = 0;
	out_framecount = 0;

	if (packagename.empty()) {
		return(BACKEND_INVALID_TEXTURE);
	}

	// A sane upper bound rather than an unbounded search -- nothing sane ships a beam
	// with more frames than this, and it keeps a typo'd TexturePackageName that happens to
	// match zero files from silently trying thousands of Opens before giving up.
	static const unsigned int MAX_SEQUENCE_FRAMES = 256;

	std::vector<void *> buffers;
	std::vector<unsigned int> sizes;

	for (unsigned int frame = 0; frame < MAX_SEQUENCE_FRAMES; frame++) {
		char filename[80];
		sprintf(filename, "%s %04u.%s", (char const *)packagename, frame, (char const *)extension);

		CCFileClass file(filename);
		if (!file.Is_Available()) {
			break;
		}

		int size = file.Size();
		void * data = Load_Alloc_Data(file);
		if (data == NULL || size <= 0) {
			delete [] (char *)data;
			break;
		}

		buffers.push_back(data);
		sizes.push_back((unsigned int)size);
	}

	if (buffers.empty()) {
		return(BACKEND_INVALID_TEXTURE);
	}

	char cachekey[96];
	if (allowcache) {
		sprintf(cachekey, "%s.%s[%u]", (char const *)packagename, (char const *)extension, (unsigned int)buffers.size());
	} else {
		static unsigned int uncached_counter = 0;
		sprintf(cachekey, "%s.%s[%u]#%u", (char const *)packagename, (char const *)extension, (unsigned int)buffers.size(), uncached_counter++);
	}

	BackendTextureHandle texture = Backend_Load_Texture_Sequence(cachekey, buffers.data(), sizes.data(), (unsigned int)buffers.size(), &out_columns, &out_rows, &out_framecount);

	for (size_t i = 0; i < buffers.size(); i++) {
		delete [] (char *)buffers[i];
	}

	return(texture);
}


/// <summary>
/// Creates a laser beam between two points in the world.
/// This routine is used by the weapon code whenever a laser, railgun or similar beam
/// weapon fires. The new beam adds itself to the master list, so the caller may forget
/// about it entirely -- it will be updated, drawn and finally retired on its own.
/// </summary>
/// <param name="zadjust">The depth bias applied to the beam's starting end.</param>
/// <param name="unknown">Vestigial flag; recorded on the beam but never consulted.</param>
/// <param name="outer_spread">The maximum random color deviation of the outer glow.</param>
/// <param name="duration">The lifetime of the beam, in game frames.</param>
/// <param name="blinks">Should the beam flicker on and off as it ages?</param>
/// <param name="fades">Should the beam's intensity slide from start to end over its life?</param>
/// <param name="gpu_beam_weapon">See the declaration in laser.h.</param>
LaserDrawClass::LaserDrawClass(Coord start, Coord end, int zadjust, bool unknown, RGBClass inner_color, RGBClass outer_color, RGBClass outer_spread, int duration, bool blinks, bool fades, float start_intensity, float end_intensity, WeaponTypeClass const * gpu_beam_weapon) :
	StageClass(),
	Start(start),
	End(end),
	ZAdjust(zadjust),
	UnusedBool1(unknown),
	InnerColor(),
	OuterColor(),
	OuterSpread(),
	Duration(duration),
	Blinks(blinks),
	BlinkState(false),
	Fades(fades),
	StartIntensity(start_intensity),
	EndIntensity(end_intensity),
	IsGPUBeam(false),
	GPUBeamStyle(),
	GPUBeamWidth(20.0f),
	GPUBeamSpeed(0.01f),
	GPUBeamSheetFrameCount(1),
	GPUBeamAnimated(false),
	GPUBeamAnimInterval(1)
{
	Set_Stage(0);
	Set_Rate(1);

	InnerColor = inner_color;
	OuterColor = outer_color;
	OuterSpread = outer_spread;

	// EXTENSION: LaserTexture's or TexturePackageName's mere presence is what turns this
	// on -- see their own doc comments in weapon.h. Textures are loaded once, here,
	// rather than on every Draw_It. TexturePackageName takes precedence over LaserTexture
	// when both are set, same as the reference this is modeled on documents.
	bool haspackage = gpu_beam_weapon != NULL && !gpu_beam_weapon->TexturePackageName.empty();
	bool hastexture = gpu_beam_weapon != NULL && !gpu_beam_weapon->LaserTexture.empty();

	if (haspackage || hastexture) {
		IsGPUBeam = true;
		GPUBeamWidth = gpu_beam_weapon->LaserTextureThickness;
		GPUBeamSpeed = gpu_beam_weapon->LaserTextureSpeed;
		GPUBeamAnimated = gpu_beam_weapon->LaserTextureAnimated;
		GPUBeamAnimInterval = std::max(1, gpu_beam_weapon->TextureAnimInterval);

		if (haspackage) {
			int columns = 1, rows = 1;
			unsigned int framecount = 1;
			GPUBeamStyle.Texture = Load_Laser_Texture_Sequence(gpu_beam_weapon->TexturePackageName, gpu_beam_weapon->TextureFormatExtension, gpu_beam_weapon->AllowTextureCache, columns, rows, framecount);
			GPUBeamStyle.IsSheet = true;
			GPUBeamStyle.SheetHorizontal = std::max(1, columns);
			GPUBeamStyle.SheetVertical = std::max(1, rows);
			GPUBeamSheetFrameCount = std::max(1, (int)framecount);
		} else {
			GPUBeamStyle.Texture = Load_Laser_Texture(gpu_beam_weapon->LaserTexture, gpu_beam_weapon->AllowTextureCache);
			GPUBeamStyle.IsSheet = gpu_beam_weapon->LaserTextureIsSheet;
			GPUBeamStyle.SheetHorizontal = std::max(1, gpu_beam_weapon->LaserTextureSheetHorizontal);
			GPUBeamStyle.SheetVertical = std::max(1, gpu_beam_weapon->LaserTextureSheetVertical);
			GPUBeamSheetFrameCount = std::max(1, gpu_beam_weapon->LaserTextureSheetFrames);
		}

		GPUBeamStyle.DistortionTexture = Load_Laser_Texture(gpu_beam_weapon->LaserDistortion, gpu_beam_weapon->AllowTextureCache);
		GPUBeamStyle.PointFilter = (gpu_beam_weapon->LaserTextureFilter == 1);
		GPUBeamStyle.SheetFrame = 0.0f;
		GPUBeamStyle.NoStretch = gpu_beam_weapon->LaserTextureNoStretch;
		GPUBeamStyle.ScrollPhase = 0.0f;
		GPUBeamStyle.DistortionWidth = gpu_beam_weapon->LaserDistortionWidth;
		GPUBeamStyle.DistortionDisplacement = gpu_beam_weapon->LaserDistortionDisplacement;
	}

	LaserDraws.Add(this);
}


/// <summary>
/// Destroys the laser beam.
/// This routine removes the beam from the master list, so that it is no longer updated
/// or drawn.
/// </summary>
LaserDrawClass::~LaserDrawClass(void)
{
	LaserDraws.Delete(this);
}


/// <summary>
/// Removes every laser beam that is currently active.
/// This routine is used when the scenario is torn down, so that no beam survives into
/// the next game.
/// </summary>
void LaserDrawClass::All_Clear(void)
{
	while (LaserDraws.Count()) {
		delete LaserDraws[0];
	}
}


/// <summary>
/// Handles the per frame logic for this laser beam.
/// This routine advances the animation stage, flips the blink state for a blinking beam,
/// and retires the beam once it has lived out its duration.
/// </summary>
/// <remarks>The laser may delete itself here, so do not touch it after this call.</remarks>
void LaserDrawClass::AI(void)
{
	Graphic_Logic();
	if (Blinks) {
		BlinkState = BlinkState == false;
	}

	if (Fetch_Stage() >= Duration) {
		delete this;
	}
}


/// <summary>
/// Processes the logic for every laser beam that is currently active.
/// This routine is called once per game logic loop. Beams that have outlived their
/// duration remove themselves from the list during this call.
/// </summary>
void LaserDrawClass::Update_All(void)
{
	for (int i = LaserDraws.Count() - 1; i >= 0; i--) {
		LaserDraws[i]->AI();
	}
}


/// <summary>
/// True while at least one active, non-blinked-off beam needs a depth/ambient snapshot to
/// draw against. See the declaration in laser.h for how this is meant to be used.
/// </summary>
bool LaserDrawClass::Has_Active_GPU_Beam(void)
{
	for (int i = 0; i < LaserDraws.Count(); i++) {
		if (LaserDraws[i]->IsGPUBeam && !LaserDraws[i]->BlinkState) {
			return(true);
		}
	}
	return(false);
}


/// <summary>
/// Takes a fresh depth/ambient snapshot for this frame's GPU beams and GPU overlay anims,
/// if either actually queued one. See the declaration in laser.h for why this is a free
/// function tactical.cpp calls explicitly rather than a side effect of Draw_All.
/// </summary>
void Capture_GPU_Effect_Snapshots(void)
{
	bool needs_snapshot = Backend_Has_Queued_Overlay_Quads() || LaserDrawClass::Has_Active_GPU_Beam();
	if (!needs_snapshot || DepthBuffer == NULL) {
		return;
	}

	Rect bounds = DepthBuffer->Get_Bounds();
	if (bounds.Width > 0 && bounds.Height > 0) {
		BSurface snapshot(bounds.Width, bounds.Height, 2);
		DepthBuffer->Copy_To(&snapshot, Rect(0, 0, bounds.Width, bounds.Height));

		void const * pixels = snapshot.Lock();
		if (pixels != NULL) {
			Backend_Upload_Depth_Snapshot(pixels, snapshot.Stride(), bounds.Width, bounds.Height, bounds.X, bounds.Y);
			snapshot.Unlock();
		}
	}

	// EXTENSION: AlphaBuffer is always constructed covering the same Rect as DepthBuffer
	// (see display.cpp), so no separate origin needs recording for it.
	if (AlphaBuffer != NULL) {
		Rect alphabounds = AlphaBuffer->Get_Bounds();
		if (alphabounds.Width > 0 && alphabounds.Height > 0) {
			BSurface alphasnapshot(alphabounds.Width, alphabounds.Height, 2);
			AlphaBuffer->Copy_To(&alphasnapshot, Rect(0, 0, alphabounds.Width, alphabounds.Height));

			void const * alphapixels = alphasnapshot.Lock();
			if (alphapixels != NULL) {
				Backend_Upload_Ambient_Snapshot(alphapixels, alphasnapshot.Stride(), alphabounds.Width, alphabounds.Height, alphabounds.X, alphabounds.Y);
				alphasnapshot.Unlock();
			}
		}
	}
}


/// <summary>
/// Draws every laser beam that is currently active.
/// This routine is called by the tactical map rendering pass so that beams appear over
/// the objects they were fired between. Callers that use any GPU beam or GPU overlay anim
/// should call Capture_GPU_Effect_Snapshots first -- tactical.cpp's own draw sequence
/// already does.
/// </summary>
void LaserDrawClass::Draw_All(void)
{
	for (int i = LaserDraws.Count() - 1; i >= 0; i--) {
		LaserDraws[i]->Draw_It();
	}
}


/// <summary>
/// Draws the laser beam onto the logical surface.
/// This routine draws the beam as a thin inner core and, when an outer color was
/// specified, a pair of parallel glow lines flanking it. A beam that lies under the fog
/// of war, or that is currently blinked off, draws nothing at all. At the lowest detail
/// level the beam degrades to a plain depth shaded line.
/// </summary>
void LaserDrawClass::Draw_It(void)
{
	if (!Scen->Special.IsFogOfWar || !Map.Is_Fogged(Start) || !Map.Is_Fogged(End)) {
		static Point2D _glow_offsets[16] = {
			Point2D(0, -1),
			Point2D(1, 0),
			Point2D(0, -1),
			Point2D(0, 1),
			Point2D(1, 0),
			Point2D(0, 1),
			Point2D(-1, 0),
			Point2D(1, 0),
			Point2D(-1, 0),
			Point2D(0, 1),
			Point2D(0, -1),
			Point2D(1, 0),
			Point2D(0, -1),
			Point2D(-1, 0),
			Point2D(-1, 0),
			Point2D(1, 0)
		};

		if (!BlinkState) {
			/// The "thin" core (InnerColor) is drawn once with no offset. The "thick"
			/// outer glow (OuterColor) is two extra lines, each a 1-pixel parallel copy
			/// of the core shifted by _glow_offsets[2*facing] / [2*facing+1].
			///
			/// The glow can look like it "leans" a slightly different way than the core at
			/// some angles. That is deliberate, not a defect here. Two reasons:
			/// 1. facing is the WORLD-space beam direction quantized to just 8 compass
			/// points (As_Dir8), so in-between angles get the nearest sector's offset.
			/// 2. facing is world-space but the offsets are applied in ISOMETRIC screen
			/// space (Y compressed ~2:1), so the offset is only approximately
			/// perpendicular to the on-screen beam.
			/// Per-facing the offset PAIR straddles the core symmetrically only for
			/// NE(1)/SE(3)/NW(7); for N(0)/E(2)/S(4)/SW(5)/W(6) the pair is an L-corner
			/// (both on one side), biasing the glow off the core axis -- this is the
			/// "thick and thin face different ways" artifact. Do NOT "fix" the table or
			/// switch to screen-space facing without opting into a deliberate deviation.
			DirType dir = Direction(Start, End);
			FacingType facing = dir.As_Facing();

			Point2D start_pixel;
			TacticalMap->Coord_To_Pixel(Start, start_pixel);
			Point2D end_pixel;
			TacticalMap->Coord_To_Pixel(End, end_pixel);

			int start_z = ZAdjust - TacticalMap->Z_Lepton_To_Pixel(Start.Z) - 2;
			int end_z = -TacticalMap->Z_Lepton_To_Pixel(End.Z) - 2;

			RGBClass outer_color;
			int outer_hicolor = 0;
			bool has_outer_glow = true;

			if (OuterColor == RGBClass(0, 0, 0)) {
				has_outer_glow = false;
			} else {
				int red = Sim_Random_Pick(-OuterSpread.Get_Red(), OuterSpread.Get_Red());
				int green = Sim_Random_Pick(-OuterSpread.Get_Green(), OuterSpread.Get_Green());
				int blue = Sim_Random_Pick(-OuterSpread.Get_Blue(), OuterSpread.Get_Blue());

				red += OuterColor.Get_Red();
				red = std::max(0, red);
				red = std::min(255, red);

				green += OuterColor.Get_Green();
				green = std::max(0, green);
				green = std::min(255, green);

				blue += OuterColor.Get_Blue();
				blue = std::max(0, blue);
				blue = std::min(255, blue);

				outer_hicolor = DSurface::Build_Hicolor_Pixel(red, green, blue);
				outer_color.Set_Red(red);
				outer_color.Set_Green(green);
				outer_color.Set_Blue(blue);
			}

			float current_intensity = 1.0;

			bool has_r = InnerColor.Get_Red() > 0;
			bool has_g = InnerColor.Get_Green() > 0;
			bool has_b = InnerColor.Get_Blue() > 0;

			if (Fades) {
				current_intensity = (StartIntensity - EndIntensity) * (Duration - Fetch_Stage()) / Duration + EndIntensity;
			}

			// EXTENSION: a GPU beam skips every one of the software line calls below --
			// core, both glow lines, and the low detail fallback alike -- in favor of one
			// depth-tested textured quad the renderer draws later in the same frame.
			if (IsGPUBeam) {
				unsigned int color = 0xFF000000
					| ((unsigned int)InnerColor.Get_Blue() << 16)
					| ((unsigned int)InnerColor.Get_Green() << 8)
					| (unsigned int)InnerColor.Get_Red();
				unsigned int alpha = (unsigned int)(current_intensity * 255.0f + 0.5f);
				color = (color & 0x00FFFFFF) | (alpha << 24);

				// EXTENSION: LaserTextureAnimated steps through GPUBeamSheetFrameCount
				// cells once every GPUBeamAnimInterval game frames -- meant for a
				// TexturePackageName sequence, but honored for a plain LaserTextureIsSheet
				// texture too, if the weapon asks for it, in preference to the
				// GPUBeamSpeed-driven stepping below. Otherwise GPUBeamSpeed drives
				// whichever animation the weapon actually asked for -- LaserTextureIsSheet
				// steps through GPUBeamSheetFrameCount cells; otherwise it scrolls the
				// texture continuously, in the fraction of its own length GPUBeamSpeed
				// covers per game frame either direction of LaserTextureNoStretch treats
				// the same way rather than needing to know the texture's actual pixel size
				// to convert a pixels-per-frame speed.
				if (GPUBeamStyle.IsSheet && GPUBeamAnimated) {
					int frame = (Fetch_Stage() / GPUBeamAnimInterval) % GPUBeamSheetFrameCount;
					GPUBeamStyle.SheetFrame = (float)frame;
				} else if (GPUBeamStyle.IsSheet) {
					int frame = (int)(Fetch_Stage() * GPUBeamSpeed) % GPUBeamSheetFrameCount;
					GPUBeamStyle.SheetFrame = (float)frame;
				} else {
					float phase = Fetch_Stage() * GPUBeamSpeed;
					GPUBeamStyle.ScrollPhase = phase - floorf(phase);
				}

				// EXTENSION: DSurface::Draw_Depth_Shaded_Line doesn't compare start_z/end_z
				// against the depth buffer directly -- it adds the buffer's own current
				// vertical scroll delta at each endpoint's screen row first, since the ring
				// buffer's stored values are relative to its scroll state rather than being
				// plain world heights. Skipping this would compare against the wrong
				// baseline whenever the tactical view isn't sitting at its initial scroll
				// position, which in practice is almost always.
				int effective_start_z = start_z;
				int effective_end_z = end_z;
				if (DepthBuffer != NULL) {
					effective_start_z += DepthBuffer->Get_Scroll_Delta(start_pixel.Y - DepthBuffer->Get_Bounds().Y);
					effective_end_z += DepthBuffer->Get_Scroll_Delta(end_pixel.Y - DepthBuffer->Get_Bounds().Y);
				}

				Backend_Queue_GPU_Beam((float)start_pixel.X, (float)start_pixel.Y, (float)effective_start_z,
					(float)end_pixel.X, (float)end_pixel.Y, (float)effective_end_z,
					GPUBeamWidth, color, GPUBeamStyle);

				return;
			}

			if (Options.DetailLevel != 0) {
				if (Blinks) {
					LogicalSurface->Draw_Depth_Antialiased_Line(TacticalRect, start_pixel, end_pixel, InnerColor, start_z, end_z, false, true, true, true, current_intensity);
					if (has_outer_glow) {
						LogicalSurface->Draw_Depth_Antialiased_Line(
							TacticalRect,
							start_pixel + _glow_offsets[2 * facing],
							end_pixel + _glow_offsets[2 * facing],
							outer_color,
							start_z, end_z, false, true, true, true, 1.0
						);

						LogicalSurface->Draw_Depth_Antialiased_Line(
							TacticalRect,
							start_pixel + _glow_offsets[2 * facing + 1],
							end_pixel + _glow_offsets[2 * facing + 1],
							outer_color,
							start_z, end_z, false, true, true, true, 1.0
						);
					}
				} else {
					LogicalSurface->Draw_Depth_Antialiased_Line(TacticalRect, start_pixel, end_pixel, InnerColor, start_z, end_z, false, has_r, has_g, has_b, current_intensity);
					if (has_outer_glow) {
						LogicalSurface->Draw_Depth_Antialiased_Line(
							TacticalRect,
							start_pixel + _glow_offsets[2 * facing],
							end_pixel + _glow_offsets[2 * facing],
							outer_color,
							start_z, end_z, false, true, false, false, 1.0
						);

						LogicalSurface->Draw_Depth_Antialiased_Line(
							TacticalRect,
							start_pixel + _glow_offsets[2 * facing + 1],
							end_pixel + _glow_offsets[2 * facing + 1],
							outer_color,
							start_z, end_z, false, true, false, false, 1.0
						);
					}
				}
			} else {
				LogicalSurface->Draw_Depth_Shaded_Line(TacticalRect, start_pixel, end_pixel,
					DSurface::Build_Hicolor_Pixel(InnerColor.Get_Red(), InnerColor.Get_Green(), InnerColor.Get_Blue()),
					start_z, end_z, false
				);

				if (has_outer_glow) {
					LogicalSurface->Draw_Depth_Shaded_Line(
						TacticalRect,
						start_pixel + _glow_offsets[2 * facing],
						end_pixel + _glow_offsets[2 * facing],
						outer_hicolor,
						start_z, end_z
					);

					LogicalSurface->Draw_Depth_Shaded_Line(
						TacticalRect,
						start_pixel + _glow_offsets[2 * facing + 1],
						end_pixel + _glow_offsets[2 * facing + 1],
						outer_hicolor,
						start_z, end_z
					);
				}
			}
		}
	}
}
