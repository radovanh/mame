// license:GPL-2.0+
// copyright-holders:Radovan Hrebicek
/*****************************************************************************
 *
 * pocketc_bas.h
 *
 * Native BASIC-text -> tokenized-line-record converter for the Sharp
 * pocketc family, sufficient to drive a MAME QUICKLOAD device for the
 * PC-1350 (single-byte tokens, IDENT_NEW_BAS in POCKTOOL's bas2img
 * terminology) and PC-1360 (0xFE-prefixed 2-byte tokens, IDENT_EXT_BAS).
 * A third table for the PC-G8xx/PC-E200/PC-E220 family (bas2img's GRP_G
 * grouping) is also included -- keyword data only for now, no MAME
 * QUICKLOAD device wiring yet, since PC-G850V/PC-E220 already have
 * working real 11-pin serial emulation (pce220_ser.cpp) and don't
 * currently need this as their only loading path the way PC-1350/1360
 * do. It's here so a future quickload for those models (their real
 * serial load is comparatively slow, ~1200 baud) doesn't need this
 * table rebuilt from scratch.
 *
 * Format and keyword tables were reverse-engineered from a real
 * third-party tool's source (POCKTOOL's bas2img_611c1d2.c, LGPL) and,
 * for the PC-1360 line-record layout and the leading/trailing 0xFF
 * program-area sentinel bytes specifically, cross-checked against a real
 * MAME memory dump of a manually-typed program. See
 * claude/pc1360-basic-tokenizer-format.md in the project for the full
 * writeup, worked examples, and open caveats (in particular: the
 * leading/trailing 0xFF sentinel behavior is hardware-confirmed for
 * PC-1360 only -- PC-1350 and the PCG family are assumed to share it,
 * not yet verified).
 *
 * Deliberately framework-free (no MAME/emu.h dependency) so it can be
 * unit-tested standalone; only src/mame/sharp/pocketc.cpp's driver code
 * (pc1350_m.cpp/pc1360_m.cpp) needs to know about address spaces/pointers.
 *
 * KNOWN LIMITATIONS (v1, not yet needed by any confirmed real-world case):
 *  - DATA statement contents are tokenized/space-stripped like any other
 *    statement, rather than being left as opaque literal text the way some
 *    BASIC dialects treat DATA -- no evidence either way from the sourced
 *    tool was found for this specific case.
 *  - Line numbers in the input are trusted to already be in ascending
 *    order (standard for a hand-written .BAS file); this tokenizer does
 *    not sort them.
 *  - Hex/octal numeric literals, multi-statement REM oddities, and a
 *    handful of model-specific keywords only relevant to *other* pocketc
 *    models (PC-1401/1403/1421/1470/1475/E500) were intentionally left
 *    out -- only PC-1350, PC-1360, and the PCG (GRP_G) family are
 *    targeted. Within the PCG table, LCOPY was left out entirely (its
 *    source is gated behind an unrelated CLI-option workaround flag with
 *    an undocumented default, so its availability couldn't be confirmed
 *    either way) and MERGE was deliberately excluded to match bas2img,
 *    which explicitly disables it for this model group.
 *
 ****************************************************************************/

#ifndef MAME_SHARP_POCKETC_BAS_H
#define MAME_SHARP_POCKETC_BAS_H

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pocketc_bas {

enum class model : uint8_t
{
	PC1350, // single-byte tokens 0x80-0xFF (POCKTOOL: IDENT_NEW_BAS, tokenL=1)
	PC1360, // 0xFE-prefixed 2-byte tokens  (POCKTOOL: IDENT_EXT_BAS, tokenL=2)
	PCG     // 0xFE-prefixed 2-byte tokens, PC-G8xx/PC-E200/PC-E220 family
	        // (POCKTOOL: IDENT_E_BAS + GRP_G, tokenL=2) -- table only, no
	        // quickload device wired up for this model yet.
};

// Tokenizes ASCII BASIC source text (one statement per line, conventional
// "<line number> <statement>" text format, LF or CRLF separated) into the
// Sharp pocketc line-record byte stream documented above for the given
// model. A single trailing 0xFF end-of-program marker (matching bas2img's
// -e/--endmark switch) is appended after the last line record; this
// byte stream does NOT include the separate leading 0xFF sentinel that
// belongs at the program-start pointer address itself -- that one byte is
// the caller's responsibility (see pc1350_state::quickload_cb() /
// pc1360_state::quickload_cb()).
//
// Returns true on success with `out` filled in. Returns false on any
// error (unparseable line number, a tokenized line over 255 content
// bytes, a jump target over 65279) and fills `error_message` with a
// human-readable, line-numbered diagnostic; `out` is unspecified in that
// case.
bool tokenize_program(model m, const std::string &source, std::vector<uint8_t> &out, std::string &error_message);

} // namespace pocketc_bas

#endif // MAME_SHARP_POCKETC_BAS_H
