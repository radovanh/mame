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
	// asks for, i.e. the whole buffer reads back as 0x00.
	//
	// PREVIOUSLY this only poked the 4 program-area pointer bytes at
	// 0x6F01-0x6F04, guessed/hand-confirmed by POKEing a value in and
	// checking that typing a program worked, with end-of-program set equal
	// to start-of-program. That was never actually verified against a real
	// cold boot -- it only appeared to fix the bug because MAME was
	// restoring a previously-nvram-saved (already-NEW'd) RAM image on every
	// subsequent launch, masking whether a genuine cold boot (no .nv file
	// yet) was fixed at all. It wasn't: on a real fresh cold boot the
	// pointer guess was incomplete (end-of-program needs to be
	// start-of-program + 1, not equal to it -- same mistake made and then
	// fixed for PC-1401), and, exactly like PC-1401, several other system
	// variable bytes a real NEW also initializes were left untouched.
	//
	// Fixed the same way as PC-1401: you captured a save state right after
	// switching to PRO mode and pressing NEW on a genuine cold boot (nvram
	// files deleted first so it couldn't be masked again), which was
	// decompressed and diffed byte-for-byte against a fresh cold-boot RAM
	// image from the same build. Every byte below is a confirmed difference
	// between "just booted" and "booted, then NEW pressed" -- nothing here
	// is guessed. This includes a mirrored second copy of the pointer/status
	// block lower in RAM (0x6007-0x6036) that the ROM's NEW routine also
	// updates, and a status-line text buffer (0x6D00 area) that briefly
	// reads "NEW" here only because the reference state was captured right
	// after pressing it -- harmless to preload, since the ROM's own
	// cold-start routine overwrites that buffer with the normal boot banner
	// before the first frame is ever displayed (confirmed: it already does
	// this today, independent of this fix).
	uint8_t *const ram = m_ram->pointer();
	if (ram[0xf01] == 0x00 && ram[0xf02] == 0x00)
	{
		ram[0x0007] = 0x30;
		ram[0x0008] = 0x60;
		ram[0x0009] = 0x31;
		ram[0x000a] = 0x60;
		ram[0x000b] = 0x30;
		ram[0x000c] = 0x60;
		ram[0x000d] = 0x30;
		ram[0x000e] = 0x6c;
		ram[0x0017] = 0x01;
		ram[0x0019] = 0x02;
		ram[0x001a] = 0x08;
		ram[0x001c] = 0x84;
		ram[0x0022] = 0x30;
		ram[0x0023] = 0x60;
		ram[0x0024] = 0x42;
		ram[0x0028] = 0x33;
		ram[0x0029] = 0x60;
		ram[0x0030] = 0xff;
		ram[0x0031] = 0xff;
		ram[0x0032] = 0x0a;
		ram[0x0033] = 0x02;
		ram[0x0034] = 0xce;
		ram[0x0035] = 0x0d;
		ram[0x0036] = 0xff;

		// status-line text buffer: reads "NEW" in the reference capture
		// (see comment above); the ROM's own cold-start routine overwrites
		// this with the normal boot banner before frame 1 is displayed
		ram[0x0d00] = 0x4e;
		ram[0x0d01] = 0x45;
		ram[0x0d02] = 0x57;
		for (offs_t a = 0x0d04; a <= 0x0d17; a++)
			ram[a] = 0x00;

		// top of RAM: 0xEB0 holds a marker byte, and the entire unused
		// variable/array area above it is filled with 0x0D rather than left
		// at zero -- same pattern as PC-1401's top-of-RAM reserve area
		ram[0x0eb0] = 0xb1;
		for (offs_t a = 0x0eb1; a <= 0x0eff; a++)
			ram[a] = 0x0d;

		// primary program-area pointers: start of program area at
		// 0x6F01/0x6F02 (-> 0x6030) confirmed unchanged from the previous
		// guess; end of program area at 0x6F03/0x6F04 needs to be start + 1
		// (0x6031), not equal to start
		ram[0x0f01] = 0x30;
		ram[0x0f02] = 0x60;
		ram[0x0f03] = 0x31;
		ram[0x0f04] = 0x60;
		ram[0x0f05] = 0x30;
		ram[0x0f06] = 0x60;
		ram[0x0f07] = 0x30;
		ram[0x0f08] = 0x6c;
		ram[0x0f11] = 0x00;
		ram[0x0f13] = 0x02;
		ram[0x0f14] = 0x08;
		ram[0x0f16] = 0x84;
		ram[0x0f1c] = 0x30;
		ram[0x0f1d] = 0x60;
		ram[0x0f1e] = 0x42;
		ram[0x0f22] = 0x33;
		ram[0x0f23] = 0x60;
		ram[0x0f2a] = 0xb0;
		ram[0x0f2b] = 0x06;
		ram[0x0f2c] = 0x90;
		ram[0x0f36] = 0x20;
		ram[0x0f38] = 0xb3;
		ram[0x0f4a] = 0x52;
		ram[0x0f4b] = 0x0f;
		ram[0x0f57] = 0x0d;
		ram[0x0f59] = 0x79;
		ram[0x0f5a] = 0xbd;
		ram[0x0f5b] = 0xbe;
	}
}
