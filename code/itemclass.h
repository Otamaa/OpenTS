/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// EXTENSION: prototype world pickup object (Otamaa) -- the actual on-map
// representation of an item dropped by ItemSlot::Drop_On_Death. Answers the
// "where's the object that inherits from ObjectClass" gap from the previous
// pass: this is that object.
//
// DESIGN, verified against the real engine rather than assumed:
//
//   - ObjectClass::Unlimbo/Mark/Limbo are all Class_Of()-null-safe (checked
//     directly in object.cpp). Class_Of() == nullptr only skips the
//     Logic.Submit(this) branch inside Unlimbo (no ObjectTypeClass::IsSentient
//     to test), NOT the Map.Submit(this) branch that puts the object in the
//     render layer. So ItemClass genuinely places into the world and draws
//     through the normal generic pipeline without needing an ItemTypeClass /
//     INI table -- it just never receives a per-object ObjectClass::AI() call
//     from LogicClass::AI's object loop.
//   - That "no generic AI()" gap is filled the same way ItemSlot fills it:
//     one static Update_All(), called once per frame from LogicClass::AI,
//     driving despawn + pickup for every dropped item in a single pass. This
//     matches ItemSlot::AI_Pass's own reasoning and VeinholeMonsterClass's
//     existing precedent in this exact codebase (see vein.h/vein.cpp --
//     VeinholeMonsterClass also self-drives via Update_All rather than the
//     generic per-object AI() dispatch).
//   - Persistence also follows the VeinholeMonsterClass precedent instead of
//     the generic per-object CLSID factory: VeinholeMonsterClass is NOT in
//     startup.cpp's REGISTER_CLASS table; it round-trips itself via its own
//     Save_All(IStream*)/Load_All(IStream*), called explicitly from
//     saveload.cpp. ItemClass does the same (see itemslot_integration.md
//     section 7) -- lighter than wiring a full COM class-factory entry for a
//     prototype object type.
//   - Class_Of() returns nullptr (no ObjectTypeClass yet). VERIFY: this repo
//     was not exhaustively grepped for every unconditional Class_Of()
//     dereference outside of Mark/Unlimbo/Limbo (which are confirmed safe);
//     flagging rather than asserting total safety.

#pragma once

#include "itemslot.h"

#include "coord.h"
#include "object.h"

#include <cstdio>

template<class T> class DynamicVectorClass;
struct IStream;

class ItemClass : public ObjectClass
{
	typedef ObjectClass BASECLASS;

public:
	// Places a dropped item at `coord`. despawnFrames <= 0 means "never
	// despawns" -- VERIFY: confirm that's the right default for a prototype;
	// an always-persistent item pile is easy to forget to bound later.
	ItemClass(ItemSlot::ItemInstance const & item, Coord const & coord, int despawnFrames = -1);

	// Default constructor for Load_All reconstruction; the loaded Serialize
	// call populates everything below.
	ItemClass(void);

	virtual ~ItemClass(void) override;

	virtual HRESULT STDMETHODCALLTYPE GetClassID(CLSID * retval) override;

	virtual RTTIType Fetch_RTTI(void) const override;

	// Per-object member list, following the same shape as every other
	// ObjectClass::Serialize override (BASECLASS::Serialize(stream) first,
	// then this class's own members). Called once per item from inside
	// Save_All/Load_All below -- not part of the generic per-object save
	// path, since ItemClass isn't in startup.cpp's CLSID factory table.
	virtual void Serialize(SaveStreamClass & stream) override;

	// Payload + DespawnTimer only -- position is deliberately NOT folded in
	// here, matching this codebase's existing convention: ObjectClass's and
	// AbstractClass's own Compute_CRC bodies don't hash raw coordinates
	// either (checked directly; ObjectClass::Compute_CRC covers flags like
	// IsInLimbo/Strength/IsActive, never Position). Staying consistent with
	// that rather than introducing a stricter policy just for this class.
	virtual void Compute_CRC(CRCEngine & crc) const override;

	// VERIFY: nullptr is tolerated by Mark/Unlimbo/Limbo (confirmed). Not
	// confirmed safe everywhere else -- see file header note.
	virtual ObjectTypeClass const * Class_Of(void) const override { return(nullptr); }

	// TODO: no art/shape for a generic item pickup yet. Left as a no-op
	// rather than guessing at a placeholder shape draw call.
	virtual void Draw_It(Point2D const & point, Rect const & cliprect) const override { (void)point; (void)cliprect; }

	ItemSlot::ItemInstance const & Item(void) const { return(Payload); }
	// NOTE: position is NOT duplicated here. ObjectClass already tracks it
	// via a Position member reached through the Get_Coord()/Set_Coord()
	// virtuals and the PositionCoord __declspec(property) (confirmed in
	// object.h -- ObjectClass::Get_Coord's default body returns Position).
	// Use the inherited Center_Coord()/PositionCoord, not a new field.

	// ------------------------------------------------------------------
	// Self-driven system (see design note above). Called once per frame
	// from LogicClass::AI, immediately after ItemSlot::AI_Pass().
	// ------------------------------------------------------------------
	static void Update_All(void);

	// Fail-safe full sweep, mirrors ItemSlot::Clear_All. Called from
	// Clear_Scenario alongside it.
	static void Clear_All(void);

	// VeinholeMonsterClass-style explicit persistence (see design note).
	static bool Save_All(IStream * stream);
	static bool Load_All(IStream * stream);

	// Wires this class in as ItemSlot's real drop callback, replacing the
	// inert DebugString default. Call once at startup, after both systems
	// exist but before any object can be destroyed. Deliberately NOT called
	// automatically by either header -- see itemslot_integration.md section 7
	// for why (keeps itemslot.cpp free of any dependency on itemclass.h).
	static void Install_As_Drop_Callback(void);

private:
	// Distance (leptons) within which a Techno picks this item up. VERIFY:
	// should probably become Rule-driven once an ItemTypeClass exists;
	// hand-picked prototype constant for now, deliberately not reusing
	// Rule->CrateRadius since crates and dropped items are conceptually
	// different systems that happen to want a similar-shaped constant.
	static constexpr int PickupRadius = 128;

	static void Try_Pickup(ItemClass * item);

	static void Spawn_From_Drop(TechnoClass * owner, ItemSlot::ItemInstance const & item, Coord const & coord);

	ItemSlot::ItemInstance Payload;

	// Frames remaining before this pickup vanishes unclaimed. < 0 means it
	// never despawns on its own (only picked up, or swept by Clear_All).
	int DespawnTimer;

	// NOTE: no separate "marked for deletion" flag. ObjectClass::Delete_Me
	// already sets IsActive = false and queues the object on the engine's
	// own deferred-deletion list (ObjectsToDelete, drained by
	// Process_Deferred_Deletion) -- Update_All() checks the inherited
	// IsActive instead of inventing a parallel flag.
};

extern DynamicVectorClass<ItemClass *> DroppedItems;
