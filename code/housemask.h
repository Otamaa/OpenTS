/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * EA's GPLv3 Section 7 additional terms and supplemental warranty
 * disclaimers apply; see LICENSE.md.
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <cstdio>
#include "crc.h"
#include "house.hh"

/*
**	Maximum number of simultaneous house instances (players, computer sides, and special
**	houses such as Neutral/Mutant) that a HouseBitArray can address.
**
**	This ceiling used to be an implicit 32, coming from the historical idiom of storing
**	ally/cloak/sensor/occupation lists in a plain `int`/`unsigned` and indexing them with
**	`1 << HeapID`. Raise this constant to raise the ceiling -- the storage in
**	HouseBitArray grows to match and is no longer tied to any native integer width.
**
**	VERIFY: raising this changes the serialized size of every field of type
**	HouseBitArray, so save games and multiplayer sync CRCs are not compatible across a
**	change to this value. Treat it the same as any other save-breaking change.
*/
constexpr int MAX_HOUSES = 64;

/*
**	Drop-in replacement for the historical "one bit per house" idiom (`1 << house`)
**	used throughout the engine for ally lists, cloak/sensor/occupation lists, spied-on
**	lists, and similar per-house flags. Backed by a fixed array of 32-bit words rather
**	than std::bitset, so it stays a trivially copyable POD type and travels through the
**	existing SaveStream / CRCEngine machinery the same way the old scalar fields did.
**
**	Templated on bit capacity because two genuinely different things were historically
**	packed into the same `1 << x` idiom and confused with each other:
**		- one bit per house *instance* (HeapID) -- ally lists, cloak/sensor/occupation.
**		  Capacity: MAX_HOUSES. Alias: HouseBitArray.
**		- one bit per house *side/type* (Class->House, i.e. GOOD/BAD/NEUTRAL/MUTANT) --
**		  who is spying on a building/radar. Capacity: HOUSE_COUNT. Alias:
**		  HouseSideBitArray.
**	Keeping them as distinct types (rather than both being a bare `unsigned`) makes it a
**	compile error to accidentally mix the two bit spaces, which is exactly the mismatch
**	that made the historical `SpiedBy`/`RadarSpied` code hard to reason about.
**
**	Historical callers wrote things like:
**		Allies |= (1L << house->HeapID);
**		Allies &= ~(1L << house->HeapID);
**		if ((Allies & (1 << house)) != 0) { ... }
**		Allies = 0;
**		SpiedBy |= 1 << house->Class->House;
**		SpiedBy &= ~(1 << house->Class->House);
**	The equivalent calls are:
**		Allies.Set(house->HeapID);
**		Allies.Clear(house->HeapID);
**		if (Allies.Is_Set(house)) { ... }
**		Allies.Clear_All();
**		SpiedBy.Set(house->Class->House);
**		SpiedBy.Clear(house->Class->House);
*/
template<int NumHouses>
class HouseBitArrayT
{
	public:
		static constexpr int WORD_BITS  = 32;
		static constexpr int WORD_COUNT = (NumHouses + WORD_BITS - 1) / WORD_BITS;

		constexpr HouseBitArrayT(void) : Words{} {}

		/*
		**	Preserves the historical "Allies(0)"/"SpiedBy(0)"-style member initializer
		**	syntax used in every constructor that zero-inits one of these fields. Zero is
		**	the only value this is ever called with; anything else is a programming error.
		*/
		constexpr HouseBitArrayT(int zero) : Words{}
		{
			(void)zero;
		}

		HouseBitArrayT & operator=(int zero)
		{
			(void)zero;
			Clear_All();
			return *this;
		}

		void Set(int house)
		{
			if (house >= 0 && house < NumHouses) {
				Words[house / WORD_BITS] |= (uint32_t(1) << (house % WORD_BITS));
			}
		}

		void Clear(int house)
		{
			if (house >= 0 && house < NumHouses) {
				Words[house / WORD_BITS] &= ~(uint32_t(1) << (house % WORD_BITS));
			}
		}

		bool Is_Set(int house) const
		{
			if (house < 0 || house >= NumHouses) {
				return false;
			}
			return (Words[house / WORD_BITS] & (uint32_t(1) << (house % WORD_BITS))) != 0;
		}

		void Clear_All(void)
		{
			for (int i = 0; i < WORD_COUNT; i++) {
				Words[i] = 0;
			}
		}

		bool Any(void) const
		{
			for (int i = 0; i < WORD_COUNT; i++) {
				if (Words[i] != 0) {
					return true;
				}
			}
			return false;
		}

		explicit operator bool(void) const { return Any(); }

