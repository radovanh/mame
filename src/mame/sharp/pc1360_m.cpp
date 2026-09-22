// license:GPL-2.0+
// copyright-holders:Peter Trauner
#include "emu.h"
#include "cpu/sc61860/sc61860.h"

#include "pocketc.h"
#include "pc1360.h"
#include "pocketc_bas.h"
#include "machine/ram.h"
#include "imagedev/snapquik.h"

#define LOG_BANK    (1U << 1)
#define LOG_SYSPORT (1U << 2)
#define LOG_STROBE  (1U << 3)
#define LOG_SERIAL  (1U << 4)
#define LOG_KEYSTB  (1U << 5)

#define VERBOSE (0)
#include "logmacro.h"

void pc1360_state::out_b_w(uint8_t data)
{
	m_outb = data;
}

void pc1360_state::out_c_w(uint8_t data)
{
}

uint8_t pc1360_state::in_a_r()
{
	int data = m_outa;
	int t = keyboard_line_r(0);

	// CONFIRMED, this time by direct instrumentation of a real ROM dump
	// running in the emulator (not just documentation): PD (0x3e00) is a
	// 1-INDEXED ROW NUMBER, not a bitmask. The internal-ROM keyboard scan
	// routine (ROM addresses 0x00fb-0x0110) writes exactly 1,2,3,4,5,6,7 to
	// 0x3e00 in a tight per-row loop (0 = idle/no row) -- confirmed by
	// logging every write during boot. The previous bitmask model
	// (BIT(t,bit), inherited from pc1350.cpp and "confirmed" against
	// Berwanger's Systemhandbuch table) could therefore only ever detect
	// rows 0-2 (t=1,2,4 are the only values in 1-7 with a single bit below
	// bit 3 set) -- rows 3-6 (which includes KEY6, e.g. the Y key) were
	// simply never reachable no matter what was pressed, which is why nothing
	// on those rows ever worked. This matches the service manual's gate-array
	// pin list, which documents KS1-KS8 as eight DISCRETE physical strobe
	// output pins -- i.e. real hardware decodes a row index into one-hot
	// strobes itself, rather than software addressing individual bits.
	// Verified empirically: with this fix, holding the Y key (KEY6) during
	// the boot-time confirmation prompt (see pocketgriffon's independent
	// account of a real "RAM CARD S1 CLEAR OK?" prompt requiring Y) makes
	// the boot sequence visibly advance -- the ROM bank register changes
	// from 3 to 0 and a large new block of VRAM content appears, versus
	// being permanently stuck beforehand. The remaining PA-driven rows
	// (KEY7-10 below) are unaffected -- PA truly is per-bit (the service
	// manual's IA1-IA8 are individual physical lines), so that part of the
	// original logic was already correct.
	// NOTE: an earlier session hypothesized that this row-5 self-test
	// (0x4189-0x41ac, comparing the readback against a hardcoded 0x55) was
	// the cause of the boot-time "RAM CARD S1 CLEAR O.K. ?" prompt showing
	// only its last 4 characters ("K. Y"/"K. ?"). That was tested directly
	// (forcing row 5 to read 0x55) and DISPROVEN -- it diverts the ROM into
	// a hidden factory/service menu instead of normal BASIC boot, so
	// self-test FAILURE (the plain behavior below) is the normal, intended
	// path. The real cause was a separate bug in the sc61860 core's DATA
	// opcode (sc61860_copy_int() in scops.hxx) -- see PC1360_DEBUG_STATUS.md.
	if (t >= 1 && t <= 7)
		data |= m_keys[t - 1]->read();

	// REMOVED (per user request): this used to auto-press Y (KEY6, row 6,
	// bit 0x40) a short while after reset to accept the boot-time "confirm
	// RAM clear" prompt automatically, via m_fakey/fakey_timer_tick(). The
	// user wants to press Y themselves like on real hardware, so that's
	// gone -- row 6 is now read purely from the real KEY6 ioport, same as
	// every other row above.

	// FIXME: only bits 0-3 of PA are wired to a key port here (KEY7-KEY10),
	// but direct instrumentation of m_outa during normal ROM execution (see
	// chat history) showed it cycling through ALL of bits 0-6, not just
	// 0-3 -- i.e. there are up to 3 more PA-selected rows (bits 4-6) that
	// this driver doesn't model at all, and any key living on one of them
	// would be completely unreachable, the same class of bug the PD/0x3e00
	// row-decode fix in this function addressed. A speculative attempt at
	// wiring these up (KEY11-13, one test bit each) found no effect on any
	// of them, so it was reverted rather than shipped half-confirmed -- see
	// chat history. MODE and LEFT/RIGHT have since been confirmed at KEY6
	// (0x02/0x04/0x08), and UP/DOWN have since been confirmed at KEY5
	// 0x04/0x08 (see INPUT_PORTS_START(pc1360) in pocketc.cpp) -- KEY5
	// 0x01/0x02 (the driver's original UP/DOWN guess) are not cursor keys
	// at all; 0x02 is the JAPAN-flag toggle and 0x01's real function is
	// still unidentified. Bits 4-6 here are still a real, uninvestigated
	// gap.
	for (int bit = 0, key = 7; bit < 4; bit++, key++)
		if (BIT(m_outa, bit))
			data |= m_keys[key]->read();

	// REVERTED (regression found by direct testing): this used to also force
	// data |= 0x08 here ("fake a CLS press at power up") whenever PA output
	// bit 1 was set during the first second after reset (m_power, see
	// pocketc_m.cpp's power_up_done()/machine_start()). That's exactly the
	// same PA bit (1) that legitimately selects KEY8's row in the loop just
	// above -- so for the first second of every boot, ANY real read of
	// KEY8's row (not just a deliberate CLS test) got bit 3 forced high
	// on top of it. The PC-1360's own boot self-test does several PA
	// row-select reads in that first second, and the forced bit corrupted
	// one of its read-back checks: verified in-sandbox with headless VRAM
	// dumps + PC-address sampling -- with this line active the CPU never
	// left a small loop of internal-ROM addresses and the display stayed
	// all-zero; with it removed the CPU reaches external ROM normally and
	// the display content (that was working before this file's keyboard
	// changes) reappears. A real CLS key press still works fine without
	// this line -- it's read like any other key by the KEY7-10 loop above
	// (KEY8, bit 0x08). pc1350.cpp has the analogous hack for its own,
	// differently-wired matrix; it wasn't touched.

	// FIXME: still an unverified guess, unrelated to the matrix table above
	// -- the manual documents the POWER switch as a software-polled fake
	// key but doesn't give its exact test condition.
	if (m_outa & 0xc0)
		data |= m_keys[11]->read();

	return data;
}

