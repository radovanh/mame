// license:GPL-2.0+
// copyright-holders:Peter Trauner
/*****************************************************************************
 *
 * includes/pc1360.h
 *
 * Pocket Computer 1360
 *
 * ==========================================================================
 *  WORK-IN-PROGRESS SKELETON DRIVER -- memory map/I-O now cross-checked
 *  against the user's own SharpPocketLib PC-1360 knowledgebase (gymfan.de,
 *  simon-lehmayr.de, aldweb.com, the official Sharp PC-1360 service manual,
 *  Saretz's and Berwanger's PC-1360 books, and direct byte-level inspection
 *  of the real ROM dump -- see chat for the full source list).
 * ==========================================================================
 * This driver was bootstrapped from the existing pc1350.cpp/pc1401.cpp/
 * pc1403.cpp drivers already in this file tree, which all share the same
 * SC61860 CPU core the real PC-1360 uses. The video/LCD rendering logic
 * (screen_update()/pc1360.cpp) was carried over from pc1350.cpp on the
 * strength of the confirmed fact that the PC-1360 uses the identical LCD
 * resolution/character layout as the PC-1350 -- and per the knowledgebase,
 * this turned out to be an exact structural match too: the PC-1360's real
 * graphics VRAM is five 512-byte column-groups at 0x2800/0x2a00/0x2c00/
 * 0x2e00/0x3000, each with row offsets 0/0x40/0x1e/0x5e from its column
 * base -- precisely the address arithmetic pc1350.cpp's screen_update()
 * (and this driver's copy of it) already used, just with a different base
 * address. Only the base address needed fixing (see pc1360_mem() in
 * pocketc.cpp), not the arithmetic.
 *
 * CONFIRMED from the knowledgebase (previously guesses in this driver):
 *   - Top-level memory map: 0x0000-0x1fff internal ROM; 0x2000-0x3fff
 *     display memory + I/O ports; 0x4000-0x7fff external ROM, one of 8
 *     16K banks at a time; 0x8000-0xffff RAM card (bank-switched if two
 *     cards fitted). This CONFIRMS the 0x4000-0x7fff banked-ROM window
 *     was placed correctly all along -- resolving the earlier open
 *     question about whether addresses 0x7c28/0x7d28 (hit by TSID/ANID/
 *     ORID in the boot ROM) were misplaced RAM. They are real per-bank
 *     ROM content, most likely a per-bank id/capability byte.
 *   - Graphics VRAM: 0x2800-0x31ff (2560 bytes). The display-segment
 *     status byte ("0 SHIFT 1 DEF 4 RUN 5 PRO 6 JAPAN 7 SML", read as
 *     m_reg[0x83c] in pc1360.cpp's screen_update()) lives at real address
 *     0x303c -- which now falls out automatically once the VRAM window's
 *     base is 0x2800 (0x2800+0x83c == 0x303c), an exact independent match.
 *   - ROM bank-select register: 0x3400, bits 0-2 select banks 0-7. Also
 *     confirmed mirrored in system RAM at the very same address (i.e. a
 *     plain read-back-what-was-written latch, which is exactly what
 *     bank_r()/bank_w() below already implement) -- so unlike before,
 *     this address is no longer a guess modelled on pc1403.cpp's ASIC
 *     register, it's the real, sourced register address.
 *   - 0x3800 = 11-pin STROBE (bit0 only); 0x3a00 = serial/11-pin control
 *     register; 0x3e00 = keyboard-strobe register (bits0-6 keyboard,
 *     bit7 export-bridge). All three modelled as simple read-back latches
 *     below (strobe_r/w, serial_r/w, keyboard_line_r/w) since the deeper
 *     per-bit hardware effects (beyond keyboard scanning) aren't needed
 *     for the CPU's own boot-time read-after-write checks to succeed.
 *   - RAM-card address windows by size (also confirmed): 4K =
 *     0xf000-0xffff, 8K = 0xe000-0xffff, 16K = 0xc000-0xffff, 32K =
 *     0x8000-0xffff. machine_start() below now uses exactly this table
 *     instead of the previous made-up tiered windows.
 *   - Addresses 0x0034/0x0038/0x003a/0x003e (sysport_r/sysport_w below)
 *     remain a separate, earlier finding from this driver's own direct ROM
 *     disassembly (not from the knowledgebase, which doesn't cover this
 *     narrow range) -- still a real read/write latch cluster, still of
 *     unknown deeper hardware meaning.
 *   - Keyboard matrix (in_a_r() in pc1360_m.cpp): Berwanger's PC-1360
 *     Systemhandbuch (in the user's local Pocket Computers/PC-1360 folder,
 *     "Die Tastaturmatrix", p.22-23) has the real matrix table, and it
 *     confirms this driver's key groupings/labels (already copied from
 *     pc1350's INPUT_PORTS as a placeholder) genuinely match, row for row --
 *     apparently not a coincidence, presumably a shared electrical
 *     convention across this whole Sharp pocket-computer family. It also
 *     gives the real 14-select-line structure (7 rows via PD/0x3e00, 4 more
 *     via PA's own output bits 0-3, PA rows 4-6 documented empty) and a
 *     clean worked example pinning CLS to PA output bit 1 / input bit 3 --
 *     both now implemented in in_a_r() (previously: only 6 of PD's 7 rows
 *     were read at all, and CLS was gated by the wrong PA bit).
 *
 * STILL UNVERIFIED / OPEN:
 *   - A handful of individual key labels in that same matrix table are
 *     still uncertain -- the table itself OCR'd very badly (grid-line
 *     noise throughout), so while the row/column *structure* above is
 *     solid (confirmed via clean surrounding prose, not the garbled table
 *     cells), a couple of individual symbols (e.g. exactly which KEY0 bit
 *     is the comma, the precise cursor-key bit order in KEY5) were inferred
 *     by matching the token pattern against this driver's pre-existing
 *     labels rather than read cleanly off the page. Worth re-checking
 *     against a cleaner scan/photo of that page if one turns up.
 *   - RAM-bank switching for a second RAM card (Port C bit 2, "BA") is not
 *     implemented -- this driver only models a single card via m_ram's
 *     selectable size, matching the common case.
 *   - The exact CPU clock (SC61860 clock copied from pc1350) is unverified
 *     for pc1360.
 *
 ****************************************************************************/

