// license:BSD-3-Clause
// copyright-holders:Radovan Hrebicek
/****************************************************************************

    pce220_prn.h

    Sharp CE-126P thermal printer, for PC-E220 / PC-G850V's 11-pin bus

    This reproduces (does not port) the device-select handshake and
    print-data byte framing found in PockEmul's Cce126::run()/Printer()
    (src/machine/sharp/Ce126.cpp), re-derived using MAME idioms: the six
    wires are the same ones pce220_serial_device already uses on this same
    connector (busy/dout/xout driven by the host CPU, din/ack/xin driven
    back by the peripheral), but the protocol riding on top of them is
    CE-126P's own two-phase device-select-then-data-byte scheme rather than
    an async UART frame. See claude/printer-emulation-feasibility.md in the
    project for the full background and PockEmul source analysis this is
    built from.

    Deliberate simplifications versus PockEmul's Cce126, documented here so
    a later pass against real hardware knows where to look first:

    - 2026-09-11: data-phase selection was originally gated strictly on the
      received device-select byte matching this unit's own DEVICE_CODE
      (0x0f for CE-126P), on the theory that PockEmul's Cce126::run()
      treating any of 0x00/0xff/0x0f/0x20/0x21/0x45 as "selected" was
      leftover/shared-class behavior (that list spans every sibling product
      built on the shared Cce126 base class -- ce123p, ce129p, 26-3591) --
      not something a real, single CE-126P unit would do. A real-ROM
      capture proved this wrong: a PC-G850V's LPRINT routine sends 0x0f,
      then unconditionally restarts the whole select handshake and sends
      0x20, every single time, before ever touching the data phase -- and
      only continues past the second (0x20) attempt. Narrowing the match to
      0x0f alone meant this device correctly ACKed the first attempt but
      then reported "not selected" on the second, forcing the real ROM's
      cleanup path (it disables the interface via port 0x15 and returns to
      READY with no error and no data sent -- exactly the silent-abort
      symptom this was chasing). So the match is back to PockEmul's full
      six-code set after all: whatever the reason a real ROM exercises more
      than one of them per LPRINT, this device has to answer "selected" to
      all of them to work with that ROM, not just its own nominal code.
    - Each handshake state arms its own explicit delay timer sized to match
      PockEmul's constants (15ms settle, 2ms inter-bit gap, 500ms bit-wait
      timeout). PockEmul instead reuses one shared timer reference across
      several states in a way that means some of its timeouts are measured
      from an earlier state's entry rather than the current one. That
      nuance is not reproduced; each state's timeout here is measured from
      when that state was actually entered.
    - 2026-09-11: this went through two false starts before landing on the
      design below, both driven by real-ROM debugger captures rather than
      just re-reading PockEmul source, and worth recording so a future
      pass doesn't repeat either mistake:
        1. The very first version had a WAIT_BUSY_LOW/9ms-ACK_HOLD settle
           pair between a matched selection and the data phase (matching
           Cce126::run()'s steps 4/5), but never re-checked BUSY's actual
           level when the 9ms hold expired. A real PC-G850V ROM raises
           BUSY for the data phase's first bit within a handful of Z80
           instructions of the select byte's last edge -- nowhere near
           9ms -- so by the time the hold expired, BUSY had already been
           sitting high for the entire 9ms with no edge left to detect;
           the first data bit was silently dropped and the two sides
           desynced permanently (the ROM retried the whole handshake
           forever).
        2. The "fix" for that removed the settle pair entirely, going
           straight from a matched selection to the data-phase state. This
           avoided the dropped-edge/infinite-retry symptom, but broke ACK
           parity: Cce126::run()'s step 4/5 sequence sets ACK to DOWN then
           UP by *explicit assignment*, not by toggling, before the data
           phase's first real toggle -- skipping straight to the data
           phase's toggle-on-every-edge rule (as this version did) means
           ACK reads the wrong level on the very first data bit even
           though the *previous* edge (the select byte's own last falling
           edge) happened to read correctly by coincidence. A real-ROM
           capture confirmed exactly this: ACK correctly read 1 right
           after the select byte's last falling edge, then incorrectly
           read 0 right after the first data bit's rising edge, and the
           ROM aborted there.
      The settle pair (ST_SELECTED_WAIT_BUSY_LOW / ST_SELECTED_ACK_HOLD
      below) is restored, faithfully matching Cce126::run()'s steps 4/5
      (explicit ACK=DOWN on the matched 8th bit, wait for BUSY to actually
      fall, explicit ACK=UP, hold 9ms, explicit ACK=DOWN) -- but with the
      bug from attempt 1 fixed: when the 9ms hold expires, out_busy()'s
      *current* latched BUSY level is checked against what it was when the
      hold began (always 0 -- the hold is only ever entered right after
      BUSY has just fallen). If the host already raised BUSY again during
      the hold and is simply holding it there waiting for a response (as a
      real ROM does, since it doesn't wait out anything like our 9ms), that
      pending edge is processed immediately as the data phase's first
      rising edge -- toggling ACK and shifting in the first bit -- instead
      of being silently missed the way attempt 1 missed it. This makes the
      exact duration of the hold irrelevant to correctness (it only has to
      be long enough that real ROMs never observe it directly, which 9ms
      -- taken from PockEmul, so presumably calibrated against real
      hardware -- comfortably is); what matters is that no BUSY transition
      during the hold is ever dropped rather than merely delayed.
    - D_IN and MT_IN are tied permanently inactive. Reading Cce126::run()
      closely, the 11-pin connector's own D_IN/MT_IN pins are never driven
      by the printer role at all -- MT_IN there is wired only to Cce126's
      separate cassette/tape connector (a different physical jack, not
      part of the printer's 11-pin computer-bus signaling), and D_IN is
      simply never assigned outside of its constructor default. ACK is the
      only signal the CE-126P actually drives back to the host.
    - The FSM only produces output while the attached printer_image_device
      is loaded (is_ready()), so a CE-126P slot with nothing attached in
      MAME's Media Control never asserts ACK -- modelling "nothing plugged
      into the port" -- rather than always claiming to be present the way
      Cce126 unconditionally does in PockEmul (which has no such concept).

    Glyph rendering: this first cut targets imagedev/printer.h, a plain
    byte-to-file sink with no bitmap/paper rendering of its own. Two of
    Cce126::Printer()'s three cases translate directly: an ordinary data
    byte is forwarded as-is, and a literal 0x0d byte (not preceded by a
    control-code prefix) is forwarded as-is too, matching real thermal
    printer / plain-text-capture conventions where 0x0d already reads as a
    line break. The remaining case -- 0x0f/0x0e/0x03 as a "next byte is a
    control code" prefix, with 0x20 after one of those meaning paper feed
    -- has no ASCII equivalent to fall back on, so it is remapped to 0x0a
    for the plain byte-sink capture (a paper feed is, in every practical
    sense, a blank line advance). A future bespoke bitmap/glyph-table
    renderer (built on ce126ptable.bmp's layout, per the feasibility doc)
    would reproduce Cce126::RefreshCe126()'s actual pixel behavior instead.

****************************************************************************/

