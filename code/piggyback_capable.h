/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "locomotor_type.h"

#include <memory>

class LocomotionClass;

/*
 * Replaces the IPiggyback COM interface (ipiggy.h). A locomotor that can
 * carry another locomotor piggyback-style (DriveLocomotionClass,
 * WalkLocomotionClass, DropPodLocomotionClass -- confirmed by grep, no
 * others implement this) derives from this in addition to LocomotionClass.
 *
 * Every real call site asking for this capability already knows the
 * concrete type statically (e.g. FootClass::Link_DropPod always constructs
 * a DropPodLocomotionClass right before asking it to piggyback), so
 * static_cast<PiggybackCapable*> replaces QueryInterface(IID_IPiggyback, ...)
 * directly -- no dynamic_cast/RTTI needed for the cases that exist today.
 */
class PiggybackCapable
{
	public:
		virtual ~PiggybackCapable(void) = default;

		/*
		 * Piggybacks a locomotor onto this one. Takes ownership of inner.
		 */
		virtual HRESULT STDMETHODCALLTYPE Begin_Piggyback(std::unique_ptr<LocomotionClass> inner) = 0;

		/*
		 * End piggyback process and hand back the restored locomotor.
		 */
		virtual HRESULT STDMETHODCALLTYPE End_Piggyback(std::unique_ptr<LocomotionClass> & pointer) = 0;

		/*
		 * Is it ok to end the piggyback process?
		 */
		virtual boolean STDMETHODCALLTYPE Is_Ok_To_End(void) = 0;

		/*
		 * Fetches the piggybacked locomotor's type. Replaces Piggyback_CLSID(GUID*).
		 */
		virtual LocomotorType STDMETHODCALLTYPE Piggyback_Type(void) = 0;

		/*
		 * Is it currently piggybacking another locomotor?
		 */
		virtual boolean STDMETHODCALLTYPE Is_Piggybacking(void) = 0;
};
