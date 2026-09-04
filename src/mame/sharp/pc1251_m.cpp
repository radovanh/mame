// license:GPL-2.0+
// copyright-holders:Peter Trauner
#include "emu.h"
#include "cpu/sc61860/sc61860.h"

#include "pocketc.h"
#include "pc1251.h"

/* C-CE while reset, program will not be destroyed! */

void pc1251_state::out_b_w(uint8_t data)
{
	m_outb = data;
}

void pc1251_state::out_c_w(uint8_t data)
{
}

uint8_t pc1251_state::in_a_r()
{
	int data = m_outa;

	if (BIT(m_outb, 0))
	{
		data |= m_keys[0]->read();

		/* At Power Up we fake a 'CL' pressure */
		if (m_power)
			data |= 0x02;       // problem with the deg lcd
	}

	if (BIT(m_outb, 1))
		data |= m_keys[1]->read();

	if (BIT(m_outb, 2))
		data |= m_keys[2]->read();

	for (int bit = 0, key = 3; bit < 7; bit++, key++)
		if (BIT(m_outa, bit))
			data |= m_keys[key]->read();

	return data;
}

uint8_t pc1251_state::in_b_r()
{
	int data = m_outb;

	if (BIT(m_outb, 3))
		data |= m_mode->read() & 0x07;

	return data;
}

int pc1251_state::reset_r()
{
	return BIT(m_extra->read(), 1);
}

void pc1251_state::machine_start()
{
	pocketc_state::machine_start();

	m_ram_nvram->set_base(memregion("maincpu")->base() + 0x8000, 0x4800);

	uint8_t *gfx = memregion("gfx1")->base();
	for (int i = 0; i < 128; i++)
		gfx[i] = i;

	m_basic_init_timer = timer_alloc(FUNC(pc1251_state::basic_init_done), this);
}

TIMER_CALLBACK_MEMBER(pc1251_state::basic_init_done)
{
	// See the long comment in machine_reset() below for why this write is
	// deferred rather than done immediately on reset.
	address_space &space = m_maincpu->space(AS_PROGRAM);

	space.write_byte(0xc6a3, 0x3d);
	space.write_byte(0xc6a4, 0x50);
	space.write_byte(0xc6a5, 0x4c);
	space.write_byte(0xc6a6, 0x4a);
	space.write_byte(0xc6b1, 0xaf);
	space.write_byte(0xc6b2, 0xc7);
	space.write_byte(0xc6b3, 0x5a);
	space.write_byte(0xc6b5, 0xba);
	space.write_byte(0xc6b6, 0xf8);
	space.write_byte(0xc6ba, 0x58);
	space.write_byte(0xc6da, 0x21);
	space.write_byte(0xc6e7, 0x15);
	space.write_byte(0xc6e9, 0x09);
	space.write_byte(0xc6ea, 0xb2);
	space.write_byte(0xc6f1, 0xb0);
	space.write_byte(0xc6f2, 0x01);
	space.write_byte(0xc6f5, 0x40);
	space.write_byte(0xc6f7, 0x60);
	space.write_byte(0xc7b0, 0x55);
	space.write_byte(0xc7b1, 0x67);
}

