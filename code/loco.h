/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "coord.h"
#include "ilocos.h"           // Still pulls in HRESULT/boolean/STDMETHODCALLTYPE etc. via <comdef.h>,
                               // and the CLSID_* declarations locomotor_type.cpp needs. LocomotionClass
                               // itself no longer inherits ILocomotion/IPersistStream from here.
#include "layer.hh"           // LayerType, needed directly now that In_Which_Layer() is declared here
                               // rather than inherited transitively through iloco.h/ILocomotion.
#include "locomotor_type.h"

class FootClass;
class SaveStreamClass;

/*
 * Game object locomotion handler.
 *
 * Formerly `: public IPersistStream, public ILocomotion` (real COM, see
 * LOCOMOTION_COM_REPLACEMENT.md for why). Method signatures below are left
 * exactly as they were -- this pass only removes the COM identity/lifetime
 * shell (QueryInterface/AddRef/Release/CLSID), not the interface itself, so
 * nothing calling through a LocomotionClass* changes behavior.
 */
class LocomotionClass
{
	public:
		LocomotionClass(void);
		virtual ~LocomotionClass(void);

		LONG STDMETHODCALLTYPE IsDirty(void) {return(Dirty ? S_OK : S_FALSE);}
		virtual HRESULT STDMETHODCALLTYPE Load(IStream * stream);
		virtual HRESULT STDMETHODCALLTYPE Save(IStream * stream, BOOL cleardirty);
		virtual LONG STDMETHODCALLTYPE GetSizeMax(ULARGE_INTEGER *pcbSize);

		virtual HRESULT STDMETHODCALLTYPE Link_To_Object(void *object);
		virtual boolean STDMETHODCALLTYPE Is_Moving(void);
		virtual Coord STDMETHODCALLTYPE Destination(void);
		virtual Coord STDMETHODCALLTYPE Head_To_Coord(void);
		virtual MoveType STDMETHODCALLTYPE Can_Enter_Cell(Cell cell);
		virtual boolean STDMETHODCALLTYPE Is_To_Have_Shadow(void);
		virtual Matrix3D STDMETHODCALLTYPE Draw_Matrix(int *key);
		virtual Matrix3D STDMETHODCALLTYPE Shadow_Matrix(int *key);
		virtual Point2D STDMETHODCALLTYPE Draw_Point(void);
		virtual Point2D STDMETHODCALLTYPE Shadow_Point(void);
		virtual VisualType STDMETHODCALLTYPE Visual_Character(boolean flag);
		virtual int STDMETHODCALLTYPE Z_Adjust(void);
		virtual ZGradientType STDMETHODCALLTYPE Z_Gradient(void);
		virtual boolean STDMETHODCALLTYPE Process(void);
		virtual void STDMETHODCALLTYPE Move_To(Coord to);
		virtual void STDMETHODCALLTYPE Stop_Moving(void);
		virtual void STDMETHODCALLTYPE Do_Turn(DirType coord);
		virtual void STDMETHODCALLTYPE Unlimbo(void);
		virtual void STDMETHODCALLTYPE Tilt_Pitch_AI(void);
		virtual boolean STDMETHODCALLTYPE Power_On(void);
		virtual boolean STDMETHODCALLTYPE Power_Off(void);
		virtual boolean STDMETHODCALLTYPE Is_Powered(void);
		virtual boolean STDMETHODCALLTYPE Is_Ion_Sensitive(void);
		virtual boolean STDMETHODCALLTYPE Push(DirType dir);
		virtual boolean STDMETHODCALLTYPE Shove(DirType dir);
		virtual void STDMETHODCALLTYPE Force_Track(int track, Coord coord);

