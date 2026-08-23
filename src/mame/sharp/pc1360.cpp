// license:GPL-2.0+
// copyright-holders:Peter Trauner
#include "emu.h"

#include "pocketc.h"
#include "pc1360.h"

// TODO: Convert to SVG rendering or internal layout

#define LOG_LCD (1U << 1)

#define VERBOSE (0)
#include "logmacro.h"

/*
 * Carried over verbatim from pc1350.cpp: the PC-1360 has been confirmed to
 * use the identical LCD resolution/24x4 5x8 character layout as the
 * PC-1350, so this rendering code (including the guessed indicator bit
 * positions in m_reg[0x83c], which were themselves never fully confirmed
 * even on the original pc1350.cpp driver) is reused as the best available
 * starting point. See pc1360.h for what is still unverified.
 */

const char* const pc1360_state::s_def[5] =
{
	"11  111 111",
	"1 1 1   1  ",
	"1 1 111 11 ",
	"1 1 1   1  ",
	"11  111 1  "
};
const char* const pc1360_state::s_shift[5] =
{
	" 11 1 1 1 111 111",
	"1   1 1 1 1    1 ",
	" 1  111 1 11   1 ",
	"  1 1 1 1 1    1 ",
	"11  1 1 1 1    1 "
};
const char* const pc1360_state::s_run[5] =
{
	"11  1 1 1  1",
	"1 1 1 1 11 1",
	"11  1 1 1 11",
	"1 1 1 1 1  1",
	"1 1  1  1  1"
};
const char* const pc1360_state::s_pro[5] =
{
	"11  11   1 ",
	"1 1 1 1 1 1",
	"11  11  1 1",
	"1   1 1 1 1",
	"1   1 1  1 "
};
const char* const pc1360_state::s_japan[5] =
{
	"  1  1  11   1  1  1",
	"  1 1 1 1 1 1 1 11 1",
	"  1 111 11  111 1 11",
	"1 1 1 1 1   1 1 1  1",
	" 1  1 1 1   1 1 1  1"
};
const char* const pc1360_state::s_sml[5] =
{
	" 11 1 1 1  ",
	"1   111 1  ",
	" 1  1 1 1  ",
	"  1 1 1 1  ",
	"11  1 1 111"
};

uint8_t pc1360_state::lcd_read(offs_t offset)
{
	uint8_t data = m_reg[offset & 0xfff];
	LOGMASKED(LOG_LCD, "pc1360 read %.3x %.2x\n",offset,data);
	return data;
}

void pc1360_state::lcd_write(offs_t offset, uint8_t data)
{
	LOGMASKED(LOG_LCD, "pc1360 write %.3x %.2x\n",offset,data);
	m_reg[offset & 0xfff] = data;
}


/* pc1360 (FIXME: layout copied from pc1350, unverified for pc1360)
   24x4 5x8 no space between chars
   [base]+000 .. +01d, [base]+200..+21d, [base]+400 ..+41d, [base]+600 ..+61d, [base]+800 .. +81d
   [base]+040 .. +05d, [base]+240..+25d, [base]+440 ..+45d, [base]+640 ..+65d, [base]+840 .. +85d
   [base]+01e .. +03b, [base]+21e..+23b, [base]+41e ..+43b, [base]+61e ..+63b, [base]+81e .. +83b
   [base]+05e .. +07b, [base]+25e..+27b, [base]+45e ..+47b, [base]+65e ..+67b, [base]+85e .. +87b
   [base]+83c: 0 SHIFT 1 DEF 4 RUN 5 PRO 6 JAPAN 7 SML */
static const int pc1360_addr[4]={ 0, 0x40, 0x1e, 0x5e };

#define DOWN 45
#define RIGHT 76

uint32_t pc1360_state::screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	const int contrast = m_dsw0->read() & 7;
	int color[4] =
	{
		colortable[contrast][0],
		colortable[contrast][1],
		8,
		7
	};

	bitmap.fill(11, cliprect);

	for (int k = 0, y = DOWN; k < 4; y += 16, k++)
		for (int x = RIGHT, i = pc1360_addr[k]; i < 0xa00; i += 0x200)
			for (int j = 0; j <= 0x1d; j++, x+=2)
				for (int bit = 0; bit < 8; bit++)
					bitmap.plot_box(x, y + bit * 2, 2, 2, color[BIT(m_reg[j+i], bit)]);

	/* 83c: 0 SHIFT 1 DEF 4 RUN 5 PRO 6 JAPAN 7 SML */
	/* I don't know how they really look like on the LCD (unverified, copied from pc1350) */
	pocketc_draw_special(bitmap, RIGHT-30, DOWN+45, s_shift, BIT(m_reg[0x83c], 0) ? color[2] : color[3]);
	pocketc_draw_special(bitmap, RIGHT-30, DOWN+55, s_def,   BIT(m_reg[0x83c], 1) ? color[2] : color[3]);
	pocketc_draw_special(bitmap, RIGHT-30, DOWN+5,  s_run,   BIT(m_reg[0x83c], 4) ? color[2] : color[3]);
	pocketc_draw_special(bitmap, RIGHT-30, DOWN+15, s_pro,   BIT(m_reg[0x83c], 5) ? color[2] : color[3]);
	pocketc_draw_special(bitmap, RIGHT-30, DOWN+25, s_japan, BIT(m_reg[0x83c], 6) ? color[2] : color[3]);
	pocketc_draw_special(bitmap, RIGHT-30, DOWN+35, s_sml,   BIT(m_reg[0x83c], 7) ? color[2] : color[3]);

	return 0;
}
