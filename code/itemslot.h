/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// EXTENSION: prototype Techno item slot system (Otamaa).
// entt-backed component/system pair providing per-TechnoClass equipment slots.
// Primary usage for this pass: stat multipliers only, with an optional
// drop-on-death hook. Everything else (item type table, INI wiring, pickup
// world objects) is out of scope and marked TODO below.

#pragma once

// VERIFY: adjust to match the actual target/include path once entt is wired
// into code/CMakeLists.txt or thirdparty/CMakeLists.txt. entt.hpp (registry,
// views, storage) is only needed here and in itemslot.cpp; techno.h only
// needs entt::entity + entt::null, which live in entity/entity.hpp -- NOT
// entity/fwd.hpp (fwd.hpp forward-declares the type but does not define the
// null/tombstone sentinels; confirmed against entt's own source). Using
// entity.hpp there instead of the full entt.hpp keeps registry/view/storage
// templates out of every translation unit that pulls in techno.h.
#include <entt/entt.hpp>

#include <array>
#include <cstddef>

class TechnoClass;
class HouseClass;
class SaveStreamClass;
class CRCEngine;
class Coord;

namespace ItemSlot
{
	// Fixed slot count for the prototype. Raise this (or make it per-TechnoType
	// driven off TechnoTypeClass) once the item type table exists.
	inline constexpr std::size_t MaxSlots = 4;

	// Placeholder item identity. Until an ItemTypeClass/INI table exists this is
	// just an opaque integer the caller assigns meaning to; None marks an empty
	// slot. Deliberately NOT an enum class tied to a hardcoded item list.
	enum class ItemTypeID : int { None = -1 };

	/// <summary>
	/// Multiplicative stat bundle a single equipped item contributes. All fields
	/// default to 1.0 (no-op) so an empty/default-constructed item never perturbs
	/// the aggregate. VERIFY: confirm which of these should stack multiplicatively
	/// vs. additively once real item design lands -- multiplicative is assumed
	/// here because it composes cleanly across N slots without an ordering
	/// dependency.
	/// </summary>
	struct StatMultiplier
	{
		float Armor      = 1.0f;
		float Firepower   = 1.0f;
		float Speed       = 1.0f;
		float Sight       = 1.0f;
		float RateOfFire  = 1.0f;

		StatMultiplier & Combine(StatMultiplier const & other)
		{
			Armor      *= other.Armor;
			Firepower  *= other.Firepower;
			Speed      *= other.Speed;
			Sight      *= other.Sight;
			RateOfFire *= other.RateOfFire;
			return(*this);
		}

		// HasSerializeMember hook (see savestream.h) -- lets std::array<ItemInstance,N>
		// serialize this element-by-element automatically.
		void Serialize(SaveStreamClass & stream);

		// Folds every field into the multiplayer desync-detection CRC. Called
		// from ItemInstance::Compute_CRC below, which is in turn called from
		// ItemSlot::Compute_CRC for every slot (see that function for why
		// empty slots are included too).
		void Compute_CRC(CRCEngine & crc) const;
	};

	/// <summary>
	/// One equipped item. TypeID == None means the slot is empty.
	/// </summary>
	struct ItemInstance
	{
		ItemTypeID     TypeID      = ItemTypeID::None;
		StatMultiplier Multiplier  {};
		bool           DropOnDeath = true;

		bool Is_Empty(void) const { return(TypeID == ItemTypeID::None); }
		void Clear(void) { *this = ItemInstance(); }

		void Serialize(SaveStreamClass & stream);
		void Compute_CRC(CRCEngine & crc) const;
	};

	/// <summary>
	/// entt component holding one TechnoClass's item slots plus the cached,
	/// pre-multiplied result. Owner is a raw back-reference for the AI pass and
	/// the drop-on-death system; it is never serialized (TechnoClass::Serialize
	/// re-establishes it on load, see ItemSlot::Serialize below).
	/// </summary>
	struct ItemSlotComponent
	{
		std::array<ItemInstance, MaxSlots> Slots {};
		StatMultiplier                     Effective {};

		// Set whenever a slot changes; cleared by AI_Pass() once Effective is
		// recomputed. Avoids recomputing N multiplications per entity per frame
		// for objects whose loadout hasn't changed.
		bool Dirty = true;

		// Set while the owning object is limboed (see On_Limbo/On_Unlimbo).
		// AI_Pass() skips suspended entities -- a limboed object is not "in the
		// world" and its multipliers cannot matter to anything reading them.
		bool Suspended = false;

		TechnoClass * Owner = nullptr;
	};

	// ------------------------------------------------------------------
	// Lifecycle. Call sites (see itemslot_integration.md):
	//   Attach()  <- TechnoClass::Init
	//   Release() <- Detach_This_From_All (tracker.cpp) / defense-in-depth in ~TechnoClass
	//   Clear_All <- Clear_Scenario (scenario.cpp)
	// ------------------------------------------------------------------

	// Creates the entity + component for `owner` if one does not already exist.
	// Idempotent: calling this twice for the same owner is a no-op the second
	// time (returns the existing entity).
	entt::entity Attach(TechnoClass * owner);

	// Destroys the entity + component for `owner`, if any. Safe to call on an
	// object that was never attached, or twice.
	void Release(TechnoClass const * owner);

	// Wipes every item-slot entity in one shot. Called once from Clear_Scenario
	// as a fail-safe full sweep; individual Release() calls via
	// Detach_This_From_All should already have emptied the registry by the time
	// this runs, so this mainly guards against anything that skipped that path.
	void Clear_All(void);

