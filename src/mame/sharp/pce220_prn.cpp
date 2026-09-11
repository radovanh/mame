// license:BSD-3-Clause
// copyright-holders:Radovan Hrebicek
/****************************************************************************

    pce220_prn.cpp

    Sharp CE-126P thermal printer, for PC-E220 / PC-G850V's 11-pin bus

    See pce220_prn.h for the protocol background, source this is derived
    from, and the specific, documented ways this reimplementation departs
    from PockEmul's Cce126 reference.

****************************************************************************/

#include "emu.h"
#include "pce220_prn.h"

// device type definition
DEFINE_DEVICE_TYPE(CE126P_PRINTER, ce126p_printer_device, "ce126p_printer", "Sharp CE-126P printer")

//-------------------------------------------------
//  ce126p_printer_device - constructor
//-------------------------------------------------

ce126p_printer_device::ce126p_printer_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, CE126P_PRINTER, tag, owner, clock)
	, m_printer(*this, "printer")
	, m_delay_timer(nullptr)
	, m_select_timeout_timer(nullptr)
	, m_state(ST_IDLE)
	, m_busy(0)
	, m_dout(0)
	, m_xout(0)
	, m_ack(0)
	, m_shift(0)
	, m_bit_count(0)
	, m_device_code(0)
	, m_selected(false)
	, m_ctrl_pending(false)
{
}

//-------------------------------------------------
//  ce126p_printer_device - destructor
//-------------------------------------------------

ce126p_printer_device::~ce126p_printer_device()
{
}

//-------------------------------------------------
//  device_add_mconfig
//-------------------------------------------------

void ce126p_printer_device::device_add_mconfig(machine_config &config)
{
	PRINTER(config, m_printer);
}

//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void ce126p_printer_device::device_start()
{
	m_delay_timer = timer_alloc(FUNC(ce126p_printer_device::delay_tick), this);
	m_select_timeout_timer = timer_alloc(FUNC(ce126p_printer_device::select_timeout_tick), this);
	m_delay_timer->reset();
	m_select_timeout_timer->reset();

	save_item(NAME(m_state));
	save_item(NAME(m_busy));
	save_item(NAME(m_dout));
	save_item(NAME(m_xout));
	save_item(NAME(m_ack));
	save_item(NAME(m_shift));
	save_item(NAME(m_bit_count));
	save_item(NAME(m_device_code));
	save_item(NAME(m_selected));
	save_item(NAME(m_ctrl_pending));
}

//-------------------------------------------------
//  device_reset - reset up the device
//-------------------------------------------------

void ce126p_printer_device::device_reset()
{
	m_state = ST_IDLE;
	m_busy = m_dout = m_xout = 0;
	m_ack = 0;
	m_shift = 0;
	m_bit_count = 0;
	m_device_code = 0;
	m_selected = false;
	m_ctrl_pending = false;

	m_delay_timer->reset();
	m_select_timeout_timer->reset();
}

//-------------------------------------------------
//  enter_state - move to a new handshake state,
//  (re)arming whichever delay timer that state
//  needs
//-------------------------------------------------

void ce126p_printer_device::enter_state(uint8_t state)
{
	m_state = state;
	m_delay_timer->reset();
	m_select_timeout_timer->reset();

	switch (state)
	{
	case ST_IDLE:
		break;

	case ST_SETTLE:
		// fresh device-select attempt: clear the bit accumulator
		m_shift = 0;
		m_bit_count = 0;
		m_delay_timer->adjust(attotime::from_msec(15));
		break;

	case ST_SELECT_BIT_WAIT:
		// re-armed on every entry (first arrival from ST_SETTLE, or after
		// each inter-bit gap) so the 500ms watchdog measures from the last
		// bit actually seen, not from the start of the whole handshake
		m_select_timeout_timer->adjust(attotime::from_msec(500));
		break;

	case ST_SELECT_BIT_GAP:
		m_delay_timer->adjust(attotime::from_msec(2));
		break;

	case ST_MISMATCH_HOLD:
		m_delay_timer->adjust(attotime::from_msec(2));
		break;

	case ST_SELECTED_WAIT_BUSY_LOW:
		// No timer: matches Cce126::run()'s step 4, which just waits
		// (indefinitely, however long the host takes) for BUSY to read low
		// again after the select byte's last clock pulse.
		break;

	case ST_SELECTED_ACK_HOLD:
		m_delay_timer->adjust(attotime::from_msec(9));
		break;
	}
}

