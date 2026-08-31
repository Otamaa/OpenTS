/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "ilocos.h"   // CLSID_DriveLocomotion etc. -- real GUID values, unchanged, kept for INI compat.

#include <memory>

class LocomotionClass;


/*
 * Locomotor identity. Replaces CLSID comparisons throughout the engine for
 * everything except the one place a real CLSID is still load-bearing: INI
 * text (Locomotor=<GUID> in rules.ini/rulesmd.ini), which is read and
 * converted at the TechnoTypeClass::Locomotor boundary -- see
 * CLSID_To_LocomotorType() below.
 */
enum class LocomotorType : uint8_t
{
	Drive,
	Hover,
	Tunnel,
	Walk,
	Ballistic,
	Flyer,
	Teleport,
	Mech,
	Jumpjet,
	Levitate,

	COUNT
};


/*
 * Converts a CLSID read from INI (INIClass::Get_CLSID, which still parses the
 * literal {xxxxxxxx-xxxx-...} text via CLSIDFromString exactly as before --
 * unchanged) into the corresponding LocomotorType.
 *
 * defvalue is returned if clsid doesn't match any known locomotor CLSID
 * (mirrors Get_CLSID's own default-value contract, so a malformed or missing
 * Locomotor= entry degrades the same way it always has).
 */
LocomotorType CLSID_To_LocomotorType(CLSID const & clsid, LocomotorType defvalue = LocomotorType::Drive);

/*
 * Reverse direction -- for anything that still needs to write a CLSID back
 * out (e.g. INIClass::Put_CLSID-based tooling/editors). Not currently called
 * anywhere in the engine (grepped -- Put_CLSID has no locomotor call site
 * today), included for symmetry and so it's there if a tool needs it.
 */
CLSID const & LocomotorType_To_CLSID(LocomotorType type);


/*
 * Constructs a fresh locomotor of the given type. Replaces
 * CoCreateInstance-by-CLSID / ILocomotionPtr(CLSID_X) construction
 * throughout the engine.
 *
 * NOTE: this only compiles once the ten concrete *LocomotionClass headers
 * are updated to derive from the new (non-COM) LocomotionClass and implement
 * Get_Type() -- that is the next, separate pass. See
 * LOCOMOTION_COM_REPLACEMENT.md / LOCOMOTION_STEP2_CHECKLIST.md for the
 * itemized per-file changes; deliberately not bundled into this file so the
 * identity/factory layer and the ten mechanical per-class edits stay
 * reviewable as separate diffs.
 */
std::unique_ptr<LocomotionClass> Create_Locomotion(LocomotorType type);
