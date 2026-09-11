#include "always.h"

#include "gpuparticle.h"

#include "ccfile.h"
#include "data.h"
#include "ccrand.h"
#include "savestream.h"
#include "_tactica.h"
#include "tactical.h"
#include "vector.h"
#include "zbuffer.h"

#include <algorithm>
#include <cstring>


DynamicVectorClass<GPUParticleClass *> GPUParticleClass::Emitters;


/// <summary>
/// Fills in a reasonable default configuration -- a modest, short-lived white puff --
/// rather than leaving any field meaningless. Whatever actually creates a
/// GPUParticleClass is expected to override the fields it cares about.
/// </summary>
GPUParticleStyle::GPUParticleStyle(void) :
	SpawnRate(1.0f),
	MaxParticles(50),
	Duration(0),
	BurstCount(0),
	Lifetime(30),
	Gravity(0.0f),
	SpreadRadius(10.0f),
	SpreadHeight(10.0f),
	InitialVelocityZ(0.0f),
	Size(4.0f),
	SizeVariance(0.25f),
	Color(0xFFFFFFFF),
	TextureFilename()
{
}


/// <summary>
/// Creates a new particle emitter at the given world position and adds it to the master
/// list. The caller does not need to hold on to the returned pointer -- like a
/// LaserDrawClass, this updates, draws, and eventually retires itself.
/// </summary>
GPUParticleClass::GPUParticleClass(Coord const & origin, GPUParticleStyle const & style) :
	Origin(origin),
	Style(style),
	Texture(BACKEND_INVALID_TEXTURE),
	EmitterAge(0),
	SpawnAccumulator(0.0f),
	BurstRemaining(style.BurstCount)
{
	Refresh_Texture();
	Particles.reserve((size_t)std::max(1, Style.MaxParticles));
	Emitters.Add(this);
}


/// <summary>
/// EXTENSION: a placeholder construction path Load uses. The real Origin/Style come from
/// the very next Serialize call once this has already added itself to Emitters, the same
/// idiom RadarEventClass::Load uses for its own placeholder objects.
/// </summary>
GPUParticleClass::GPUParticleClass(void) :
	Origin(Coord(0, 0, 0)),
	Style(),
	Texture(BACKEND_INVALID_TEXTURE),
	EmitterAge(0),
	SpawnAccumulator(0.0f),
	BurstRemaining(0)
{
	Emitters.Add(this);
}


/// <summary>
/// Retires this emitter, taking it out of the master list.
/// </summary>
GPUParticleClass::~GPUParticleClass(void)
{
	Emitters.Delete(this);
}


/// <summary>
/// (Re)loads Style.TextureFilename, or clears Texture back to
/// BACKEND_INVALID_TEXTURE when it's empty. Called after construction and again after a
/// Serialize load, since a texture handle is a runtime GPU resource reference that means
/// nothing carried across a save.
/// </summary>
void GPUParticleClass::Refresh_Texture(void)
{
	Texture = BACKEND_INVALID_TEXTURE;

	if (Style.TextureFilename.empty()) {
		return;
	}

	CCFileClass file((char const *)Style.TextureFilename);
	if (!file.Is_Available()) {
		return;
	}

	int size = file.Size();
	void * data = Load_Alloc_Data(file);
	if (data == NULL || size <= 0) {
		delete [] (char *)data;
		return;
	}

	Texture = Backend_Load_Texture((char const *)Style.TextureFilename, data, (unsigned int)size);
	delete [] (char *)data;
}


/// <summary>
/// Adds one new particle at the emitter's own origin, with a random initial velocity
/// bounded by Style.SpreadRadius/SpreadHeight and a size baked in from
/// Style.Size/SizeVariance. Sim_Random_Pick is used throughout rather than a plain rand(),
/// since this runs as part of synced game logic and needs to produce the same sequence on
/// every machine in a multiplayer game.
/// </summary>
void GPUParticleClass::Spawn_Particle(void)
{
	Particle particle;
	particle.OffsetX = 0.0f;
	particle.OffsetY = 0.0f;
	particle.OffsetZ = 0.0f;
	particle.VelocityX = (float)Sim_Random_Pick(-1000, 1000) * 0.001f * Style.SpreadRadius;
	particle.VelocityY = (float)Sim_Random_Pick(-1000, 1000) * 0.001f * Style.SpreadRadius;
	particle.VelocityZ = Style.InitialVelocityZ + (float)Sim_Random_Pick(-1000, 1000) * 0.001f * Style.SpreadHeight;
	particle.Age = 0;

	float variance = 1.0f + (float)Sim_Random_Pick(-1000, 1000) * 0.001f * Style.SizeVariance;
	particle.Size = std::max(0.5f, Style.Size * variance);

	Particles.push_back(particle);
}


