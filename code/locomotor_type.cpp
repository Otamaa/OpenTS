/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "locomotor_type.h"

#include "loco.h"

// Concrete locomotor headers -- needed for Create_Locomotion. This is the
// point where this translation unit depends on Step 2 (each of these ten
// headers updated to derive from the new LocomotionClass and implement
// Get_Type()) having landed; nothing above this comment does.
#include "drive.h"
#include "hover.h"
#include "tunnel.h"
#include "walk.h"
#include "droppod.h"
#include "fly.h"
#include "teleport.h"
#include "mech.h"
#include "jumpjet.h"
#include "levitate.h"


namespace {

	// Real GUID values, unchanged from ilocos_i.c -- table only exists to
	// avoid repeating the same ten-way if/else in both conversion directions.
	struct LocomotorCLSIDEntry
	{
		LocomotorType Type;
		CLSID const & Id;
	};

	LocomotorCLSIDEntry const _LocomotorCLSIDTable[] =
	{
		{ LocomotorType::Drive,     CLSID_DriveLocomotion },
		{ LocomotorType::Hover,     CLSID_HoverLocomotion },
		{ LocomotorType::Tunnel,    CLSID_TunnelLocomotion },
		{ LocomotorType::Walk,      CLSID_WalkLocomotion },
		{ LocomotorType::Ballistic, CLSID_BallisticLocomotion },
		{ LocomotorType::Flyer,     CLSID_FlyerLocomotion },
		{ LocomotorType::Teleport,  CLSID_TeleportLocomotion },
		{ LocomotorType::Mech,      CLSID_MechLocomotion },
		{ LocomotorType::Jumpjet,   CLSID_JumpjetLocomotion },
		{ LocomotorType::Levitate,  CLSID_LevitateLocomotion },
	};

	static_assert((int)LocomotorType::COUNT == (sizeof(_LocomotorCLSIDTable) / sizeof(_LocomotorCLSIDTable[0])),
		"_LocomotorCLSIDTable needs an entry for every LocomotorType.");

}   // namespace


LocomotorType CLSID_To_LocomotorType(CLSID const & clsid, LocomotorType defvalue)
{
	for (LocomotorCLSIDEntry const & entry : _LocomotorCLSIDTable) {
		if (clsid == entry.Id) {
			return(entry.Type);
		}
	}
	return(defvalue);
}


CLSID const & LocomotorType_To_CLSID(LocomotorType type)
{
	for (LocomotorCLSIDEntry const & entry : _LocomotorCLSIDTable) {
		if (entry.Type == type) {
			return(entry.Id);
		}
	}
	// Deliberately falls through to Drive rather than returning a null/zero
	// CLSID -- a caller asking for the CLSID of a well-formed LocomotorType
	// enum value should never hit this; if it does, GUID_NULL would be a
	// worse failure mode (silently "valid-looking" empty CLSID) than a
	// clearly-wrong-but-legal one.
	return(CLSID_DriveLocomotion);
}


std::unique_ptr<LocomotionClass> Create_Locomotion(LocomotorType type)
{
	switch (type) {
		case LocomotorType::Drive:     return(std::make_unique<DriveLocomotionClass>());
		case LocomotorType::Hover:     return(std::make_unique<HoverLocomotionClass>());
		case LocomotorType::Tunnel:    return(std::make_unique<TunnelLocomotionClass>());
		case LocomotorType::Walk:      return(std::make_unique<WalkLocomotionClass>());
		case LocomotorType::Ballistic: return(std::make_unique<DropPodLocomotionClass>());
		case LocomotorType::Flyer:     return(std::make_unique<FlyLocomotionClass>());
		case LocomotorType::Teleport:  return(std::make_unique<TeleportLocomotionClass>());
		case LocomotorType::Mech:      return(std::make_unique<MechLocomotionClass>());
		case LocomotorType::Jumpjet:   return(std::make_unique<JumpjetLocomotionClass>());
		case LocomotorType::Levitate:  return(std::make_unique<LevitateLocomotionClass>());
		case LocomotorType::COUNT:     break;
	}
	return(nullptr);
}
