#pragma once

#include "always.h"

#include "coord.h"
#include "stringid.h"
#include "bgfxbackend.h"

#include <vector>

template<class T> class DynamicVectorClass;
class SaveStreamClass;
struct IStream;


/*
**	EXTENSION: a GPU particle emitter's whole configuration -- spawn behavior, per-particle
**	motion, and appearance. Deliberately not a full INI-driven TypeClass the way a weapon
**	or anim is; something that fires this many distinct one-off effects (a muzzle spark
**	burst, an impact puff, a persistent smoke trail) doesn't obviously benefit from an INI
**	section per variant the way a WeaponType does, and skipping that machinery keeps this
**	increment to something that can actually be finished. Filled in directly by whatever
**	code creates a GPUParticleClass; nothing here reads from rules.ini on its own yet.
*/
struct GPUParticleStyle
{
	GPUParticleStyle(void);

	/*
	**	Spawn behavior. SpawnRate is new particles per game frame -- fractional is fine,
	**	e.g. 0.5 for one every two frames. MaxParticles caps how many can be alive from
	**	this emitter at once. Duration is how many game frames the emitter keeps spawning
	**	for; 0 means forever, until something explicitly deletes it. BurstCount, when
	**	nonzero, spawns that many particles once at creation and then stops -- SpawnRate
	**	and Duration are ignored in that case.
	*/
	float SpawnRate;
	int MaxParticles;
	int Duration;
	int BurstCount;

	/*
	**	Per-particle motion. Lifetime is in game frames. Gravity, the spread values, and
	**	InitialVelocityZ are all in leptons per game frame (leptons per game frame squared
	**	for Gravity specifically). SpreadRadius/SpreadHeight bound the random initial
	**	horizontal/vertical velocity a spawned particle gets.
	*/
	int Lifetime;
	float Gravity;
	float SpreadRadius;
	float SpreadHeight;
	float InitialVelocityZ;

	/*
	**	Appearance. Size is the on-screen quad size in pixels at spawn; SizeVariance is a
	**	random +/- fraction of that applied per particle. Color tints every particle,
	**	0xAABBGGRR -- the alpha channel is treated as this color's own peak opacity, faded
	**	down across the particle's Lifetime rather than held constant. TextureFilename
	**	empty draws the plain soft circular falloff (see Backend_Queue_GPU_Particle's own
	**	doc comment); set, it's loaded the same way a weapon's LaserTexture is.
	*/
	float Size;
	float SizeVariance;
	unsigned int Color;
	TStringID<64> TextureFilename;
};


class GPUParticleClass
{
	public:
		/*
		**	origin is where the emitter sits in the world; every particle's own position
		**	is Origin plus that particle's own accumulated offset, so a still-alive
		**	emitter can be moved (Origin is public for exactly that -- a weapon effect
		**	attached to a moving unit, for instance) without needing to touch every
		**	particle already spawned from it.
		*/
		GPUParticleClass(Coord const & origin, GPUParticleStyle const & style);
		~GPUParticleClass(void);

		Coord Origin;

		/*
		**	Advances this emitter by one game frame: spawns new particles per Style,
		**	steps every live particle's position and age, and retires both particles
		**	past their Lifetime and (once Duration/BurstCount are exhausted and every
		**	particle has died out) the emitter itself. Called once per game tick from
		**	LogicClass::AI, which is what makes this naturally pause-aware -- nothing
		**	here needs its own pause check, since the whole call simply doesn't happen
		**	while the game is paused, the same as every other tick-driven system.
		*/
		void AI(void);

		/*
		**	Queues this emitter's currently-alive particles as GPU quads for the frame
		**	being rendered right now. Called once per render frame regardless of pause,
		**	so the last simulated state stays visible on screen while paused rather than
		**	the particles vanishing.
		*/
		void Draw_It(void);

		void Serialize(SaveStreamClass & stream);

		static void Update_All(void);
		static void Draw_All(void);
		static void Clear_All(void);
		static bool Save(IStream * stream);
		static bool Load(IStream * stream);

	private:
		// A placeholder-construction path Load uses -- see Load's own comment in
		// gpuparticle.cpp for why a default Origin/Style is fine there.
		GPUParticleClass(void);

		void Refresh_Texture(void);
		void Spawn_Particle(void);

		struct Particle {
			// Leptons relative to Origin; X/Y form the ground-plane offset, Z is height.
			float OffsetX, OffsetY, OffsetZ;
			float VelocityX, VelocityY, VelocityZ;
			int Age;

			// Baked in once at spawn (Style.Size varied by Style.SizeVariance), rather
			// than recomputed every draw call, so a particle's own size doesn't flicker
			// randomly frame to frame.
			float Size;
		};

		GPUParticleStyle Style;
		BackendTextureHandle Texture;
		std::vector<Particle> Particles;
		int EmitterAge;
		float SpawnAccumulator;
		int BurstRemaining;

		static DynamicVectorClass<GPUParticleClass *> Emitters;
};