uint8_t pc1360_state::in_b_r()
{
	return m_outb;
}

/*
 * Shift-chord fix for clipboard paste. See the "SHIFT (paste)" phantom
 * field on the EXTRA port and the comment on KEY0 0x40 SHIFT, both in
 * pocketc.cpp, for the full background: the PC-1360's real SHIFT key does
 * not behave like a normal simultaneous-hold modifier -- confirmed by the
 * user from direct experience with real hardware, and independently
 * reproduced here via headless bit-sweep/latch testing (a bit-sweep of
 * every KEY0 bit combined with Q never once registered Q while KEY0 0x40
 * was simultaneously held, at either 200ms or 350ms per-field delay, ruling
 * out timing) -- it must be tapped and released before the next key is
 * pressed, matching the user's own description of a right-hand SHIFT key
 * that "holds down automatically" once tapped.
 *
 * MAME's natural-keyboard engine (natural_keyboard::timer() in
 * natkeyboard.cpp) only ever implements a simultaneous hold for a
 * multi-field character: it presses the shift field, then presses the base
 * field while the shift field is still held, then releases both together.
 * Rather than changing that generic engine (which every other PORT_CHAR
 * driver in MAME relies on), this phantom field intercepts just the shift
 * press for pc1360: it carries PORT_CHAR(UCHAR_SHIFT_1) so natural_keyboard
 * presses IT (never the real SHIFT key directly) to begin a chord, and its
 * PORT_CHANGED_MEMBER callback below converts that hold into a real
 * SHIFT tap-then-release, well before natural_keyboard's own per-field
 * delay (choose_delay()) presses the base character key afterwards.
 */
