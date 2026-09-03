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

#include "point.h"
#include "sun.h"

class Coord;
class Cell;

Coord Coord_Scatter(Coord const & coord, int distance, bool lock=false);
Coord Adjacent_Coord_With_Height(Coord const & coord, FacingType dir);


/****************************************************************************
**	These are custom C&C specific types. The CELL is used for map coordinate
**	with cell resolution. The COORDINATE type is used for map coordinates that
**	have a lepton resolution. CELL is more efficient when indexing into the map
**	and when size is critical. COORDINATE is more efficient when dealing with
**	accuracy and object movement.
*/

typedef int LEPTON;

class Cell : public TPoint2D<short>
{
		typedef TPoint2D<short> BASECLASS;

	public:
		Cell(void) : BASECLASS() {}
		Cell(Coord const & cell);
		Cell(int x, int y) : BASECLASS(x, y) {}
		explicit Cell(TPoint2D<int> const & pt) : BASECLASS(pt.X, pt.Y) {}
		Cell(BASECLASS const & pt) : BASECLASS(pt) {};
		// SUSPECT: hardcoded to a 128-wide grid (a Red Alert/TD legacy convention)
		// rather than MAP_CELL_W, and unrelated to any of the CellPack schemes below.
		// No call site in this codebase passes a raw int here as of this writing --
		// left as-is rather than guessed at, since "fixing" dead code risks masking
		// its real intended semantics if a caller does turn up later.
		explicit Cell(int cellnum) : BASECLASS(cellnum % 128, cellnum / 128) {}

		Coord As_Coord(int z = 0) const;

		int As_Int(void) const { return((Y - (MAP_CELL_W * (X + Y)) - X) << 6); }
};


/****************************************************************************
** A cell's X/Y is stored, transmitted, or saved as a single packed integer
** in several unrelated places (xTargetClass::ID, CellTags, Waypoints,
** terrain/team-mission cell references...). Historically each call site
** re-derived its own "% base" / "/ base" arithmetic inline, which is how a
** single hardcoded base ends up capping the whole engine's map size in
** several places at once. Centralizing every scheme here means there is
** exactly one place to look, and one place to touch if a scheme ever needs
** to change again.
*/
namespace CellPack
{
	// Legacy (NewINIFormat < 4) 128-wide packed cell number, inherited from
	// Red Alert / Tiberian Dawn map files. This describes the OLD FILE
	// FORMAT, not the current map, so it must never change regardless of
	// MAP_CELL_W/MAP_CELL_H -- it exists purely so old maps/saves still load.
	inline Cell From_Legacy128(int value) { return Cell(value % 128, value / 128); }
	inline int To_Legacy128(Cell const & cell) { return cell.Y * 128 + cell.X; }

	// NewINIFormat == 4 packed cell number (decimal, base 1000). Caps X at
	// 999, so this format cannot address map sizes beyond that -- kept only
	// so files already saved in this format keep loading correctly.
	inline Cell From_Decimal1000(int value) { return Cell(value % 1000, value / 1000); }
	inline int To_Decimal1000(Cell const & cell) { return cell.Y * 1000 + cell.X; }

	// NewINIFormat >= 5 packed cell number (decimal, base 10000). Supports X
	// up to 9999 -- comfortably above MAP_CELL_W on both the 32- and 64-bit
	// builds (see sun.h). This is the CURRENT format for human-readable INI
	// fields (CellTags, Waypoints, terrain placement, team-mission cell
	// targets).
	inline Cell From_Decimal10000(int value) { return Cell(value % 10000, value / 10000); }
	inline int To_Decimal10000(Cell const & cell) { return cell.Y * 10000 + cell.X; }

	// Version-aware decode for INI text fields -- picks the scheme that
	// matches whatever NewINIFormat the file declared. Writing always uses
	// the current (newest) scheme; there is no "To_INI" version switch.
	inline Cell From_INI(int value, int ini_format)
	{
		if (ini_format >= 5) return From_Decimal10000(value);
		if (ini_format >= 4) return From_Decimal1000(value);
		return From_Legacy128(value);
	}
	inline int To_INI(Cell const & cell) { return To_Decimal10000(cell); }

