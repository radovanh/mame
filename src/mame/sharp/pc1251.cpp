// license:GPL-2.0+
// copyright-holders:Peter Trauner
#include "emu.h"

#include "pocketc.h"
#include "pc1251.h"

// TODO: Convert to SVG rendering or internal layout

#define LOG_LCD (1U << 1)

#define VERBOSE (0)
#include "logmacro.h"

const char *const pc1251_state::s_def[5] =
{
	"11  111 111",
	"1 1 1   1  ",
	"1 1 111 11 ",
	"1 1 1   1  ",
	"11  111 1  "
};
const char *const pc1251_state::s_shift[5] =
{
	" 11 1 1 1 111 111",
	"1   1 1 1 1    1 ",
	" 1  111 1 11   1 ",
	"  1 1 1 1 1    1 ",
	"11  1 1 1 1    1 "
};
const char *const pc1251_state::s_de[5] =
{
	"11  111",
	"1 1 1  ",
	"1 1 111",
	"1 1 1  ",
	"11  111"
};
const char *const pc1251_state::s_g[5] =
{
	" 11",
	"1  ",
	"1 1",
	"1 1",
	" 11"
};
const char *const pc1251_state::s_rad[5] =
{
	"11   1  11 ",
	"1 1 1 1 1 1",
	"11  111 1 1",
	"1 1 1 1 1 1",
	"1 1 1 1 11 "
};
const char *const pc1251_state::s_run[5] =
{
	"11  1 1 1  1",
	"1 1 1 1 11 1",
	"11  1 1 1 11",
	"1 1 1 1 1  1",
	"1 1  1  1  1"
};
const char *const pc1251_state::s_pro[5] =
{
	"11  11   1  ",
	"1 1 1 1 1 1 ",
	"11  11  1 1 ",
	"1   1 1 1 1 ",
	"1   1 1  1  "
};
const char *const pc1251_state::s_rsv[5] =
{
	"11   11 1   1",
	"1 1 1   1   1",
	"11   1   1 1 ",
	"1 1   1  1 1 ",
	"1 1 11    1  "
};
const char *const pc1251_state::s_line[5] = /* simple bar, cf. pc1401_state::s_line -- 8px
                                                wide to match the tick marks already baked
                                                into artwork/pc1260.zip's pc1260.png (measured
                                                8px wide, see pc1260_state::screen_update()) */
{
	"        ",
	"        ",
	"11111111",
	"11111111",
	"11111111"
};

const char *const pc1251_state::s_small[5] = /* "SMALL" -- third left-margin
                                                  indicator on PC-1260/1261,
                                                  above SHIFT and DEF (see
                                                  pc1260_state::screen_update()) */
{
	" 11 1   1  1  1   1  ",
	"1   11 11 1 1 1   1  ",
	" 1  1 1 1 111 1   1  ",
	"  1 1   1 1 1 1   1  ",
	"11  1   1 1 1 111 111"
};
const char *const pc1251_state::s_kana[8] = /* "カナ" (KANA) -- fourth
                                                 left-margin indicator on
                                                 PC-1260/1261, above SMALL
                                                 (see
                                                 pc1260_state::screen_update(),
                                                 which draws this one with
                                                 its own small loop instead
                                                 of pocketc_draw_special(),
                                                 since that helper hard-codes
                                                 a 5-row glyph and this one is
                                                 8). Real katakana, not a
                                                 Latin-letter approximation
                                                 -- per the user's explicit
                                                 correction, drawn as the
                                                 literal characters, matching
                                                 what the reference pokecom
                                                 project draws with its own
                                                 text renderer (see
                                                 lcd_read()'s long comment).
                                                 Rasterized from Noto Sans
                                                 CJK JP Bold at high
                                                 resolution, then box-filter
                                                 downscaled to 21x8 and
                                                 thresholded -- verified
                                                 legible at this size via
                                                 saved preview renders before
                                                 being hand-transcribed here
                                                 (not hand-drawn from
                                                 scratch, to avoid guessing
                                                 at real katakana stroke
                                                 shapes). */
{
	"   11          11    ",
	"   11          11    ",
	"111111111      11    ",
	"   11  11  1111111111",
	"   1   11      11    ",
	"  11   11      11    ",
	"  11   1       11    ",
	" 11    1      11     "
};

