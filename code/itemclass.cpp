/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "itemclass.h"

#include "globals.h"
#include "inline.h"
#include "isun.h"
#include "crc.h"
#include "rtti.hh"
#include "savestream.h"
#include "techno.h"
#include "tracker.h"
#include "vector.h"

DynamicVectorClass<ItemClass *> DroppedItems;

// ------------------------------------------------------------------
// Construction / destruction
// ------------------------------------------------------------------

ItemClass::ItemClass(ItemSlot::ItemInstance const & item, Coord const & coord, int despawnFrames) :
	BASECLASS(),
	Payload(item),
	DespawnTimer(despawnFrames)
{
	Create_ID();
	DroppedItems.Add(this);

	if (coord != COORD_NONE) {
		if (!Unlimbo(coord)) {
			// Could not place (e.g. no valid cell under `coord`) -- Delete_Me
			// handles Detach_This_From_All + Limbo + deferred deletion, same
			// as SmudgeClass does for a failed placement.
			Delete_Me();
		}
	}
}

ItemClass::ItemClass(void) :
	BASECLASS(),
	Payload(),
	DespawnTimer(-1)
{
	// Reconstruction path for Load_All. Position and Payload arrive via the
	// Serialize call that follows construction; this object is intentionally
	// left in limbo (not Unlimbo'd) until Load_All calls Unlimbo explicitly
	// with the loaded coordinate -- see Load_All below.
	Create_ID();
	DroppedItems.Add(this);
}

ItemClass::~ItemClass(void)
{
	// Detach_This_From_All / Limbo already ran as part of Delete_Me() by the
	// time the deferred-deletion pass actually destroys this object; the
	// destructor only needs to drop out of our own tracking list, the same
	// shape as TechnoClass::~TechnoClass's Technos.Delete(this).
	DroppedItems.Delete(this);
}

// ------------------------------------------------------------------
// COM identity / RTTI / persistence plumbing
// ------------------------------------------------------------------

HRESULT STDMETHODCALLTYPE ItemClass::GetClassID(CLSID * retval)
{
	if (retval == NULL) {
		return(E_POINTER);
	}
	*retval = CLSID_ItemClass;
	return(S_OK);
}

RTTIType ItemClass::Fetch_RTTI(void) const
{
	return(RTTI_ITEM);
}

void ItemClass::Serialize(SaveStreamClass & stream)
{
	BASECLASS::Serialize(stream);

	stream.Serialize(Payload);
	stream.Serialize(DespawnTimer);
}

void ItemClass::Compute_CRC(CRCEngine & crc) const
{
	BASECLASS::Compute_CRC(crc);

	Payload.Compute_CRC(crc);
	crc(DespawnTimer);
}

// ------------------------------------------------------------------
// Self-driven update (see design note in itemclass.h)
// ------------------------------------------------------------------

void ItemClass::Update_All(void)
{
	// Snapshot the count once: DespawnTimer expiring calls Delete_Me(), which
	// defers actual destruction (see ~ItemClass), so DroppedItems.Count()
	// cannot shrink out from under this loop mid-pass the way it could if
	// items were destroyed immediately.
	int const count = DroppedItems.Count();
	for (int index = 0; index < count; index++) {
		ItemClass * item = DroppedItems[index];
		if (item == NULL || !item->IsActive) {
			// Already Delete_Me()'d (this frame or an earlier one) and just
			// waiting for Process_Deferred_Deletion to finish it off.
			continue;
		}

		if (item->DespawnTimer > 0) {
			item->DespawnTimer--;
			if (item->DespawnTimer == 0) {
				item->Delete_Me();
				continue;
			}
		}

		Try_Pickup(item);
	}
}

void ItemClass::Try_Pickup(ItemClass * item)
{
	if (item->Payload.Is_Empty()) {
		// Already handed off (or was never populated); nothing to give away.
		// Left in place rather than despawned early -- DespawnTimer (if any)
		// still governs when it actually disappears.
		return;
	}

	Coord const coord = item->Center_Coord();

	// VERIFY: no distance-sort / "closest first" pass -- this hands the item
	// to the first Techno within range in Technos' storage order, not
	// necessarily the nearest one. Fine for a prototype; revisit if pickup
	// priority ever matters (e.g. player units before AI units).
	for (int index = 0; index < Technos.Count(); index++) {
		TechnoClass * techno = Technos[index];
		if (techno == NULL || !techno->IsActive || techno->IsInLimbo) {
			continue;
		}

		auto coordT= techno->Center_Coord();
		
		if (item->Distance(coordT) > PickupRadius) {
			continue;
		}

		if (ItemSlot::Equip_First_Empty(techno, item->Payload.TypeID,
			item->Payload.Multiplier, item->Payload.DropOnDeath)) {
			item->Delete_Me();
			return;
		}

		// This Techno was in range but full; keep scanning in case another
		// nearby Techno has an open slot.
	}
}

