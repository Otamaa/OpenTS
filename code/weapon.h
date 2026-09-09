/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Electronic Arts Inc.
 * Copyright 2026 OpenTS contributors
 *
 * Contains material derived from Electronic Arts source code.
 * Modified by OpenTS contributors, 2026.
 * EA's GPLv3 Section 7 additional terms and supplemental warranty
 * disclaimers apply; see LICENSE.md.
 ******************************************************************************/

/* $Header: /CounterStrike/WEAPON.H 1     3/03/97 10:26a Joe_bostic $ */
/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                    File Name : WEAPON.H                                                     *
 *                                                                                             *
 *                   Programmer : Joe L. Bostic                                                *
 *                                                                                             *
 *                   Start Date : 05/17/96                                                     *
 *                                                                                             *
 *                  Last Update : May 17, 1996 [JLB]                                           *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

#include "abstype.h"
#include "rgb.h"
#include "stringid.h"
#include "typelist.h"

#include "armor.hh"
#include "mph.hh"
#include "threat.hh"
#include "weapon.hh"

class ParticleSystemTypeClass;
class AnimTypeClass;
class WarheadTypeClass;
class BulletTypeClass;


/**********************************************************************
**	This is the constant data associated with a weapon. Some objects
**	can have multiple weapons and this class is used to isolate and
**	specify this data in a convenient and selfcontained way.
*/
class WeaponTypeClass : public AbstractTypeClass
{
		typedef AbstractTypeClass BASECLASS;

	public:
		WeaponTypeClass(char const * ininame = NULL);
		~WeaponTypeClass(void);

		virtual HRESULT STDMETHODCALLTYPE GetClassID(CLSID * retval) override;

		static WeaponType From_Name(char const * name);

		virtual void Serialize(SaveStreamClass & stream) override;

		virtual RTTIType Fetch_RTTI(void) const override {return(RTTI_WEAPONTYPE);}

		virtual void Compute_CRC(CRCEngine &) const override;

		static WeaponTypeClass *Find_Or_Make(const char * name);

		char const * Name(void) const {return(IniName);}
		bool Read_INI(CCINIClass const & ini);
		ThreatType Allowed_Threats(void) const;
		bool Is_Wall_Destroyer(void) const;

		void Init_Max_Speed(void);

		/*
		 * This is the damage inflicted upon everything the weapon's beam or wave passes
		 * over, as opposed to the Attack damage its projectile delivers at the target
		 * itself. Only the sonic and railgun weapons wash their path this way.
		 */
		int AmbientDamage;

		/*
		**	This is the number of shots this weapon first (in rapid succession).
		**	The normal value is 1, but for the case of two shooter weapons such as
		**	the double barreled gun turrets of the Mammoth tank, this value will be
		**	set to 2.
		*/
		int Burst;

		/*
		**	This is the unit class of the projectile fired. A subset of the unit types
		**	represent projectiles. It is one of these classes that is specified here.
		**	If this object does not fire anything, then this value will be BULLET_NONE.
		*/
		BulletTypeClass const * Bullet;

		/*
		**	This is the damage (explosive load) to be assigned to the projectile that
		**	this object fires. For the rare healing weapon, this value is negative.
		*/
		int Attack;

		/*
		**	Speed of the projectile launched.
		*/
		MPHType MaxSpeed;

		/*
		**	Warhead to attach to the projectile.
		*/
		WarheadTypeClass const * WarheadPtr;

		/*
		**	Objects that fire (which can be buildings as well) will fire at a
		**	frequency controlled by this value. This value serves as a count
		**	down timer between shots. The smaller the value, the faster the
		**	rate of fire.
		*/
		int ROF;

		/*
		**	When this object fires, the range at which it's projectiles travel is
		**	controlled by this value. The value represents the number of cells the
		**	projectile will travel. Objects outside of this range will not be fired
		**	upon (in normal circumstances).
		*/
		LEPTON Range;

		/*
		 * This is the distance the projectile is fueled for, expressed in leptons. A fueled
		 * projectile burns this down as it flies and detonates when it runs out, so that a
		 * missile cannot chase an evading target forever.
		 */
		LEPTON ProjectileRange;