uint8_t pc1251_state::lcd_read(offs_t offset)
{
	uint8_t data = m_reg[offset & 0xff];
	LOGMASKED(LOG_LCD, "pc1251 read %.3x %.2x\n", offset, data);

	// PC-1260/1261 only: pin 0x203d bit 3 (KANA) to whatever the "LANG"
	// dip switch (pocketc.cpp, present in this shared
	// INPUT_PORTS_START(pc1251) block but otherwise unread outside this
	// driver's PC-1260/1261 machines, same as the "Cycle RUN/PRO/RSV
	// switch" key) selects. Scoped by system name -- like the cold-boot
	// pointer fixes in machine_reset() below -- since lcd_read()/
	// lcd_write() are shared, non-virtual functions used by every machine
	// in this file, and 0x203d bit 3 means something different (or
	// nothing) on those other models.
	//
	// This is deliberately a direct override of the CPU-visible value, not
	// a hardware signal being modeled: a completely separate PC-1261
	// reverse-engineering project (digihori/pokecom, an Android emulator
	// built from scratch, independent of MAME) confirmed the same 0x203D
	// bit layout found here by ROM disassembly (BUSY/PRINT/KANA=bit3/
	// SML=bit4/SHIFT=bit5/DEF=bit6) -- and that project's own analysis
	// found no external hardware signal for KANA at all on real PC-1261
	// hardware: it's purely an internal ROM flag, set and read by the
	// firmware itself. So there's no real "signal" to fake here the way
	// in_b_r()'s fix (pc1251_m.cpp) models one for the SHIFT+8 dispatch
	// gate -- forcing the flag directly is the closest equivalent to a
	// permanently-latched KANA mode.
	//
	// Confirmed empirically (not just by inspection) that this bit is
	// actually consulted on every normal keystroke, not just inside the
	// SHIFT+8 modifier-key handler: with this bit forced set and a single
	// "A" key press simulated, the byte sequence the ROM wrote into
	// character RAM for that keypress came out completely different from
	// an unforced run (7c 12 11 12 7c vs. 08 46 4a 32 1e, both
	// reproducible across repeated runs) -- i.e. the ROM's own keyboard-
	// to-character-code translation genuinely branches on this bit. This
	// driver still has no real katakana glyph ROM dump behind its
	// character rendering, though (see the TODO at the top of this file --
	// glyphs here are raw per-character segment bytes read straight out of
	// a synthetic identity-mapped "gfx1" region, not a font lookup against
	// a dumped character ROM), so "Japanese" mode will make the ROM write
	// genuinely different byte values into display RAM -- checkable via a
	// real BASIC PEEK, which goes through this exact same read -- but this
	// driver cannot yet render those bytes as real katakana shapes.
	//
	// User-corrected: an earlier pass only forced this bit *set* here on
	// read, for "Japanese"; "English/Export" was left as whatever the ROM
	// last wrote, which let SHIFT+8 (or anything else touching 0x203d)
	// still set it on a running machine, and never touched the
	// underlying stored byte at all -- so anything reading m_reg[0x3d]
	// directly instead of through here (e.g. screen_update()'s indicator
	// drawing, added below) never saw the forced value. Both directions
	// are now pinned -- see lcd_write() below, which enforces the exact
	// same rule on every write so the *stored* byte itself always matches
	// the switch, not just what this read patches in transiently.
	if ((offset & 0xff) == 0x3d &&
			(strcmp(machine().system().name, "pc1260") == 0 || strcmp(machine().system().name, "pc1261") == 0))
	{
		if (BIT(ioport("LANG")->read(), 0))
			data &= ~0x08;
		else
			data |= 0x08;
	}

	return data;
}

void pc1251_state::lcd_write(offs_t offset, uint8_t data)
{
	LOGMASKED(LOG_LCD, "pc1251 write %.3x %.2x\n", offset, data);

	// See lcd_read() above for the full rationale. Enforced here too, on
	// the *stored* byte, so the ROM can never leave 0x203d bit 3 sitting
	// at the wrong value even momentarily: the ROM's own SHIFT+8 handler
	// (pc1251.cpp/pc1251_m.cpp comments) both sets (ORID 08) and clears
	// (ANID F7) this exact bit depending on what it thinks the current
	// state is, and without correcting the write itself, code that reads
	// m_reg[0x3d] directly instead of via lcd_read() -- specifically
	// pc1260_state::screen_update()'s new KANA indicator below -- would
	// see whatever the ROM last wrote, not what the dip switch selects.
	if ((offset & 0xff) == 0x3d &&
			(strcmp(machine().system().name, "pc1260") == 0 || strcmp(machine().system().name, "pc1261") == 0))
	{
		if (BIT(ioport("LANG")->read(), 0))
			data &= ~0x08;
		else
			data |= 0x08;
	}

	m_reg[offset & 0xff] = data;
}

// PC-1260/1261's second display line (0x2800-0x28ff, "Anzeige-Speicher 3/4"
// -- see the long comment on pc1260_state::screen_update()'s second draw
// loop). Same access pattern as lcd_read/lcd_write above, separate backing
// store.
uint8_t pc1251_state::lcd2_read(offs_t offset)
{
	uint8_t data = m_reg2[offset & 0xff];
	LOGMASKED(LOG_LCD, "pc1251 line2 read %.3x %.2x\n", offset, data);
	return data;
}

void pc1251_state::lcd2_write(offs_t offset, uint8_t data)
{
	LOGMASKED(LOG_LCD, "pc1251 line2 write %.3x %.2x\n", offset, data);
	m_reg2[offset & 0xff] = data;
}