/// <summary>
/// Advances this emitter by one game frame. See the declaration in gpuparticle.h for the
/// pause-awareness this gets purely from being called from LogicClass::AI.
/// </summary>
void GPUParticleClass::AI(void)
{
	EmitterAge++;

	bool stillspawning;
	if (Style.BurstCount > 0) {
		while (BurstRemaining > 0 && (int)Particles.size() < Style.MaxParticles) {
			Spawn_Particle();
			BurstRemaining--;
		}
		stillspawning = BurstRemaining > 0;
	} else {
		stillspawning = (Style.Duration == 0 || EmitterAge <= Style.Duration);
		if (stillspawning) {
			SpawnAccumulator += Style.SpawnRate;
			while (SpawnAccumulator >= 1.0f && (int)Particles.size() < Style.MaxParticles) {
				Spawn_Particle();
				SpawnAccumulator -= 1.0f;
			}
		}
	}

	for (size_t index = 0; index < Particles.size(); ) {
		Particle & particle = Particles[index];

		particle.VelocityZ -= Style.Gravity;
		particle.OffsetX += particle.VelocityX;
		particle.OffsetY += particle.VelocityY;
		particle.OffsetZ += particle.VelocityZ;
		particle.Age++;

		if (particle.Age >= Style.Lifetime) {
			// Swap-and-pop rather than a mid-vector erase, since particle order doesn't
			// matter and this is the per-tick hot path for however many particles this
			// emitter has alive.
			particle = Particles.back();
			Particles.pop_back();
		} else {
			index++;
		}
	}

	// Once nothing is left to spawn and every already-spawned particle has aged out,
	// there is nothing left for this emitter to ever do again.
	if (!stillspawning && Particles.empty()) {
		delete this;
	}
}


/// <summary>
/// Queues this emitter's currently-alive particles as GPU quads. See the declaration in
/// gpuparticle.h for why this runs every render frame regardless of pause.
/// </summary>
void GPUParticleClass::Draw_It(void)
{
	if (Particles.empty() || TacticalMap == NULL) {
		return;
	}

	for (size_t index = 0; index < Particles.size(); index++) {
		Particle const & particle = Particles[index];

		Coord world = Origin;
		world.X += (int)particle.OffsetX;
		world.Y += (int)particle.OffsetY;
		world.Z += (int)particle.OffsetZ;

		Point2D pixel;
		if (!TacticalMap->Coord_To_Pixel(world, pixel)) {
			continue;
		}

		// EXTENSION: same scroll-delta correction Backend_Queue_GPU_Beam's own depth
		// needs -- see laser.cpp for why. Computed per particle, at that particle's own
		// screen row, rather than once for the whole emitter, since particles from the
		// same emitter can easily end up several rows apart on screen.
		int rawdepth = -TacticalMap->Z_Lepton_To_Pixel(world.Z) - 2;
		if (DepthBuffer != NULL) {
			rawdepth += DepthBuffer->Get_Scroll_Delta(pixel.Y - DepthBuffer->Get_Bounds().Y);
		}

		float lifefraction = (float)particle.Age / (float)std::max(1, Style.Lifetime);
		float fade = 1.0f - lifefraction;
		if (fade < 0.0f) {
			fade = 0.0f;
		}

		unsigned int alpha = (unsigned int)((float)((Style.Color >> 24) & 0xFF) * fade);
		unsigned int color = (Style.Color & 0x00FFFFFF) | (alpha << 24);

		Backend_Queue_GPU_Particle((float)pixel.X, (float)pixel.Y, (float)rawdepth, particle.Size, color, Texture);
	}
}


