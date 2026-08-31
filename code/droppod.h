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

#pragma once

#include "loco.h"
#include "piggyback_capable.h"


class DropPodLocomotionClass : public LocomotionClass, public PiggybackCapable
{
		typedef LocomotionClass BASECLASS;

	public:

		/*---------------------------------------------------------------------
		**	Constructors, Destructors, and overloaded operators.
		*/
		DropPodLocomotionClass(void);
		virtual ~DropPodLocomotionClass(void) override;

		virtual LocomotorType STDMETHODCALLTYPE Get_Type(void) const override;

		virtual void Serialize(SaveStreamClass & stream) override;

		virtual boolean STDMETHODCALLTYPE Is_Moving(void) override;
		virtual Coord STDMETHODCALLTYPE Destination(void) override;
		virtual void STDMETHODCALLTYPE Move_To(Coord to) override;
		virtual void STDMETHODCALLTYPE Stop_Moving(void) override;
		virtual boolean STDMETHODCALLTYPE Process(void) override;
		virtual LayerType STDMETHODCALLTYPE In_Which_Layer(void) override;
		virtual int STDMETHODCALLTYPE Drawing_Code(void) override;

		virtual HRESULT STDMETHODCALLTYPE Begin_Piggyback(std::unique_ptr<LocomotionClass> inner) override;
		virtual HRESULT STDMETHODCALLTYPE End_Piggyback(std::unique_ptr<LocomotionClass> & pointer) override;
		virtual boolean STDMETHODCALLTYPE Is_Ok_To_End(void) override;
		virtual LocomotorType STDMETHODCALLTYPE Piggyback_Type(void) override;
		virtual boolean STDMETHODCALLTYPE Is_Piggybacking(void) override {return(Piggybacker != NULL);}

	private:
		enum DropPodDirType {
			DPOD_DIR_NE,
			DPOD_DIR_NW,
			DPOD_DIR_SE,
			DPOD_DIR_SW,
		};

		/*
		 * This is the compass direction the pod falls in, chosen so that its approach
		 * begins on screen wherever possible. It decides which way the pod slides as it
		 * descends, the artwork it is drawn with, and the wreckage it leaves on landing.
		 */
		int Direction;

		/*
		 * This is the ground coordinate the pod is falling toward, and the point its
		 * covering fire is aimed at on the way down. It is COORD_NONE until the pod is
		 * given somewhere to land, after which any later request is ignored.
		 */
		Coord DestinationCoord;

		/*
		 * This is the locomotor set aside while the drop pod carries the object down. It is
		 * handed back the moment the pod touches ground, so that the object resumes moving
		 * the way its type normally does.
		 */
		std::unique_ptr<LocomotionClass> Piggybacker;
};