#define DOWN 62
#define RIGHT 68

// "Cycle RUN/PRO/RSV switch" key (EXTRA port bit 0x04, see pocketc.cpp) --
// a MAME-side convenience, not a real key on any of these machines. The
// MODE dip switch (real hardware's physical 4-position RUN/PRO/RSV/Off
// switch, present on every machine in this file) has no "assign any key"
// binding available anywhere in MAME's UI: dip switches are their own
// input class, excluded from the general Input(this system) remap menu
// (confirmed by reading the UI source, not a guess), and reachable only
// through the dedicated Dip Switches menu's own left/right navigation. So
// this key is the only direct way to flip it. Shared by every
// screen_update() in this file (pc1251_state's own below, for PC-1250/
// 1251/1255/1245/TRS-80-PC-3; pc1260_state's override further down, for
// PC-1260/1261/1262) since the key-polling and the underlying dip switch
// are identical either way -- only what its value visibly drives differs
// per model (see each screen_update() for that).
//
// Edge-detected against m_mode_switch_key_prev so a held key advances one
// step, not once a frame; select_next_setting() is the same call MAME's
// own Dip Switches menu makes when you press right there, so it takes
// effect immediately, same as that menu (confirmed live in the sandbox --
// no reset needed) -- and cycles through this dip switch's actual
// declared settings in order (Off/On-RUN/On-PRO/On-RSV, wrapping around),
// so it stays correct even if that list is ever edited.
void pc1251_state::poll_mode_switch_key()
{
	const bool mode_switch_key = BIT(m_extra->read(), 2);
	if (mode_switch_key && !m_mode_switch_key_prev)
		m_mode->field(0x07)->select_next_setting();
	m_mode_switch_key_prev = mode_switch_key;
}

uint32_t pc1251_state::screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	int color[2] =
	{
		7, //pocketc_colortable[PC1251_CONTRAST][0],
		8, //pocketc_colortable[PC1251_CONTRAST][1]
	};

	bitmap.fill(11, cliprect);

	// See poll_mode_switch_key()'s own comment above for what this does and
	// why it's needed. PC-1250/1251/1255/1245/TRS-80-PC-3 (this function)
	// never read the resulting MODE dip switch value directly -- their ROM
	// already samples it live through in_b_r() (see pc1251_m.cpp) whenever
	// it executes an "IN B" with output-bit 3 set, the same as a real key
	// press would look to it, and writes the result back into m_reg[0x3e]
	// below for display -- so simply changing the dip switch's live value
	// here is enough; no separate direct-read bypass is needed the way
	// pc1260_state::screen_update() needed one (that model's ROM never
	// samples the switch into any CPU-visible register at all -- see that
	// function's own long comment).
	poll_mode_switch_key();

	const int contrast = m_dsw0->read() & 7;
	int x = RIGHT;
	int y = DOWN;
	for (int i = 0; i < 60; x += 3)
		for (int j = 0; j < 5; j++, i++, x += 3)
			m_gfxdecode->gfx(0)->opaque(bitmap, cliprect, m_reg[i], contrast, 0, 0, x, y);

	for (int i = 0x7b; i >= 0x40; x += 3)
		for (int j = 0; j < 5; j++, i--, x += 3)
			m_gfxdecode->gfx(0)->opaque(bitmap,cliprect, m_reg[i], contrast, 0, 0, x, y);

	/* 0x3c 1 def?, 4 g, 8 de
	   0x3d 2 shift, 4 rad, 8 error
	   0x3e 1 pro?, 2 run?, 4rsv?*/

	pocketc_draw_special(bitmap, RIGHT+18,  DOWN-10, s_def,   BIT(m_reg[0x3c], 0) ? color[1] : color[0]);
	pocketc_draw_special(bitmap, RIGHT+142, DOWN-10, s_g,     BIT(m_reg[0x3c], 2) ? color[1] : color[0]);
	pocketc_draw_special(bitmap, RIGHT+134, DOWN-10, s_de,    BIT(m_reg[0x3c], 3) ? color[1] : color[0]);

	pocketc_draw_special(bitmap, RIGHT,     DOWN-10, s_shift, BIT(m_reg[0x3d], 1) ? color[1] : color[0]);
	pocketc_draw_special(bitmap, RIGHT+146, DOWN-10, s_rad,   BIT(m_reg[0x3d], 2) ? color[1] : color[0]);

	pocketc_draw_special(bitmap, RIGHT+38,  DOWN-10, s_pro,   BIT(m_reg[0x3e], 0) ? color[1] : color[0]);
	pocketc_draw_special(bitmap, RIGHT+53,  DOWN-10, s_run,   BIT(m_reg[0x3e], 1) ? color[1] : color[0]);
	pocketc_draw_special(bitmap, RIGHT+68,  DOWN-10, s_rsv,   BIT(m_reg[0x3e], 2) ? color[1] : color[0]);

	return 0;
}

