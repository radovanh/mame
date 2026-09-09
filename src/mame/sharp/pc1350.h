// license:GPL-2.0+
// copyright-holders:Peter Trauner
/*****************************************************************************
 *
 * includes/pc1350.h
 *
 * Pocket Computer 1350
 *
 ****************************************************************************/

#ifndef MAME_SHARP_PC1350_H
#define MAME_SHARP_PC1350_H

#include "pocketc.h"
#include "machine/ram.h"

#include <system_error>
#include <utility>

class snapshot_image_device;

class pc1350_state : public pocketc_state
{
public:
	pc1350_state(const machine_config &mconfig, device_type type, const char *tag)
		: pocketc_state(mconfig, type, tag)
		, m_ram(*this, RAM_TAG)
		, m_keys(*this, "KEY%u", 0U)
	{
		std::fill(std::begin(m_reg), std::end(m_reg), 0);
	}

	void pc1350(machine_config &config);

	// Shift-chord fix for clipboard paste (see the "SHIFT (paste)" phantom
	// field on the EXTRA port and the KEY0 0x40 SHIFT field's comment, both
	// in pocketc.cpp): the phantom field's PORT_CHANGED_MEMBER callback
	// pulses the real SHIFT bit via a one-shot timer instead of holding it
	// down for the whole chord, reproducing the real hardware's
	// tap-then-release SHIFT behavior for pasted shifted characters (same
	// mechanism as pc1360_state, confirmed by the user to apply here too).
	// Public because the ioport configuration code that wires it up via
	// FUNC(...) in pocketc.cpp is not a member of this class. Implemented
	// in pc1350_m.cpp.
	DECLARE_INPUT_CHANGED_MEMBER(shift_chord_changed);

	// QUICKLOAD callback for the "quikload" file-manager slot added in
	// pocketc.cpp's pc1350() -- tokenizes a plain-text .BAS file (native
	// C++ port of POCKTOOL's bas2img, see pocketc_bas.h/.cpp) and injects
	// it directly into the program area, bypassing serial-protocol
	// emulation entirely. See claude/pc1360-basic-tokenizer-format.md in
	// the project for the format/pointer background this is built on.
	// Public for the same reason shift_chord_changed() is: the device
	// config code that binds it via FUNC(...) in pocketc.cpp isn't a
	// member of this class.
	std::pair<std::error_condition, std::string> quickload_cb(snapshot_image_device &image);

protected:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

	// One-shot completion callback for shift_chord_changed() above.
	TIMER_CALLBACK_MEMBER(release_shift_pulse);

	uint32_t screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);

	void pc1350_mem(address_map &map) ATTR_COLD;

	void out_b_w(uint8_t data);
	void out_c_w(uint8_t data);

	uint8_t in_a_r();
	uint8_t in_b_r();
	uint8_t lcd_read(offs_t offset);
	void lcd_write(offs_t offset, uint8_t data);
	uint8_t keyboard_line_r();

private:
	required_device<ram_device> m_ram;
	required_ioport_array<12> m_keys;

	uint8_t m_reg[0x1000]{};

	// One-shot timer backing shift_chord_changed()/release_shift_pulse()
	// above -- allocated in machine_start() (pc1350_m.cpp).
	emu_timer *m_shift_pulse_timer = nullptr;

	static const char* const s_def[5];
	static const char* const s_shift[5];
	static const char* const s_run[5];
	static const char* const s_pro[5];
	static const char* const s_japan[5];
	static const char* const s_sml[5];
};

#endif // MAME_SHARP_PC1350_H