//-------------------------------------------------
//  shift_in_bit - LSB-first bit accumulator shared
//  by the select-byte and data-byte phases
//-------------------------------------------------

void ce126p_printer_device::shift_in_bit(uint8_t bit)
{
	m_shift >>= 1;
	if (bit)
		m_shift |= 0x80;
	m_bit_count = (m_bit_count + 1) & 7; // wraps to 0 once the 8th bit lands
}

//-------------------------------------------------
//  receive_byte - a full data byte has arrived
//  while this unit is selected; handle the
//  control-code-prefix / paper-feed framing from
//  Cce126::Printer() and forward everything else
//  to the attached printer_image_device
//-------------------------------------------------

void ce126p_printer_device::receive_byte(uint8_t data)
{
	logerror("CE126P: receive_byte raw=%02X ctrl_pending=%d\n", data, m_ctrl_pending ? 1 : 0);

	// Reproduces Cce126::Printer() exactly -- see the header comment's
	// 2026-09-11 note for why this isn't translated into anything more
	// ASCII-like any more (the earlier "0x20 after a prefix means paper
	// feed, remap to 0x0a" mapping was simply wrong).
	if (m_ctrl_pending && data == 0x20)
	{
		// Only this exact combination clears the pending prefix.
		m_ctrl_pending = false;
		logerror("CE126P: -> literal space (prefix consumed)\n");
		m_printer->output(0x20);
		return;
	}

	if (data == 0x0f || data == 0x0e || data == 0x03)
	{
		m_ctrl_pending = true; // stays set if it already was
		logerror("CE126P: -> control prefix, pending\n");
		return;
	}

	if (data == 0xf0)
	{
		// CE-126P's own reserved glyph code for a distinctively-drawn "0"
		// (confirmed via real-ROM LLIST captures -- see the header
		// comment's 2026-09-11 note) rather than plain ASCII 0x30. This
		// first cut targets a plain-text byte-sink capture file, not a
		// real glyph-table renderer, so there's no reason to carry the
		// hardware's own font code through to the file -- translate it to
		// the ordinary digit it represents instead.
		logerror("CE126P: -> output 30 (0xF0 zero-glyph translated)%s\n", m_ctrl_pending ? " (ctrl_pending still set)" : "");
		m_printer->output('0');
		return;
	}

	// A literal 0x0d (not behind a control-code prefix) is CE-126P's own
	// carriage-return/line-advance byte in Cce126::RefreshCe126() -- already
	// exactly what a plain-text capture wants, so no translation needed.
	// Note m_ctrl_pending is deliberately NOT cleared here if it was set --
	// matches Cce126::Printer()'s own behavior of leaving ctrl_char set
	// until a prefix is specifically followed by 0x20.
	logerror("CE126P: -> output %02X%s\n", data, m_ctrl_pending ? " (ctrl_pending still set)" : "");
	m_printer->output(data);
}

//-------------------------------------------------
//  out_busy - host-driven BUSY line (pin 4)
//-------------------------------------------------

void ce126p_printer_device::out_busy(uint8_t state)
{
	state &= 1;
	if (state == m_busy)
		return;

	bool const rising = (state == 1);
	m_busy = state;

	if (!m_printer->is_ready())
		return; // nothing attached -- stay off the bus entirely

	switch (m_state)
	{
	case ST_SELECT_BIT_WAIT:
		if (rising)
		{
			shift_in_bit(m_dout & 1);
			m_ack = 0;

			if (m_bit_count == 0) // 8th bit just landed
			{
				m_device_code = m_shift;
				m_selected = is_recognized_device_code(m_device_code);
				logerror("CE126P: select byte = %02X (%s)\n", m_device_code, m_selected ? "selected" : "not selected");

				if (m_selected)
				{
					// Cce126::run()'s step 4: ACK is already DOWN (just set
					// above); wait for BUSY to actually fall before raising
					// it again. See the header comment for why this settle
					// pair is back after briefly being removed.
					enter_state(ST_SELECTED_WAIT_BUSY_LOW);
				}
				else
				{
					enter_state(ST_MISMATCH_HOLD);
				}
			}
			else
			{
				enter_state(ST_SELECT_BIT_GAP);
			}
		}
		break;

	case ST_SELECTED_WAIT_BUSY_LOW:
		if (!rising) // BUSY has fallen -- the select byte's last clock pulse is over
		{
			m_ack = 1; // Cce126::run()'s step 4->5: explicit ACK=UP, not a toggle
			enter_state(ST_SELECTED_ACK_HOLD);
		}
		break;

	case ST_IDLE:
		if (m_selected)
		{
			// Data phase (Cce126::run()'s second block): every BUSY edge
			// toggles ACK as a handshake, and a rising edge additionally
			// clocks in one data bit from D_OUT.
			m_ack ^= 1;

			if (rising)
			{
				shift_in_bit(m_dout & 1);
				if (m_bit_count == 0) // 8th bit just landed
					receive_byte(m_shift);
			}
		}
		break;

	default:
		// mid-handshake states (ST_SETTLE / ST_SELECT_BIT_GAP /
		// ST_MISMATCH_HOLD / ST_SELECTED_ACK_HOLD) don't react to BUSY
		// here; they're only waiting on their own delay timer. (A BUSY
		// edge arriving during ST_SELECTED_ACK_HOLD isn't lost -- see the
		// catch-up check in delay_tick()'s ST_SELECTED_ACK_HOLD case. A
		// 2026-09-11 real-ROM capture with per-bit logging confirmed this
		// hold never actually receives more than the one edge the catch-up
		// check accounts for, so the design is fine as-is.)
		break;
	}
}

