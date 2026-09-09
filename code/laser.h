/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "bgfxbackend.h"
#include "coord.h"
#include "rgb.h"
#include "stage.h"

template<class T> class DynamicVectorClass;
class WeaponTypeClass;

class LaserDrawClass : public StageClass
{
	public:
		// EXTENSION: gpu_beam_weapon supplies LaserTexture/LaserDistortion/etc when
		// non-NULL, per its own tag documentation in weapon.h; the beam's textures (if
		// any) are loaded once here, at construction, not on every Draw_It call. NULL
		// (the default, and what ion.cpp/partsys.cpp still pass) draws the plain
		// depth-tested lines exactly as before this feature existed.
		LaserDrawClass(Coord start, Coord end, int zadjust, bool,
			RGBClass inner_color, RGBClass outer_color, RGBClass outer_spread,
			int duration, bool blinks, bool fades, float start_intensity, float end_intensity,
			WeaponTypeClass const * gpu_beam_weapon = NULL);
		~LaserDrawClass(void);

		void Draw_It(void);
		void AI(void);

		static void Update_All(void);
		static void All_Clear(void);
		static void Draw_All(void);

		// EXTENSION: true while at least one active, non-blinked-off beam in LaserDraws
		// needs a depth/ambient snapshot to draw against. Used by
		// Capture_GPU_Effect_Snapshots (below, not a member) rather than by Draw_All
		// itself, which now only draws -- see that function's own doc comment for why
		// snapshot timing moved out to tactical.cpp.
		static bool Has_Active_GPU_Beam(void);

	public:
		/*
		 * These are the world coordinates of the two ends of the beam. Neither end follows
		 * the object that fired or the object that was hit, so the beam stays where it was
		 * first drawn for the whole of its short life.
		 */
		Coord Start;
		Coord End;

		/*
		 * This is the depth bias applied to the starting end of the beam, so that a shot
		 * leaving a muzzle above or below the firing object's own render row still sorts
		 * correctly against it.
		 */
		int ZAdjust;

		/// Unused
		bool UnusedBool1;

		/*
		 * This is the color of the beam's thin core, drawn as a single line from one end to
		 * the other.
		 */
		RGBClass InnerColor;

		/*
		 * This is the color of the beam's outer glow -- two further lines drawn a pixel to
		 * either side of the core. If it is black, then the beam has no glow at all.
		 */
		RGBClass OuterColor;

		/*
		 * This is how far each channel of the glow color may stray from OuterColor. A fresh
		 * deviation is rolled every frame, which is what makes the glow shimmer.
		 */
		RGBClass OuterSpread;

		/*
		 * This is the lifetime of the beam, expressed in game frames. The beam deletes
		 * itself once its animation stage reaches this value.
		 */
		int Duration;

		/*
		 * If this beam should flicker on and off as it ages, then this flag will be true.
		 */
		bool Blinks;

		/*
		 * This is the current phase of a blinking beam, toggled every frame. While it is
		 * true the beam draws nothing at all, which is what produces the flicker.
		 */
		bool BlinkState;

		/*
		 * If the beam's intensity should slide from StartIntensity to EndIntensity across
		 * its life, then this flag will be true. Otherwise it draws at full intensity.
		 */
		bool Fades;

		/*
		 * These are the intensities the beam is drawn at when it is created and when it
		 * expires (0 - 1). They are interpolated over the beam's duration, but only when
		 * the Fades flag is set.
		 */
		float StartIntensity;
		float EndIntensity;

		/*
		 * EXTENSION: everything to do with drawing this beam as a hardware-composited,
		 * textured, distorting quad instead of through the depth-tested lines Draw_It
		 * otherwise uses. IsGPUBeam is true exactly when gpu_beam_weapon's own LaserTexture
		 * was set, per that tag's own documentation in weapon.h; everything else here is a
		 * straight copy of that weapon's matching tag, or the textures it named, loaded
		 * once at construction.
		 */
		bool IsGPUBeam;
		BackendGPUBeamStyle GPUBeamStyle;
		float GPUBeamWidth;
		float GPUBeamSpeed;

		// How many of GPUBeamStyle's SheetHorizontal x SheetVertical cells are actually
		// used (LaserTextureSheetFrames); animation cycles through this many rather than
		// every cell the sheet's grid could hold, in case the sheet has unused ones.
		int GPUBeamSheetFrameCount;

		// LaserTextureAnimated/TextureAnimInterval: governs frame stepping specifically
		// for a TexturePackageName sequence, as an alternative to the GPUBeamSpeed-driven
		// stepping a plain LaserTextureIsSheet texture uses.
		bool GPUBeamAnimated;
		int GPUBeamAnimInterval;

		/*
		 * This is the master list of every beam that is currently active. A beam adds
		 * itself here when it is created and removes itself when it is deleted, so the
		 * update and draw passes need no other bookkeeping.
		 */
		static DynamicVectorClass<LaserDrawClass *> LaserDraws;
};


// EXTENSION: takes a fresh depth/ambient snapshot for this frame's GPU beams and GPU
// overlay anims to draw against, if either actually need one. Deliberately a free
// function called explicitly from tactical.cpp's own draw sequence -- right after
// Draw_Objects and before LaserDrawClass::Draw_All, the same point Draw_All used to do
// this internally -- rather than a side effect hidden inside Draw_All itself, since
// "when does the scene's depth become final enough to snapshot" is a property of the
// whole tactical draw sequence (which tactical.cpp already owns and ticks once per
// frame), not something specific to drawing lasers. See laser.cpp for the actual capture,
// which is unchanged from what Draw_All used to do; only where it gets called from moved.
void Capture_GPU_Effect_Snapshots(void);
