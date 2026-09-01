// license:GPL-2.0+
// copyright-holders:Peter Trauner
/*****************************************************************************
 *
 * includes/pc1251.h
 *
 * Pocket Computer 1251
 *
 ****************************************************************************/

#ifndef MAME_SHARP_PC1251_H
#define MAME_SHARP_PC1251_H

#include "pocketc.h"

class pc1251_state : public pocketc_state
{
public:
	pc1251_state(const machine_config &mconfig, device_type type, const char *tag)
		: pocketc_state(mconfig, type, tag)
		, m_mode(*this, "MODE")
		, m_keys(*this, "KEY%u", 0U)
	{
		std::fill(std::begin(m_reg), std::end(m_reg), 0);
	}

	void init_pc1251();

	void pc1255(machine_config &config);
	void pc1251(machine_config &config);
	void pc1250(machine_config &config);

protected:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

	TIMER_CALLBACK_MEMBER(basic_init_done);

	uint32_t screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);

	void pc1250_mem(address_map &map) ATTR_COLD;
	void pc1251_mem(address_map &map) ATTR_COLD;
	void pc1255_mem(address_map &map) ATTR_COLD;
	void pc1260_mem(address_map &map) ATTR_COLD;
	void pc1261_mem(address_map &map) ATTR_COLD;

	void out_b_w(uint8_t data);
	void out_c_w(uint8_t data);

	int reset_r();
	uint8_t in_a_r();
	uint8_t in_b_r();
	uint8_t lcd_read(offs_t offset);
	void lcd_write(offs_t offset, uint8_t data);

	// Second LCD display-RAM window ("Anzeige-Speicher 3/4" in the Ditze
	// Systemhandbuch, CPU addresses 0x2800-0x28ff) -- PC-1260/1261 only, the
	// second physical display line. Declared here alongside lcd_read/
	// lcd_write (rather than solely in pc1260_state) purely for consistency
	// with how m_reg/lcd_read/lcd_write are already shared infrastructure in
	// this class even though only pc1260_state ends up using them for a
	// second bank; pc1250/pc1251/pc1255/trs80pc3 never map anything to these.
	uint8_t lcd2_read(offs_t offset);
	void lcd2_write(offs_t offset, uint8_t data);

	// Polls the "Cycle RUN/PRO/RSV switch" key (EXTRA port bit 0x04, see
	// pocketc.cpp) and, on a fresh key-down (edge-detected against
	// m_mode_switch_key_prev below), advances the MODE dip switch to its
	// next declared setting live -- shared by every machine in this file
	// that has the physical 4-position switch (PC-1250/1251/1255/1245/
	// TRS-80-PC-3 via this class's own screen_update() below, PC-1260/1261/
	// 1262 via pc1260_state's override), since the key-polling and the dip
	// switch itself are identical either way; only what the switch's value
	// visibly drives differs per model (see each screen_update() for that).
	void poll_mode_switch_key();

protected:
	// PC-1260/1261 (pc1260_state below) needs read access to these for its
	// own screen_update() override -- see the "Modus-Flag" comment there.
	uint8_t m_reg[0x100]{};

	// Backing store for lcd2_read/lcd2_write above -- PC-1260/1261's second
	// display line. See pc1260_state::screen_update()'s comment for the
	// second draw loop that reads this.
	uint8_t m_reg2[0x100]{};

	static const char *const s_def[5];
	static const char *const s_shift[5];
	static const char *const s_de[5];
	static const char *const s_g[5];
	static const char *const s_rad[5];
	static const char *const s_run[5];
	static const char *const s_pro[5];
	static const char *const s_rsv[5];

	// Small tick mark used by pc1260_state::screen_update() for the DEG/RAD/
	// GRAD indicators and the RUN/PRO/RSV mode-switch indicator -- same
	// glyph/purpose as pc1401_state::s_line and pc1403_state::s_line (a
	// short bar drawn just above/beside a bezel-printed label, rather than a
	// full word drawn inside the LCD glass itself). Declared here rather
	// than duplicated in pc1260_state purely to match how s_def/s_shift/etc.
	// above are already declared in this class even though only
	// pc1260_state's screen_update() ends up using this one.
	static const char *const s_line[5];

	// "SMALL" -- a third indicator alongside SHIFT/DEF in PC-1260/1261's
	// left margin (see pc1260_state::screen_update()), for the small-
	// character mode the user confirmed SHIFT+8 actually toggles on real
	// hardware. Register bit confirmed (0x203d bit 4, empirically found
	// from the user's own hand-PEEKed data) -- see the long comment on
	// pc1260_state::screen_update() for the methodology.
	static const char *const s_small[5];

	// Also needed by pc1260_state::screen_update() -- see that function's
	// comment for why the RUN/PRO/RSV indicator reads this port directly
	// rather than any CPU-visible register.
	required_ioport m_mode;

	// Edge-detect state for poll_mode_switch_key() above.
	bool m_mode_switch_key_prev = false;

private:
	required_ioport_array<10> m_keys;

	emu_timer *m_basic_init_timer = nullptr;
};

class pc1260_state : public pc1251_state
{
public:
	pc1260_state(const machine_config &mconfig, device_type type, const char *tag)
		: pc1251_state(mconfig, type, tag)
	{ }

	void pc1260(machine_config &config);
	void pc1261(machine_config &config);

protected:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

	uint32_t screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);
};

#endif // MAME_SHARP_PC1251_H