/// <summary>
/// Lists the members one particle emitter carries, including its own particle array.
/// The texture handle is deliberately not part of this -- see Refresh_Texture's own doc
/// comment for why it's re-resolved instead.
/// </summary>
void GPUParticleClass::Serialize(SaveStreamClass & stream)
{
	stream.Serialize(Origin);
	stream.Serialize(Style.SpawnRate);
	stream.Serialize(Style.MaxParticles);
	stream.Serialize(Style.Duration);
	stream.Serialize(Style.BurstCount);
	stream.Serialize(Style.Lifetime);
	stream.Serialize(Style.Gravity);
	stream.Serialize(Style.SpreadRadius);
	stream.Serialize(Style.SpreadHeight);
	stream.Serialize(Style.InitialVelocityZ);
	stream.Serialize(Style.Size);
	stream.Serialize(Style.SizeVariance);
	stream.Serialize(Style.Color);
	stream.Serialize(Style.TextureFilename);
	stream.Serialize(EmitterAge);
	stream.Serialize(SpawnAccumulator);
	stream.Serialize(BurstRemaining);

	int count = (int)Particles.size();
	stream.Serialize(count);
	if ((size_t)count != Particles.size()) {
		// Only takes this path on load -- count was just overwritten from the stream,
		// above, to whatever the saved emitter actually had.
		Particles.resize((size_t)count);
	}

	for (int index = 0; index < count; index++) {
		stream.Serialize(Particles[index].OffsetX);
		stream.Serialize(Particles[index].OffsetY);
		stream.Serialize(Particles[index].OffsetZ);
		stream.Serialize(Particles[index].VelocityX);
		stream.Serialize(Particles[index].VelocityY);
		stream.Serialize(Particles[index].VelocityZ);
		stream.Serialize(Particles[index].Age);
		stream.Serialize(Particles[index].Size);
	}

	Refresh_Texture();
}


/// <summary>
/// Advances every particle emitter in play. Iterates backward specifically because
/// AI can delete the emitter it was called on (once it has nothing left to spawn or
/// age out), the same self-deletion-safe pattern LaserDrawClass::Draw_All already uses.
/// </summary>
void GPUParticleClass::Update_All(void)
{
	for (int i = Emitters.Count() - 1; i >= 0; i--) {
		Emitters[i]->AI();
	}
}


/// <summary>
/// Draws every particle emitter in play.
/// </summary>
void GPUParticleClass::Draw_All(void)
{
	for (int i = 0; i < Emitters.Count(); i++) {
		Emitters[i]->Draw_It();
	}
}


/// <summary>
/// Retires every particle emitter immediately, discarding whatever they were doing.
/// </summary>
void GPUParticleClass::Clear_All(void)
{
	for (int i = Emitters.Count() - 1; i >= 0; i--) {
		delete Emitters[i];
	}
}


/// <summary>
/// Writes every particle emitter currently in play to a saved game.
/// </summary>
/// <param name="stream">The stream to write the emitters to.</param>
/// <returns>bool; Were the emitters written successfully?</returns>
bool GPUParticleClass::Save(IStream * stream)
{
	SaveStreamClass savestream(stream, SaveStreamClass::MODE_SAVE);

	int count = Emitters.Count();
	savestream.Serialize(count);

	for (int index = 0; index < count; index++) {
		Emitters[index]->Serialize(savestream);
	}

	return(SUCCEEDED(savestream.Result()));
}


/// <summary>
/// Reads particle emitters back from a saved game. Any emitters currently in play are
/// discarded first, so the stream's emitters entirely replace them.
/// </summary>
/// <param name="stream">The stream to read the emitters from.</param>
/// <returns>bool; Were the emitters read successfully?</returns>
bool GPUParticleClass::Load(IStream * stream)
{
	Clear_All();

	SaveStreamClass savestream(stream, SaveStreamClass::MODE_LOAD);
	savestream.Set_Context("GPUParticleClass");

	int count = 0;
	savestream.Serialize(count);

	for (int index = 0; index < count; index++) {
		GPUParticleClass * emitter = new GPUParticleClass();
		emitter->Serialize(savestream);
	}

	return(SUCCEEDED(savestream.Result()));
}