#ifndef MAME_SHARP_PCE220_PRN_H
#define MAME_SHARP_PCE220_PRN_H

#pragma once

#include "imagedev/printer.h"


// ======================> ce126p_printer_device

class ce126p_printer_device : public device_t
{
public:
	// construction/destruction
	ce126p_printer_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
	virtual ~ce126p_printer_device();

	// host (CPU) -> printer, same wire names/roles as pce220_serial_device
	// on this connector
	void out_busy(uint8_t state);
	void out_dout(uint8_t state);
	void out_xout(uint8_t state); // MT_OUT1

	// printer -> host (CPU)
	uint8_t in_din() const  { return 0; } // D_IN: never driven by the printer role, see header comment
	uint8_t in_ack() const  { return m_ack & 0x01; }
	uint8_t in_xin() const  { return 0; } // MT_IN: never driven by the printer role, see header comment

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;

	TIMER_CALLBACK_MEMBER(delay_tick);
	TIMER_CALLBACK_MEMBER(select_timeout_tick);

private:
	// Every device-select code a real PC-G850V ROM was observed treating as
	// "a CE-126P-compatible printer is here" -- see the header comment's
	// 2026-09-11 note for the real-ROM capture that overturned the earlier,
	// narrower (0x0f-only) assumption. This is PockEmul's own Cce126::run()
	// match list, shared across every product built on that base class.
	static bool is_recognized_device_code(uint8_t code)
	{
		switch (code)
		{
		case 0x00: case 0xff: case 0x0f: case 0x20: case 0x21: case 0x45:
			return true;
		default:
			return false;
		}
	}

	enum : uint8_t
	{
		ST_IDLE = 0,             // no active handshake, or (if m_selected) the data phase --
		                         // see out_busy()'s ST_IDLE case
		ST_SETTLE,               // MT_OUT1 raised, waiting 15ms before ACKing
		ST_SELECT_BIT_WAIT,      // waiting for a BUSY pulse to sample one select-byte bit
		ST_SELECT_BIT_GAP,       // 2ms gap between select-byte bits
		ST_MISMATCH_HOLD,        // device code didn't match, 2ms hold before giving up
		ST_SELECTED_WAIT_BUSY_LOW, // matched selection; waiting for BUSY to fall
		                           // before starting the post-selection ACK hold
		                           // (Cce126::run()'s step 4)
		ST_SELECTED_ACK_HOLD       // BUSY fell, ACK raised; 9ms hold before the data
		                           // phase proper begins (Cce126::run()'s step 5) --
		                           // see the header comment for the catch-up check
		                           // this state's expiry performs
	};

	void enter_state(uint8_t state);
	void shift_in_bit(uint8_t bit);
	void receive_byte(uint8_t data);

	required_device<printer_image_device> m_printer;

	emu_timer *m_delay_timer;         // single in-flight handshake-phase delay
	emu_timer *m_select_timeout_timer; // 500ms "gave up waiting for a bit" watchdog

	uint8_t m_state;

	// latched host-driven lines
	uint8_t m_busy;
	uint8_t m_dout;
	uint8_t m_xout; // MT_OUT1

	// device-driven line
	uint8_t m_ack;

	uint8_t m_shift;      // bit-shift accumulator, LSB-first
	uint8_t m_bit_count;  // bits accumulated so far (0-7)

	uint8_t m_device_code; // last device-select byte seen
	bool    m_selected;    // is_recognized_device_code(m_device_code)

	bool m_ctrl_pending; // previous data byte was a 0x0f/0x0e/0x03 control-code prefix
};

// device type definition
DECLARE_DEVICE_TYPE(CE126P_PRINTER, ce126p_printer_device)

#endif // MAME_SHARP_PCE220_PRN_H
