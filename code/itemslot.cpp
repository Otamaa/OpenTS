/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "itemslot.h"

#include "dbgprint.h"
#include "crc.h"
#include "savestream.h"
#include "techno.h"

namespace ItemSlot
{
	namespace
	{
		// Meyer's singleton: avoids static initialization order issues with
		// anything that might touch the registry before main() runs (e.g. from
		// another translation unit's static object registration).
		entt::registry & Registry_Impl(void)
		{
			static entt::registry registry;
			return(registry);
		}

		DropCallback g_DropCallback = nullptr;

		void Default_Drop_Callback(TechnoClass * owner, ItemInstance const & item, Coord const & coord)
		{
			// TODO: spawn a world pickup representation once OpenTS has one.
			// Kept intentionally inert (no world mutation) so the default
			// behavior of the prototype is observable-but-harmless.
			(void)owner;
			(void)coord;
			DebugString("ItemSlot: dropped item %d on death (no pickup object wired yet)\n",
				(int)item.TypeID);
		}
	}

	entt::registry & Registry(void)
	{
		return(Registry_Impl());
	}

	// ------------------------------------------------------------------
	// StatMultiplier / ItemInstance serialization
	// ------------------------------------------------------------------

	void StatMultiplier::Serialize(SaveStreamClass & stream)
	{
		stream.Serialize(Armor);
		stream.Serialize(Firepower);
		stream.Serialize(Speed);
		stream.Serialize(Sight);
		stream.Serialize(RateOfFire);
	}

	void ItemInstance::Serialize(SaveStreamClass & stream)
	{
		stream.Serialize(TypeID);
		stream.Serialize(Multiplier);
		stream.Serialize(DropOnDeath);
	}

	void StatMultiplier::Compute_CRC(CRCEngine & crc) const
	{
		crc(Armor);
		crc(Firepower);
		crc(Speed);
		crc(Sight);
		crc(RateOfFire);
	}

	void ItemInstance::Compute_CRC(CRCEngine & crc) const
	{
		// TypeID is an enum, not one of CRCEngine::operator()'s overloaded
		// types -- cast to int, same as TechnoClass::Compute_CRC does for
		// its own enum fields (e.g. crc((int)Arm), crc((int)LimpetType)).
		crc((int)TypeID);
		Multiplier.Compute_CRC(crc);
		crc(DropOnDeath);
	}

	// ------------------------------------------------------------------
	// Lifecycle
	// ------------------------------------------------------------------

	entt::entity Attach(TechnoClass * owner)
	{
		if (owner == nullptr) {
			return(entt::null);
		}

		if (owner->ItemSlotEntity != entt::null) {
			// VERIFY: this assumes Init() cannot legitimately run twice on a
			// live object. Confirmed for the constructor call path (see
			// techno.cpp / unit.cpp / building.cpp / infantry.cpp / aircraft.cpp);
			// flag loudly rather than silently leaking a second entity if that
			// assumption is ever violated.
			if (!Registry_Impl().valid(owner->ItemSlotEntity)) {
				DebugString("ItemSlot: owner %p had a stale entity handle, reattaching\n", (void *)owner);
				owner->ItemSlotEntity = entt::null;
			} else {
				return(owner->ItemSlotEntity);
			}
		}

		entt::entity entity = Registry_Impl().create();
		ItemSlotComponent & component = Registry_Impl().emplace<ItemSlotComponent>(entity);
		component.Owner = owner;
		owner->ItemSlotEntity = entity;
		return(entity);
	}

	void Release(TechnoClass const * owner)
	{
		if (owner == nullptr || owner->ItemSlotEntity == entt::null) {
			return;
		}

		if (Registry_Impl().valid(owner->ItemSlotEntity)) {
			Registry_Impl().destroy(owner->ItemSlotEntity);
		}

		// const_cast is safe here: we are only clearing the cached handle on
		// the owning object, not mutating any logical game state.
		const_cast<TechnoClass *>(owner)->ItemSlotEntity = entt::null;
	}

	void Clear_All(void)
	{
		// Sweeps every remaining entity's Owner back-reference clean before the
		// registry itself is cleared, in case anything still holds a stale
		// TechnoClass* -> ItemSlotEntity pairing across the wipe (defensive;
		// Clear_Scenario deletes every ObjectClass around the same time, so in
		// practice these should already be empty by the time this runs).
		Registry_Impl().view<ItemSlotComponent>().each([](ItemSlotComponent & component) {
			if (component.Owner != nullptr) {
				component.Owner->ItemSlotEntity = entt::null;
			}
		});

		Registry_Impl().clear();
	}

	// ------------------------------------------------------------------
	// Slot manipulation
	// ------------------------------------------------------------------

