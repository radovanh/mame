// license:GPL-2.0+
// copyright-holders:Peter Trauner
#include "emu.h"
#include "cpu/sc61860/sc61860.h"

#include "pocketc.h"
#include "pc1350.h"
#include "machine/ram.h"

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

void pc1350_state::machine_start()
{
	pocketc_state::machine_start();

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
