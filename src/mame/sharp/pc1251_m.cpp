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

void pc1260_state::machine_start()
{
	pocketc_state::machine_start();

	m_ram_nvram->set_base(memregion("maincpu")->base() + 0x4000, 0x2800);

	uint8_t *gfx = memregion("gfx1")->base();
	for (int i = 0; i < 128; i++)
		gfx[i] = i;
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
	// anything, and a genuine cold boot never initializes them:
	//
	//   0x66e1/0x66e2 - BASIC program start pointer (low/high)
	//   0x66e3/0x66e4 - BASIC program end pointer (low/high)
	//   0x66fc/0x66fd - dimensioned/array variable storage pointer (low/high)
	//
	// Confirmed via live SC61860 tracing in a sandbox build: bas1260.rom's
	// NEW handler reads 0x66fc's low byte and requires it to be >= 0x0f,
	// erroring with ERROR 6 (memory overflow) otherwise; on a cold boot that
	// byte sits at 0x00, so NEW always fails this check before it can do
	// anything else. The documented (Sharp PC-1260/1261 memory maps: BASIC
	// program storage starts at &5880, and DIM'd array variables grow
	// downward from &64FF) default values for a freshly-cleared/empty
	// program are:
	//
	//   BASIC start = BASIC end = 0x5880 (the single 0xff terminator byte
	//   that marks an empty program serves as both pointers' target)
	//   array storage pointer = 0x64ff (nothing allocated yet)
	//
	// which is what's written below. This clears the confirmed ERROR 6 on
	// NEW. Note: this driver's RUN/PRO/RSV mode-switch simulation has a
	// separate, deeper, not-yet-root-caused issue (the ROM's internal mode
	// flag never reflects the "MODE" DIP switch no matter how/when it's set
	// -- confirmed by exhaustively logging every keyboard/mode port read
	// during boot and NEW), which independently causes ERROR 9 on NEW
	// ("not in PRO mode") in this sandbox's automated testing. That is
	// unrelated to the pointers fixed here and is not something a
	// machine_reset() override can address; setting the mode via MAME's own
	// DIP switch configuration (Tab menu / -dsw commands) rather than a
	// simulated switch flip may behave differently and was not re-verified
	// here.
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
		space.write_byte(0x66e1, 0x80);
		space.write_byte(0x66e2, 0x58);
		space.write_byte(0x66e3, 0x80);
		space.write_byte(0x66e4, 0x58);
		space.write_byte(0x5880, 0xff);
		space.write_byte(0x66fc, 0xff);
		space.write_byte(0x66fd, 0x64);
	}
}