void pc1251_state::machine_reset()
{
	pocketc_state::machine_reset();

	// Same class of bug as pc1350/pc1401 (see the long comments on their
	// machine_reset() overrides in pc1350_m.cpp/pc1401_m.cpp): on a genuine
	// cold boot, BASIC's system-variable area comes up all-zero instead of
	// what a real NEW leaves behind, so entering PRO mode and typing a
	// program corrupts things.
	//
	// pc1251_state is shared by five machines with different RAM sizes and
	// addresses (pc1245/pc1250/trs80pc3: 2KB at 0xc000; pc1251: 4KB at
	// 0xb800; pc1255: 10KB at 0xa000). The values below were derived
	// specifically for pc1251 (confirmed against a real working save state
	// the user captured, cross-checked byte-for-byte against a from-scratch
	// simulated cold-boot + NEW sequence in a sandbox build -- every byte
	// below is a confirmed difference between "just booted" and "booted,
	// then NEW", nothing here is guessed). They are NOT valid for the
	// other four machines (different RAM window, so these very addresses
	// may not even be mapped), so this is scoped to the pc1251 system only
	// rather than applied to the whole shared class.
	if (strcmp(machine().system().name, "pc1251") != 0)
		return;

	// Unlike pc1350/pc1401, this ROM's own cold-start code runs a pass
	// (within the first couple of video frames after reset) that clears a
	// handful of these exact bytes -- 0xc6b3/c6b5/c6b6/c6ba/c6da -- as part
	// of its normal system-variable init, regardless of what machine_reset()
	// wrote beforehand. Poking them here (synchronously, before the CPU has
	// executed a single instruction) gets silently overwritten a couple of
	// frames later: three of those five bytes (c6b3/c6b5/c6b6) happen to be
	// harmless live values the ROM re-derives on its own shortly after (so
	// clobbering them costs nothing), but the other two (c6ba/c6da) are
	// never re-derived without a real NEW and are confirmed (via a 200+
	// frame no-input trace) to sit at 0x00 forever otherwise -- which is the
	// actual corruption. So instead of writing directly, defer the whole
	// poke to fire shortly after reset, once the ROM's own init pass has
	// already run. 500ms is roughly 10 video frames at this driver's 20Hz
	// refresh -- far more than the 1-2 frames the init pass actually takes,
	// but still well under any realistic human reaction time for flipping
	// the MODE switch to PRO and typing.
	address_space &space = m_maincpu->space(AS_PROGRAM);
	if (space.read_byte(0xc6a3) == 0x00 && space.read_byte(0xc7b0) == 0x00)
		m_basic_init_timer->adjust(attotime::from_msec(500));
}