#ifndef MAME_SHARP_PC1360_H
#define MAME_SHARP_PC1360_H

#pragma once

#include "pocketc.h"
#include "machine/ram.h"

class pc1360_state : public pocketc_state
{
public:
	pc1360_state(const machine_config &mconfig, device_type type, const char *tag)
		: pocketc_state(mconfig, type, tag)
		, m_ram(*this, RAM_TAG)
		, m_keys(*this, "KEY%u", 0U)
	{
		std::fill(std::begin(m_reg), std::end(m_reg), 0);
	}

	void pc1360(machine_config &config);

protected:
	virtual void machine_start() override ATTR_COLD;

	// Auto-presses the Y key (KEY6, row 6) a short while after boot, so the
	// "confirm RAM clear" prompt the real ROM shows at every cold start (see
	// in_a_r() in pc1360_m.cpp) gets accepted without the user needing to
	// hold Y themselves. Empirically (headless testing, see pc1360_m.cpp)
	// Y must NOT already read as pressed in the first fraction of a second
	// after reset -- the ROM appears to require seeing a genuine
	// released->pressed transition, and ignores Y forever if it looks stuck
	// down from the very first read (sensible real-keyboard-debounce
	// behaviour, and it also matches how a human naturally presses Y only
	// after noticing the prompt, not before). So this is done in two timed
	// phases, both driven by the same m_fakey_timer/fakey_timer_tick():
	// phase 0 leaves m_fakey (and so the emulated Y line) released for an
	// initial delay, then flips it to pressed for a further window, then
	// releases it again so Y behaves like a normal key for the rest of the
	// session.
	TIMER_CALLBACK_MEMBER(fakey_timer_tick);
	emu_timer *m_fakey_timer = nullptr;
	bool m_fakey = false;
	u8 m_fakey_phase = 0;

	uint32_t screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);

	void pc1360_mem(address_map &map) ATTR_COLD;

	void out_b_w(uint8_t data);
	void out_c_w(uint8_t data);

	uint8_t in_a_r();
	uint8_t in_b_r();
	uint8_t lcd_read(offs_t offset);
	void lcd_write(offs_t offset, uint8_t data);

	// CONFIRMED (knowledgebase): keyboard-strobe register at 0x3e00.
	// keyboard_line_r() feeds in_a_r()'s PC-1350-derived (still placeholder)
	// key-matrix scan logic.
	uint8_t keyboard_line_r(offs_t offset = 0);
	void keyboard_line_w(offs_t offset, uint8_t data);

	// CONFIRMED (knowledgebase): ROM bank-select register at 0x3400,
	// bits 0-2 select one of 8 external ROM banks; real hardware mirrors
	// the selected bank back at the same address, matching this simple
	// latch implementation exactly.
	uint8_t bank_r(offs_t offset);
	void bank_w(offs_t offset, uint8_t data);

	// CONFIRMED (via ROM disassembly) real I/O latches at 0x0030-0x003f
	// (only 0x34/0x38/0x3a/0x3e are actually touched by the boot ROM);
	// modelled as a simple read-back-what-was-written latch since the
	// real per-bit hardware effect is still unknown. NOTE: unrelated to
	// the 0x3800/0x3a00/0x3e00 registers below despite similar names --
	// this is a separate, narrower cluster inside the internal-ROM range.
	uint8_t sysport_r(offs_t offset);
	void sysport_w(offs_t offset, uint8_t data);

	// CONFIRMED (knowledgebase) addresses, modelled as simple latches:
	// 0x3800 = 11-pin STROBE (bit0 only; other bits unused/unknown),
	// 0x3a00 = serial/11-pin control register (IO1/IO2/Dout/Din/ACK-out,
	// SIO ER/RR/RS bits).
	uint8_t strobe_r(offs_t offset);
	void strobe_w(offs_t offset, uint8_t data);
	uint8_t serial_r(offs_t offset);
	void serial_w(offs_t offset, uint8_t data);

private:
	required_device<ram_device> m_ram;
	// NOTE: sized to 12 to match the keyboard-scan logic carried over
	// verbatim from pc1350_state::in_a_r() (see pc1360_m.cpp) -- but the
	// PC-1360's actual physical keyboard has more keys than the PC-1350's,
	// so this almost certainly needs to grow once the real matrix is known.
	required_ioport_array<12> m_keys; // FIXME: array size/matrix layout unverified

	uint8_t m_reg[0x1000]{};
	uint8_t m_bank = 0;
	uint8_t m_sysport[0x10]{};
	uint8_t m_strobe = 0;
	uint8_t m_serial = 0;
	uint8_t m_kb_strobe = 0;

	static const char* const s_def[5];
	static const char* const s_shift[5];
	static const char* const s_run[5];
	static const char* const s_pro[5];
	static const char* const s_japan[5];
	static const char* const s_sml[5];
};

#endif // MAME_SHARP_PC1360_H