//-------------------------------------------------
//  out_dout - host-driven D_OUT line (pin 5);
//  just a level latch, sampled on a BUSY edge
//-------------------------------------------------

void ce126p_printer_device::out_dout(uint8_t state)
{
	m_dout = state & 1;
}

//-------------------------------------------------
//  out_xout - host-driven MT_OUT1 line (pin 7);
//  raising it starts a device-select attempt
//-------------------------------------------------

void ce126p_printer_device::out_xout(uint8_t state)
{
	state &= 1;
	if (state == m_xout)
		return;

	bool const rising = (state == 1);
	m_xout = state;

	if (!m_printer->is_ready())
		return;

	if (rising)
	{
		if (m_state == ST_IDLE)
		{
			logerror("CE126P: XOUT rising -- new select attempt starting (was %s)\n",
					m_selected ? "selected" : "not selected");
			enter_state(ST_SETTLE);
		}
		// a rising edge seen mid-handshake is not expected on real
		// hardware; ignored rather than restarting the sequence
	}
	else
	{
		if (m_state == ST_SETTLE)
			enter_state(ST_IDLE); // host dropped MT_OUT1 before the settle delay elapsed
	}
}

//-------------------------------------------------
//  delay_tick - fires when a state's own fixed
//  delay (15ms settle / 2ms bit gap / 2ms mismatch
//  hold) has elapsed
//-------------------------------------------------

TIMER_CALLBACK_MEMBER(ce126p_printer_device::delay_tick)
{
	switch (m_state)
	{
	case ST_SETTLE:
		// MT_OUT1 falling would already have reverted us to ST_IDLE, so if
		// we're still here it's still raised -- invite the select byte
		m_ack = 1;
		enter_state(ST_SELECT_BIT_WAIT);
		break;

	case ST_SELECT_BIT_GAP:
		m_ack = 1;
		enter_state(ST_SELECT_BIT_WAIT);
		break;

	case ST_MISMATCH_HOLD:
		m_ack = 0;
		enter_state(ST_IDLE);
		break;

	case ST_SELECTED_ACK_HOLD:
	{
		// Cce126::run()'s step 5->INIT_MODE: ACK drops unconditionally once
		// the hold elapses, and the data phase becomes live.
		m_ack = 0;
		enter_state(ST_IDLE); // m_selected is already true, set back in out_busy()

		// Catch-up check -- see the header comment's 2026-09-11 note. BUSY
		// was 0 when this hold began (that's the only way to get here); if
		// it reads 1 now, the host already raised it for the data phase's
		// first bit and has simply been holding it there waiting for us,
		// since real ROMs don't wait out anything resembling this hold.
		// Treat that pending level exactly like out_busy()'s ST_IDLE case
		// would have handled the rising edge that produced it.
		if (m_busy == 1)
		{
			m_ack ^= 1;
			shift_in_bit(m_dout & 1);
			if (m_bit_count == 0) // 8th bit just landed
				receive_byte(m_shift);
		}
		break;
	}

	default:
		break; // stale fire from a timer left running past a state change
	}
}

//-------------------------------------------------
//  select_timeout_tick - 500ms watchdog: gave up
//  waiting for the next select-byte bit
//-------------------------------------------------

TIMER_CALLBACK_MEMBER(ce126p_printer_device::select_timeout_tick)
{
	if (m_state == ST_SELECT_BIT_WAIT)
		enter_state(ST_IDLE);
}