INPUT_CHANGED_MEMBER(pc1360_state::shift_chord_changed)
{
	// Only the phantom field's rising edge matters here -- that's
	// natural_keyboard beginning a chord. Its later release of this same
	// phantom field (the falling edge) has no real hardware bit behind it
	// and needs no action.
	if (!newval)
		return;

	// Tap the REAL SHIFT key (KEY0 0x40) on now...
	m_keys[0]->field(0x40)->set_value(1);

	// ...and release it again shortly afterwards, well before
	// natural_keyboard presses the base character key (see choose_delay()
	// in natkeyboard.cpp for pc1360's per-field delay), so the real
	// hardware never sees the two keys held down together.
	m_shift_pulse_timer->adjust(attotime::from_msec(150));
}

TIMER_CALLBACK_MEMBER(pc1360_state::release_shift_pulse)
{
	m_keys[0]->field(0x40)->clear_value();
}

/*
 * CONFIRMED (SharpPocketLib PC-1360 knowledgebase, cross-sourced from
 * gymfan.de and the Saretz/Berwanger books): real ROM-bank-select register
 * at 0x3400, bits 0-2 select one of the 8 x 16K external ROM banks (see
 * ROM_START(pc1360) in pocketc.cpp, region "user1", 0x20000 total) visible
 * at 0x4000-0x7fff. Real hardware also mirrors the selected bank back at
 * the very same address (a memory-mapped register, not a separate shadow
 * copy) -- i.e. it really is the simple read-back-what-was-written latch
 * modelled below, not a guess anymore.
 */
uint8_t pc1360_state::bank_r(offs_t offset)
{
	LOGMASKED(LOG_BANK, "pc1360 bank_r %.4x == %.2x\n", 0x3400 + offset, m_bank);
	return m_bank;
}

void pc1360_state::bank_w(offs_t offset, uint8_t data)
{
	m_bank = data & 7; // CONFIRMED: 3 bits, 8 banks
	membank("bank1")->set_base(memregion("user1")->base() + (m_bank << 14));
	LOGMASKED(LOG_BANK, "pc1360 bank_w %.4x <- %.2x (bank %d)\n", 0x3400 + offset, data, m_bank);
}

/*
 * CONFIRMED (knowledgebase): 0x3800 = 11-pin STROBE, bit0 only -- an
 * earlier pass at this same document described 0x3800 as a fuller
 * serial/printer control register, but a more detailed source narrows it
 * to just the strobe bit (the fuller register is 0x3a00, see serial_r/w
 * below). Modelled as a plain latch since the strobe pulse itself has no
 * emulated downstream effect (no printer/11-pin device modelled).
 */
uint8_t pc1360_state::strobe_r(offs_t offset)
{
	uint8_t data = m_strobe;
	LOGMASKED(LOG_STROBE, "pc1360 strobe_r %.4x == %.2x\n", 0x3800 + offset, data);
	return data;
}

void pc1360_state::strobe_w(offs_t offset, uint8_t data)
{
	m_strobe = data;
	LOGMASKED(LOG_STROBE, "pc1360 strobe_w %.4x <- %.2x\n", 0x3800 + offset, data);
}

/*
 * CONFIRMED (knowledgebase): 0x3a00 = serial/11-pin control register
 * (bit0 IO1-out, bit1 IO2-out, bit2 Dout-out, bit3 Din-out, bit4 ACK-out,
 * bit5 SIO ER-open, bit6 SIO RR, bit7 SIO RS). Modelled as a plain latch --
 * no serial/11-pin peripheral is emulated, so only read-after-write
 * consistency matters here.
 */
uint8_t pc1360_state::serial_r(offs_t offset)
{
	uint8_t data = m_serial;
	LOGMASKED(LOG_SERIAL, "pc1360 serial_r %.4x == %.2x\n", 0x3a00 + offset, data);
	return data;
}

void pc1360_state::serial_w(offs_t offset, uint8_t data)
{
	m_serial = data;
	LOGMASKED(LOG_SERIAL, "pc1360 serial_w %.4x <- %.2x\n", 0x3a00 + offset, data);
}

/*
 * CONFIRMED via disassembly of the real cpu-1360.rom: the boot code does
 * load/modify-a-bit/store sequences against 0x0034, 0x0038, 0x003a and
 * 0x003e (e.g. LIDP 0038; LDD; ORIA 40; STD -- read, set bit 6, write
 * back), with a RAM-resident shadow of the 0x0038 value at 0xbe2c. This
 * models the whole 0x0030-0x003f window as a simple latch: reads return
 * whatever was last written. That's almost certainly not 100% faithful to
 * the real hardware (we don't yet know what each bit actually controls
 * downstream -- display enable? contrast? something else?), but it's
 * enough for the CPU's own read-after-write logic to behave consistently
 * instead of reading stale ROM bytes, which is strictly better than
 * before. LOG_SYSPORT (enable via -debug and the logging options, or a
 * quick VERBOSE=1 rebuild) will show every access if you want to watch it.
 */