		/*
		 * These are the delays, in game frames, between the successive shots of a burst. The
		 * entry used is chosen by which shot of the burst has just been fired, and an entry
		 * of -1 leaves that gap to a short random delay instead.
		 */
		int BurstDelay[4];

		/*
		 * This is the closest a target may be before this weapon refuses to fire upon it,
		 * expressed in leptons. If zero, then the weapon has no minimum range.
		 */
		LEPTON MinimumRange;

		/*
		**	This is the typical sound generated when firing.
		*/
		TypeList<int> Sound;

		/*
		**	This is the animation to display at the firing coordinate.
		*/
		TypeList<AnimTypeClass const *> Anim;

		/*
		 * This points to the particle system type this weapon spawns when it fires. The
		 * flame, spark and railgun weapons carry their effect -- and their damage -- in that
		 * system rather than in a projectile.
		 */
		ParticleSystemTypeClass *AttachedParticleSystem;

		/*
		 * This is the color of the bright core of the laser beam this weapon draws.
		 */
		RGBClass LaserInnerColor;

		/*
		 * This is the color of the glow drawn around that core. If it is black, then no
		 * outer glow is drawn at all.
		 */
		RGBClass LaserOuterColor;

		/*
		 * This is how far each color channel of the outer glow may wander from
		 * LaserOuterColor. A fresh offset is picked every frame, so the glow shimmers
		 * instead of sitting at one flat color.
		 */
		RGBClass LaserOuterSpread;

		/*
		 * If this weapon attacks with a stream of fire particles rather than a projectile,
		 * then this flag will be true. The firer must stand still to use it, and cannot fire
		 * again until the particle system it spawned has burned itself out.
		 */
		bool UseFireParticles;

		/*
		 * If this weapon attacks with a spray of sparks rather than a projectile, then this
		 * flag will be true. Like the fire weapon, it can only be used standing still and
		 * only once the previous spray has finished.
		 */
		bool UseSparkParticles;

		/*
		 * If this weapon fires a railgun beam, then this flag will be true. The beam damages
		 * everything standing along its line at the instant it is fired, and is drawn as a
		 * particle system that must expire before the weapon can be fired again.
		 */
		bool IsRailgun;

		/*
		 * If this weapon always lobs its shot in a high arc, then this flag will be true.
		 * Other weapons resort to an arc only when the target sits high enough overhead that
		 * a flat shot would bury itself in the ground between.
		 */
		bool IsLobber;

		/*
		 * If the explosion of this weapon's projectile should light up the terrain around
		 * it, then this flag will be true.
		 */
		bool IsBright;

		/*
		 * This is the number of game frames that the laser beam remains drawn for. The beam
		 * fades toward the end of that time rather than simply vanishing.
		 */
		char LaserDuration;

		/*
		 * EXTENSION: this weapon's beam texture, as a filename Load_Laser_Texture (see
		 * laser.cpp) resolves the same way any other game asset is -- including from a
		 * mix. Modeled on a third-party D3D9 ddraw wrapper's own LaserTexture= tag. Its
		 * mere presence is what turns this beam into a hardware-composited, textured,
		 * distorting quad instead of the plain depth-tested lines above; there is no
		 * separate on/off flag. Unlike a GPU overlay anim, the beam still occludes
		 * correctly against terrain and units, by sampling a snapshot of the software
		 * renderer's own depth buffer. DDS is the only format actually decoded right now;
		 * see Backend_Load_Texture's own doc comment.
		 */
		TStringID<64> LaserTexture;

		/*
		 * EXTENSION: an alternative to LaserTexture that loads a numbered sequence of
		 * separate files instead of one -- NAME 0000.EXT, NAME 0001.EXT, and so on
		 * (mind the space before the number), starting at 0000 and stopping at the first
		 * one that fails to load. Takes precedence over LaserTexture when both are set.
		 * The loaded frames are combined into one sheet texture the same machinery
		 * LaserTextureIsSheet already uses handles internally; see Load_Laser_Texture in
		 * laser.cpp for the actual file probing.
		 */
		TStringID<64> TexturePackageName;
		TStringID<8> TextureFormatExtension;

