// license:GPL-2.0+
// copyright-holders:Peter Trauner
#include "emu.h"
#include "cpu/sc61860/sc61860.h"

#include "pocketc.h"
#include "pc1401.h"
#include "machine/ram.h"

/* C-CE while reset, program will not be destroyed! */

/* error codes
1 syntax error
2 calculation error
3 illegal function argument
4 too large a line number
5 next without for
  return without gosub
6 memory overflow
7 print using error
8 i/o device error
9 other errors*/

void pc1401_state::out_b_w(uint8_t data)
{
	m_outb = data;
}

void pc1401_state::out_c_w(uint8_t data)
{
	m_portc = data;
}

uint8_t pc1401_state::in_a_r()
{
	int data = m_outa;

	for (int bit = 0; bit < 5; bit++)
		if (BIT(m_outb, bit))
			data |= m_keys[bit]->read();

	if (m_outb & 0x20)
	{
		data |= m_keys[5]->read();

		/* At Power Up we fake a 'C-CE' pressure */
		if (m_power)
			data |= 0x01;
	}

	for (int bit = 0, key = 6; bit < 7; bit++, key++)
		if (BIT(m_outa, bit))
			data |= m_keys[key]->read();

	return data;
}

uint8_t pc1401_state::in_b_r()
{
	int data = m_outb;

	if (BIT(m_extra->read(), 2))
		data |= 0x01;

	return data;
}

int pc1401_state::reset_r()
{
	return (m_extra->read() & 0x02);
}

void pc1401_state::machine_start()
{
	pocketc_state::machine_start();

	// NOTE: this has the same class of bug already fixed in pc1350's
	// machine_start() -- it points "ram_nvram" at an offset into the
	// "maincpu" ROM region instead of the real RAM (which here is a plain
	// address_map .ram() range, not a ram_device, so there's no single
	// buffer pointer/size this driver already owns the way pc1350's does).
	// Left as-is for now: unlike pc1350, this driver's raw RAM already
	// defaults to 0x00 on its own (a plain .ram() range, not a ram_device
	// with its own 0xFF default fill), so the cold-boot pointer bug below
	// reproduces either way. Fixing the nvram binding itself (so a saved
	// program actually persists across MAME launches) is a separate,
	// untouched issue -- see machine_reset() below for the bug this
	// session was asked to fix.
	m_ram_nvram->set_base(memregion("maincpu")->base() + 0x2000, 0x2800);

	uint8_t *gfx = memregion("gfx1")->base();
	for (int i = 0; i < 128; i++)
		gfx[i] = i;
}

void pc1401_state::machine_reset()
{
	pocketc_state::machine_reset();

	// Same class of bug as pc1350 (see the long comment on
	// pc1350_state::machine_reset() in pc1350_m.cpp): on a genuine cold
	// boot the BASIC system pointers -- start of program area at 0x46E1
	// (low byte) / 0x46E2 (high byte), end of program area at 0x46E3 /
	// 0x46E4 -- come up as 0x00/0x00 instead of a real address (this
	// driver's RAM at that range is a plain address_map .ram() block,
	// which MAME zero-fills by default, so both pointers read 0/0 with
	// nothing to override them the way pc1350's raw 0xFF fill did).
	//
	// Setting just those 4 bytes (start=0x3C00, end=0x3C01) was not
	// enough -- BASIC still refused to accept a typed program until NEW
	// was pressed manually. The reason: those two bytes are only
	// *pointers*; the memory they point at, and several other system
	// variables the pointers don't cover, also need to hold the specific
	// values a real NEW leaves behind, not MAME's default zero fill (for
	// example, 0x3C00/0x3C01 -- the bytes the pointers above point *at* --
	// need to hold the actual end-of-program marker token, 0xFF, not 0).
	// To find every such byte, you captured a save state on your own
	// build right after BASIC mode + NEW (thank you), which we
	// decompressed and diffed byte-for-byte against a fresh cold-boot RAM
	// image from the same build. Every byte below is a confirmed
	// difference between "just booted" and "booted, then NEW pressed" --
	// nothing here is guessed.
	address_space &space = m_maincpu->space(AS_PROGRAM);
	if (space.read_byte(0x46E1) == 0x00 && space.read_byte(0x46E2) == 0x00)
	{
		space.write_byte(0x46E1, 0x00); // start of program area, low byte
		space.write_byte(0x46E2, 0x3C); // ...high byte -> 0x3C00
		space.write_byte(0x46E3, 0x01); // end of program area, low byte
		space.write_byte(0x46E4, 0x3C); // ...high byte -> 0x3C01 (one past start -- empty program)

		// content at the program-start address itself: the (empty)
		// program's end-of-program marker token
		space.write_byte(0x3C00, 0xFF);
		space.write_byte(0x3C01, 0xFF);

		// other system-variable / working-register bytes a real NEW
		// leaves in a specific non-zero pattern, not zero
		space.write_byte(0x46B7, 0x56);
		space.write_byte(0x46B8, 0x0F);
		space.write_byte(0x46B9, 0xEB);
		space.write_byte(0x46BA, 0x81);
		space.write_byte(0x46D8, 0x02);
		space.write_byte(0x46DB, 0x79);
		space.write_byte(0x46DC, 0xBC);
		space.write_byte(0x46DD, 0xB4);
		space.write_byte(0x46E9, 0x0E);
		space.write_byte(0x46EA, 0xB3);
		space.write_byte(0x46F1, 0xB0);
		space.write_byte(0x46FC, 0xD0);
		space.write_byte(0x46FD, 0x45);
		space.write_byte(0x46FE, 0x03);
		space.write_byte(0x46FF, 0x3C);

		// top of RAM: 0x47B0 holds a marker byte, and the entire unused
		// variable/array area above it (0x47B1-0x47FF) is filled with
		// 0x0D rather than left at zero
		space.write_byte(0x47B0, 0xB1);
		for (offs_t a = 0x47B1; a <= 0x47FF; a++)
			space.write_byte(a, 0x0D);
	}
}