// ------------------------------------------------------------------
// Fail-safe full sweep (mirrors ItemSlot::Clear_All), called from
// Clear_Scenario alongside it -- see itemslot_integration.md section 5.
// ------------------------------------------------------------------

void ItemClass::Clear_All(void)
{
	// Delete_Scenario/Clear_Scenario already tears down every ObjectClass in
	// the generic Objects list (see scenario.cpp's while(Objects.Count())
	// loop), which includes every still-active ItemClass -- that deletion
	// path runs ~ItemClass, which prunes DroppedItems itself. This sweep only
	// needs to catch anything left dangling because it was never Unlimbo'd
	// (and therefore never entered the generic Objects list to begin with),
	// which can happen for a freshly-constructed, not-yet-placed ItemClass.
	while (DroppedItems.Count()) {
		delete DroppedItems[0];
	}
}

// ------------------------------------------------------------------
// Explicit persistence, VeinholeMonsterClass-style (see design note in
// itemclass.h for why this bypasses the generic per-object CLSID factory).
// Wire the calls into saveload.cpp per itemslot_integration.md section 7.
// ------------------------------------------------------------------

bool ItemClass::Save_All(IStream * stream)
{
	int itemCount = DroppedItems.Count();
	if (FAILED(stream->Write(&itemCount, sizeof(itemCount), NULL))) {
		return(false);
	}

	for (int index = 0; index < itemCount; index++) {
		ItemClass * item = DroppedItems[index];

		Coord coord = item->Center_Coord();
		if (FAILED(stream->Write(&coord, sizeof(coord), NULL))) {
			return(false);
		}

		SaveStreamClass savestream(stream, SaveStreamClass::MODE_SAVE);
		savestream.Set_Context(typeid(*item).name(), (uintptr_t)item);
		item->Serialize(savestream);
		if (FAILED(savestream.Result())) {
			return(false);
		}
	}

	return(true);
}

bool ItemClass::Load_All(IStream * stream)
{
	Clear_All();

	int itemCount;
	if (FAILED(stream->Read(&itemCount, sizeof(itemCount), NULL))) {
		return(false);
	}

	for (int index = 0; index < itemCount; index++) {
		Coord coord;
		if (FAILED(stream->Read(&coord, sizeof(coord), NULL))) {
			return(false);
		}

		// The default constructor tracks the object (DroppedItems.Add) but
		// does not Unlimbo it -- Serialize below restores Payload/DespawnTimer,
		// then Unlimbo places it at the coordinate read above. This mirrors
		// how TechnoClass-derived objects are reconstructed during load:
		// construct first, populate via Serialize, place second.
		ItemClass * item = new ItemClass();

		SaveStreamClass savestream(stream, SaveStreamClass::MODE_LOAD);
		savestream.Set_Context(typeid(*item).name(), (uintptr_t)item);
		item->Serialize(savestream);
		if (FAILED(savestream.Result())) {
			return(false);
		}

		if (!item->Unlimbo(coord)) {
			item->Delete_Me();
			return(false);
		}
	}

	return(true);
}

// ------------------------------------------------------------------
// Wiring back to ItemSlot (see itemslot.h's DropCallback / Set_Drop_Callback)
// ------------------------------------------------------------------

void ItemClass::Spawn_From_Drop(TechnoClass * owner, ItemSlot::ItemInstance const & item, Coord const & coord)
{
	(void)owner;

	// despawnFrames = -1 (never despawns) -- VERIFY per the header note;
	// a persistent, un-owned item pile is easy to forget to bound later.
	new ItemClass(item, coord);
}

void ItemClass::Install_As_Drop_Callback(void)
{
	ItemSlot::Set_Drop_Callback(&ItemClass::Spawn_From_Drop);
}