		/*
		 * What display layer is it located in. Pure virtual in the original ILocomotion
		 * (no base body) -- every one of the ten concrete locomotors provides its own.
		 * This was missed in the first pass (only the first ~100 lines of iloco.h were
		 * checked at the time); confirmed against the full interface now.
		 */
		virtual LayerType STDMETHODCALLTYPE In_Which_Layer(void) = 0;
		virtual void STDMETHODCALLTYPE Force_Immediate_Destination(Coord coord);
		virtual void STDMETHODCALLTYPE Force_New_Slope(int ramp);
		virtual boolean STDMETHODCALLTYPE Is_Moving_Now(void) {return(Is_Moving());}
		virtual int STDMETHODCALLTYPE Apparent_Speed(void);
		virtual int STDMETHODCALLTYPE Drawing_Code(void);
		virtual FireErrorType STDMETHODCALLTYPE Can_Fire(void);
		virtual int STDMETHODCALLTYPE Get_Status() {return(0);}
		virtual void STDMETHODCALLTYPE Acquire_Hunter_Seeker_Target(void) {}
		virtual boolean STDMETHODCALLTYPE Is_Surfacing() {return(false);}
		virtual void STDMETHODCALLTYPE Mark_All_Occupation_Bits(int mark) {}
		virtual boolean STDMETHODCALLTYPE Is_Moving_Here(Coord to) {return(false);}
		virtual boolean STDMETHODCALLTYPE Will_Jump_Tracks(void) {return(false);}
		virtual boolean STDMETHODCALLTYPE Is_Really_Moving_Now(void) {return(Is_Moving_Now());}
		virtual void STDMETHODCALLTYPE Stop_Movement_Animation(void) {}
		virtual void STDMETHODCALLTYPE Lock(void) {}
		virtual void STDMETHODCALLTYPE Unlock(void) {}
		virtual int STDMETHODCALLTYPE Get_Track_Number(void) {return(-1);}
		virtual int STDMETHODCALLTYPE Get_Track_Index(void) {return(-1);}
		virtual int STDMETHODCALLTYPE Get_Speed_Accum(void) {return(-1);}

		/*
		 * Identifies which concrete locomotor this is. Replaces
		 * GetClassID(CLSID*) -- every concrete *LocomotionClass must
		 * implement this (Step 2).
		 */
		virtual LocomotorType STDMETHODCALLTYPE Get_Type(void) const = 0;

		/*
		 * Lists this locomotor's members for the save game. An implementation serializes
		 * its base class first and then names every member it owns in the order the header
		 * declares them, so that the same description serves saving and loading.
		 * UNCHANGED -- this was never COM, it's your own SaveStreamClass mechanism.
		 */
		virtual void Serialize(SaveStreamClass & stream);

		/*
		 * Restores whatever the record could not carry. Load_Members calls this once the
		 * members are in place, so a base class fixup runs even when the load was entered
		 * through a derived class.
		 */
		virtual void Post_Load(void);

	protected:
		/*
		 * These carry the record a class describes through Serialize. A class calls these
		 * from its Load and Save; the record is the swizzle identity followed by whatever
		 * members the class names. UNCHANGED.
		 */
		HRESULT Save_Members(IStream * stream, BOOL cleardirty);
		HRESULT Load_Members(IStream * stream);

	protected:
		/*
		 * Pointer to the object this locomotor carries about. It is attached as the object
		 * is created, and every service the locomotor offers is performed through it.
		 */
		FootClass *LinkedTo;

		/*
		 * If this locomotor is able to move its object under its own means, then this flag
		 * will be true. It is cleared when an EM pulse or a loss of base power is to strand
		 * the object where it stands.
		 */
		bool IsPowered;

		/*
		 * If this locomotor has changed since it was last written out, then this flag will
		 * be true. It starts out set and is only cleared by a save that asks for it, so the
		 * persistence machinery never assumes a locomotor is already safely on disk.
		 */
		bool Dirty;

		// RefCount removed -- lifetime is now a plain std::unique_ptr<LocomotionClass>
		// owned by FootClass (and by whichever concrete locomotor is currently
		// piggybacking another one), not COM reference counting.
};
