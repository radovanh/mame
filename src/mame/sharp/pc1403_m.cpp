// license:GPL-2.0+
// copyright-holders:Peter Trauner
#include "emu.h"
#include "cpu/sc61860/sc61860.h"

#include "pocketc.h"
#include "pc1403.h"
#include "pocketc_bas.h"
#include "machine/ram.h"
#include "imagedev/snapquik.h"

#define LOG_ASIC (1U << 1)

#define VERBSOE  (0)
#include "logmacro.h"

/* C-CE while reset, program will not be destroyed! */

/*
   port 2:
     bits 0,1: external rom a14,a15 lines
   port 3:
     bits 0..6 keyboard output select matrix line
*/

void pc1403_state::asic_write(offs_t offset, uint8_t data)
{
	m_asic[offset >> 9] = data;
	switch (offset >> 9)
	{
	case 0: // 0x3800
		// output
		LOGMASKED(LOG_ASIC, "asic write %.4x %.2x\n", offset, data);
		break;
	case 1: // 0x3a00
		LOGMASKED(LOG_ASIC, "asic write %.4x %.2x\n", offset, data);
		break;
	case 2: // 0x3c00
		membank("bank1")->set_base(memregion("user1")->base() + ((data & 7) << 14));
		LOGMASKED(LOG_ASIC, "asic write %.4x %.2x\n", offset, data);
		break;
	case 3: // 0x3e00
		break;
	}
}

uint8_t pc1403_state::asic_read(offs_t offset)
{
	uint8_t data = m_asic[offset >> 9];
	switch (offset >> 9)
	{
	case 0:
	case 1:
	case 2:
		LOGMASKED(LOG_ASIC, "asic read %.4x %.2x\n", offset, data);
		break;
	}
	return data;
}

uint8_t pc1403_state::in_a_r()
{
	uint8_t data = m_outa;

	for (int bit = 0; bit < 7; bit++)
		if (BIT(m_asic[3], bit))
			data |= m_keys[bit]->read();

	if (BIT(m_outa, 0))
	{
		data |= m_keys[7]->read();

		/* At Power Up we fake a 'C-CE' pressure */
		if (m_power)
			data |= 0x02;
	}

	for (int bit = 1, key = 8; bit < 7; bit++, key++)
		if (BIT(m_outa, bit))
			data |= m_keys[key]->read();

	return data;
}

void pc1403_state::out_c_w(uint8_t data)
{
	m_portc = data;
}

int pc1403_state::reset_r()
{
	return BIT(m_extra->read(), 1);
}

void pc1403_state::machine_start()
{
	pocketc_state::machine_start();

	membank("bank1")->set_base(memregion("user1")->base());

	m_ram_nvram->set_base(memregion("maincpu")->base() + 0x8000, 0x8000);

	uint8_t *gfx = memregion("gfx1")->base();
	for (int i = 0; i < 128; i++)
		gfx[i] = i;
}

/*
 * pc1403_state::quickload_cb -- .BAS quickload for PC-1403 / PC-1403H
 * ---------------------------------------------------------------------
 * Tokenizes the text file (pocketc_bas, PC1403 model = this ROM's own
 * keyword tables, single-byte + 0xEF-prefixed) and writes it into the
 * program area.
 *
 * Layout confirmed by diffing real save states (NEW, then
 * 10 PRINT "HELLO") on both pc1403 (RAM 0xE000-0xFFFF) and pc1403h
 * (RAM 0x8000-0xFFFF) -- the same ROM, only the RAM base differs:
 *   0xFF01/0xFF02 = start of program = RAM base + 0x30 (leading 0xFF)
 *   0xFF03/0xFF04 = end of program   (points at the trailing 0xFF)
 *   RAM base + 0x07..0x0A holds a second copy of both pointers, which the
 *   ROM updates together with 0xFF01-0xFF04 whenever a line is entered,
 *   so both copies are written here.
 * The fixed variables A-Z sit just below 0xFB10 (the ROM's free-memory
 * figure for a 1403 is 0xFB10 - 0xE030 - 2), so the program must end
 * below that.
 */
std::pair<std::error_condition, std::string> pc1403_state::quickload_cb(snapshot_image_device &image)
{
	uint64_t size = image.length();
	std::vector<char> text(size);
	if (size > 0 && image.fread(text.data(), size) != size)
		return std::make_pair(std::errc::io_error, std::string("Error reading file"));

	std::vector<uint8_t> tokenized;
	std::string error;
	if (!pocketc_bas::tokenize_program(pocketc_bas::model::PC1403, std::string(text.data(), size), tokenized, error))
		return std::make_pair(std::errc::invalid_argument, error);

	address_space &space = m_maincpu->space(AS_PROGRAM);

	constexpr uint32_t PTR_START = 0xff01; // start at +0/+1, end at +2/+3
	constexpr uint32_t AREA_TOP  = 0xfb10; // fixed variables start here
	const uint32_t copy = m_ram_base + 0x07; // ROM-maintained pointer copy

	uint32_t start = space.read_byte(PTR_START) | (space.read_byte(PTR_START + 1) << 8);
	if (start < m_ram_base + 0x30 || start >= AREA_TOP)
		start = m_ram_base + 0x30; // pointer not initialised -- use the NEW value

	const uint32_t end = start + uint32_t(tokenized.size()); // address of the trailing 0xFF
	if (end >= AREA_TOP)
	{
		return std::make_pair(std::errc::file_too_large,
			util::string_format("Tokenized program (%u bytes) does not fit: %u bytes free from %04X",
				(unsigned)tokenized.size(), (unsigned)(AREA_TOP - 1 - start), start));
	}

	space.write_byte(start, 0xff); // leading sentinel
	for (size_t i = 0; i < tokenized.size(); i++)
		space.write_byte(start + 1 + i, tokenized[i]); // ends with the trailing 0xFF

	for (uint32_t base : { PTR_START, copy })
	{
		space.write_byte(base + 0, start & 0xff);
		space.write_byte(base + 1, start >> 8);
		space.write_byte(base + 2, end & 0xff);
		space.write_byte(base + 3, end >> 8);
	}

	return std::make_pair(std::error_condition(), std::string());
}