	bool Equip(TechnoClass * owner, std::size_t slotIndex, ItemTypeID type,
		StatMultiplier const & multiplier, bool dropOnDeath)
	{
		if (slotIndex >= MaxSlots) {
			return(false);
		}

		entt::entity entity = Attach(owner);
		if (entity == entt::null) {
			return(false);
		}

		ItemSlotComponent & component = Registry_Impl().get<ItemSlotComponent>(entity);
		ItemInstance & slot = component.Slots[slotIndex];
		slot.TypeID = type;
		slot.Multiplier = multiplier;
		slot.DropOnDeath = dropOnDeath;
		component.Dirty = true;
		return(true);
	}

	bool Equip_First_Empty(TechnoClass * owner, ItemTypeID type,
		StatMultiplier const & multiplier, bool dropOnDeath)
	{
		entt::entity entity = Attach(owner);
		if (entity == entt::null) {
			return(false);
		}

		ItemSlotComponent & component = Registry_Impl().get<ItemSlotComponent>(entity);
		for (std::size_t index = 0; index < MaxSlots; index++) {
			if (component.Slots[index].Is_Empty()) {
				return(Equip(owner, index, type, multiplier, dropOnDeath));
			}
		}
		return(false);
	}

	bool Unequip(TechnoClass * owner, std::size_t slotIndex, ItemInstance * out)
	{
		if (owner == nullptr || slotIndex >= MaxSlots || owner->ItemSlotEntity == entt::null) {
			return(false);
		}

		if (!Registry_Impl().valid(owner->ItemSlotEntity)) {
			return(false);
		}

		ItemSlotComponent & component = Registry_Impl().get<ItemSlotComponent>(owner->ItemSlotEntity);
		ItemInstance & slot = component.Slots[slotIndex];
		if (slot.Is_Empty()) {
			return(false);
		}

		if (out != nullptr) {
			*out = slot;
		}

		slot.Clear();
		component.Dirty = true;
		return(true);
	}

	void Mark_Dirty(TechnoClass * owner)
	{
		if (owner == nullptr || owner->ItemSlotEntity == entt::null) {
			return;
		}

		if (Registry_Impl().valid(owner->ItemSlotEntity)) {
			Registry_Impl().get<ItemSlotComponent>(owner->ItemSlotEntity).Dirty = true;
		}
	}

	// ------------------------------------------------------------------
	// Queries
	// ------------------------------------------------------------------

	StatMultiplier const * Find_Effective(TechnoClass const * owner)
	{
		if (owner == nullptr || owner->ItemSlotEntity == entt::null) {
			return(nullptr);
		}

		if (!Registry_Impl().valid(owner->ItemSlotEntity)) {
			return(nullptr);
		}

		return(&Registry_Impl().get<ItemSlotComponent>(owner->ItemSlotEntity).Effective);
	}

	StatMultiplier Effective_Or_Default(TechnoClass const * owner)
	{
		StatMultiplier const * effective = Find_Effective(owner);
		return(effective != nullptr ? *effective : StatMultiplier());
	}

	// ------------------------------------------------------------------
	// Systems
	// ------------------------------------------------------------------

	// DESIGN NOTE (answers "global vs Techno-specific" from the request):
	//
	// This is a single global pass hooked into LogicClass::AI, not into
	// TechnoClass::AI (or its FootClass/BuildingClass/InfantryClass/UnitClass/
	// AircraftClass overrides). Reasons:
	//
	//  1. TechnoClass::AI is overridden by every concrete subclass. Hooking it
	//     per-object means touching 5+ call sites instead of 1, which is a
	//     bigger diff and a bigger place for an override to be missed later.
	//  2. LogicClass::AI already runs exactly this shape of update once per
	//     frame for other singleton systems (VeinholeMonsterClass::Update_All,
	//     TiberiumClass::Tiberium_Growth, SpotLightClass::Update_All, ...) --
	//     this follows the existing precedent instead of inventing a new one.
	//  3. An entt registry.view iteration is already a contiguous, cache
	//     friendly batch pass over exactly the entities that have the
	//     component. Routing it through the per-object ObjectClass::AI() vtable
	//     dispatch loop would throw that away for no benefit.
	//
	// AI_Pass only recomputes entities that are Dirty and not Suspended, so an
	// idle loadout costs one branch per entity per frame, not N multiplications.
	void AI_Pass(void)
	{
		Registry_Impl().view<ItemSlotComponent>().each([](entt::entity, ItemSlotComponent & component) {
			if (component.Suspended || !component.Dirty) {
				return;
			}

			StatMultiplier effective {};
			for (ItemInstance const & slot : component.Slots) {
				if (!slot.Is_Empty()) {
					effective.Combine(slot.Multiplier);
				}
			}

			component.Effective = effective;
			component.Dirty = false;
		});
	}