uint8_t pc1360_state::sysport_r(offs_t offset)
{
	uint8_t data = m_sysport[offset & 0xf];
	LOGMASKED(LOG_SYSPORT, "pc1360 sysport_r %.4x == %.2x\n", 0x30 + offset, data);
	return data;
}

void pc1360_state::sysport_w(offs_t offset, uint8_t data)
{
	m_sysport[offset & 0xf] = data;
	LOGMASKED(LOG_SYSPORT, "pc1360 sysport_w %.4x <- %.2x\n", 0x30 + offset, data);
}

/*
 * CONFIRMED (knowledgebase + direct ROM instrumentation, see in_a_r() in
 * this file): 0x3e00 = keyboard-strobe register. It holds a 1-INDEXED ROW
 * NUMBER (1-7 select PD rows KEY0-KEY6, 0 = idle/no row), not a bitmask --
 * the real internal-ROM scan routine writes exactly 1,2,3,...,7 here in a
 * tight loop. Real scanning protocol: write a row-select value here (plus
 * Port A row bits via out_a_w), then read Port A back (in_a_r(), via the
 * CPU's INA instruction) for the column bits. This replaces the previous
 * version of this function, which read m_reg[0xe00] -- a dead/never-hit
 * index left over from when the whole LCD window was mapped at the wrong
 * (guessed) base address 0x8000.
 */
uint8_t pc1360_state::keyboard_line_r(offs_t offset)
{
	uint8_t data = m_kb_strobe;
	LOGMASKED(LOG_KEYSTB, "pc1360 keyboard_line_r %.4x == %.2x\n", 0x3e00 + offset, data);
	return data;
}

void pc1360_state::keyboard_line_w(offs_t offset, uint8_t data)
{
	m_kb_strobe = data;
	LOGMASKED(LOG_KEYSTB, "pc1360 keyboard_line_w %.4x <- %.2x\n", 0x3e00 + offset, data);
}

void pc1360_state::machine_start()
{
	pocketc_state::machine_start();

	// Backs shift_chord_changed()/release_shift_pulse() above -- the
	// clipboard-paste shift-chord fix.
	m_shift_pulse_timer = timer_alloc(FUNC(pc1360_state::release_shift_pulse), this);

	membank("bank1")->set_base(memregion("user1")->base());

	address_space &space = m_maincpu->space(AS_PROGRAM);

	// CORRECTED: 0x8000-0xffff is flat, always-present 32K RAM, not a
	// size-dependent window with the remainder nop_readwrite'd. The
	// previous size-dependent scheme (4K -> base 0xf000, nop'ing
	// 0x8000-0xefff and similar for 8K/16K) was contradicted by (1) the
	// user directly testing address 0xe030 on the real device's debugger
	// and confirming it's live RAM there, and (2) Pokecom Go (a
	// confirmed-working reference PC-1360 emulator on the same ROMs)
	// mapping this whole range as one flat mainram[] array with its
	// optional RAM-card banking disabled. See the RAM(config, m_ram)
	// comment in pocketc.cpp's pc1360() for the full writeup.
	const uint32_t ram_size = m_ram->size();
	const uint32_t ram_base = 0x10000 - ram_size;

	space.install_ram(ram_base, 0xffff, m_ram->pointer());

	// FIX (2026-09-12): pc1360_mem()'s static address map declares the
	// WHOLE 0x8000-0xffff range as .ram() (a separate, always-present
	// auto-allocated buffer, unrelated to m_ram) -- that comment block
	// explains this was intentional for the single-size-only case this
	// driver had at the time. install_ram() above only overrides the
	// actual card window [ram_base, 0xffff] with m_ram's buffer; it does
	// NOT remove the static map's own RAM underneath the rest of the
	// range. So for any card smaller than 32K, addresses below ram_base
	// were still fully live, persistent RAM (confirmed empirically: a
	// byte written at 0x9000 with an 8K card read back correctly instead
	// of reading as absent) -- meaning the BASIC ROM's own memory-size
	// probe at boot always found a full 32K physically present no matter
	// what RAM() size was selected, so the MEM command never reflected
	// the selected card size. Unmapping the region below the real card's
	// window makes the address space match the per-size window table in
	// pc1360.h (4K/8K/16K/32K) exactly, so the ROM's own detection now
	// sees only the actually-installed window. No-op for the 32K default
	// (ram_base == 0x8000), preserving the already-hardware-confirmed
	// flat-32K behavior for that case untouched.
	if (ram_base > 0x8000)
		space.unmap_readwrite(0x8000, ram_base - 1);

	// Points directly at the RAM device's own buffer -- unlike the
	// pc1350_state::machine_start() pattern this was originally copied
	// from (which points m_ram_nvram at an offset into the "maincpu" ROM
	// region, i.e. not into the actual RAM at all, a pre-existing quirk in
	// that older driver), this persists the real RAM-card contents.
	m_ram_nvram->set_base(m_ram->pointer(), ram_size);
}

