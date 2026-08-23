// license:GPL-2.0+
// copyright-holders:Peter Trauner
#include "emu.h"
#include "cpu/sc61860/sc61860.h"

#include "pocketc.h"
#include "pc1360.h"
#include "machine/ram.h"

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
	if (t >= 1 && t <= 7)
		data |= m_keys[t - 1]->read();

	// Auto-accept the boot-time "confirm RAM clear" prompt (see the CONFIRMED
	// comment above) by faking a Y press a short while after reset, so the
	// machine reaches BASIC without the user needing to hold Y themselves.
	// Gated tightly on t==7 (row 6, Y's actual row) rather than on an
	// output-port bit -- unlike the old fake-CLS hack this replaced (see the
	// machine_start()-adjacent history in this file), this can't fire during
	// an unrelated read, since it only ever applies while the ROM is
	// genuinely strobing row 6 itself. m_fakey itself is sequenced by
	// fakey_timer_tick()/machine_start() -- see the comment on m_fakey in
	// pc1360.h for why this can't just be "true from t=0 for a few seconds":
	// empirically (headless MAME testing, holding/releasing the real KEY6
	// ioport field at different frame counts across many runs, with clean
	// nvram each time) the ROM ignores Y entirely if it already reads as
	// pressed in the first fraction of a second after reset -- it needs to
	// see a released->pressed transition, not just a held level. An initial
	// run of this feature that set m_fakey = true from machine_start()
	// onward (turning it off again after 4s) reproduced that exact failure
	// (bank register stuck at 3, VRAM stuck at its pre-boot content) even
	// though the injected bit was verified (via temporary logging) to be
	// firing on every single row-6 read throughout the window -- switching
	// to a released-then-pressed sequence fixed it (confirmed: bank register
	// reaches 0, VRAM gets a large new content block, matching a real,
	// manually-timed Y press).
	if (t == 7 && m_fakey)
		data |= 0x40; // KEY6 bit 0x40 = "Y"

	// FIXME: only bits 0-3 of PA are wired to a key port here (KEY7-KEY10),
	// but direct instrumentation of m_outa during normal ROM execution (see
	// chat history) showed it cycling through ALL of bits 0-6, not just
	// 0-3 -- i.e. there are up to 3 more PA-selected rows (bits 4-6) that
	// this driver doesn't model at all, and any key living on one of them
	// would be completely unreachable, the same class of bug the PD/0x3e00
	// row-decode fix in this function addressed. MODE (see the FIXME next
	// to it in pocketc.cpp's INPUT_PORTS_START(pc1360)) is the prime
	// suspect: it's confirmed to be inert wherever it's currently wired
	// within KEY7's row, and it might simply live on one of these unread
	// rows instead. Not yet investigated -- would need three more key ports
	// (KEY12-KEY14 or similar) and array-size/loop changes to test.
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

TIMER_CALLBACK_MEMBER(pc1360_state::fakey_timer_tick)
{
	if (m_fakey_phase == 0)
	{
		// Phase 0 -> 1: "press" Y now. m_fakey has been false (Y reads as
		// released) since reset, so the ROM sees a genuine release->press
		// transition here rather than Y appearing permanently stuck down --
		// see the long comment on this in in_a_r() for why that distinction
		// turned out to matter.
		m_fakey = true;
		m_fakey_phase = 1;
		m_fakey_timer->adjust(attotime::from_seconds(4));
	}
	else
	{
		// Phase 1 -> 2: release Y again, so it behaves like any other key
		// for the rest of the session instead of reading as permanently
		// held down (which would otherwise block real Y input, e.g. typing
		// an actual "Y" in BASIC, forever after boot).
		m_fakey = false;
	}
}

void pc1360_state::machine_start()
{
	pocketc_state::machine_start();

	membank("bank1")->set_base(memregion("user1")->base());

	// See in_a_r(): auto-accepts the boot-time confirm prompt. Two-phase
	// timing chosen empirically (headless MAME testing, holding/releasing
	// the real KEY6 Y ioport field at many different frame counts, clean
	// nvram each run):
	//   - Phase 0 (released) lasts 1.5s. This must be long enough for the
	//     ROM to have started up and be actively polling row 6 -- an edge
	//     delivered too early (before ~0.25s in sandbox testing) is missed
	//     entirely, so 1.5s leaves generous margin over that observed
	//     minimum. It must also not be so long that a real user trying to
	//     press Y themselves during this window would find it overridden --
	//     1.5s is well under the several-seconds humans took in practice.
	//   - Phase 1 (pressed) lasts a further 4s. The real scan loop that
	//     tests row 6 runs continuously, and in sandbox testing the prompt
	//     was reliably reached and accepted within a few hundred
	//     milliseconds of the press; 4s leaves ample margin.
	//   - Phase 2 (released) is permanent, so Y works normally afterward.
	m_fakey = false;
	m_fakey_phase = 0;
	m_fakey_timer = timer_alloc(FUNC(pc1360_state::fakey_timer_tick), this);
	m_fakey_timer->adjust(attotime::from_double(1.5));

	address_space &space = m_maincpu->space(AS_PROGRAM);

	// CONFIRMED (knowledgebase): RAM-card address window is size-dependent,
	// always ending at 0xffff and based at 0x10000-size -- 4K -> 0xf000,
	// 8K -> 0xe000, 16K -> 0xc000, 32K -> 0x8000. This replaces the
	// previous made-up tiered-window scheme (which didn't match any
	// documented real card layout).
	const uint32_t ram_size = m_ram->size();
	const uint32_t ram_base = 0x10000 - ram_size;

	space.install_ram(ram_base, 0xffff, m_ram->pointer());

	if (ram_base > 0x8000)
	{
		space.nop_readwrite(0x8000, ram_base - 1);
	}

	// Points directly at the RAM device's own buffer -- unlike the
	// pc1350_state::machine_start() pattern this was originally copied
	// from (which points m_ram_nvram at an offset into the "maincpu" ROM
	// region, i.e. not into the actual RAM at all, a pre-existing quirk in
	// that older driver), this persists the real RAM-card contents.
	m_ram_nvram->set_base(m_ram->pointer(), ram_size);
}