	void Drop_On_Death(TechnoClass * owner)
	{
		if (owner == nullptr || owner->ItemSlotEntity == entt::null) {
			return;
		}

		if (!Registry_Impl().valid(owner->ItemSlotEntity)) {
			return;
		}

		ItemSlotComponent & component = Registry_Impl().get<ItemSlotComponent>(owner->ItemSlotEntity);
		DropCallback callback = (g_DropCallback != nullptr) ? g_DropCallback : &Default_Drop_Callback;

		Coord const coord = owner->Center_Coord();

		bool changed = false;
		for (ItemInstance & slot : component.Slots) {
			if (slot.Is_Empty() || !slot.DropOnDeath) {
				continue;
			}

			callback(owner, slot, coord);
			slot.Clear();
			changed = true;
		}

		if (changed) {
			component.Dirty = true;
		}
	}

	void On_Limbo(TechnoClass * owner)
	{
		if (owner == nullptr || owner->ItemSlotEntity == entt::null) {
			return;
		}

		if (Registry_Impl().valid(owner->ItemSlotEntity)) {
			Registry_Impl().get<ItemSlotComponent>(owner->ItemSlotEntity).Suspended = true;
		}
	}

	void On_Unlimbo(TechnoClass * owner)
	{
		if (owner == nullptr || owner->ItemSlotEntity == entt::null) {
			return;
		}

		if (Registry_Impl().valid(owner->ItemSlotEntity)) {
			ItemSlotComponent & component = Registry_Impl().get<ItemSlotComponent>(owner->ItemSlotEntity);
			component.Suspended = false;
			// Loadout didn't change while limboed, but Effective may not have
			// been computed yet if the object was equipped and limboed inside
			// the same frame; cheap to just recompute on the way back in.
			component.Dirty = true;
		}
	}

	void On_Captured(TechnoClass * owner, HouseClass * newOwner)
	{
		(void)newOwner;

		// TODO: house-scoped item rules (eject on capture, reroll multipliers
		// for the new owner, etc.) go here once item design calls for them.
		// For now, items are not house-scoped, so capture is a no-op beyond
		// making sure a stale Effective isn't read before the next AI_Pass.
		Mark_Dirty(owner);
	}

	// ------------------------------------------------------------------
	// Serialization
	// ------------------------------------------------------------------

	void Serialize(TechnoClass * owner, SaveStreamClass & stream)
	{
		if (owner == nullptr) {
			return;
		}

		// On load, TechnoClass::Init() has already run (constructors call it
		// before Serialize() is invoked to populate state), so the component
		// normally already exists. Attach() is idempotent, so calling it here
		// too costs nothing and protects against Init()/Serialize() ordering
		// changing later.
		entt::entity entity = Attach(owner);
		if (entity == entt::null) {
			return;
		}

		ItemSlotComponent & component = Registry_Impl().get<ItemSlotComponent>(entity);
		stream.Serialize(component.Slots);

		if (stream.Is_Loading()) {
			component.Dirty = true;
			component.Suspended = false;
		}
	}

	// ------------------------------------------------------------------

	void Set_Drop_Callback(DropCallback callback)
	{
		g_DropCallback = callback;
	}

	void Compute_CRC(TechnoClass const * owner, CRCEngine & crc)
	{
		if (owner == nullptr || owner->ItemSlotEntity == entt::null) {
			return;
		}

		if (!Registry_Impl().valid(owner->ItemSlotEntity)) {
			return;
		}

		ItemSlotComponent const & component = Registry_Impl().get<ItemSlotComponent>(owner->ItemSlotEntity);

		// Every slot is folded in, including empty ones: slot *position*
		// matters here (item in slot 0 vs slot 1 must hash differently even
		// though Effective's multiplication doesn't care about order), and a
		// fixed number of crc() calls keeps every client's CRC sequence in
		// lockstep regardless of how many slots happen to be occupied.
		//
		// Deliberately NOT included: Dirty (pure cache-invalidation
		// bookkeeping, not game state -- two clients can legitimately differ
		// on whether AI_Pass already ran this tick without being desynced),
		// Suspended (redundant with TechnoClass::IsInLimbo, already covered
		// by ObjectClass::Compute_CRC up the BASECLASS chain), and Effective
		// (a derived cache recomputed deterministically from Slots -- CRCing
		// both the source data and its deterministic derivative adds no
		// detection power, only doubles the cost).
		for (ItemInstance const & slot : component.Slots) {
			slot.Compute_CRC(crc);
		}
	}
}