	// ------------------------------------------------------------------
	// Slot manipulation.
	// ------------------------------------------------------------------

	// Returns false if slotIndex is out of range. Overwrites whatever was in
	// the slot; callers that care about the previous contents should Unequip
	// first (e.g. to hand it back to the player / drop it explicitly).
	bool Equip(TechnoClass * owner, std::size_t slotIndex, ItemTypeID type,
		StatMultiplier const & multiplier, bool dropOnDeath = true);

	// Same as Equip, but finds the first empty slot itself instead of taking
	// one from the caller. Returns false (and equips nothing) if every slot
	// is occupied. Added for TechnoClass-side pickup logic (see ItemClass),
	// which doesn't know or care which slot index an item lands in.
	bool Equip_First_Empty(TechnoClass * owner, ItemTypeID type,
		StatMultiplier const & multiplier, bool dropOnDeath = true);

	// Empties a slot. If `out` is non-null, the previous contents are copied
	// there before being cleared (so the caller can spawn a pickup, refund it,
	// etc.). Returns false if slotIndex is out of range or already empty.
	bool Unequip(TechnoClass * owner, std::size_t slotIndex, ItemInstance * out = nullptr);

	// Forces a recompute on the next AI_Pass() even if nothing here thinks it
	// changed (used by On_Captured, and available for external callers that
	// mutate a slot's Multiplier in place instead of going through Equip).
	void Mark_Dirty(TechnoClass * owner);

	// ------------------------------------------------------------------
	// Queries. Both are nullptr/identity-safe for an owner with no component.
	// ------------------------------------------------------------------

	// Read-only access to the cached, already-multiplied stat bundle. Returns
	// nullptr if `owner` has no item-slot component (never attached, or already
	// released) -- callers should treat that the same as "no bonuses".
	StatMultiplier const * Find_Effective(TechnoClass const * owner);

	// Convenience wrapper: same as Find_Effective, but returns a default
	// (all-1.0, i.e. no-op) StatMultiplier instead of nullptr. Use this at
	// call sites that would otherwise need a null check on every read.
	StatMultiplier Effective_Or_Default(TechnoClass const * owner);

	// ------------------------------------------------------------------
	// Systems.
	// ------------------------------------------------------------------

	// Single global pass, called once per frame from LogicClass::AI (NOT from
	// TechnoClass::AI / its per-subclass overrides -- see design note in
	// itemslot_integration.md for why). Recomputes Effective for every dirty,
	// non-suspended entity. O(active item-slot entities), independent of the
	// engine's ObjectClass list.
	void AI_Pass(void);

	// Called from TechnoClass::Take_Damage's RESULT_DESTROYED branch, before
	// the object is actually torn down. Walks the owner's slots and, for every
	// occupied slot with DropOnDeath == true, invokes the drop callback (see
	// Set_Drop_Callback) then clears the slot. No-op if owner has no component.
	void Drop_On_Death(TechnoClass * owner);

	// Called from TechnoClass::Limbo / TechnoClass::Unlimbo. Toggles Suspended
	// so AI_Pass() skips this entity while the object is off-map. Both are
	// no-ops if owner has no component.
	void On_Limbo(TechnoClass * owner);
	void On_Unlimbo(TechnoClass * owner);

	// Called from TechnoClass::Captured. The prototype's item slots are not
	// house-scoped, so this does not eject or reroll anything by default --
	// it just marks the component dirty in case a future item type has an
	// owner-dependent multiplier (e.g. a house-specific bonus). This is the
	// natural place to add "house-locked items get dropped on capture" logic
	// later; left as a TODO rather than guessed at.
	void On_Captured(TechnoClass * owner, HouseClass * newOwner);

	// ------------------------------------------------------------------
	// Serialization. Called from TechnoClass::Serialize (both save and load).
	// Item slots are round-tripped as plain data (see StatMultiplier::Serialize
	// / ItemInstance::Serialize above), not as the entt::entity handle -- entity
	// IDs are only meaningful within one process's registry and should never be
	// persisted. On load, this re-Attaches the component if it doesn't already
	// exist (Init() runs before Serialize() during load, so in practice it
	// already does) and marks it dirty so AI_Pass() recomputes Effective.
	// ------------------------------------------------------------------
	void Serialize(TechnoClass * owner, SaveStreamClass & stream);

	// ------------------------------------------------------------------
	// Multiplayer desync detection. Called from TechnoClass::Compute_CRC.
	// Folds every slot (including empty ones -- see itemslot.cpp for why)
	// into the running CRC. No-op if owner has no item-slot component, same
	// as every other query in this file.
	// ------------------------------------------------------------------
	void Compute_CRC(TechnoClass const * owner, CRCEngine & crc);

	// ------------------------------------------------------------------
	// Drop-on-death world hook. Deliberately NOT wired to any concrete
	// crate/pickup class -- OpenTS does not have an item pickup object yet.
	// The default callback just DebugStrings; replace it (e.g. from game
	// init) once a pickup representation exists.
	// ------------------------------------------------------------------
	using DropCallback = void(*)(TechnoClass * owner, ItemInstance const & item, Coord const & coord);
	void Set_Drop_Callback(DropCallback callback);

	// Direct registry access for anything that needs a raw entt::view (e.g. a
	// debug dump). Prefer the typed helpers above where they cover the need.
	entt::registry & Registry(void);
}