uint32_t pc1260_state::screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	// Started as a near-copy of pc1251_state::screen_update() above, but has
	// since diverged substantially: PC-1260/1261's own two-line, 24-column
	// layout (vs. pc1251_state's single line), and indicator positions/
	// styles based on a user-supplied photo of real PC-1260/1261 hardware
	// (see the comments further down) rather than pc1251_state's inherited,
	// never-independently-confirmed guesses.
	//
	// pc1251_state's version reads m_reg[0x3e] bits 0/1/2 for those three
	// lights -- an old, never-confirmed guess (see the "1 pro?, 2 run?,
	// 4rsv?" comment there). That's wrong for two independent reasons, only
	// the second of which is fixable by reading a different CPU register:
	//
	// 1) Per the Ditze "PC-1261/1260 ML & System Info" Systemhandbuch (p.17,
	//    "Tabelle für Modus-Flag"), the documented mode register lives at
	//    0x203c (== m_reg[0x3c] under this driver's 0x2000-0x20ff LCD-window
	//    mapping) with its high nibble holding a single value -- 1=RSV,
	//    2=PRO, 8=RUN -- not three independent flag bits at 0x3e.
	//
	// 2) More fundamentally: sandbox tracing showed the ROM's own boot code
	//    never actually samples the MODE dip switch into 0x203c in the first
	//    place. cpu1260.rom reads port B (the SC61860 "IN B" opcode) at only
	//    a handful of fixed addresses during boot, and in every one of them
	//    the preceding "OUT B" value has bits 0, 2, or 5 set -- never bit 3,
	//    which is the only bit this driver's in_b_r() checks before OR-ing
	//    m_mode into the result (see in_b_r() in pc1251_m.cpp). So none of
	//    the ROM's actual port-B reads ever see the switch's value at all;
	//    0x203c's high nibble ends up fixed at 8 regardless of switch
	//    position (confirmed by booting with all four MODE settings baked
	//    into a pre-boot .cfg and diffing 0x203c: identical every time). The
	//    handful of "mode-flag"-looking routines around 0x18e0-0x1933 that
	//    write bit 7 of 0x203c turned out on closer reading to be unrelated
	//    self-test/comparison logic, not switch sampling.
	//
	//    This means the visible PRO/RUN/RSV indicator most likely isn't
	//    CPU-driven at all on real hardware -- it reads the physical switch
	//    directly, the same way this driver already reads the contrast DIP
	//    switch (m_dsw0) straight into screen_update() below rather than via
	//    any ROM-visible register. So the fix here reads m_mode directly
	//    too, instead of chasing where in the ROM a working sample of it
	//    might be hiding (there may not be one).
	//
	// SHIFT/DEF/SMALL's register bits, unlike DEG/RAD/GRAD below, turned out
	// to be genuinely wrong (not just unconfirmed) -- empirically confirmed,
	// first for SHIFT/DEF by holding each real key in a live sandbox run and
	// diffing 0x203c/0x203d against a baseline every 5 frames:
	//   SHIFT (":KEY1"/"SHIFT") held alone: only 0x203d bit 5 (0x20) changes.
	//   DEF   (":KEY2"/"DEF")   held alone: only 0x203d bit 6 (0x40) changes.
	// (An earlier pass mistakenly renamed DEF's on-screen glyph to "SMALL",
	// reading a real photo's key-cap legend rather than what the LCD itself
	// shows -- corrected back to "DEF" per user report; it's the exact same
	// indicator/bit pc1251_state's own s_def already reads on PC-1250/1251,
	// just repositioned here for PC-1260/1261's two-line layout instead of
	// pc1251_state's single-line one.)
	//
	// SMALL's real bit stayed unconfirmed through several live-sandbox
	// search attempts (see the long "Modified files" log entry for what was
	// tried and why each came up empty) until the user, unable to run a
	// BASIC program to automate a search (blocked on a separate, deferred
	// ERROR 1 issue), instead directly PEEKed 0x203d by hand in all four
	// SHIFT/small-character combinations:
	//   &00 large chars, SHIFT not held   &20 large chars, SHIFT held
	//   &10 small chars,  SHIFT not held   &30 small chars,  SHIFT held
	// 0x20 matches the already-confirmed SHIFT bit exactly, and it's plainly
	// additive with a second, independent bit -- 0x10, bit 4 -- that's set
	// whenever small-character mode is active, regardless of SHIFT. That's
	// SMALL's real bit.
	//
	// All three are now read from their confirmed bits below instead of the
	// old inherited guesses (0x3d bit 1 for SHIFT, 0x3c bit 0 for DEF; SMALL
	// didn't inherit a guess at all, since pc1251_state has no equivalent
	// indicator).
	//
	// DEG/RAD/GRAD's bits are still exactly the inherited pc1251_state
	// guesses, not independently confirmed the same way -- untouched here.
	int color[2] =
	{
		7, //pocketc_colortable[PC1251_CONTRAST][0],
		8, //pocketc_colortable[PC1251_CONTRAST][1]
	};

	bitmap.fill(11, cliprect);

	// See pc1251_state::poll_mode_switch_key()'s own comment for what this
	// does and why it's needed at all (no built-in MAME key-remap covers a
	// dip switch); PC-1260/1261/1262 read the switch's resulting value
	// directly, below, rather than through any CPU-visible register --
	// see the long comment on that further down.
	poll_mode_switch_key();

	// PC-1260/1261-specific vertical layout, packed to fit inside this
	// machine's own artwork (artwork/pc1260.zip -- pc1260.lay/pc1260.png).
	// DOWN/RIGHT above are shared by pc1251_state::screen_update() too
	// (PC-1250/1251/1255/TRS-80-PC-3, each with their own single-line
	// artwork calibrated around DOWN=62), so they're deliberately left
	// alone here rather than changed in place -- changing them would have
	// shifted every other machine sharing this file's rendering too; this
	// function uses its own local x0/down1/down2 instead (below).
	//
	// Measured directly from a sandbox render of pc1260.lay (the LCD glass
	// cutout is opaque art, not a transparent hole, but the raw screen
	// bitmap coordinate space maps 1:1 onto the final snapshot pixel space
	// here since MAME's snapshot resolution defaults to the screen's own
	// declared size -- set_size(608, 300) both above and below -- so the
	// artwork's internal stretch-to-640x333-then-scale-back-to-608x300 is a
	// no-op and raw draw coordinates land exactly where they're drawn):
	// scanning the glass's own green content color column by column and
	// row by row (not the earlier passes' rougher eyeballing) gives an
	// exact content rectangle of x=49-498, y=47-91/92 -- notably shorter
	// top-to-bottom than the "y=47-101" estimate used by earlier passes,
	// which mistook part of the glass's own ~13px-thick gray border band
	// (y=93-105) for glass content.
	//
	// That correction is what exposed the real problem the user reported:
	// with pc1251_charlayout's 21px-tall characters (sized for PC-1250/
	// 1251's single-line display), two rows plus even a minimal gap already
	// use the *entire* 45px of actual glass height (47 to 91/92) on their
	// own, leaving nothing for the BUSY/PRINT/DEG/RAD/GRAD indicator row
	// below them -- which is why that row was landing on the border/label
	// area instead of the glass, no matter what y it was given. Fixed at
	// the font level instead of by further squeezing this layout:
	// pc1260_state::pc1260() now swaps in gfx_pc1260 (see pocketc.cpp),
	// using pc1260_charlayout's 14px-tall glyphs in place of
	// pc1251_charlayout's 21px ones -- same character ROM data, just a 2x
	// vertical stretch instead of 3x. That leaves real room to spare:
	constexpr int down1 = 47;         // line 1 chars: 14px tall (ends 60)
	constexpr int down2 = 63;         // line 2 chars: 2px gap after line 1, 14px tall (ends 76) -- 15px clear of the glass's own y=91/92 bottom edge, plenty for the indicator row (below) plus real breathing room

	// Quadrant layout, per a live user test (typing the alphabet end to end
	// and screenshotting the real PC-1260 artwork): m_reg/m_reg2 (line 1's
	// and line 2's independent CPU-visible register windows, 0x2000-0x20ff
	// and 0x2800-0x28ff) each hold TWO 12-character halves, and the ROM
	// fills them in this order as text is typed: m_reg's first half, then
	// m_reg2's first half, then m_reg's second half, then m_reg2's second
	// half. That's NOT "line 1 window == top row, line 2 window == bottom
	// row" -- filling m_reg2's first half straight after m_reg's first half
	// (before m_reg's own second half) only makes sense if m_reg2's first
	// half is physically the CONTINUATION of the top row (top-right), and
	// m_reg's second half is actually the bottom row's start (bottom-left).
	// Confirmed visually: with the natural pre-fix mapping (m_reg full =
	// top row, m_reg2 full = bottom row), typing 26 letters showed "A-L" at
	// top-left, "M-X" at bottom-left, and "Y"/"Z" mirrored at the far right
	// of the top row -- i.e. exactly a diagonal top-left/bottom-left/
	// top-right/bottom-right fill order, not the top-left/top-right/
	// bottom-left/bottom-right a 2-line display should have. Swapping which
	// quadrant m_reg's second half and m_reg2's first half draw into (below)
	// makes typing continuously fill the whole top row before starting the
	// bottom row, like a normal 2-line, 24-column display.
	//
	// This also incidentally fixes the mirroring the user separately
	// reported for the top-right quadrant: m_reg's second half used to be
	// drawn in DEScending register-index order as x increased (a quirk
	// inherited from pc1251_state's single-line layout, wired for its own
	// hardware); now that this half is drawn at bottom-left instead, it's
	// read ASCENDING like every other quadrant, so nothing reads backwards
	// anywhere on this display.
	const int contrast = m_dsw0->read() & 7;

	// Horizontal start x for both text rows. Deliberately its own local
	// constant rather than the shared RIGHT macro (still =68, used above
	// only by pc1251_state's single-line screen_update()): the user
	// reported RIGHT's position overlapping the SHIFT/DEF margin text
	// (below) added earlier this session, since those weren't accounted
	// for when RIGHT was calibrated for machines that don't have them.
	// x0=75 clears DEF/SHIFT's own widest glyph (s_shift, 18px, drawn at
	// x=51) with a few px to spare (51+18=69, 6px short of 75).
	//
	// Character pitch (x increment) also nudged from the original 3px
	// inner-column/3px extra-per-character gap (18px/char) down to 3px/2px
	// (17px/char): unchanged column width (pc1260_charlayout above is only
	// shorter than pc1251_charlayout, not narrower), just a 1px-tighter gap
	// between characters -- enough that all 24 columns plus the x0=75
	// margin still fit inside the glass's measured right edge (x=498):
	// 75 + 24*17 = 483, 15px to spare.
	constexpr int x0 = 75;

	// Top row: m_reg's first half (top-left) then m_reg2's first half
	// (top-right) -- one continuous x sweep across the full 24-column row.
	int x = x0;
	int y = down1;
	for (int i = 0; i < 60; x += 2)
		for (int j = 0; j < 5; j++, i++, x += 3)
			m_gfxdecode->gfx(0)->opaque(bitmap, cliprect, m_reg[i], contrast, 0, 0, x, y);

	for (int i = 0; i < 60; x += 2)
		for (int j = 0; j < 5; j++, i++, x += 3)
			m_gfxdecode->gfx(0)->opaque(bitmap, cliprect, m_reg2[i], contrast, 0, 0, x, y);

	// Bottom row -- PC-1260/1261 only (pc1251_state's own screen_update()
	// above has no equivalent, since PC-1250/1251/1255/TRS-80-PC-3 only have
	// one line): m_reg's second half (bottom-left) then m_reg2's second half
	// (bottom-right), backed by lcd_write/lcd2_write respectively. m_reg2 is
	// "Anzeige-Speicher 3/4" in the Ditze Systemhandbuch, CPU addresses
	// 0x2800-0x28ff; that window existed since earlier this session (added
	// to fix the PC-1260 ERROR 1 arithmetic bug) but nothing rendered any of
	// it until this session, so anything the ROM wrote there -- including
	// the result of an immediate-mode calculation like "1+1" -- was silently
	// invisible rather than actually missing. User-confirmed end to end on
	// their own build: "1+1"+ENTER now both computes and is visible here.
	x = x0;
	y = down2;
	for (int i = 0x40; i <= 0x7b; x += 2)
		for (int j = 0; j < 5; j++, i++, x += 3)
			m_gfxdecode->gfx(0)->opaque(bitmap, cliprect, m_reg[i], contrast, 0, 0, x, y);

	for (int i = 0x40; i <= 0x7b; x += 2)
		for (int j = 0; j < 5; j++, i++, x += 3)
			m_gfxdecode->gfx(0)->opaque(bitmap, cliprect, m_reg2[i], contrast, 0, 0, x, y);

	// KANA/SMALL/SHIFT/DEF -- per a user-supplied photo of real PC-1262
	// hardware, SMALL/SHIFT/DEF sit stacked in the LCD's own left margin,
	// top-to-bottom, not one beside each text line and not sharing a strip
	// at the glass top like PRO/RUN/RSV used to (below). An earlier pass
	// got this wrong twice: first pairing only two of the three (SMALL/
	// SHIFT) one per text line, then -- misreading a key-cap legend as what
	// the LCD itself shows -- replacing SMALL with a repositioned DEF
	// instead of adding it as a genuine third indicator. User-corrected:
	// all three exist and stack SMALL/SHIFT/DEF top-to-bottom, reusing
	// SMALL and SHIFT's original x/row-aligned positions (the part that
	// was already right) and adding DEF as a new third row below them --
	// room the smaller pc1260_charlayout font (above) freed up.
	//
	// The user separately identified a fourth indicator, KANA, actually
	// placed above this stack -- top-to-bottom order KANA/SMALL/SHIFT/DEF
	// -- so SMALL/SHIFT/DEF each shift down one 11px slot (47->58->69->80)
	// from where they sat before, and KANA takes the row they vacated at
	// the glass content top (down1=47). All four stay at the glass's left
	// edge (x=51, matches the artwork's own x=49-498 glass bounds used for
	// down1/down2 above), still spaced 11px apart (5px glyph + 6px gap).
	// The bottom-most glyph (DEF) now ends at y=84, closer to the y=83
	// DEG/RAD/GRAD tick row below than the 3-indicator layout's old 9px
	// buffer -- but that tick row is bezel-printed artwork drawn at x=196+
	// (pc1260_state::screen_update(), further below), a completely
	// different column from this x=51 margin stack, so there's no actual
	// pixel overlap regardless of the reduced vertical gap.
	//
	// Bits: SHIFT (0x3d bit 5), DEF (0x3d bit 6), and SMALL (0x3d bit 4)
	// are all empirically confirmed -- see the long comment above for
	// SMALL's specifically (found by the user hand-PEEKing 0x203d in all
	// four SHIFT/small-character combinations, since a BASIC program to
	// automate the search wasn't available -- see below). The user
	// reported the old inherited SHIFT/DEF bits never lit up on a real key
	// press, and live testing confirmed why.
	pocketc_draw_special(bitmap, 51, 58, s_small, BIT(m_reg[0x3d], 4) ? color[1] : color[0]);
	pocketc_draw_special(bitmap, 51, 69, s_shift, BIT(m_reg[0x3d], 5) ? color[1] : color[0]);
	pocketc_draw_special(bitmap, 51, 80, s_def, BIT(m_reg[0x3d], 6) ? color[1] : color[0]);

	// KANA ("カナ") -- drawn with its own small loop rather than
	// pocketc_draw_special() above, since s_kana is 8 rows tall (real
	// katakana, per the user's correction -- see the long comment on
	// s_kana, pc1251.h) and that helper hard-codes 5. Unlike SMALL/SHIFT/
	// DEF above, deliberately reads the "LANG" dip switch directly
	// (m_lang, same ioport lcd_read()/lcd_write() pin 0x203d bit 3 to)
	// rather than m_reg[0x3d] bit 3 itself: the user reported that with
	// the register-bit version, flipping the dip switch live left the
	// indicator showing the old state until the next keypress -- because
	// m_reg[0x3d] only actually changes when the CPU touches that address
	// (via lcd_read()/lcd_write()), which normally only happens as part of
	// handling a keystroke, so nothing re-derives the stored byte the
	// instant the switch itself changes. Reading the switch directly here
	// -- the same pattern m_mode/m_dsw0 already use elsewhere in this
	// function for the RUN/PRO/RSV and contrast indicators -- makes this
	// indicator track the switch instantly, with no dependency on CPU
	// activity at all.
	{
		int kana_color = !BIT(m_lang->read(), 0) ? color[1] : color[0];
		for (int row = 0; row < 8; row++)
			for (int col = 0; s_kana[row][col]; col++)
				if (s_kana[row][col] != ' ')
					bitmap.pix(47 + row, 51 + col) = kana_color;
	}

	// RUN/PRO/RSV -- per the same photo, these are NOT LCD content at all on
	// real hardware: there's no glass segment for them anywhere. The switch
	// position is simply visible by looking at the physical slider, printed
	// right next to fixed RSV/PRO/RUN case labels on the bezel to the LCD's
	// right (this driver's own artwork/pc1260.zip bakes in the same
	// RSV/PRO/RUN labels and a static slider graphic at a fixed position,
	// since it's a raster image, not an animated switch). Since the
	// emulated slider can't actually move, a small highlight block is drawn
	// instead, directly over the switch's own physical body (not next to the
	// text labels, as an earlier pass had it -- the user pointed out with a
	// side-by-side crop that the indicator belongs ON the switch itself,
	// where the real slider knob would be, sized to roughly the same small
	// rectangle they marked).
	//
	// IMPORTANT CORRECTION to every other bezel measurement's comment in
	// this function ("same 1:1 raw-coordinate mapping used throughout"):
	// that assumption is wrong. `pc1260.png` is authored at 640x333, but
	// this screen is `set_size(608, 300)` (unchanged since pc1250), and a
	// composited `screen:snapshot()` -- confirmed empirically this pass to
	// actually capture the full backdrop-plus-screen view, not just the raw
	// framebuffer, contrary to what an earlier pass assumed -- shows the
	// backdrop image scaled down to fit that 608x300 canvas (~608/640=0.95x
	// horizontally, ~300/333=0.90x vertically) while THIS function's own
	// draw calls, already native to the 608x300 bitmap, land unscaled. So a
	// position measured directly from `pc1260.png`'s own pixels needs
	// scaling by (0.95, 0.90) before it matches where it actually lands
	// relative to the backdrop -- every other bezel tick in this function
	// was positioned by the unscaled `pc1260.png` coordinate instead, which
	// is why they're all *slightly* (order of a few px, worse to the right
	// side of the screen where the accumulated x error is largest) off from
	// dead center on their labels, even though "visually re-verified" at
	// the time called them close enough. Not revisited this pass -- out of
	// scope for what the user asked about here -- but flagged in case a
	// future pass wants to redo them properly.
	//
	// This element was instead measured directly in composited-snapshot
	// (post-scale) space, avoiding the conversion entirely: the switch's
	// black body spans x=567-589 (23px wide) and y=36-78 (43px tall, three
	// equal RSV/PRO/RUN bands ~14px each, confirmed against the backdrop's
	// own baked-in slider-knob highlight sitting right at the top of that
	// range, i.e. in the RSV band -- the real switch is photographed in the
	// RSV position). A plain filled rectangle is used rather than the
	// `s_line` glyph (designed for the DEG/RAD/GRAD bezel ticks' fixed 8px
	// width, too narrow and the wrong shape for this) so the block's own
	// size can be set directly, matching the size of the box the user drew
	// over their own screenshot to request it (proportionally: ~76% of the
	// switch's width, ~9% of its height): 17px wide (570-586, centered on
	// the switch) by 5px tall.
	//
	// PRO/RUN's *vertical* position needed a second pass: the first version
	// spaced the three bands assuming they filled the switch's full 43px
	// height evenly (centers at 43/57/72), but the user marked up a
	// screenshot showing RUN landing almost on the OFF bracket and PRO
	// sitting visibly low too -- only RSV (already independently confirmed
	// against the backdrop's own baked-in knob highlight, above) was
	// actually right. Recalibrated the same way as the box's own size: from
	// the user's own marked-up crop, this time measuring where they placed
	// all three colored boxes (red/blue/green for RSV/PRO/RUN) relative to
	// the switch body in *that* image, then reapplying those same fractions
	// (RSV center at 12.7% down the switch, PRO at 39.1%, RUN at 64.1%) to
	// this driver's own measured switch bounds (y=36-78) -- which lands the
	// three centers an even 10px apart (43/53/63) rather than the previous
	// pass's 14-15px guess, comfortably clear of the OFF bracket below.
	// Only the currently-selected one is drawn -- unlike the glass
	// indicators above, there's no real "off" state to show here
	// (background is the bezel's brown plastic, not glass, so there's no
	// matching "unlit" color to fall back on).
	const uint8_t mode_dip = m_mode->read() & 0x07;
	switch (mode_dip)
	{
	case 0x01: bitmap.fill(color[1], rectangle(570, 586, 41, 45)); break; // RSV (center 43, unchanged)
	case 0x02: bitmap.fill(color[1], rectangle(570, 586, 51, 55)); break; // PRO (center 53, was 57)
	case 0x00: bitmap.fill(color[1], rectangle(570, 586, 61, 65)); break; // RUN (center 63, was 72)
	default: break; // 0x04: switch centered between positions -- none selected
	}

	// BUSY/PRINT/DEG/RAD/GRAD, per the real PC-1260/1261 bezel: these are
	// NOT full words drawn inside the LCD glass either. Each is a short
	// printed label silk-screened on the bezel just below the glass, with a
	// small LCD tick mark (segment, not text) lit directly above its label,
	// still on the glass -- to point at it.
	//
	// y=83 (glyph fills y=85-87, only the bottom 3 of s_line's 5 rows are
	// lit) replaces the previous y=97: that was based on an earlier, wrong
	// measurement of the glass's own bottom edge (see the long layout
	// comment above -- the glass's real content area ends at y=91/92, not
	// y=101, so y=97 was landing on the border/label area below the glass
	// rather than on the glass itself, which is what the user reported).
	// y=83 sits well inside the corrected bounds, with the tick's lit rows
	// (85-87) still a few px clear of the glass's own y=91/92 bottom edge.
	//
	// x positions are unchanged by the above (a purely vertical fix) --
	// still re-measured from a snapshot of this driver's own rendering
	// (608x300, same 1:1 raw-coordinate space as everywhere else in this
	// function) by scanning for each label's own dark pixels directly in
	// the composited snapshot, independent of any real-hardware photo since
	// the tick and its label are baked into the same artwork/pc1260.zip's
	// pc1260.png:
	//   label centers:  BUSY=99  PRINT=149  DEG=200  RAD=233  GRAD=266
	//   tick x's (centered under each label, x = center - 4, half the 8px tick width):
	//                    96        146        196      229      263
	pocketc_draw_special(bitmap, 196, 83, s_line, BIT(m_reg[0x3c], 3) ? color[1] : color[0]); // DEG
	pocketc_draw_special(bitmap, 229, 83, s_line, BIT(m_reg[0x3d], 2) ? color[1] : color[0]); // RAD
	pocketc_draw_special(bitmap, 263, 83, s_line, BIT(m_reg[0x3c], 2) ? color[1] : color[0]); // GRAD

	// BUSY and PRINT have no register binding at all yet in this driver --
	// unlike DEG/RAD/GRAD above, pc1251_state's screen_update() never read
	// any bit for these two, so there's no even-unconfirmed guess to carry
	// forward. Left unimplemented (undrawn) rather than inventing a bit;
	// their re-measured bezel tick positions (BUSY=96 PRINT=146, same y=83)
	// are documented above for whenever the source register/bit is found.

	return 0;
}
