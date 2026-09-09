// license:GPL-2.0+
// copyright-holders:Peter Trauner
#include "emu.h"
#include "cpu/sc61860/sc61860.h"

#include "pocketc.h"
#include "pc1350.h"
#include "pocketc_bas.h"
#include "machine/ram.h"
#include "imagedev/snapquik.h"

void pc1350_state::out_b_w(uint8_t data)
{
	m_outb = data;
}

void pc1350_state::out_c_w(uint8_t data)
{
}

uint8_t pc1350_state::in_a_r()
{
	int data = m_outa;
	int t = keyboard_line_r();

	for (int bit = 0; bit < 6; bit++)
		if (BIT(t, bit))
			data |= m_keys[bit]->read();

	for (int bit = 0, key = 6; bit < 2; bit++, key++)
		if (BIT(m_outa, bit))
			data |= m_keys[key]->read();

	if (BIT(m_outa, 2))
	{
		data |= m_keys[8]->read();

		/* At Power Up we fake a 'CLS' pressure */
		if (m_power)
			data |= 0x08;
	}

	for (int bit = 3, key = 9; bit < 5; bit++, key++)
		if (BIT(m_outa, bit))
			data |= m_keys[key]->read();

	if (m_outa & 0xc0)
		data |= m_keys[11]->read();

	// missing lshift

	return data;
}

uint8_t pc1350_state::in_b_r()
{
	return m_outb;
}

/*
 * Shift-chord fix for clipboard paste -- same mechanism as
 * pc1360_state::shift_chord_changed()/release_shift_pulse() (pc1360_m.cpp),
 * applied here because the user confirmed PC-1350's real SHIFT keys behave
 * the same way: a simultaneous-hold left SHIFT and a separate tap-to-latch
 * right SHIFT (this driver's real, host-key-bound SHIFT field, KEY0 0x40,
 * is the latter). MAME's natural-keyboard engine only ever implements the
 * simultaneous-hold style, so pasted SHIFT-chorded characters (the
 * "!"/'"'/'#'/etc. row) need this same interception: the phantom
 * "SHIFT (paste)" field on the EXTRA port (see pocketc.cpp) carries
 * PORT_CHAR(UCHAR_SHIFT_1) so natural_keyboard presses IT (never the real
 * SHIFT key directly) to begin a chord, and this callback converts that
 * hold into a real tap-then-release of the real SHIFT field before
 * natural_keyboard presses the base character field.
 */
INPUT_CHANGED_MEMBER(pc1350_state::shift_chord_changed)
{
	// Only the phantom field's rising edge matters -- that's
	// natural_keyboard beginning a chord. Its later release of this same
	// phantom field (the falling edge) has no real hardware bit behind it
	// and needs no action.
	if (!newval)
		return;

	// Tap the REAL SHIFT key (KEY0 0x40) on now...
	m_keys[0]->field(0x40)->set_value(1);

	// ...and release it again shortly afterwards, well before
	// natural_keyboard presses the base character key (see choose_delay()
	// in natkeyboard.cpp for PC-1350's per-field delay), so the real
	// hardware never sees the two keys held down together.
	m_shift_pulse_timer->adjust(attotime::from_msec(150));
}

TIMER_CALLBACK_MEMBER(pc1350_state::release_shift_pulse)
{
	m_keys[0]->field(0x40)->clear_value();
}

void pc1350_state::machine_start()
{
	pocketc_state::machine_start();

	// Backs shift_chord_changed()/release_shift_pulse() above -- the
	// clipboard-paste shift-chord fix.
	m_shift_pulse_timer = timer_alloc(FUNC(pc1350_state::release_shift_pulse), this);

	address_space &space = m_maincpu->space(AS_PROGRAM);

	space.install_ram(0x6000, 0x6fff, &m_ram->pointer()[0x0000]);

	if (m_ram->size() >= 0x3000)
	{
		space.install_ram(0x4000, 0x5fff, &m_ram->pointer()[0x1000]);
	}
	else
	{
		space.nop_readwrite(0x4000, 0x5fff);
	}

	if (m_ram->size() >= 0x5000)
	{
		space.install_ram(0x2000, 0x3fff, &m_ram->pointer()[0x3000]);
	}
	else
	{
		space.nop_readwrite(0x2000, 0x3fff);
	}

	// FIXED: this used to point at memregion("maincpu")->base() + 0x2000,
	// i.e. an offset into the ROM region -- but 0x2000-0x6fff in the CPU's
	// address space is always backed by m_ram->pointer() (see the
	// install_ram() calls above), never by the "maincpu" ROM region at
	// those offsets. So the nvram device was persisting/restoring an
	// unrelated, effectively dead block of memory instead of the RAM the
	// CPU actually reads and writes -- meaning nvram_default()/nvram_read()
	// never touched the real RAM at all, which was left at ram_device's own
	// raw fill (0xFF throughout, see m_default_value in machine/ram.cpp,
	// never overridden by set_default_value() in pocketc.cpp's RAM(config,
	// m_ram) call). That's exactly why 0x6F01/0x6F02 -- the BASIC "start of
	// program area" pointer (low byte/high byte) -- came up as FF/FF
	// instead of the correct 0x30/0x60 (-> address 0x6030): the intended
	// "NVRAM(config, "ram_nvram", nvram_device::DEFAULT_ALL_0)" cold-clear
	// behaviour declared in pocketc.cpp never actually reached this memory.
	// Point it at the real RAM buffer instead (matches the equivalent,
	// already-fixed pattern in pc1360_state::machine_start()), sized to
	// whatever is actually installed/mapped for the selected RAM option.
	m_ram_nvram->set_base(m_ram->pointer(), m_ram->size());
}