/*
 * QUICKLOAD_LOAD_MEMBER(pc1360_state, quickload_cb)
 * ---------------------------------------------------------------------
 * Loads a plain-text .BAS file selected via MAME's quickload file
 * manager, tokenizes it natively (pocketc_bas::tokenize_program(),
 * PC1360 model -- 0xFE-prefixed 2-byte tokens), and writes the result
 * directly into the program area at &FFD7/&FFD8 -- the direct-memory-
 * injection alternative to real 11-pin serial emulation explored in
 * claude/pc1350-pc1360-serial-feasibility.md, proved out by hand via the
 * MAME debugger first (see claude/pc1360-basic-tokenizer-format.md for
 * the full worked example this callback mechanizes).
 *
 * The leading 0xFF sentinel at the program-start address and the
 * "end pointer addresses the trailing 0xFF itself" convention are both
 * confirmed against a real MAME memory dump of a manually-typed program
 * (see the doc above) -- this callback asserts the leading sentinel
 * itself rather than trusting it's already there, specifically because
 * machine_start() above installs RAM but there is still no
 * machine_reset() override for this driver (unlike pc1350_m.cpp's), so
 * whether a genuine cold boot alone leaves that byte and the start/end
 * pointers in a sane state is still an open question -- this makes the
 * quickload path robust to that regardless of how it's eventually
 * resolved.
 */
std::pair<std::error_condition, std::string> pc1360_state::quickload_cb(snapshot_image_device &image)
{
	uint64_t size = image.length();
	std::vector<char> text(size);
	if (size > 0 && image.fread(text.data(), size) != size)
		return std::make_pair(std::errc::io_error, std::string("Error reading file"));

	std::vector<uint8_t> tokenized;
	std::string error;
	if (!pocketc_bas::tokenize_program(pocketc_bas::model::PC1360, std::string(text.data(), size), tokenized, error))
		return std::make_pair(std::errc::invalid_argument, error);

	address_space &space = m_maincpu->space(AS_PROGRAM);

	// Formula confirmed against 3 independent data points (fixed PC-1350
	// window, PC-1360 "MEM B"/"MEM C" RAM-card windows) -- see the doc.
	const uint32_t ram_base = 0x10000 - m_ram->size();
	const uint32_t expected_start = ram_base + 0x30;

	uint32_t start = space.read_byte(0xffd7) | (space.read_byte(0xffd8) << 8);
	if (start < ram_base || start >= 0x10000)
		start = expected_start; // cold-boot pointer wasn't sane -- fall back

	if (tokenized.size() > (0xffffU - start))
	{
		return std::make_pair(std::errc::file_too_large,
			util::string_format("Tokenized program (%u bytes) does not fit in the %u bytes free from %04X",
				(unsigned)tokenized.size(), (unsigned)(0xffffU - start), start));
	}

	space.write_byte(start, 0xff); // leading sentinel (see doc)
	for (size_t i = 0; i < tokenized.size(); i++)
		space.write_byte(start + 1 + i, tokenized[i]);

	const uint32_t end = start + uint32_t(tokenized.size());

	space.write_byte(0xffd7, start & 0xff);
	space.write_byte(0xffd8, (start >> 8) & 0xff);
	space.write_byte(0xffd9, end & 0xff);
	space.write_byte(0xffda, (end >> 8) & 0xff);

	return std::make_pair(std::error_condition(), std::string());
}
