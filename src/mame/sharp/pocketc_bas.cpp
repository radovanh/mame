// license:GPL-2.0+
// copyright-holders:Radovan Hrebicek
#include "pocketc_bas.h"

#include <cctype>
#include <cstring>
#include <sstream>

namespace pocketc_bas {

namespace {

struct keyword
{
	const char *name;
	uint16_t token;
};

// ---------------------------------------------------------------------
// PC-1350 keyword table (single-byte tokens 0x80-0xFF).
//
// Mechanically extracted from POCKTOOL's bas2img_611c1d2.c, the
// `ident == IDENT_NEW_BAS` branch (tokenL=1), restricted to entries that
// apply unconditionally there -- i.e. excluding the pcId==1401/1421-only
// sub-blocks in that same branch, which are irrelevant to the PC-1350.
// Cross-checked against RSV1350.IMG's reserved-key byte dump (REC=0x81,
// INPUT=0xDF, PRINT=0xDE, IF=0xD4 all matched independently).
// ---------------------------------------------------------------------
const keyword pc1350_keywords[] = {
	{ "ABS", 0x99 }, { "ACS", 0x9E }, { "AHC", 0x8E }, { "AHS", 0x8D },
	{ "AHT", 0x8F }, { "AND", 0xA1 }, { "AREAD", 0xE1 }, { "ASC", 0xA4 },
	{ "ASN", 0x9D }, { "ATN", 0x9F }, { "BASIC", 0xEC }, { "BEEP", 0xC4 },
	{ "CALL", 0xCC }, { "CHAIN", 0xE5 }, { "CHR$", 0xA8 }, { "CLEAR", 0xC9 },
	{ "CLOAD", 0xB7 }, { "CLOSE", 0xBC }, { "CLS", 0xCE }, { "CONSOLE", 0xBF },
	{ "CONT", 0xB2 }, { "COS", 0x96 }, { "CSAVE", 0xB6 }, { "CUR", 0x89 },
	{ "CURSOR", 0xCF }, { "DATA", 0xDC }, { "DECI", 0x84 }, { "DEG", 0x9B },
	{ "DEGREE", 0xC1 }, { "DIM", 0xCB }, { "DMS", 0x9C }, { "END", 0xD8 },
	{ "EQU#", 0xB9 }, { "EXP", 0x93 }, { "FACT", 0x90 }, { "FOR", 0xD5 },
	{ "GCURSOR", 0xE6 }, { "GOSUB", 0xE0 }, { "GOTO", 0xC6 }, { "GPRINT", 0xE7 },
	{ "GRAD", 0xC3 }, { "HCS", 0x8B }, { "HEX", 0x85 }, { "HSN", 0x8A },
	{ "HTN", 0x8C }, { "IF", 0xD4 }, { "INKEY$", 0xAD }, { "INPUT", 0xDF },
	{ "INT", 0x98 }, { "LEFT$", 0xAB }, { "LEN", 0xA6 }, { "LET", 0xD6 },
	{ "LINE", 0xE8 }, { "LIST", 0xB4 }, { "LLIST", 0xB5 }, { "LN", 0x91 },
	{ "LOAD", 0xBE }, { "LOG", 0x92 }, { "LPRINT", 0xE2 }, { "MDF", 0x80 },
	{ "MEM#", 0xBA }, { "MEM", 0xAF }, { "MERGE", 0xB8 }, { "MID$", 0xAA },
	{ "NEW", 0xB1 }, { "NEXT", 0xD9 }, { "NOT", 0xA3 }, { "ON", 0xD3 },
	{ "OPEN$", 0xEE }, { "OPEN", 0xBB }, { "OR", 0xA2 }, { "PASS", 0xB3 },
	{ "PAUSE", 0xDD }, { "PEEK", 0xA7 }, { "PI", 0xAE }, { "POINT", 0xE9 },
	{ "POKE", 0xCD }, { "POL", 0x82 }, { "PRESET", 0xEB }, { "PRINT", 0xDE },
	{ "PSET", 0xEA }, { "RADIAN", 0xC2 }, { "RANDOM", 0xC0 }, { "RCP", 0x87 },
	{ "READ", 0xDB }, { "REC", 0x81 }, { "REM", 0xD7 }, { "RESTORE", 0xE4 },
	{ "RETURN", 0xE3 }, { "RIGHT$", 0xAC }, { "RND", 0xA0 }, { "ROT", 0x83 },
	{ "RUN", 0xB0 }, { "SAVE", 0xBD }, { "SGN", 0x9A }, { "SIN", 0x95 },
	{ "SQR", 0x94 }, { "SQU", 0x88 }, { "STEP", 0xD1 }, { "STOP", 0xDA },
	{ "STR$", 0xA9 }, { "TAN", 0x97 }, { "TEN", 0x86 }, { "TEXT", 0xED },
	{ "THEN", 0xD2 }, { "TO", 0xD0 }, { "TROFF", 0xC8 }, { "TRON", 0xC7 },
	{ "USING", 0xCA }, { "VAL", 0xA5 }, { "WAIT", 0xC5 },
};

// ---------------------------------------------------------------------
// PC-1360 keyword table (0xFE-prefixed 2-byte tokens).
//
// Mechanically extracted from bas2img_611c1d2.c's `ident == IDENT_EXT_BAS`
// branch (tokenL=2): the block shared with GRP_E (E500-series), the block
// shared with IDENT_E_BAS, and the pcId==1360-specific graphics keywords
// (GCURSOR/LINE/POINT/PRESET/PSET/AREAD/MEM). Four entries here (TO, ON,
// RND, OR) were previously only inferred from a real-memory line-record
// decode -- this extraction confirms all four exactly.
// ---------------------------------------------------------------------
const keyword pc1360_keywords[] = {
	{ "AUTOGOTO", 0xFE75 }, { "AUTO", 0xFE1A }, { "ARUN", 0xFE74 },
	{ "BASIC", 0xFE36 }, { "CHAIN", 0xFE67 }, { "COLOR", 0xFE44 },
	{ "CONSOLE", 0xFE24 }, { "COPY", 0xFE3D }, { "CROTATE", 0xFE6E },
	{ "CSIZE", 0xFE43 }, { "DEFDBL", 0xFE46 }, { "DEFSNG", 0xFE47 },
	{ "DSKF", 0xFEB1 }, { "ERL", 0xFEC1 }, { "ERN", 0xFEC0 },
	{ "ERROR", 0xFE78 }, { "GLCURSOR", 0xFE6C }, { "GRAPH", 0xFE41 },
	{ "INIT", 0xFE1D }, { "LF", 0xFE42 }, { "LLINE", 0xFE6A },
	{ "LTEXT", 0xFE40 }, { "LOC", 0xFEB3 }, { "NAME", 0xFE3E },
	{ "OPEN$", 0xFEE8 }, { "PAUSE", 0xFE5F }, { "RENUM", 0xFE19 },
	{ "RLINE", 0xFE6B }, { "SET", 0xFE3F }, { "SORGN", 0xFE6D },
	{ "TEXT", 0xFE37 }, { "ROT", 0xFE83 }, { "DECI", 0xFE84 }, { "HEX", 0xFE85 },
	{ "AREAD", 0xFE63 }, { "MEM", 0xFEAF },
	{ "GCURSOR", 0xFE68 }, { "LINE", 0xFE69 }, { "POINT", 0xFEAD },
	{ "PRESET", 0xFE35 }, { "PSET", 0xFE34 },
	{ "ABS", 0xFE99 }, { "ACS", 0xFE9E }, { "AHC", 0xFE8E }, { "AHS", 0xFE8D },
	{ "AHT", 0xFE8F }, { "AND", 0xFEA1 }, { "ASN", 0xFE9D }, { "ATN", 0xFE9F },
	{ "COS", 0xFE96 }, { "CUB", 0xFEBF }, { "CUR", 0xFE89 }, { "DEG", 0xFE9B },
	{ "DMS", 0xFE9C }, { "EXP", 0xFE93 }, { "FACT", 0xFE90 }, { "HCS", 0xFE8B },
	{ "HSN", 0xFE8A }, { "HTN", 0xFE8C }, { "INT", 0xFE98 }, { "LN", 0xFE91 },
	{ "LOG", 0xFE92 }, { "LOF", 0xFEB2 }, { "NCR", 0xFEB6 }, { "NPR", 0xFEB7 },
	{ "OR", 0xFEA2 }, { "PI", 0xFEAE }, { "POL", 0xFE82 }, { "RCP", 0xFE87 },
	{ "REC", 0xFE81 }, { "RND", 0xFEA0 }, { "SGN", 0xFE9A }, { "SIN", 0xFE95 },
	{ "SQR", 0xFE94 }, { "SQU", 0xFE88 }, { "TAN", 0xFE97 }, { "TEN", 0xFE86 },
	{ "XOR", 0xFEA5 },
	{ "AKCNV$", 0xFEE0 }, { "APPEND", 0xFE72 }, { "AS", 0xFE73 }, { "ASC", 0xFED0 },
	{ "BEEP", 0xFE29 }, { "CALL", 0xFE31 }, { "CHR$", 0xFEF0 }, { "CIRCLE", 0xFE6F },
	{ "CLEAR", 0xFE2E }, { "CLOAD", 0xFE16 }, { "CLOSE", 0xFE22 }, { "CLS", 0xFE50 },
	{ "CONT", 0xFE12 }, { "CONVERT", 0xFE1E }, { "CSAVE", 0xFE20 }, { "CURSOR", 0xFE51 },
	{ "DATA", 0xFE5E }, { "DEGREE", 0xFE26 }, { "DELETE", 0xFE1B }, { "DIM", 0xFE30 },
	{ "END", 0xFE5A }, { "EOF", 0xFEB0 }, { "ERASE", 0xFE3A }, { "FIELD", 0xFE48 },
	{ "FILES", 0xFE1C }, { "FOR", 0xFE57 }, { "GET", 0xFE4A }, { "GOSUB", 0xFE62 },
	{ "GOTO", 0xFE2B }, { "GPRINT", 0xFE33 }, { "GRAD", 0xFE28 }, { "HEX$", 0xFEF2 },
	{ "IF", 0xFE56 }, { "INKEY$", 0xFEE9 }, { "INPUT", 0xFE61 }, { "JIS$", 0xFEE2 },
	{ "KILL", 0xFE3C }, { "KLEN", 0xFED3 }, { "KMID$", 0xFEED }, { "KLEFT$", 0xFEEE },
	{ "KRIGHT$", 0xFEEF }, { "KACNV$", 0xFEE1 }, { "LEFT$", 0xFEEB }, { "LEN", 0xFED2 },
	{ "LET", 0xFE58 }, { "LFILES", 0xFE3B }, { "LIST", 0xFE14 }, { "LLIST", 0xFE15 },
	{ "LOCATE", 0xFE51 }, { "LOAD", 0xFE18 }, { "LPRINT", 0xFE64 }, { "LSET", 0xFE4B },
	{ "MDF", 0xFE80 }, { "MERGE", 0xFE17 }, { "MID$", 0xFEEA }, { "NEW", 0xFE11 },
	{ "NEXT", 0xFE5B }, { "NOT", 0xFEA3 }, { "ON", 0xFE55 }, { "OPEN", 0xFE21 },
	{ "OUTPUT", 0xFE71 }, { "PAINT", 0xFE70 }, { "PASS", 0xFE13 }, { "PEEK", 0xFEA4 },
	{ "POKE", 0xFE32 }, { "PRINT", 0xFE60 }, { "PUT", 0xFE49 }, { "RADIAN", 0xFE27 },
	{ "RANDOMIZE", 0xFE25 }, { "RANDOM", 0xFE25 }, { "READ", 0xFE5D }, { "REM", 0xFE59 },
	{ "RESTORE", 0xFE66 }, { "RETURN", 0xFE65 }, { "RIGHT$", 0xFEEC }, { "RSET", 0xFE4C },
	{ "RUN", 0xFE10 }, { "SAVE", 0xFE23 }, { "STEP", 0xFE53 }, { "STOP", 0xFE5C },
	{ "STR$", 0xFEF1 }, { "THEN", 0xFE54 }, { "TO", 0xFE52 }, { "TROFF", 0xFE2D },
	{ "TRON", 0xFE2C }, { "USING", 0xFE2F }, { "VAL", 0xFED1 }, { "WAIT", 0xFE2A },
	{ "WIDTH", 0xFE38 },
};

// ---------------------------------------------------------------------
// PC-G8xx/PC-E200/PC-E220 keyword table (0xFE-prefixed 2-byte tokens).
//
// Mechanically extracted from bas2img_611c1d2.c's `ident == IDENT_E_BAS`
// path (which PC-G8xx/E200/E220 all reach via pcgrpId == GRP_G): the
// GRP_G-specific block, the block shared with GRP_E (E500-series)
// (structured-programming keywords: CASE/SWITCH/WHILE/REPEAT/UNTIL/etc.,
// not present on PC-1360), and the main block shared with IDENT_EXT_BAS.
// Two keywords are deliberately NOT here even though the main block
// defines them: MERGE (bas2img explicitly returns "unsupported" for this
// model group, overriding the main block's entry) and LCOPY (gated
// behind an unrelated, undocumented-default CLI workaround flag in the
// source -- left out rather than guessed at).
//
// NOT YET WIRED to a MAME QUICKLOAD device -- table only, see the header.
// ---------------------------------------------------------------------
const keyword pcg_keywords[] = {
	// GRP_G-specific (PC-G8xx/E200/E220 command set)
	{ "BLOAD", 0xFE16 }, { "BSAVE", 0xFE20 }, { "FIX", 0xFEC7 },
	{ "LNINPUT", 0xFE63 }, { "VDEG", 0xFED3 }, { "DMS$", 0xFEF3 },
	{ "HDCOPY", 0xFE4C }, { "HEX", 0xFEF2 }, { "INP", 0xFEA6 },
	{ "MEM", 0xFEAF }, { "MOD", 0xFEC6 }, { "MON", 0xFE0F },
	{ "OUT", 0xFE45 }, { "PAUSE", 0xFE60 }, { "PIOGET", 0xFEA8 },
	{ "PIOSET", 0xFE48 }, { "PIOPUT", 0xFE49 }, { "RENUM", 0xFE17 },
	{ "SPOUT", 0xFE4A }, { "SPINP", 0xFE4B }, { "LCOPY", 0xFE1F },
	// Shared with GRP_E (structured-programming extensions)
	{ "CASE", 0xFE7D }, { "DEFAULT", 0xFE7E }, { "ELSE", 0xFE76 },
	{ "ENDIF", 0xFE4D }, { "ENDSWITCH", 0xFE7F }, { "FRE", 0xFEAF },
	{ "GCURSOR", 0xFE68 }, { "LINE", 0xFE69 }, { "POINT", 0xFEAD },
	{ "PRESET", 0xFE35 }, { "PSET", 0xFE34 }, { "REPEAT", 0xFE4E },
	{ "SWITCH", 0xFE7C }, { "UNTIL", 0xFE4F }, { "WEND", 0xFE7B },
	{ "WHILE", 0xFE7A },
	// Shared main block (also used by PC-1360's table above)
	{ "ABS", 0xFE99 }, { "ACS", 0xFE9E }, { "AHC", 0xFE8E }, { "AHS", 0xFE8D },
	{ "AHT", 0xFE8F }, { "AND", 0xFEA1 }, { "ASN", 0xFE9D }, { "ATN", 0xFE9F },
	{ "COS", 0xFE96 }, { "CUB", 0xFEBF }, { "CUR", 0xFE89 }, { "DEG", 0xFE9B },
	{ "DMS", 0xFE9C }, { "EXP", 0xFE93 }, { "FACT", 0xFE90 }, { "HCS", 0xFE8B },
	{ "HSN", 0xFE8A }, { "HTN", 0xFE8C }, { "INT", 0xFE98 }, { "LN", 0xFE91 },
	{ "LOG", 0xFE92 }, { "LOF", 0xFEB2 }, { "NCR", 0xFEB6 }, { "NPR", 0xFEB7 },
	{ "OR", 0xFEA2 }, { "PI", 0xFEAE }, { "POL", 0xFE82 }, { "RCP", 0xFE87 },
	{ "REC", 0xFE81 }, { "RND", 0xFEA0 }, { "SGN", 0xFE9A }, { "SIN", 0xFE95 },
	{ "SQR", 0xFE94 }, { "SQU", 0xFE88 }, { "TAN", 0xFE97 }, { "TEN", 0xFE86 },
	{ "XOR", 0xFEA5 },
	{ "AKCNV$", 0xFEE0 }, { "APPEND", 0xFE72 }, { "AS", 0xFE73 }, { "ASC", 0xFED0 },
	{ "BEEP", 0xFE29 }, { "CALL", 0xFE31 }, { "CHR$", 0xFEF0 }, { "CIRCLE", 0xFE6F },
	{ "CLEAR", 0xFE2E }, { "CLOAD", 0xFE16 }, { "CLOSE", 0xFE22 }, { "CLS", 0xFE50 },
	{ "CONT", 0xFE12 }, { "CONVERT", 0xFE1E }, { "CSAVE", 0xFE20 }, { "CURSOR", 0xFE51 },
	{ "DATA", 0xFE5E }, { "DEGREE", 0xFE26 }, { "DELETE", 0xFE1B }, { "DIM", 0xFE30 },
	{ "END", 0xFE5A }, { "EOF", 0xFEB0 }, { "ERASE", 0xFE3A }, { "FIELD", 0xFE48 },
	{ "FILES", 0xFE1C }, { "FOR", 0xFE57 }, { "GET", 0xFE4A }, { "GOSUB", 0xFE62 },
	{ "GOTO", 0xFE2B }, { "GPRINT", 0xFE33 }, { "GRAD", 0xFE28 }, { "HEX$", 0xFEF2 },
	{ "IF", 0xFE56 }, { "INKEY$", 0xFEE9 }, { "INPUT", 0xFE61 }, { "JIS$", 0xFEE2 },
	{ "KILL", 0xFE3C }, { "KLEN", 0xFED3 }, { "KMID$", 0xFEED }, { "KLEFT$", 0xFEEE },
	{ "KRIGHT$", 0xFEEF }, { "KACNV$", 0xFEE1 }, { "LEFT$", 0xFEEB }, { "LEN", 0xFED2 },
	{ "LET", 0xFE58 }, { "LFILES", 0xFE3B }, { "LIST", 0xFE14 }, { "LLIST", 0xFE15 },
	{ "LOCATE", 0xFE51 }, { "LOAD", 0xFE18 }, { "LPRINT", 0xFE64 }, { "LSET", 0xFE4B },
	{ "MDF", 0xFE80 }, { "MID$", 0xFEEA }, { "NEW", 0xFE11 },
	{ "NEXT", 0xFE5B }, { "NOT", 0xFEA3 }, { "ON", 0xFE55 }, { "OPEN", 0xFE21 },
	{ "OUTPUT", 0xFE71 }, { "PAINT", 0xFE70 }, { "PASS", 0xFE13 }, { "PEEK", 0xFEA4 },
	{ "POKE", 0xFE32 }, { "PRINT", 0xFE60 }, { "PUT", 0xFE49 }, { "RADIAN", 0xFE27 },
	{ "RANDOMIZE", 0xFE25 }, { "RANDOM", 0xFE25 }, { "READ", 0xFE5D }, { "REM", 0xFE59 },
	{ "RESTORE", 0xFE66 }, { "RETURN", 0xFE65 }, { "RIGHT$", 0xFEEC }, { "RSET", 0xFE4C },
	{ "RUN", 0xFE10 }, { "SAVE", 0xFE23 }, { "STEP", 0xFE53 }, { "STOP", 0xFE5C },
	{ "STR$", 0xFEF1 }, { "THEN", 0xFE54 }, { "TO", 0xFE52 }, { "TROFF", 0xFE2D },
	{ "TRON", 0xFE2C }, { "USING", 0xFE2F }, { "VAL", 0xFED1 }, { "WAIT", 0xFE2A },
	{ "WIDTH", 0xFE38 },
};

struct model_traits
{
	const keyword *table;
	size_t table_len;
	bool two_byte;
	uint16_t rem_tok, goto_tok, gosub_tok, then_tok, on_tok;
};

const model_traits &traits_for(model m)
{
	static const model_traits k1350{ pc1350_keywords, std::size(pc1350_keywords), false,
		0xD7, 0xC6, 0xE0, 0xD2, 0xD3 };
	static const model_traits k1360{ pc1360_keywords, std::size(pc1360_keywords), true,
		0xFE59, 0xFE2B, 0xFE62, 0xFE54, 0xFE55 };
	// REM/GOTO/GOSUB/THEN/ON all come from the block shared with PC-1360's
	// table (see pcg_keywords above), so the PCG family's special token
	// values are identical to PC-1360's.
	static const model_traits kpcg{ pcg_keywords, std::size(pcg_keywords), true,
		0xFE59, 0xFE2B, 0xFE62, 0xFE54, 0xFE55 };

	switch (m)
	{
	case model::PC1350: return k1350;
	case model::PCG:    return kpcg;
	case model::PC1360:
	default:            return k1360;
	}
}

// Longest keyword match starting exactly at upper_rest[0]. Mirrors the
// real hardware's own behavior (and hence bas2img's): there is no word
// boundary check, since these machines restrict variable names to a
// single letter (optionally with one digit or a trailing '$'), so a
// multi-letter run is unambiguously either a keyword or not.
bool match_keyword(const model_traits &t, const char *upper_rest, size_t remaining, uint16_t &token, size_t &matched_len)
{
	size_t best_len = 0;
	uint16_t best_token = 0;
	for (size_t i = 0; i < t.table_len; i++)
	{
		size_t klen = std::strlen(t.table[i].name);
		if (klen > best_len && klen <= remaining && std::memcmp(upper_rest, t.table[i].name, klen) == 0)
		{
			best_len = klen;
			best_token = t.table[i].token;
		}
	}
	if (best_len == 0)
		return false;
	token = best_token;
	matched_len = best_len;
	return true;
}

std::string format_error(const char *fmt_prefix, unsigned long line_number, const std::string &detail)
{
	std::ostringstream oss;
	oss << fmt_prefix << " " << line_number << ": " << detail;
	return oss.str();
}

} // anonymous namespace

bool tokenize_program(model m, const std::string &source, std::vector<uint8_t> &out, std::string &error_message)
{
	const model_traits &t = traits_for(m);

	out.clear();

	std::istringstream lines(source);
	std::string raw_line;
	unsigned text_line = 0;

	while (std::getline(lines, raw_line))
	{
		text_line++;

		while (!raw_line.empty() && (raw_line.back() == '\r' || raw_line.back() == '\n'))
			raw_line.pop_back();

		size_t p = 0;
		while (p < raw_line.size() && raw_line[p] == ' ')
			p++;
		if (p >= raw_line.size())
			continue; // blank line

		size_t numstart = p;
		while (p < raw_line.size() && std::isdigit((unsigned char)raw_line[p]))
			p++;
		if (p == numstart)
		{
			error_message = format_error("Source line", text_line, "missing line number");
			return false;
		}
		unsigned long line_number = std::strtoul(raw_line.substr(numstart, p - numstart).c_str(), nullptr, 10);
		if (line_number > 65279)
		{
			error_message = format_error("Source line", text_line, "line number is too large (max 65279)");
			return false;
		}
		if (p < raw_line.size() && raw_line[p] == ' ')
			p++;

		std::vector<uint8_t> content;
		bool in_string = false;
		bool expect_line_ref = false;
		bool in_on_list = false;

		while (p < raw_line.size())
		{
			char c = raw_line[p];

			if (in_string)
			{
				content.push_back((uint8_t)c);
				if (c == '"')
					in_string = false;
				p++;
				continue;
			}
			if (c == '"')
			{
				in_string = true;
				content.push_back((uint8_t)c);
				p++;
				continue;
			}
			if (c == ' ')
			{
				// Spaces outside string literals carry no information on
				// these machines and are not stored -- confirmed against a
				// real MAME memory dump (e.g. "CLS : WAIT 0" tokenizes with
				// no 0x20 bytes at all around the stripped spaces).
				p++;
				continue;
			}
			if (c == ':')
			{
				// New statement: an ON...GOTO/GOSUB number list (and any
				// pending single jump-target expectation) doesn't reach
				// across a statement separator.
				in_on_list = false;
				expect_line_ref = false;
				content.push_back((uint8_t)c);
				p++;
				continue;
			}
			if (c == ',' && in_on_list)
			{
				// Separator inside an ON...GOTO/GOSUB target list: pass it
				// through literally WITHOUT falling into the "expect_line_ref
				// = false" reset below, so the next number is still treated
				// as a jump target (240,250,260 must all three get the 0x1F
				// encoding, not just the first).
				content.push_back((uint8_t)c);
				p++;
				continue;
			}

			if (expect_line_ref && std::isdigit((unsigned char)c))
			{
				size_t numend = p;
				while (numend < raw_line.size() && std::isdigit((unsigned char)raw_line[numend]))
					numend++;
				unsigned long target = std::strtoul(raw_line.substr(p, numend - p).c_str(), nullptr, 10);
				if (target > 65279)
				{
					error_message = format_error("Line", line_number, "jump target is too large (max 65279)");
					return false;
				}
				content.push_back(0x1F);
				content.push_back((uint8_t)(target >> 8));
				content.push_back((uint8_t)(target & 0xFF));
				p = numend;
				expect_line_ref = in_on_list; // stays armed only inside an ON-list
				continue;
			}
			expect_line_ref = false;

			std::string upper_rest;
			upper_rest.reserve(raw_line.size() - p);
			for (size_t i = p; i < raw_line.size(); i++)
				upper_rest.push_back((char)std::toupper((unsigned char)raw_line[i]));

			uint16_t token;
			size_t matched_len;
			if (match_keyword(t, upper_rest.c_str(), upper_rest.size(), token, matched_len))
			{
				if (token == t.rem_tok)
				{
					// REM: emit the token, drop exactly one following
					// space (bas2img's "delREMspc"), then copy the rest of
					// the line completely verbatim -- it's comment text,
					// not code, so no further tokenizing or space-removal.
					if (t.two_byte)
					{
						content.push_back((uint8_t)(token >> 8));
						content.push_back((uint8_t)(token & 0xFF));
					}
					else
					{
						content.push_back((uint8_t)token);
					}
					p += matched_len;
					if (p < raw_line.size() && raw_line[p] == ' ')
						p++;
					for (; p < raw_line.size(); p++)
						content.push_back((uint8_t)raw_line[p]);
					break;
				}

				if (t.two_byte)
				{
					content.push_back((uint8_t)(token >> 8));
					content.push_back((uint8_t)(token & 0xFF));
				}
				else
				{
					content.push_back((uint8_t)token);
				}

				if (token == t.on_tok)
					in_on_list = true;
				if (token == t.goto_tok || token == t.gosub_tok || token == t.then_tok)
					expect_line_ref = true;

				p += matched_len;
				continue;
			}

			// Plain literal byte: digit, identifier letter, operator,
			// punctuation, or parenthesis.
			content.push_back((uint8_t)c);
			p++;
		}

		content.push_back(0x0D);
		if (content.size() > 255)
		{
			error_message = format_error("Line", line_number, "tokenized content exceeds the 255-byte line limit");
			return false;
		}

		out.push_back((uint8_t)(line_number >> 8));
		out.push_back((uint8_t)(line_number & 0xFF));
		out.push_back((uint8_t)content.size());
		out.insert(out.end(), content.begin(), content.end());
	}

	// Trailing end mark -- matches bas2img's -e/--endmark switch (BAS_EOF_INCL),
	// confirmed against a real MAME memory dump to be required for a loaded
	// program to be recognized (see the project's tokenizer-format doc).
	out.push_back(0xFF);
	return true;
}

} // namespace pocketc_bas