		/*
		 * Governs playback speed specifically for a TexturePackageName sequence: true
		 * steps to the next frame every TextureAnimInterval game frames. False (the
		 * default) leaves the sequence on its first frame, same as any other sheet
		 * texture that isn't told otherwise.
		 */
		bool LaserTextureAnimated;
		int TextureAnimInterval;

		/*
		 * EXTENSION: whether a texture this weapon loads (LaserTexture, LaserDistortion,
		 * or a TexturePackageName sequence) may be reused from the cache a later weapon
		 * using the same filename(s) would otherwise hit. True (the default) always
		 * reuses a cache hit; false always decodes and uploads a fresh copy instead. The
		 * reference this is modeled on defaults this off and specifically discourages it
		 * for DDS textures, for reasons tied to its own D3D9 texture pool that don't apply
		 * here, so this engine's own default is the opposite of that one's.
		 */
		bool AllowTextureCache;

		/*
		 * The beam's on-screen width in pixels when LaserTexture is set. Ignored
		 * otherwise. Named to match the reference's own LaserTextureThickness=.
		 */
		float LaserTextureThickness;

		/*
		 * How fast the beam's texture scrolls: in LaserTextureNoStretch (bullet) mapping,
		 * pixels per game frame; in the default (direct/stretch) mapping, lengths of the
		 * texture per game frame.
		 */
		float LaserTextureSpeed;

		/*
		 * false (the default) stretches LaserTexture across the beam's full length and
		 * scrolls it; true keeps the texture at its own length and moves it along the
		 * beam instead, repeating as needed. Ignored when LaserTextureIsSheet is true --
		 * the two mapping styles are mutually exclusive, same as the reference.
		 */
		bool LaserTextureNoStretch;

		/*
		 * 1 samples LaserTexture with point (nearest) filtering; anything else (2 is the
		 * reference's own default) samples it linearly.
		 */
		int LaserTextureFilter;

		/*
		 * If true, LaserTexture is a sprite sheet sliced into LaserTextureSheetHorizontal
		 * by LaserTextureSheetVertical cells, animated through LaserTextureSheetFrames of
		 * them at LaserTextureSpeed frames per game frame.
		 */
		bool LaserTextureIsSheet;
		int LaserTextureSheetFrames;
		int LaserTextureSheetHorizontal;
		int LaserTextureSheetVertical;

		/*
		 * A second texture (same loading and sheet rules as LaserTexture) whose presence
		 * turns on the beam's distortion pass: the composited scene behind the beam is
		 * warped by LaserDistortionDisplacement using this texture's own RG channels as a
		 * per-pixel offset, the same two-pass technique the reference's own
		 * LaserDistortion= plus its ReShade LaserBlit.fx use. Empty skips the pass
		 * entirely, at zero extra render cost.
		 */
		TStringID<64> LaserDistortion;
		float LaserDistortionWidth;
		float LaserDistortionDisplacement;

		/*
		 * If the glow that accompanies the laser beam should be the wider of the two sizes,
		 * then this flag will be true.
		 */
		bool IsBigLaser;

		/*
		 * If this weapon attacks with a sonic wave rather than a projectile, then this flag
		 * will be true. The wave rolls out to the target damaging everything it crosses, and
		 * the firer cannot fire again until it has arrived.
		 */
		bool IsSonic;

		/*
		**	Increase the weapon speed if the target is flying.
		*/
		bool IsTurboBoosted;

		/*
		**	If potential targets of this weapon should be scanned for
		**	nearby friendly structures and if found, firing upon the target
		**	would be discouraged, then this flag will be true.
		*/
		bool IsSupressed;

		/*
		**	If this weapon is equipped with a camera that reveals the
		**	area around the firer, then this flag will be true.
		*/
		bool IsCamera;

		/*
		**	If this weapon requires charging before it can fire, then this
		**	flag is true. In actuality, this only applies to the Tesla coil
		**	which has specific charging animation. The normal rate of fire
		**	value suffices for all other cases.
		*/
		bool IsElectric;

		/*
		 * If this weapon zaps its target with a laser beam, then this flag will be true. The
		 * colors and lifetime of that beam are given by the Laser fields above.
		 */
		bool IsLaser;

		/*
		 * If this weapon cannot be fired during an ion storm, then this flag will be true.
		 */
		bool IsIonSensitive;
};

ArmorType Armor_From_Name(char const * name);