		/*
		**	Builds a single-house mask on the fly. Replaces the historical "(1 << house)"
		**	idiom used to construct a throwaway mask before OR-ing/AND-ing it in.
		*/
		static HouseBitArrayT Bit(int house)
		{
			HouseBitArrayT result;
			result.Set(house);
			return result;
		}

		HouseBitArrayT & operator|=(HouseBitArrayT const & rhs)
		{
			for (int i = 0; i < WORD_COUNT; i++) {
				Words[i] |= rhs.Words[i];
			}
			return *this;
		}

		HouseBitArrayT & operator&=(HouseBitArrayT const & rhs)
		{
			for (int i = 0; i < WORD_COUNT; i++) {
				Words[i] &= rhs.Words[i];
			}
			return *this;
		}

		HouseBitArrayT operator|(HouseBitArrayT const & rhs) const
		{
			HouseBitArrayT result(*this);
			result |= rhs;
			return result;
		}

		HouseBitArrayT operator&(HouseBitArrayT const & rhs) const
		{
			HouseBitArrayT result(*this);
			result &= rhs;
			return result;
		}

		HouseBitArrayT operator~(void) const
		{
			HouseBitArrayT result;
			for (int i = 0; i < WORD_COUNT; i++) {
				result.Words[i] = ~Words[i];
			}
			return result;
		}

		bool operator==(HouseBitArrayT const & rhs) const
		{
			for (int i = 0; i < WORD_COUNT; i++) {
				if (Words[i] != rhs.Words[i]) {
					return false;
				}
			}
			return true;
		}

		bool operator!=(HouseBitArrayT const & rhs) const { return !(*this == rhs); }

		/*
		**	Self-describing serialization: picked up automatically by SaveStream's
		**	HasSerializeMember path, so existing "stream.Serialize(Allies);" /
		**	"stream.Serialize(SpiedBy);" call sites need no change at all.
		*/
		template<typename S>
		void Serialize(S & stream)
		{
			stream.Serialize(Words);
		}

		/*
		**	Feeds the raw bit storage into the running multiplayer sync CRC. Replaces the
		**	historical "crc((int)LimpetType)"/"crc((int)SpiedBy)" casts, which silently
		**	truncated to the first 32 bits (harmless for the 4-bit side masks, but wrong
		**	for anything sized off MAX_HOUSES).
		*/
		void Add_To_CRC(CRCEngine & crc) const
		{
			crc(Words, sizeof(Words));
		}

		/*
		**	Writes every house's bit as hex, most-significant word first, into `buffer`.
		**	`buffer` must be at least Hex_Digits()+1 bytes. For debug/diagnostic display
		**	only; never truncates the bit range regardless of capacity.
		*/
		static constexpr int Hex_Digits(void) { return WORD_COUNT * 8; }

		void Format_Hex(char * buffer, int buffer_size) const
		{
			for (int i = 0; i < WORD_COUNT && buffer_size >= 9; i++) {
				std::snprintf(buffer, buffer_size, "%08X", Words[WORD_COUNT - 1 - i]);
				buffer += 8;
				buffer_size -= 8;
			}
		}

		/*
		**	Same idea as Format_Hex(), but sized to only the hex digits this capacity
		**	actually needs (rounded up to a whole nibble) instead of a full 8-digit word,
		**	so a small mask (e.g. HouseSideBitArray's HOUSE_COUNT=4 bits, one nibble)
		**	doesn't force every caller's debug layout to reserve 8 columns for it.
		**	Only meaningful for single-word capacities (NumHouses <= 32); anything wider
		**	should use Format_Hex() instead so no bits are silently left off-screen.
		*/
		static constexpr int Significant_Hex_Digits(void) { return (NumHouses + 3) / 4; }

		void Format_Hex_Compact(char * buffer, int buffer_size) const
		{
			std::snprintf(buffer, buffer_size, "%0*X", Significant_Hex_Digits(), Words[0]);
		}

	private:
		uint32_t Words[WORD_COUNT];
};

/*
**	One bit per house *instance* (HeapID). Used for ally lists and per-house
**	cloak/sensor/occupation lists. Capacity is MAX_HOUSES.
*/
using HouseBitArray = HouseBitArrayT<MAX_HOUSES>;

/*
**	One bit per house *side/type* (Class->House -- GOOD/BAD/NEUTRAL/MUTANT). Used for
**	who-is-spying-on-this lists (TechnoClass::SpiedBy, HouseClass::RadarSpied), which
**	are keyed by side rather than by house instance. Capacity is HOUSE_COUNT, so this
**	stays exactly as compact as the historical `unsigned`/`int` for as long as
**	HOUSE_COUNT stays at 4, but is no longer silently mixable with a HouseBitArray.
*/
using HouseSideBitArray = HouseBitArrayT<HOUSE_COUNT>;