uint8_t pc1260_state::in_b_r()
{
	// Fixes a user-reported bug: SHIFT+8 ("small characters") sometimes
	// switches the PC-1260/1261 into an unwanted "Japanese mode" instead
	// (user-confirmed via PEEK: 0x203d reads 0x08 or 0x18, i.e. bit 3
	// set, when this happens; the bit the ROM actually wants to toggle
	// for "small" is bit 4).
	//
	// Traced via static ROM disassembly (bas1260.rom, $0000-$2000): the
	// SHIFT+8 handler at $11FD-$122A decides between toggling "SMALL"
	// (0x203d bit 4) and "Japanese mode" (0x203d bit 3) based on bit 7 of
	// a *different* status byte, 0x203c. That bit is driven by a strobe at
	// $1920-$1933 (only reached from the SHIFT+8 handler itself, via
	// $1201/$1205): it clears 0x203c bit 7, drives Port B's device-select
	// nibble to 4 (OUTB $..4), waits, reads Port B back at PC=$192C, and
	// -- only if bit 3 of that specific read does NOT come back set --
	// latches 0x203c bit 7 on again. No routine in the disassembled ROM
	// ever clears it outside this strobe.
	//
	// pc1251_state::in_b_r() only reflects the MODE switch into the read
	// value when the caller already drove bit 3 high (BIT(m_outb,3)
	// below); this probe always drives the select nibble to 4 (bit 3
	// clear), so with the unmodified base implementation it can never see
	// bit 3 come back set. Confirmed with a headless, zero-key-input
	// trace: 0x203c bit 7 latches on at PC=$1933, video frame 2 (well
	// under a second after boot), and never changes again for the rest of
	// the run -- i.e. every SHIFT+8 press picks "Japanese mode"
	// unconditionally in the unmodified driver, not merely "sometimes" as
	// reported on real hardware.
	//
	// A genuine (non-Japan-market) PC-1260/1261 must present bit 3 as
	// fixed-high on this specific readback for the ROM to ever leave
	// 0x203c bit 7 clear.
	//
	// IMPORTANT correction from an earlier pass this session: Port B bit 3
	// is NOT tested only here. A live PC-scoped trace (logging every
	// in_b_r() call's PC, added temporarily) found a second, much more
	// frequent probe at PC=$18ED/$18EE -- part of a shared routine at
	// $18DA-$1933, called from $114C near the top of the main keyboard
	// dispatch loop (recognizable by the many "JRM 113e" returns
	// throughout the character handlers) on essentially every loop
	// iteration (359 calls in a 4-second, single-keypress test run, vs.
	// exactly 1 call at PC=$192D for the actual SHIFT+8 strobe). That
	// probe's result feeds a *different* write, at $1159-1161, which
	// rewrites 0x203c's entire low nibble (bits 0-3) from a freshly
	// zeroed base -- and bit 3 of that nibble is the DEG indicator
	// (pc1260_state::screen_update() reads 0x203c bit 3 for DEG). A third,
	// low-frequency context (PC around $CBE8, ~18 calls/4s, purpose not
	// identified) also reads Port B.
	//
	// The original fix here forced bit 3 on *every* in_b_r() call whenever
	// LANG was "English/Export", which correctly fixed SHIFT+8 but also
	// silently corrupted the unrelated $114C/$18DA periodic probe -- this
	// is what caused a separate user-reported regression, DEG only ever
	// lighting up in "Japanese" mode (the one setting that left this
	// probe unforced/stock). Scoping the force to PC=$192D specifically
	// -- the one exact, confirmed call site the SHIFT+8 fix actually
	// needs -- fixes DEG without losing the SHIFT+8 fix: every other
	// Port B read (including the $18ED probe and the $CBE8 one) is now
	// left completely unforced/stock in both LANG settings, identically
	// to how "Japanese" mode already behaved before this correction.
	//
	// Made switchable (rather than always forced high) via the "LANG" dip
	// switch (pocketc.cpp) at the user's request, so the ROM's own
	// Japanese-mode branch can still be reached deliberately -- e.g. to
	// compare emulated behavior against a real Japan-market unit, or
	// against a real PC-1260/1261's SHIFT+8 behavior before this fix.
	// "English/Export" (0x01, the default) reproduces the fixed-high
	// signal described above; "Japanese" (0x00) leaves bit 3 exactly as
	// pc1251_state::in_b_r() would return it on its own (always low for
	// this specific probe, per the comment above), letting 0x203c bit 7
	// latch on and the SHIFT+8 handler take its Japanese-mode branch.
	uint8_t data = pc1251_state::in_b_r();

	// ---- MODE (RUN/PRO/RSV) switch: added pokecom-style per-strobe contacts ----
	//
	// pc1251_state::in_b_r() above already covers ONE of the three strobe
	// patterns pokecom's independent PC-1251/1261 reverse-engineering found
	// real hardware uses for this switch: BIT(m_outb, 3) -> OR the whole raw
	// "MODE" dip switch value in. Comparing directly against pokecom's
	// source (Sc61860_1251.java / Sc61860_1261.java inb() -- confirmed
	// byte-for-byte identical between those two models, strong evidence
	// PC-1251 and PC-1261 share the same physical switch wiring) shows that
	// call is only 1 of 3 branches a real unit responds to:
	//
	//   ibval & 8 (bit 3): PRO -> bit 1, RSV -> bit 0, RUN -> nothing
	//   ibval & 1 (bit 0): RSV -> bit 3, everything else -> nothing
	//   ibval & 2 (bit 1): PRO -> bit 3, everything else -> nothing
	//
	// (pokecom's "prog_mode" is 0=RUN/1=PRO/2=RSV; translated here to this
	// driver's own "MODE" dip switch encoding, 0x00=RUN/0x02=PRO/0x01=RSV/
	// 0x04=Off -- Off has no pokecom equivalent, since real PC-1251/1261
	// hardware apparently has no electrical "neither" position; treated the
	// same as RUN's rest state below, i.e. contributes nothing under either
	// new branch.) The first branch's bit target already matches
	// pc1251_state::in_b_r()'s own bit 0/1 OR under the same BIT(m_outb,3)
	// trigger (Off's extra bit 2 there has no pokecom counterpart, left
	// as-is rather than removed, since it's this driver's own addition, not
	// modeled behavior); what's missing is the other two branches entirely,
	// added below, additively, without touching the already-verified bit 3
	// case above.
	//
	// This matters because a live trace earlier this session (see the
	// SHIFT+8/KANA comment above) found PC-1260's actual boot code
	// strobing Port B with bits 0, 2, or 5 set at various points -- never
	// bit 3 -- which is exactly why the driver's on-screen RUN/PRO/RSV
	// indicator (pc1260_state::screen_update()) was built to read the
	// "MODE" ioport directly instead of any CPU-visible register: bit 3
	// being the only strobe this driver modeled, that register genuinely
	// never got written by this specific machine's ROM. Bit 0 is exactly
	// one of the three strobes that trace found -- previously unmodeled --
	// so the branch below gives the real boot-time IN B reads with outb
	// bit 0 set an actual chance to sample RSV for the first time. Bit 1 is
	// not one of the three observed boot-time strobes, but is added too for
	// completeness with pokecom's model and in case some other, not-yet-
	// traced ROM routine (e.g. after boot, inside PRO/RUN/RSV-specific
	// code) uses it.
	if (!BIT(m_outb, 3))
	{
		const uint8_t mode = m_mode->read() & 0x07;
		if (BIT(m_outb, 0) && mode == 0x01)        // RSV
			data |= 0x08;
		else if (BIT(m_outb, 1) && mode == 0x02)   // PRO
			data |= 0x08;
	}

	if (BIT(m_lang->read(), 0) && m_maincpu->state_int(SC61860_PC) == 0x192d)
		data |= 0x08;
	return data;
}