void pc1350_state::machine_reset()
{
	pocketc_state::machine_reset();

	// With the nvram-binding fix in machine_start() above, a genuine cold
	// boot (no .nv file saved for this system/RAM-size combination yet)
	// now actually reaches the real RAM buffer with the "NVRAM(config,
	// "ram_nvram", nvram_device::DEFAULT_ALL_0)" fill pocketc.cpp already
	// asks for, i.e. the whole buffer reads back as 0x00. That's still not
	// quite right on its own: the BASIC system pointers at 0x6F01/0x6F02
	// (start of program area, low/high byte) and 0x6F03/0x6F04 (end of
	// program area, low/high byte) are then 0x00/0x00, which is just as
	// invalid as the FF/FF this whole fix is for -- real hardware's own
	// cold-start routine would set them to real addresses instead. Detect
	// exactly that "freshly cleared, never touched" case and set both
	// pointers to 0x30/0x60 (-> address 0x6030): confirmed by hand for the
	// start pointer (POKEing it in after a broken PRO-mode boot, then NEW
	// and a soft reset (F3), was enough to make typed programs store
	// correctly again), and the end pointer must equal the start pointer
	// here too, since on a freshly cleared machine with no program typed
	// in yet the program area is empty -- "end of program" and "start of
	// program" are the same address until you actually type something. A
	// real saved program's pointers are never 0x0000, so this can't
	// misfire against genuine nvram-restored state.
	uint8_t *const ram = m_ram->pointer();
	if (ram[0xf01] == 0x00 && ram[0xf02] == 0x00)
	{
		ram[0xf01] = 0x30; // start of program area, low byte
		ram[0xf02] = 0x60; // ...high byte -> 0x6030
		ram[0xf03] = 0x30; // end of program area, low byte -- == start
		ram[0xf04] = 0x60; // ...high byte -> 0x6030 (empty program)
	}
}

/*
 * QUICKLOAD_LOAD_MEMBER(pc1350_state, quickload_cb)
 * ---------------------------------------------------------------------
 * Loads a plain-text .BAS file selected via MAME's quickload file
 * manager, tokenizes it natively (pocketc_bas::tokenize_program(),
 * PC1350 model -- single-byte tokens), and writes the result directly
 * into the program area -- the direct-memory-injection alternative to
 * real 11-pin serial emulation explored in
 * claude/pc1350-pc1360-serial-feasibility.md.
 *
 * PC-1350's program area is always at a fixed 0x6030 regardless of the
 * optional 12K/20K RAM-card size (that RAM only extends the workspace at
 * 0x2000-0x5fff; the program-pointer bytes at 0x6f01-0x6f04 always live
 * in the fixed 0x6000-0x6fff window, see machine_start() above), and
 * machine_reset() above already forces this pointer pair to 0x6030 on a
 * genuine cold boot. This callback still defensively re-derives the base
 * address if the current pointer looks outside that window, rather than
 * assuming machine_reset() always ran first.
 *
 * NOTE: the leading/trailing 0xFF bracketing bytes around the tokenized
 * content are confirmed against real MAME memory for PC-1360 specifically
 * (see the project's tokenizer-format doc) but NOT yet independently
 * verified against real PC-1350 hardware/MAME memory -- applied here on
 * the working assumption that this BASIC-ROM-family convention (and the
 * "end pointer addresses the trailing FF itself" rule) is shared, since
 * both models already share the exact same start/end-pointer-pair layout
 * convention. Please double-check against a real PC-1350 memory dump
 * before relying on this beyond testing.
 */
std::pair<std::error_condition, std::string> pc1350_state::quickload_cb(snapshot_image_device &image)
{
	uint64_t size = image.length();
	std::vector<char> text(size);
	if (size > 0 && image.fread(text.data(), size) != size)
		return std::make_pair(std::errc::io_error, std::string("Error reading file"));

	std::vector<uint8_t> tokenized;
	std::string error;
	if (!pocketc_bas::tokenize_program(pocketc_bas::model::PC1350, std::string(text.data(), size), tokenized, error))
		return std::make_pair(std::errc::invalid_argument, error);

	address_space &space = m_maincpu->space(AS_PROGRAM);

	constexpr uint32_t ram_window_lo = 0x6000, ram_window_hi = 0x6fff;
	constexpr uint32_t expected_start = 0x6030;

	uint32_t start = space.read_byte(0x6f01) | (space.read_byte(0x6f02) << 8);
	if (start < ram_window_lo || start > ram_window_hi)
		start = expected_start; // cold-boot pointer wasn't sane -- fall back

	if (tokenized.size() > (ram_window_hi - start))
	{
		return std::make_pair(std::errc::file_too_large,
			util::string_format("Tokenized program (%u bytes) does not fit in the %u bytes free from %04X",
				(unsigned)tokenized.size(), (unsigned)(ram_window_hi - start), start));
	}

	space.write_byte(start, 0xff); // leading sentinel (see doc)
	for (size_t i = 0; i < tokenized.size(); i++)
		space.write_byte(start + 1 + i, tokenized[i]);

	const uint32_t end = start + uint32_t(tokenized.size());

	space.write_byte(0x6f01, start & 0xff);
	space.write_byte(0x6f02, (start >> 8) & 0xff);
	space.write_byte(0x6f03, end & 0xff);
	space.write_byte(0x6f04, (end >> 8) & 0xff);

	return std::make_pair(std::error_condition(), std::string());
}