	// Bit-packed cell number used by xTargetClass::ID for RTTI_CELL targets
	// (target.cpp). 12 bits per axis (values 0-4095) fits exactly in the 24
	// low bits of the packed TARGET int, alongside RTTI in the top byte
	// (xTargetClass::Encode/Decode, target.h). This -- not the INI format --
	// is the hard ceiling on MAP_CELL_W/MAP_CELL_H; see the comment there.
	// This scheme is purely a runtime/in-memory and network-wire convention,
	// not a persisted file format, so it is not NewINIFormat-gated.
	constexpr int TARGET_CELL_BITS = 12;
	constexpr int TARGET_CELL_MAX  = (1 << TARGET_CELL_BITS) - 1; // 4095
	inline int To_Target_ID(Cell const & cell) { return (cell.X & TARGET_CELL_MAX) | ((cell.Y & TARGET_CELL_MAX) << TARGET_CELL_BITS); }
	inline Cell From_Target_ID(int id) { return Cell(id & TARGET_CELL_MAX, (id >> TARGET_CELL_BITS) & TARGET_CELL_MAX); }
}

static_assert(MAP_CELL_W - 1 <= CellPack::TARGET_CELL_MAX, "MAP_CELL_W exceeds what xTargetClass can pack into 12 bits -- see CellPack::TARGET_CELL_BITS in coord.h");
static_assert(MAP_CELL_H - 1 <= CellPack::TARGET_CELL_MAX, "MAP_CELL_H exceeds what xTargetClass can pack into 12 bits -- see CellPack::TARGET_CELL_BITS in coord.h");


class Coord : public Point3D
{
		typedef TPoint3D<int> BASECLASS;

	public:
		Coord(void) : BASECLASS() {}
		Coord(Point2D const & pt, LEPTON z) {X=pt.X; Y=pt.Y; Z=z;}
		Coord(Cell const & cell, LEPTON z = 0);
		Coord(int x, int y, int z = 0) : BASECLASS(x, y, z) {}
		Coord(BASECLASS const & pt) : BASECLASS(pt) {};

		Cell As_Cell(void) const;

		int As_Int(void) { return((X / 10) + ((Y / 10) << 16)); }
};


inline Cell::Cell(Coord const & coord)
{
	*this = coord.As_Cell();
}


inline Coord Cell::As_Coord(int z) const
{
	Coord coord(X * CELL_LEPTON_W + CELL_LEPTON_W / 2, Y * CELL_LEPTON_H + CELL_LEPTON_H / 2, z);
	return(coord);
}


inline Coord::Coord(Cell const & cell, LEPTON z) :
	Point3D((cell.X * CELL_LEPTON_W) + (CELL_LEPTON_W / 2), (cell.Y * CELL_LEPTON_H) + (CELL_LEPTON_H / 2), z)
{

}


inline Cell Coord::As_Cell(void) const
{
	return(Cell(X / CELL_LEPTON_W, Y / CELL_LEPTON_H));
}


/*
 * These are types used by EventClass/TargetClass.
 */
struct xCell
{
	/*
	 * These are the column and row of the cell on the map. They mirror the like named
	 * members of Cell, which cannot serve here itself because a member of a union may not
	 * have a constructor.
	 */
	short X;
	short Y;

	xCell & operator = (Cell const & that)
	{
		X = that.X;
		Y = that.Y;
		return(*this);
	}
};


struct xCoord
{
	/*
	 * These are the horizontal and vertical position on the map, expressed in leptons. They
	 * mirror the like named members of Coord, but there is no Z here -- a position sent
	 * through an event keeps no height, and comes back out of one at ground level.
	 */
	LEPTON X;
	LEPTON Y;

	xCoord & operator = (Coord const & that)
	{
		X = that.X;
		Y = that.Y;
		return(*this);
	}
};