void pc1260_state::machine_start()
{
	pocketc_state::machine_start();

	m_ram_nvram->set_base(memregion("maincpu")->base() + 0x4000, 0x2800);

	uint8_t *gfx = memregion("gfx1")->base();
	for (int i = 0; i < 128; i++)
		gfx[i] = i;

	m_pointer_init_timer = timer_alloc(FUNC(pc1260_state::pointer_init_done), this);
}

void pc1260_state::machine_reset()
{
	pc1251_state::machine_reset();

	// Same class of bug as pc1251/pc1350/pc1401: on a genuine cold boot,
	// BASIC's system-variable pointers come up all-zero instead of what a
	// real NEW leaves behind, which corrupts things (LIST shows phantom
	// lines, RUN errors) once you switch to PRO and type a program.
	//
	// Unlike pc1251, this isn't about ROM/RAM-init leftovers scattered across
	// a wide system-variable table -- bas1260/bas1261.rom's NEW command
	// itself explicitly checks a handful of pointers before it will do
	// anything, and a genuine cold boot never initializes them. Originally
	// (see the correction log entry below for what changed) this was
	// believed to be just three 2-byte pointers:
	//
	//   0x66e1/0x66e2 - BASIC program start pointer (low/high)
	//   0x66e3/0x66e4 - BASIC program end pointer (low/high)
	//   0x66fc/0x66fd - dimensioned/array variable storage pointer (low/high)
	//
	// Confirmed via live SC61860 tracing in a sandbox build: bas1260.rom's
	// NEW handler reads 0x66fc's low byte and requires it to be >= 0x0f,
	// erroring with ERROR 6 (memory overflow) otherwise; on a cold boot that
	// byte sits at 0x00, so NEW always fails this check before it can do
	// anything else. Writing plausible values for just those three pointers
	// did clear that specific ERROR 6, but the user then reported real
	// corruption remained (LIST showing a garbage phantom line, freezing
	// briefly on typing a new line) until they manually typed NEW -- see the
	// correction log below for how the *actual* correct values (and the
	// missing fields above) were determined empirically, replacing the
	// guesses this comment used to describe.
	//
	// Scoped to pc1260/pc1261 only (both share this state class and both
	// were confirmed to hit the identical ERROR 6, despite pc1261's larger
	// RAM window -- the affected addresses sit in the system-variable area
	// both machines map identically).
	if (strcmp(machine().system().name, "pc1260") != 0 && strcmp(machine().system().name, "pc1261") != 0)
		return;

	address_space &space = m_maincpu->space(AS_PROGRAM);
	if (space.read_byte(0x66fc) == 0x00 && space.read_byte(0x66e1) == 0x00)
	{
		write_pointer_defaults();

		// 2026-09-02: user report -- despite the immediate write above (which
		// an exhaustive per-frame sandbox trace, both idle and with MODE
		// already at PRO at boot, confirmed stays correct and unclobbered for
		// the full length of several 10-second headless test runs, with no
		// ROM-side reversion ever observed), the user still found NEW
		// necessary to get a working pointer set after entering PRO mode on
		// their own build. The immediate write's *values* turned out to be
		// the actual problem (see the correction log below), not timing --
		// but this deferred re-assertion is left in place regardless, same
		// reasoning as when it was added: cheap, safe, only ever arms once
		// per genuine cold boot, and guards against any real-hardware/build
		// timing difference this headless sandbox doesn't reproduce.
		m_pointer_init_timer->adjust(attotime::from_msec(500));
	}
}

TIMER_CALLBACK_MEMBER(pc1260_state::pointer_init_done)
{
	// See the long comment in machine_reset() above. Unconditional --
	// unlike the write there, this isn't guarded on the bytes still being
	// zero, since by the time this fires (500ms after reset) legitimate
	// ROM activity may already have advanced them somewhat; the point of
	// this second pass is specifically to stomp back to the known-good
	// defaults regardless, the same way the immediate write does at reset.
	write_pointer_defaults();
}

void pc1260_state::write_pointer_defaults()
{
	// 2026-09-02, correction: the values below replace an earlier, narrower
	// guess (0x66e1-0x66e4 and 0x66fc/0x66fd only, end pointer == start
	// pointer, array pointer = 0x64ff) that cleared ERROR 6 on NEW but left
	// real corruption behind -- the user reported LIST still showing a
	// garbage phantom line ("57346: READ GOSUB INPUT N INPUT..." -- decoded
	// tokens read from uninitialized memory past where the empty-program
	// check should have stopped) and typing a new program line freezing
	// briefly then showing scrambled/cleared text, until NEW was run
	// manually. The user saved MAME save states from immediately before and
	// after running NEW on their own build and sent both; loading each in a
	// sandbox build (via the Lua `manager.machine:load()` API, which
	// exercises MAME's own state-restore code rather than hand-parsing the
	// file) and diffing the full 0x5800-0x67ff RAM window between them
	// revealed the earlier guess was wrong in two concrete ways and missing
	// several fields entirely:
	//
	//   - The "empty program" marker is TWO 0xff bytes (0x5880 AND 0x5881),
	//     not one -- 0x5881 sat at 0x00 (untouched cold-boot RAM) under the
	//     old fix, which is almost certainly why LIST kept reading past the
	//     terminator into garbage: whatever check the ROM does past the
	//     first 0xff apparently isn't satisfied by a single terminator byte.
	//   - The end pointer (0x66e3/0x66e4) is one past the terminator
	//     (0x5881), not equal to the start pointer (0x5880) -- the old fix's
	//     "start == end" was simply off by one.
	//   - The array/DIM storage pointer (0x66fc/0x66fd) is 0x6500, not
	//     0x64ff -- also off by one, same direction as the end pointer.
	//   - Several more fields the old fix never touched at all (left at
	//     cold-boot zero) get set by a real NEW: 0x6618-0x661c (a second,
	//     5-byte copy of the start/end pointer pair plus a leading flag
	//     byte -- structurally clean, high confidence), 0x66fe/0x66ff (a
	//     third 2-byte pointer immediately after the array pointer, same
	//     high-confidence reasoning), and 0x66e9/0x66ea/0x66f1 and 0x67b0
	//     (less obviously "pointer-shaped", but corroborated independently:
	//     these sit at the *exact same relative offsets* -- 0xe9/0xea, 0xf1,
	//     and 0xb0-on-the-next-page -- as confirmed fields in pc1251's own
	//     table above, basic_init_done(), including one identical value
	//     (0xb0) at the matching offset; PC-1251 and PC-1260/1261 clearly
	//     share the same underlying Sharp BASIC system-variable layout,
	//     relocated to a different base address, which is strong evidence
	//     these are genuine NEW-set defaults here too rather than coincidence).
	//
	// Deliberately NOT included, despite also differing between the two
	// captured states: 0x6627/0x6628, 0x6680, 0x66d8, and 0x66db-0x66dd.
	// Each of these already held a plausible-looking *nonzero* value in the
	// "before NEW" capture (unlike every field above, which sat at cold-boot
	// zero beforehand) -- i.e. something was already live-updating them
	// before NEW ran, which is far more consistent with an ordinary running
	// counter, clock tick, or PRNG seed than with a one-time NEW default.
	// Freezing those to one specific run's snapshot values risked doing more
	// harm than the (unconfirmed) good it might do, so they're left alone;
	// this can be revisited if the fields below turn out not to be the
	// whole story.
	address_space &space = m_maincpu->space(AS_PROGRAM);
	space.write_byte(0x5880, 0xff);
	space.write_byte(0x5881, 0xff);
	space.write_byte(0x6618, 0x03);
	space.write_byte(0x6619, 0x80);
	space.write_byte(0x661a, 0x58);
	space.write_byte(0x661b, 0x80);
	space.write_byte(0x661c, 0x58);
	space.write_byte(0x66e1, 0x80);
	space.write_byte(0x66e2, 0x58);
	space.write_byte(0x66e3, 0x81);
	space.write_byte(0x66e4, 0x58);
	space.write_byte(0x66e9, 0x0f);
	space.write_byte(0x66ea, 0xb3);
	space.write_byte(0x66f1, 0xb0);
	space.write_byte(0x66fc, 0x00);
	space.write_byte(0x66fd, 0x65);
	space.write_byte(0x66fe, 0x83);
	space.write_byte(0x66ff, 0x58);
	space.write_byte(0x67b0, 0xb1);
}
