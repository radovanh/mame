# PC-1360 MAME driver debugging — status handoff

Living record of an ongoing MAME `pocketc.cpp`/`pc1360_m.cpp` debugging session
for the Sharp PC-1360. Written so a **new chat** can pick this up cold. Read
this whole file before doing anything else if you're resuming.

## Environment / methodology (read this first)

- Working setup: a sandboxed clone of MAME lives at `/tmp/mame` in the cloud
  session (rebuilt fresh each new chat — nothing persists there). The user's
  REAL MAME checkout is on their Windows machine at `C:\mame`, reached via the
  remote-devices file bridge when their desktop app is open. Fixes get
  delivered with `SendUserFile` + `mcp__remote-devices__device_commit_files`
  to `C:\mame\src\mame\sharp\pocketc.cpp` / `pc1360_m.cpp`.
- Other connected folders on the user's machine (per last `get_device_info`):
  `C:\Users\Radovan Hrebicek\OneDrive\Documents\GitHub\mame`,
  `C:\Users\Radovan Hrebicek\OneDrive\Documents\GitHub\SharpPocketLib\.knowledgebase\Sharp\PC-1350-1360`,
  `C:\Users\Radovan Hrebicek\OneDrive\Documents\Pocket Computers\PC-1360`.
- User has a REAL PC-1360 and can cross-check emulator behavior against real
  hardware — use this. Several bugs in this project were only resolved by
  asking the user to test something on the real device.
- To build/run in the sandbox: `cd /tmp/mame && ./pocketc pc1360 ...` (a
  prebuilt binary named `pocketc` exists at the repo root; rebuild with the
  project's normal MAME build command if sources change — not yet needed to
  re-derive in a fresh session since findings below are already validated).
- **ALWAYS delete stale NVRAM before any test that needs a deterministic
  boot**: `rm -f /tmp/mame/nvram/pc1360/ram_nvram /tmp/mame/nvram/pc1360/cpu_nvram`.
  Leftover NVRAM from a previous run has caused multiple false
  positives/negatives this project (see "False leads" below) — this is the
  single most important gotcha.
- Launch flags used throughout: `-window -resolution 320x240 -nomax
  -skip_gameinfo -seconds_to_run N -autoboot_script /tmp/foo.lua`. Add
  `-debug -debugger none` to get headless access to `manager.machine.debugger`
  from Lua (MAME starts PAUSED under `-debug`, so the autoboot script MUST
  call `dbg:command("go")` around frame 5 or execution never advances).

### Lua/MAME debugger techniques that work (all verified this session)

- `manager.machine.debugger:command("<cmd>")` — run any debugger console
  command. Key ones: `trace <file>,maincpu,noloop` (full disassembled
  instruction trace to a file — the single most useful tool in this project;
  `trace off,maincpu` stops it), `pc=<addr>` (redirect PC), `go`, `fill
  <addr>,<len>,<data...>` (NOT addr,data,len), `map <addr>` (shows resolved
  read/write/fetch handler — critical for confirming a memory-map fix is
  live), `print <expr>` with `b@`/`w@`/`d@`/`q@` operators, `dasm
  <file>,<addr>,<len>` (static disassembly dump — reliable for ROM content
  but does NOT show which branches are actually taken at runtime; only a
  live `trace` or a write/read-tap shows real control flow).
- `cpu.spaces["program"]:install_read_tap(start,end,name,callback)` /
  `install_write_tap(...)` — log reads/writes with
  `cpu.state["GENPC"].value` for the exact PC. This was how the graphics bug
  (see below) was actually solved — tracing alone was ambiguous, taps gave
  the definitive answer.
- Keyboard input from Lua: there is NO natural-keyboard/PORT_CHAR support in
  this driver (the pc1360 input ports use `PORT_NAME`+`PORT_CODE` only, no
  `PORT_CHAR`), so `manager.machine.natkeyboard:post()` won't work. Instead
  poke ioport fields directly:
  ```lua
  local function press(portname, keyname, val)
    local port = manager.machine.ioport.ports[":"..portname]
    for name, field in pairs(port.fields) do
      if name == keyname then field:set_value(val) end
    end
  end
  -- press(port, exact_field_name, 1) ... a few frames later ... press(..., 0)
  ```
  Boot settles enough to accept input by ~frame 400-420 (20s @ 20fps-ish
  ticks in this harness's frame counting). Typing works fine with ~20-frame
  press+release+gap cadence; tested up to a 90-frame ENTER hold with no
  difference in outcome (rules out hold-timing as a factor in the PRINT bug
  below).
- Reading internal (P/Q-addressed) CPU RAM — this is a SEPARATE address space
  from the external program bus (`cpu.spaces["program"]`) and is NOT
  reachable via read/write taps or `prg:read_u8`. Use the MAME save-state
  item mechanism instead:
  ```lua
  local idx = nil
  for name, i in pairs(cpu.items) do
    if name:match("m_ram$") then idx = i end   -- keys come back as "0/m_ram" etc, hence match not ==
  end
  local item = emu.item(idx)
  item:read(0x33)   -- reads internal RAM offset 0x33
  ```
- Screenshots: `manager.machine.video:snapshot()` writes to
  `/tmp/mame/snap/pc1360/NNNN.png` (sequential numbering — clear the dir
  first with `rm -rf snap/pc1360 && mkdir -p snap/pc1360` so numbering is
  predictable per-run).
- `Date.now()`-equivalent nondeterminism gotcha: none observed from wall-clock
  sources, BUT see "OPEN: bank-switch nondeterminism" below — a repeat run
  with identical script/timing showed a different bank-switch trace than an
  earlier full-instruction trace of the "same" scenario. Not yet explained.
  Could be genuine run-to-run nondeterminism in this driver (a real bug) or
  an artifact of exactly which frame the debugger's `go` landed on. Treat any
  single trace as needing a second corroborating run before trusting a
  conclusion built on exact instruction sequence.

### Key ROM facts established (via MAME's OWN validated disassembler —
### cross-checked against live execution traces, not just static reads)

- SC61860 CPU, reset vector `PC=0x0000`, internal ROM (`cpu-1360.rom`,
  8192 bytes) always mapped `0x0000-0x1FFF`.
- Banked ROM window `0x4000-0x7FFF` (16KB), 8 banks (`b0`..`b7`-1360.rom, each
  16384 bytes), selected via `0x3400` (bit-masked, only low 3 bits matter for
  bank number — but the raw byte written there can carry other bit patterns
  that appear to be reused as scratch storage by some routines, e.g. traced
  writes to 0x3400 of 0x32, 0x37, 0x76, 0x73 etc. during keyboard-scan/typing
  that clearly aren't intended bank switches; only trust a bank switch when
  it's immediately followed by a `JP`/`CALL` into `0x4000-0x7FFF`).
- Flat 32K RAM `0x8000-0xFFFF`, always present (this was THE major fix
  delivered this session — see "Fixed and delivered" below).
- Graphics VRAM `0x2800-0x37FF` (driver's `lcd_read`/`lcd_write`), 5
  column-groups x 4 row-stripes x 512-byte column stride, double-mapped every
  256 bytes. Real 601-byte ROM clear pattern at internal ROM `0x67D7`
  (`LCD_CLEAR_ROM` in the user's own asm library replicates this exactly;
  the naive full-2560-byte clear the user's library used to have was
  confirmed BROKEN on real hardware — corrupts BASIC's resume-after-RTN state).
- `LCD_ON` = `0x13C8` (`LIA 01;LIP 5f;EXAM;OUTC;RTN`), `LCD_OFF` = `0x13C4`
  (`LIA 00;JRP 13ca` falls into the same tail). Both confirmed via live trace.
- `GFX_PSET_ENTRY` ROM routine = `0x5717`, lives in ROM BANK 2. Takes X
  (0-149) in internal RAM `$20`, Y (0-31) in internal RAM `$22` (P-addressed,
  via `LP`/`EXAM` — NOT DP-addressed, a PC-1403-derived gotcha the user's
  asm library correctly avoids). Mode byte at main-RAM `0xFD0F`
  (0=POINT,1=PSET,2=PRESET). Must select ROM bank 2 via `POKE 0x3400,2`
  immediately before calling and restore bank 0 after.
- **Undocumented GFX_PSET_ENTRY precondition, found and CONFIRMED this
  session**: `0x5717` has a gate at `0x5796-0x5799`
  (`LP 33;TSIM 04;JRNZP 57cc`) that checks internal RAM `$33` bit `0x04`. If
  set, the routine returns immediately via `RC;RTN` at `0x57CC` WITHOUT ever
  touching VRAM — silent no-op, exactly matching "LCD stays blank, nothing
  happens". This bit is NOT set/cleared anywhere in the user's
  `PLOT_PIXEL`/`SET_XY`/`SELECT_BANK2` — it's an undocumented dependency.
  Confirmed via full instruction trace AND a write-tap: clearing this bit
  (`LP 33;ANIM 0xFB`) immediately before `CALL 0x5717` makes the routine
  proceed all the way through and produce a real VRAM write
  (`0x280a <- 0x66` for a test X=10,Y=5 plot). Internal RAM resets to
  all-zero on CPU reset (`memset` in `sc61860_device::device_reset()`,
  confirmed in source), so this bit starts clear; something during normal
  boot/idle sets it to 1 by the time user code would typically call in
  (observed via live monitoring: `RAM[0x33]` flips `0x00`→`0xFF` at
  `frame=43, PC=0x4ff0` during boot and stays effectively 0xFF through at
  least frame 155). **This finding was reported to the user but not yet
  turned into a delivered code fix** — see "Pending decisions" below.
- Real boot sequence (via live trace from `PC=0`, NOT static guessing):
  `0x0000-0x000F` port-bit toggle + ON-key poll (`TEST 40`) → `0x009F`:
  explicit `CALL 0x13C4` (`LCD_OFF`) → sets bank `0x3400=3` → jumps into
  banked code `0x4001→0x412D` → hardware self-test/poll loop
  `0x4189-0x41AC` expecting a `0x55` response → inits status ports
  `0x3800/0x3A00/0x3C00` → clears main RAM from `0x8000` upward (`0x41B8:
  LIDP 8000; MVBD`...) → falls into keyboard-scan idle loop
  `0x00D5-0x00EE`. There's also a warm/cold-boot branch at `0x0022`
  (`LIDP 0000;LDD;CPIA 02`) that reads a signature byte at external address
  `0x0000` to decide whether to skip full init on a restart — **this is the
  natural place to look for the soft-reset bug below**, not yet
  investigated.
- Keyboard field-name map (from `pocketc.cpp` `INPUT_PORTS_START(pc1360)`,
  verified working): `1`=KEY4/"1", `+`=KEY2/"+", `5`=KEY3/"5",
  `ENTER`=KEY9/"ENTER", `P`=KEY10/"P     Alpha", `R`=KEY4/"R     $",
  `I`=KEY8/"I     Pi", `N`=KEY6/"N", `T`=KEY5/"T     %", space=KEY8/"SPC",
  `C`=KEY3/"C", `L`=KEY9/"L", `S`=KEY2/"S", `SHIFT`=KEY0/"SHIFT",
  `CLS`=KEY7/"CLS   CA" (dedicated key). Full port table is in
  `src/mame/sharp/pocketc.cpp` around line 759 (`INPUT_PORTS_START(pc1360)`).

## Fixed and delivered (confirmed correct, live on the user's C:\mame)

1. **RAM mapping bug — `0x8000-0xFFFF` must be flat, always-present 32K.**
   Root cause: driver modeled it as a size-dependent "RAM card" window
   (4K/8K/16K/32K options), `nop_readwrite`-ing everything below the card's
   base address (e.g. `0x8000-0xEFFF` unmapped for the 4K default). User
   directly tested address `0xE030` on real hardware's debugger (load/FILL)
   and confirmed it's live RAM there; also cross-confirmed against Pokecom Go
   (a working reference PC-1360 emulator using the same ROMs), which maps
   this whole range as one flat always-present array.
   - `src/mame/sharp/pocketc.cpp`, `pc1360()` machine_config: changed
     `RAM(config, m_ram).set_default_size("4K").set_extra_options("8K,16K,32K")`
     to `RAM(config, m_ram).set_default_size("32K")`.
   - `src/mame/sharp/pc1360_m.cpp`, `machine_start()`: removed the
     `if (ram_base > 0x8000) space.nop_readwrite(...)` block; now always
     `space.install_ram(0x8000, 0xffff, m_ram->pointer())`.
   - Verified: doesn't regress the existing regression suite (boot, typing,
     LEFT/RIGHT, MODE toggle, CLS) with clean NVRAM; confirmed independently
     on the user's real MAME build via `map e030` (showed `memory@8000`) then
     `print b@e030` after `fill e030,5,aa` showing `AA`.
   - **Does NOT fix the original "1+1 crash" report** — confirmed still
     present at the time (though see below, that "crash" characterization
     itself later turned out to not reproduce cleanly).

2. Diagnostic-only (no code change): the "fill doesn't seem to work" report
   turned out to be the user viewing MAME's graphical Memory window with its
   dropdown set to "Region ':maincpu'" (a static raw ROM/RAM blob) instead of
   the live address-space view ("maincpu 'program'"). Not a bug — resolved by
   explaining the correct dropdown selection.

## Confirmed NOT a bug / debunked

- **The uploaded full-ROM-disassembly text file
  (`pc1360_sc61860_full_disassembly.txt`, generated by some external tool,
  NOT MAME's own disassembler) has real decoding bugs.** Two concrete
  divergences found by cross-checking against MAME's own live-traced
  disassembly at the same addresses: (1) at ROM offset `0x0006` it merges two
  separate one-byte opcodes (`EXAM` then `OUTC`) into a bogus `CAL 1BDF`; (2)
  at offset `0x004B` it decodes a `LIDP` operand backwards, showing `0070`
  where the real machine executes `LIDP 7000`. **Treat that file as a rough
  map/grep index only — never trust it for precise instruction-level
  analysis.** Always re-derive via MAME's own `dasm`/`trace` debugger
  commands instead (see methodology above).
- **The original "1+1 + ENTER → 'K. ?' crash" does not reproduce** with a
  clean boot (fresh NVRAM). Tested `1+1`+ENTER directly: machine just
  silently returns to the blank `>` prompt, no crash, no "K. ?", no garbage.
  Strong suspicion the original finding (from before a context-compaction
  boundary in this project) was itself an NVRAM-contamination artifact — the
  exact same false-positive failure mode already independently caught once
  this session during regression testing of the RAM-mapping fix. **This
  specific crash signature should be considered closed/unreproducible unless
  new evidence appears.**

## OPEN — the real, currently-active bug thread

**Confirmed by the user against real hardware: `PRINT 1+1`+ENTER should
display `2`. In MAME it displays nothing at all.**

Established facts (all via live traces/taps in the sandbox, clean NVRAM,
multiple confirming runs):

- `1+1`+ENTER (bare expression, no PRINT), `PRINT 1+1`+ENTER, and even
  `PRINT A`+ENTER (bare variable reference, no arithmetic at all) ALL produce
  **zero visible output** — not a crash, not an error message, just silent
  return to the `>` prompt.
- Ruled out: NVRAM contamination (clean NVRAM used throughout), key-hold
  timing (tested 20-frame vs 90-frame ENTER hold, identical result), and
  literal absence-of-typing (screenshots confirm "PRINT 1+1" / "PRINT A" are
  correctly echoed to the screen before ENTER is pressed).
- A write-tap across the ENTIRE external RAM range (`0x2000-0xFFFF`) during
  the whole post-ENTER execution window shows **zero** writes that look like
  a printable ASCII character (`0x20-0x7e`), and zero writes at all in the
  two most likely output-buffer candidate ranges (`0x2E00-0x2FFF`,
  `0xFD80-0xFDFF`). The interpreter never reaches a digit-to-ASCII conversion
  step (also confirmed absent: grepped a full post-ENTER trace for
  `ORIA 30`/`ADIA 30`-style digit conversion idioms — zero hits).
- Traced the real (bank-switching) command dispatch path taken after ENTER:
  jumps into banked ROM around `0x47F2` (tokenizer), through a keyword-table
  linear search (repeating comparison loop around `0x5610-0x566C`, matches
  "PRINT" successfully — confirmed since a wrong keyword would produce
  different, garbled behavior, not a clean return-to-prompt), sets several
  status flags (`0xFD01`,`0xFD11`,`0xFD14`,`0xFD15`,`0xFD1C`, internal RAM
  `$35`), does ANOTHER bank switch (~`0x1292`/`0x12BF: LIDP 3400;...;STD`),
  runs what looks like a "skip spaces / scan rest of line" loop around
  `0x6297-0x62A4`, and finally does a fixed 79-iteration `IXL;IYS;LOOP`
  cleanup loop (`0x53D0-0x53D7`) before returning straight to the
  keyboard-idle loop. **No arithmetic evaluation and no output-writing code
  is ever reached in this path.**
- The most promising lead not yet chased down: right after the scan loop
  there's a branch `0x6288: JRNCP 628B` — in every observed trace this jump
  IS taken (falls into `628B`, which leads to the dead-end 79-loop cleanup).
  The NOT-taken side (`0x6289` onward) has never been seen executing and its
  disassembly hasn't been pulled yet — **this is the single best next
  concrete step**: statically disassemble `0x6289` onward (with the correct
  bank selected — check via a live bank-write tap what bank is active at
  that point, don't assume) to see what the "real" evaluate-and-print path
  would have looked like, then figure out what carry-flag precondition
  (set earlier in the trace) is failing to take that branch.
- **A precondition-gate pattern search was done at `0x4FE2-0x503B`**
  (checks main-RAM `0xFD03` bits 0x20/0x01 and internal RAM `$35` bit 0x10 —
  structurally identical in spirit to the `$33`-bit-0x04 gate that turned out
  to gate graphics PSET). In the traces captured, this particular gate WAS
  satisfied (fell through to the "good" continuation at `0x4FB4`), so it is
  NOT the blocker for this specific bug — but the analogous pattern (a
  status-flag gate silently short-circuiting a ROM routine) is worth keeping
  in mind; there may be another one further down that we haven't found yet,
  most likely guarding the `0x6288` branch above.

### UPDATE, new session: `0x628B` "dead end" framing was WRONG; also a
### major re-scoping finding — this bug reproduces on `pc1350` too, so it's
### almost certainly in the shared `sc61860` CPU core, not the PC-1360 ROM

**Correction to the paragraph above (`0x6288: JRNCP 628B`):** the framing of
"NOT-taken side never seen, dead-end cleanup on the taken side" was based on
a wrong assumption about what `DXL` does. Actually checked this session:

- `DXL` (opcode `0x25`) is implemented as `sc61860_dec_load_dp_load()`
  (`scops.hxx` ~line 631): it decrements the 16-bit `X` pointer (the
  `XL`/`XH` register pair, internal-RAM offsets 4/5) **and loads `A` from
  `READ_BYTE(m_dp)` at the new address**. Per `sc61860_dec_load_dp()`
  (~line 613) it does **not touch the zero or carry flag at all**. This is
  completely different from what the earlier framing assumed ("XL-counter
  reaching zero clears carry").
- Re-reading the actual loop at `0x629C-0x62A9` with that corrected
  understanding, it decodes as a completely ordinary "trim trailing
  carriage-returns while scanning backward through memory" loop: compare
  `RAM[XL]` (the pointer's own low byte) against `0x20` as a
  buffer-underflow safety net (`629D: CPIM 20` / `629F: JRCP 62a9`, exit
  with carry SET if it fires — confirmed via `grep`/`awk` on the captured
  131k-line disassembly trace that this never fires across 76 real
  executions, i.e. no underflow happened, unsurprising for a short line);
  otherwise compare the previously-loaded char against CR
  (`62A1: CPIA 0d`), then `62A3: DXL` (decrement X, load next char), then
  `62A4: JRZM 629c` loops back **only while the char just tested was CR**
  (zero flag is still the one `CPIA` set — `DXL` didn't touch it); once a
  non-CR byte is found the code falls to `62A6: IX;IX` (nudge pointer back)
  `; 62A8: RC ; 62A9: RTN` — i.e. **exits with carry CLEAR as its NORMAL,
  successful-completion outcome**, not as a wrong-path indicator.
- That means `0x6288: JRNCP 628B` taking its branch (carry clear → jump) is
  the **expected/normal** result of that loop, and `0x628B` onward is very
  likely legitimate cleanup/continuation code, not a "dead end." Continued
  tracing forward from `0x628B` this session (through `0x12AC`, the
  `0x1292`/`0x12BF` bank switch, and a `CALL FF0D → JP 4091 → JP 6CD8`
  chain) landed on a **generic character-scanning utility** at
  `0x6CD8-0x6D03` that classifies each byte as "literal text" (`<0x81`) vs.
  "BASIC token" (`>=0x81`) via `CPIA 81` (`0x17E8`), called from many
  places in the ROM (not print-specific) — consistent with "advance past
  literal text", not evaluation.
- Grepped the **entire** ~131k-line trace for any comparison against ASCII
  digit-range or operator constants (`CPIA/CPIM 30-3a`, `2b`, `2d`) to look
  for where the "1+1" text might get recognized as a number/operator to
  evaluate. Found exactly **one** `CPIA 2b` in the whole trace, and it's a
  false lead: it's part of an unrelated one-byte command-token dispatch
  table at `0x54BB-0x54F2` (`CPIA 59/21/2b/62/75/74/54/66/5e`, each
  followed by its own `JRZP` to a different handler) — those are single
  **token byte values** being matched, not the runtime ASCII `+` operator.
  **No code path that recognizes/evaluates the typed digits or `+` was
  found anywhere in the traced execution.** Either the real evaluator is
  never called at all (most likely, given the above), or it's reached via
  a path this trace never took because of a wrong flag/branch upstream.

**The important new finding this session — re-scopes the whole
investigation, and includes a correction of a mistake made mid-session:**

First pass (WRONG, since corrected — leaving this here so nobody repeats the
mistake): ran an automated `PRINT 1+1`+ENTER test against **`pc1350`**
(`src/mame/sharp/pc1350.cpp`, a sibling driver on the same shared `sc61860`
CPU core, *not* flagged `MACHINE_NOT_WORKING` unlike `pc1360`) using a
`press("KEY9", "ENTER", ...)` Lua helper copy-pasted from the `pc1360`
scripts. Result looked like the same "line just sits there forever, zero
output" symptom as `pc1360` — but this was a **testing artifact, not a real
finding**. `pc1350`'s actual `INPUT_PORTS_START(pc1350)` block (in
`pocketc.cpp`, ~line 676) names that key `PORT_NAME("ENTER P<->NP")`, not
`"ENTER"` (that plain name is only `pc1360`'s label, `pocketc.cpp` ~line
120) — the exact-string-match `press()` helper silently failed to find a
field called `"ENTER"` on `pc1350` and returned `false`, i.e. **ENTER was
never actually pressed in that test at all**. The "stuck line" was just an
unsubmitted line sitting in the editor, not a bug.

Corrected re-test, same session: fixed the field name to `"ENTER P<->NP"`
and reran.
- `PRINT 1+1`+ENTER and `PRINT 1`+ENTER on `pc1350` → the screen shows
  **`ERROR 1`**.
- **User confirmed directly: this `ERROR 1` is CORRECT, real-hardware
  behavior, not a bug.** (Makes sense in hindsight — `PRINT` on these
  Sharp pocket computers is a PROGRAM-mode statement; typing it bare at
  the RUN-mode `>` prompt is exactly the kind of thing that should be
  rejected.) So the entire "chase `pc1350`'s `ERROR 1` dispatch" thread
  from earlier in this session is a **dead end — do not pursue it**, and
  the trace captured for it (`print1_pc1350_full.log`) is not useful for
  this bug.

**The user then gave the actual, precise bug report: a bare expression —
`1+1`+ENTER, typed directly at the RUN-mode `>` prompt with NO `PRINT`
keyword — should immediately evaluate and display `2` on real hardware
(these Sharp pocket computers double as calculators: the RUN-mode prompt
accepts a bare expression and echoes its value, no `PRINT` needed).** This
had never actually been tested correctly before this point — every earlier
test in this project used either `PRINT 1+1` or a single bare digit `1`
(which prints nothing observable even when working, since there's no `+`
to force evaluation output). Tested properly this session, with the
correct key names/timing for each driver:

- **`pc1350`: bare `1+1`+ENTER → screen shows `1+1` then, right-aligned on
  the next line, `2.` — CORRECT, matches real hardware.** (Screenshot
  confirmed, reproducible via `test_bare11_pc1350.lua`, sandbox-only.)
- **`pc1360`: bare `1+1`+ENTER → screen just clears back to a blank `>`
  prompt. No `2` (or anything else) ever appears — THIS IS THE ACTUAL
  BUG.** (Screenshot confirmed, reproducible via `test_bare11_pc1360.lua`,
  sandbox-only; also reconfirmed with a full disassembly trace,
  `bare11_pc1360_full.log`.)

**This is a major, clean result: since `pc1350` gets this right using the
exact same shared `sc61860` CPU core, the bug is almost certainly NOT in
the shared core after all (reversing the earlier, more tentative
hypothesis) — it's very likely specific to `pc1360`'s own ROM, its
memory-map/bank-switching setup, or `pc1360_m.cpp`'s driver-specific
code.** This finally gives a clean, correctly-isolated repro to chase,
instead of the ambiguous "silent failure that might be a shared-core bug"
framing from earlier.

Two full disassembly traces were captured this session for exactly this
comparison (both in the sandbox only, not preserved/delivered — regenerate
via the scripts named if needed):
- `bare11_pc1350_full.log` (~558k lines, `trace_bare11_pc1350.lua`) — the
  **successful** `pc1350` evaluation. Banked-ROM execution enters cleanly
  at a single trampoline, `0x8021: JP AA39`, right after ENTER — this is
  the same entry point `PRINT 1`'s (legitimate) `ERROR 1` path used too,
  confirming `0x8021`/`0xAA39` is a generic "tokenize and dispatch this
  line" entry, not specific to any one statement type. The whole
  evaluation, from dispatch to drawing `2.` and returning to idle, is
  ~8300 banked-ROM instructions.
- `bare11_pc1360_full.log` (~615k lines, `trace_bare11_pc1360.lua`) — the
  **failing** `pc1360` case. This trace is noisier than the `pc1350` one
  because it necessarily also captures boot + the Y-keypress RAM-clear
  confirmation (pc1360 needs both; the `pc1350` trace didn't need to
  simulate either). ~29000 banked-ROM lines total. The clearest signal
  found so far: right around where ENTER should be processed, the trace
  shows a `44FA: JP 405C` → ... → `4072: JP 1283` sequence, then (after a
  gap of purely internal-ROM code) `FF0D: JP 400D` → `400D: JP 49D0` →
  `49D0: LIA 49; PUSH; LIA d0; PUSH; CALL 47E0` → `47E0: LIDP 303c; LP 0d;
  MVMD; LIDP ffec; TSID 20; ...` — this *looks* like it could be `pc1360`'s
  analogue of the generic dispatch trampoline (pushing what looks like a
  manufactured return address, `0x49D0`, then checking status bits at
  `0x303c`/`0xffec`, addresses already known from the earlier `PRINT`
  investigation), but this has **not been confirmed** — the trace is too
  cluttered with boot/Y-confirm noise to be sure this is really the
  ENTER-triggered path and not something else. **Not yet done, and the
  single best next step:** recapture the `pc1360` trace the same clean way
  the `pc1350` one was done — start the live `trace` command only a few
  frames before the ENTER press (after boot and the Y-confirm are already
  handled, exactly like `trace_bare11_pc1350.lua` does, which never needed
  to simulate Y at all since it started later) — then diff that clean
  dispatch-entry point against `pc1350`'s `0x8021: JP AA39` structurally
  (same relative position in the boot/dispatch sequence, even though the
  absolute addresses will differ between the two ROMs) to find exactly
  where the two ROMs' otherwise-parallel logic diverges.

**Spot-checked against a public SC61860 instruction-set reference this
session** (github.com/utz82/SC61860-Instruction-Set) for the opcodes that
appear on the traced dispatch path — all checked out correct against
`sctable.hxx`/`scops.hxx`:
- Branch polarity for every conditional jump/branch opcode: `JRZP/JRZM`
  (`m_zero`), `JRNZP/JRNZM` (`!m_zero`), `JRCP/JRCM` (`m_carry`),
  `JRNCP/JRNCM` (`!m_carry`), and their absolute-jump equivalents
  `JPZ/JPNZ/JPC/JPNC` (cases 126/124/127/125) — all match the documented
  polarity, no swaps found.
- `TSIA/TSID/TSIM` (bit-test): all set only the zero flag from a bitwise
  AND, carry untouched — matches spec (`sc61860_test`/`sc61860_test_ext`).
- `ANIM`/`ORID` and friends: zero-only flag semantics, matches spec.
- `CPIA`/`CPIM` (`sc61860_cmp`, `scops.hxx` ~line 288): `t = READ_RAM(reg) -
  value; zero = t==0; carry = t<0` using `int` arithmetic on promoted
  `uint8_t` operands — correctly implements "carry set if unsigned
  RAM/accumulator value < unsigned immediate", matches spec.

**ENTER/keyboard handling itself is now RULED OUT as the cause** (this was
the leading candidate before the ENTER-field-name mistake was caught — see
above): both `pc1360` and `pc1350`, tested with the correct key names and
generous timing, accept ENTER completely normally (line clears / error
message appears, either way the editor clearly processed it).

**`PRINT`/`ERROR 1` thread is CLOSED — not a bug, confirmed by the user
against real hardware.** Don't chase it. The `print1_pc1350_full.log`
trace and the "find the error-code dispatch" plan from earlier in this
doc are both obsolete; ignore that guidance if you see it further up.

**The real, now-cleanly-isolated bug: bare `1+1`+ENTER at the RUN-mode `>`
prompt (calculator-style immediate evaluation, no `PRINT`) works correctly
on `pc1350` (`→ 2.`) but produces silent nothing on `pc1360`.** Given the
shared CPU core gets this right on a sibling machine, the leading
hypothesis is now **`pc1360`-specific ROM or driver code**, not the shared
`sc61860` core — though this isn't proven yet, just the best-supported
guess so far.

**NOT yet audited** (candidates for where a real bug could still be hiding,
roughly in priority order — REVISED given the finding above):
1. **A clean, non-noisy disassembly trace of `pc1360`'s bare `1+1`+ENTER
   dispatch.** The one captured this session (`bare11_pc1360_full.log`) is
   cluttered with boot + Y-confirm activity because the trace had to start
   from frame 5. Redo it the way `trace_bare11_pc1350.lua` did — advance
   past boot/Y-confirm with plain key presses first (no trace running),
   *then* start `dbg:command("trace ...")` only a few frames before the
   ENTER press. That gives an isolated, ~hundreds-of-lines dispatch trace
   directly comparable to `pc1350`'s clean `0x8021: JP AA39` one. The
   noisy trace's tentative-and-unconfirmed candidate for `pc1360`'s
   dispatch entry is a `FF0D: JP 400D → 400D: JP 49D0 → CALL 47E0` chain
   (checking status bits at `0x303c`/`0xffec`) — worth confirming or
   refuting first thing.
2. **`pc1360`-specific memory-map/bank-switching code** (`pc1360_m.cpp`,
   and the `membank`/address-space setup in `pc1360.cpp`) — this is a
   strong candidate precisely *because* it's the main thing that's
   genuinely different between the two drivers (the shared CPU core and a
   lot of shared low-level BASIC-ROM code are, per this session's
   findings, essentially equivalent between the two machines). If `pc1360`
   uses a different/buggy bank-select sequence when jumping into the
   expression-evaluator region of its ROM, that would produce exactly this
   symptom (falls into whatever garbage/idle code happens to be mapped at
   that address instead of the real evaluator, with no crash and no
   error).
3. `CAL` (internal-ROM-relative call) vs `CALL` (absolute) addressing/bank
   handling in `sc61860.cpp`/`scops.hxx` — not reviewed this session at all
   despite being used constantly on this path; lower priority now than #2
   since it's shared-core and `pc1350` proves the shared core can get this
   right, but still worth a look if #1/#2 don't pan out.
4. The actual arithmetic opcodes (`ADN/SBN/ADW/SBW/ADB/SBB/ADM/SBM`) and
   the BCD add/subtract routines (`sc61860_add_bcd_a`/`sc61860_add_bcd`/
   `sc61860_sub_bcd_a`/`sc61860_sub_bcd`, `scops.hxx` ~line 483-576) — same
   lower-priority reasoning as #3 (shared code, `pc1350` proves it works).

### UPDATE, same session: got the clean trace (candidate 1 done) — found the
### actual statement-dispatch mechanism (`PTC`/`ETC`), and it's genuinely
### suspicious

Recaptured `pc1360`'s trace cleanly (`bare11_pc1360_clean.log`,
`trace_bare11_pc1360_clean.lua`, both sandbox-only): boot + Y-confirm +
typing done with plain key presses first, then `dbg:command("trace ...")`
started only ~15 frames before the ENTER press, same approach as the
successful `pc1350` capture. Much smaller (~240k lines total, ~10600
banked-ROM lines) and — importantly — the ENTER keystroke is the **only**
pass through the per-keystroke handler at `0x47F2` in the whole trace, so
everything in it is directly attributable to processing ENTER, nothing else
mixed in.

Following it through: ENTER (byte `0x0D`) goes through the same per-key
input handler seen in the earlier `PRINT` investigation (`49D9: EXAB;
JPNC 4bcd; CPIA 0a; ...`), and lands on:

```
4ACA: PTC   05,4ae0
4ACE: ETC
```

**`PTC`/`ETC` is a real SC61860 opcode pair — "prepare table call" /
"execute table call" — i.e. this is the actual jump-table-based STATEMENT
DISPATCH mechanism** (this had never been identified by name before in
this project's notes; earlier sessions only ever saw its *targets*, not
this dispatch instruction itself). `PTC 05,4ae0` sets up an indexed call
into a table based at `0x4AE0`; `ETC` performs the indexed jump.

Following the landing point forward: `4EC1: LIDP 303c; TSID 40; JRZP 4ee6`
→ `4EE6: LIDP fd02; ORID 08` (sets a status flag) → `4EEB: CALL 4180` →
`4180: LIJ 91; JRP 41c2` → `41C2: LIQ 01; JP 1283` → `1283: ...` → ends in
`CALL FF0D` → `FF0D: JP 4091` → `4091: JP 6CD8` — **the exact same
`0x6CD8` generic "scan text, classify literal-vs-token bytes, do nothing
with the result" utility** that the earlier `PRINT` investigation (this
session, and last session) traced exhaustively and confirmed never invokes
any arithmetic/evaluation/output-writing code — it just advances a pointer
and eventually returns to idle.

**The important part: `LIJ 91` here (bare expression) vs. `LIJ 88` in the
earlier `PRINT` trace (`print_trace2.log`, at the structurally identical
`4174: LIJ 88; JRP 41c2` step) are DIFFERENT values feeding into the
IDENTICAL follow-on code (`41C2: LIQ 01; JP 1283 → ... → CALL FF0D →
4091 → 6CD8`).** So `pc1360` DOES correctly distinguish "this is PRINT"
from "this is a bare expression" (different `LIJ` operand from the `PTC`
table, presumably the statement's token/type code) — but then **both paths
immediately re-converge onto the same generic, non-evaluating cleanup
code**, regardless of which one was recognized. On a working
implementation, the `LIJ` value (or something derived from it) would have
to get used *somewhere* downstream to actually branch into
statement-specific handling — evaluate-and-print for a bare expression,
argument-parsing for `PRINT`, etc. In this trace, on `pc1360`, it visibly
doesn't: everything after `41C2` is identical regardless of what was typed.

**This is now the most concrete, well-evidenced lead in the whole
investigation.** Two candidate explanations, not yet distinguished: (a)
the `PTC 05,4ae0` table at `0x4AE0` itself has a bad/incomplete entry for
one or both of these token codes (easy to check: statically dump table
entries at `0x4AE0 + 5*n` for a few `n` and see where they point — haven't
done this yet), or (b) the table entries are fine but something downstream
(maybe inside the unexplored parts of `0x1283`, which does a
`LIDP 3400`/bank-switch-looking sequence) is supposed to consult the `LIJ`
value again and route accordingly, and isn't. **Next concrete step:**
statically read the `PTC` table at `0x4AE0` (need to determine the right
bank first — use a live tap/breakpoint or `dbg:command("dasm")` rather than
assuming direct `read_u8`, since banked-ROM reads have been unreliable via
`install_read_tap` earlier this session) for the entries indexed by `0x91`
and `0x88`, and see whether they legitimately point to different handlers
that both happen to fall through to `0x6CD8`, or whether one/both are
simply wrong.

### UPDATE, same session: tried to read the `PTC` table directly — hit the
### SAME `install_read_tap` unreliability documented earlier, but the
### tracing approach paid off anyway: found where the "which statement is
### this" marker gets set and confirmed it's never checked before falling
### to idle

Attempted to statically dump the `PTC 05,4ae0` / `ETC` table bytes
(`0x4ACF-0x4ADF`, format confirmed from `sc61860_prepare_table_call`/
`sc61860_execute_table_call` in `scops.hxx`: `m_h` count-many
`(match-byte, big-endian word)` triples, testing `READ_RAM(A)` against
each `match-byte` and jumping to the first match's address, falling to one
final default word if none match — table data is genuinely INLINE after
the `ETC` opcode, not a separately-addressed table, and MAME's own static
disassembler (`scdasm.cpp`'s `Etc` case) does NOT know how to skip it,
since the entry count `m_h` is runtime CPU state, not encoded in the `ETC`
byte — so don't try to `dasm` over this region expecting sane output,
only live execution decodes it correctly).

**Confirmed, reproducibly, that `install_read_tap` cannot be trusted to
fire on a banked-ROM address's *second or later* execution, even across 8
separate attempts this session** (this generalizes the earlier `0x6288`
finding from the `PRINT` investigation into something more solid): address
`0x4ACE` executes exactly twice in a full boot-to-ENTER trace — once during
boot (where, in a *different* bank, the same address happens to decode as
unrelated code, `CALL 4da6`/`CALL 4ae1`) and once for our real ENTER
dispatch (`PTC 05,4ae0`/`ETC`). A read-tap on `0x4ACE` fired on the FIRST
(boot, wrong-bank) execution in every one of 8 runs and never once fired on
the second (real) execution — not intermittent, consistently one-sided.
Tried tapping `0x4EC1` instead (`ETC`'s landing address, confirmed hit
exactly once in the whole trace, so no bank-ambiguity risk) — the tap
didn't fire at all, in 4/4 attempts. **Conclusion for future sessions:
don't rely on `install_read_tap` for anything beyond an address's first
banked-ROM execution — use `dbg:command("trace ...")` instead and read the
disassembly text, which has been reliable all session.**

Given that dead end, went back to the trace text instead and looked more
carefully at what happens between `LIJ 91`/`LIJ 88` (the "this is a bare
expression"/"this is PRINT" marker, per the earlier finding) and the point
where both cases visibly converge. Found something concrete:

```
4180: LIJ   91
4182: JRP   41c2      <- unconditional jump (JRP with no test = always taken)
41C2: LIQ   01
41C4: JP    1283
1283: LIP   5c
...
128D: LIJ   40         <- J is OVERWRITTEN here, unconditionally
128F: MVDM
1290: LIJ   01          <- and OVERWRITTEN AGAIN here
...
12A9: CALL  ff0d
```

**`4182: JRP 41c2` is an unconditional jump — there is no test of the `LIJ`
value anywhere between setting it (`91` or `88`) and reaching `0x1283`.**
And inside `0x1283`, `J` gets reloaded twice more (`0x40`, then `0x01`) for
what look like generic byte-count/index purposes in a couple of `MVDM`
memory-copy operations — **by the time this routine reaches `CALL FF0D`,
whatever `J` held right after the `PRINT`-vs-`bare-expression` recognition
step is long gone, overwritten by unrelated local bookkeeping.** Combined
with the earlier-confirmed fact that `PRINT` (`LIJ 88`) and bare-expression
(`LIJ 91`) both reach this exact point and then both fall through
`FF0D → 6CD8`'s generic "scan past this text" utility and then straight to
the keyboard-idle loop with no output — **this is now a strong, concrete
hypothesis: the "which kind of statement was this" signal is being
generated correctly, but is never consulted again before execution falls
back to idle.** Either something *should* branch on it inside `0x1283`
(or one of the internal-ROM helpers it calls) and doesn't, or the intended
consumer of that signal lives further up the call chain, past wherever
`FF0D`'s target (`6CD8` here) eventually returns to — and that return
never happens because something in that chain does a raw `JP` (not `RTN`)
straight into the idle loop instead of unwinding back to a caller that
would check it. **Not yet distinguished between those two — this is the
single most concrete next step.**

### UPDATE, same session: followed it all the way through — it DOES return
### normally, and there's now a single, specific, never-taken conditional
### branch that's the best candidate for the actual bug

Traced `0x6CD8` all the way to its `RTN` in the clean bare-`1+1` trace.
**It returns completely normally** — no rogue `JP` bypassing the call
stack. The chain unwinds `6CD8 → ... → 6CF2: RTN → 0x12AC → ... → 0x12C6:
RTN → 0x4EEE` — landing on exactly the kind of status-flag gate this doc
already knew about (`4EEE: LP 35; ANIM bf; TSIM 10; JRZP 4efe` — the
internal-RAM `$35` bit-`0x10` gate flagged earlier this session as
structurally similar to the `$33`-bit-`0x04` PSET gate). That gate passes
(bit clear, branch taken) and leads into `0x4F00-0x549C`, which turns out
to be **PRINT's argument-list scanner** — it watches for `"` (string
literal), `:` (statement separator), `,`/`;` (print-zone separators) while
scanning the typed characters — and this is the SAME code reached whether
`PRINT` was typed explicitly or not (bare-expression immediate-print
apparently reuses PRINT's own argument formatting machinery once dispatch
has decided "format and show a value", which makes sense as shared code).
It then falls into `0x5530-0x5570`, which does print-zone/column
positioning, and returns — **explicitly clearing carry right before
returning** (`556F: RC`, then `5570: RTN`).

**The very next instruction after that return is:**

```
4F39: JPC   4f46
```

**A conditional jump on carry, tested immediately after a subroutine that
just explicitly cleared it.** In every trace captured this session — both
`PRINT 1+1` (`print_trace.log`/`print_trace2.log`, prior + this session)
and bare `1+1` (`bare11_pc1360_full.log`, `bare11_pc1360_clean.log`) —
**this branch is NEVER taken.** `0x4F46` does not appear even once in any
captured trace. Instead, execution always falls through into the `4F3E:
LP 06; 4F3F: CPIM 6f; ...` scan-for-`0x6F`-terminator loop (previously
documented, goes on to return to idle with no output).

**This is now the single most concrete candidate for the actual bug.**
Two questions, neither answered yet:
1. What is at `0x4F46`? (Never observed executing, so no trace evidence —
   would need either a live disassemble at that exact PC with the correct
   bank active, or working out the byte immediately after `4F39`'s 2-byte
   encoded jump target from a raw memory read. **Both attempted this
   session and both failed** — `install_read_tap` would not fire
   reliably even for a first-and-only occurrence of an address in several
   more attempts this session (not just the earlier-documented
   second-occurrence problem — this looks like a broader, still-unexplained
   flakiness in this specific tooling technique, not merely a
   banked-ROM-caching quirk). **If the user has their own MAME built with
   `-debug` and can drive the interactive debugger UI directly** (rather
   than this session's headless Lua-script approach), that would very
   likely be the fastest way to get this one piece of missing information:
   set a breakpoint at `0x4F39`, single-step or `dasm` from `0x4F46`
   onward once stopped there (with the correct bank already active,
   guaranteed since you'd be stopped mid-execution).
2. Is carry supposed to be set here for a bare-expression / PRINT-value
   case, or is `0x4F46` actually irrelevant to this bug (e.g. an error
   path) and carry-clear is correct, with the real fix needed even earlier
   (in `0x4F00-0x549C`'s argument scanner, or the `$35`-bit-`0x10` gate
   before it)? Not yet determined — this is exactly what reading `0x4F46`
   would help settle.

**Suggested next step for whoever picks this up:** get `0x4F46`'s
disassembly by whatever means works (interactive debugger strongly
preferred over more headless Lua/tap attempts, given this session's
repeated tooling failures) and read forward from there — if it looks like
an evaluate-expression/BCD-math/output-formatting routine, this is very
likely the fix locus (something upstream needs to `SC` instead of
falling through with carry clear, or the `556F: RC` in the print-zone
handler is itself wrong and should be conditional). If `0x4F46` looks like
an unrelated error path, the search needs to move earlier — into
`0x4F00-0x549C`'s argument-list scan or the `$35` bit-`0x10` gate right
before it. Don't resume
disassembly of `0x628B` onward — that framing (from the `PRINT`
investigation, now also understood to be chasing a non-bug) is a red
herring for this bug too. **Whenever testing key input on `pc1350`/other
sibling drivers again, always grep that driver's own
`INPUT_PORTS_START(...)` block in `pocketc.cpp` for the exact
`PORT_NAME(...)` string first — do not assume `pc1360`'s field names carry
over. `pc1350`'s ENTER key is named `"ENTER P<->NP"`, not `"ENTER"`; this
mistake cost real time this session and would silently corrupt any test
that copy-pastes a `pc1360` key-press script onto another driver.**
`pc1350`'s ROM is staged in the sandbox at `/tmp/mame/roms/pc1350.zip`
(from the user's `C:\mame\roms\pc1350.zip`) for further differential
testing; use `nvram/pc1350/` for clean-boot tests.

### UPDATE, new session: user corrected the framing — `ERROR 1` for `PRINT 1`
### is CORRECT real-hardware behavior; the actual bug is bare `1+1`+ENTER
### (calculator-style, no `PRINT`) producing nothing instead of `2`

**User's exact correction, verbatim:** "the ERROR 1 in RUN mode is actually
the correct behavior. That happens on real device too. But 1+1 should
result in 2 on the screen." This **closes the `PRINT`/`ERROR 1` thread
entirely** — everything in this doc framed around "why does `PRINT 1` fail"
was chasing expected/correct behavior, not a bug. **Do not resume that
thread.** The real, still-open bug: typing a bare expression with no
keyword (e.g. `1+1`) at the RUN-mode `>` prompt and pressing ENTER should
evaluate it immediately (calculator mode — these Sharp pocket computers
double as calculators) and display `2`. On `pc1360` it instead silently
returns to a blank `>` prompt. Verified this reproduces correctly on
`pc1350` (shows `2.`) using the same shared `sc61860` CPU core, so the bug
is `pc1360`-specific (ROM content/dispatch), not a generic core opcode bug.

### UPDATE, same session: user supplied ground-truth disassembly for
### `0x4F39`-`0x5140` from their own interactive debugger — used it plus a
### byte-for-byte diff of the two captured traces to find the REAL fork
### point, which is NOT `0x4F39` after all

The user drove their own MAME debugger to `0x4F39` (the never-taken `JPC
4f46` flagged as the leading suspect) and screenshotted ~9 screens of
disassembly forward from there through `0x5140`. Cross-referencing that
against the two traces already captured (`bare11_pc1360_clean.log` for the
buggy `1+1` case, `print_trace2.log` for the correctly-erroring `PRINT 1`
case) settled question 2 from the prior update: **`0x4F39`'s branch is
never taken in EITHER case** — it's identical, generic end-of-line cleanup
code shared by every statement type, not specific to this bug. Confirmed by
diffing the two full instruction traces starting from the shared `4180:
LIJ 91` anchor: they are **byte-for-byte identical**, modulo differing loop
iteration counts, all the way from dispatch through `0x4F39` through
`0x4F3C`'s scan loop and beyond — i.e. `0x4F39`/`0x4F46` is a red herring
for explaining the *difference* between the two cases, even though it may
still matter for other reasons.

**The real fork point**, found by walking the diff forward byte-for-byte
(`python3` line-by-line comparison, not just `diff`, since the two traces
have different lengths and naive diffs get lost in the noise): at
`0x54AD-0x54AF`:

```
54AD: CPIA  41      ; compare current char against 0x41 ('A')
54AF: JRCP  54b5     ; if carry (char < 0x41, i.e. NOT an uppercase letter) jump to 54B5
```

- **`PRINT 1`** (current char `'P'`, `0x50`, NOT `< 0x41`): carry clear,
  falls through to `54B1: CPIA 5b` / `54B3: JRCP 5571` → `CALL 55ED` — a
  ~250-instruction keyword-recognition/dispatch block, only ever entered in
  this case. This is presumably what correctly recognizes `PRINT` and
  ultimately (not traced in detail — confirmed correct behavior already, no
  need to) produces `ERROR 1`.
- **bare `1+1`** (current char `'1'`, `0x31`, `< 0x41`): carry set, jumps
  straight to `54B5: CPIA fe`, **completely skipping** the keyword-dispatch
  block above, and merges into generic scanning code
  (`54FB→17E8→54FD→5506…→549C→549E→54A0→5530…`) that is **the exact same
  code** `PRINT`'s own argument list uses — confirmed via the earlier-traced
  `0x4F00-0x549C`/`0x5530-0x5570` path, all now shown to run identically
  regardless of which case triggered it.

**Followed the bare-`1+1` path all the way to the end of the captured
trace (`bare11_pc1360_clean.log`, 460 frames, ~223k executed instructions
past this point) — it never executes anything resembling BCD digit
arithmetic or a number-to-string/format routine.** It ends up in a stable,
indefinitely-repeating idle loop (`0x00FB-0x010B`, the display/cursor
refresh loop) for the entire remainder of the trace, matching the observed
symptom exactly (blank `>` prompt, nothing more ever happens — not a
truncated-capture artifact). **Conclusion: on this ROM, entering a token
that starts with a non-letter character (a digit, in this case) never goes
through the keyword-dispatch machinery at all, and nothing else in the
traced path evaluates or prints anything.** The only remaining candidate
for where "evaluate expression and print result" could legitimately live is
still `0x4F46` (via the `4F39: JPC 4f46` branch) — now understood as a gate
that apparently needs to fire for *both* the `PRINT`-argument case and the
bare-expression case (since both funnel through the identical `0x5530-
0x5570` code before reaching it), but is **never observed taking carry-set
in any of the four traces captured across both sessions** (`print_trace.log`,
`print_trace2.log`, `bare11_pc1360_full.log`, `bare11_pc1360_clean.log`).

**The one remaining unknown, and the actual current blocker:** the `else`
branch of the comparison immediately gating that `0x4F39` carry state:

```
5560: CPMA          ; compare M[P=06] against A (loaded from external mem at DP=fd04)
5561: JRNZP 5568     ; if NOT equal, jump to 5568 (the path both traces always take)
      ????           ; <-- addresses 0x5562-0x5567 (6 bytes), NEVER observed executing
                      ;     in any trace captured this or last session — this is the
                      ;     "equal" case, and is the single most likely place carry
                      ;     actually gets SET (via an `SC` before some other `RTN`)
                      ;     for the case this bug needs to hit.
5568: LIDP  fcb3     ; ... (already fully traced — always ends 556F: RC / 5570: RTN,
                      ;     i.e. always forces carry clear, confirmed via live trace
                      ;     in every case captured so far)
```

Tried once more to extract these 6 bytes via `install_read_tap` this
session — targeted `0x5568` specifically (confirmed by trace to execute
**exactly once** during the test run, in a straight-line code region with
no cross-bank ambiguity, unlike the earlier `0x4ACE` attempts) — **still
hit count 0, tap never fired.** This reconfirms `install_read_tap` cannot
be relied on in this sandbox at all, not just for repeated/banked
addresses; do not spend more time on it in future sessions.

**Suggested next step:** get the disassembly of `0x5556`-`0x5580` (covers
some margin either side of the unknown `0x5562-0x5567` gap) from the user's
own interactive debugger, the same way the `0x4F39` range was obtained.
Specifically want to know: does this range contain an `SC` (set carry)
followed by an `RTN` that's reachable only when `5561`'s `JRNZP` is NOT
taken? If so, that confirms the gate logic is sound in principle and the
real bug is further upstream — whatever determines the `CPMA` comparison
result at `0x5560` (i.e. why doesn't the bare-`1+1` case's characters ever
make `M[P=06]` equal the byte at external `0xfd04`?) — which would need
`0x5556`'s block traced with register-level values (not just disassembly
text), likely via getting the user to inspect register/memory values live
in their debugger at the moment `CPMA` executes, since this session's
tooling cannot reliably capture register contents (only disassembly text
via `trace`). If instead `0x5562-0x5567` turns out to be unrelated/dead
code, the search needs to move to `0x5530-0x5556` (the `TSID 80` check
against external `0xfe72` bit `0x80`, and the `0x5539-0x5554` loop that
calls `0x131b`) as the next candidate gate.

### UPDATE, same session: user supplied the missing `0x5556-0x5582` bytes —
### the gate is PROVABLY, structurally incapable of ever setting carry; ran
### a live single-byte patch experiment to test it; found the polarity was
### backwards from what this doc assumed, but the experiment still nailed
### down the real mechanism and corrected a big standing false assumption

**The user's screenshot answered the open question decisively.** Full bytes
for `0x5556-0x5582` (both branches of the `0x5560/0x5561 CPMA` gate):

```
5560: CPMA          C7
5561: JRNZP 5568     28 06     <- our traced path (not-equal) always takes this
5563: LIDP  fd03     10 FD 03  <- the "equal" branch, NEVER observed executing before now
5566: ANID  df       D4 DF     <- clears one bit of external fd03, then FALLS INTO 5568 below
5568: LIDP  fcb3     10 FC B3
556B: LDD            57
556C: LP    09       89
556D: SBM            45
556E: LDM            59
556F: RC             D1        <- unconditional, on BOTH branches
5570: RTN            37
```

**Both branches of the gate converge on the same `5568-5570` tail, and
there is no `SC` (set-carry) opcode anywhere in this block.** This isn't
"never observed setting carry" any more — it's now proven from actual ROM
bytes that **this subroutine cannot return with carry set, for any input,
full stop.** The `4F39: JPC 4f46` branch is unreachable as currently coded.

**Ran a live experiment to test what firing it would actually do.** Located
the physical ROM byte: address `0x556F` is bank 0 (`b0-1360.rom`, confirmed
live via the bank register at `0x3400` reading `00` at that PC), file
offset `0x156F`, containing `0xD1` (`RC`) — verified this is the genuine,
official dump (`b0-1360.rom` CRC32 `afe1d3d6`, matching the checksum in
`pocketc.cpp`'s `ROM_LOAD`, and matching the user's screenshot byte-for-
byte), not a sandbox corruption. Patched that one byte to `0xD0` (`SC`) in
the sandbox ROM only, forcing carry always set instead of always clear, and
reran both the `PRINT 1` and bare `1+1` tests:

- **Before the patch: both show nothing** (confirmed, again, this session
  — see the correction below).
- **After the patch: both show `ERROR 0`.** The silence broke — the
  machine visibly does something different for the first time all session.

**This is proof the `0x556F` gate is a genuine, load-bearing mechanism,
not a coincidence** — but tracing what `0x4F46` actually does once
reachable (patched trace, `patched_print1_trace.log`) shows it is a
**generic error-report entry point**, not an expression evaluator:
`4F46: CAL 1153` (restore/pop some saved context) → `CALL 4170` → `1283`
(the same dispatch trampoline documented earlier) → `FF0D` → this time
vectors to `0x645F`/`0x16D4` — a routine that reads an error-code byte and
formats it into the `ERROR n` message. Both `PRINT 1` and bare `1+1`
landed on error code `0` — consistent with a generic/default code being
shown because nothing upstream ever populates a real one (this whole path
is normally dead, remember), not because `0` is individually meaningful
here.

**This flips the polarity this doc had been assuming.** Carry set at
`0x4F39` → **report an error** (now confirmed by direct observation, not
guessed). Carry clear (the current, only-reachable state) → **continue
normally** — i.e. today's always-clear-carry behavior is not itself "the
bug" in the sense of blocking a working evaluator; it means the ROM
(as coded) essentially never raises this particular error class. **Forcing
carry set is therefore not the fix** — it just reroutes every case,
valid or not, into a spurious error display. **The real "evaluate the
expression and print the result" logic must live somewhere in the
carry-clear/"continue normally" path** — traced this with the (properly
reverted) clean ROM: `4F5E → 4F60 (CALL 7c94) → 4F63: JRZP 4fd8 (taken)
→ 4FD8...4FB4: CALL 539e (sets PRINT-related bits at fd01/fd15/fd14/fd11/
fd1c) → 4174: LIJ 88 → 41C2 → 1283 → FF0D → this time vectors to 0x6285
→ 6291 → 6297`, which walks straight into **the `0x6288`-area backward
scan loop a prior session already flagged and marked a dead end** ("Don't
resume disassembly of `0x628B` onward"). **That framing should probably be
revisited** — this session reached that exact neighborhood via a real,
live-traced continuation from the carry-clear path this doc now believes
is the correct one to be following, not as a standalone tangent. Whoever
picks this up next should re-open `0x6297` onward with this context (it's
reached from `PRINT`-flag-setting code, i.e. plausibly *is* on the road to
wherever formatting/BCD-conversion/output happens) rather than treating it
as already-exhausted.

**Important correction to this whole doc, found while setting up a clean
`PRINT 1` comparison trace this session:** the belief that `PRINT 1` on
**`pc1360`** correctly shows `ERROR 1` was wrong. That screenshot
(`snap/print1full/0001.png`) was never actually re-verified against the
current `pc1360` build this session — freshly retested twice, with two
independently-written scripts, with clean NVRAM: **`PRINT 1` on the current
`pc1360` build shows nothing** (blank `>` prompt), exactly like every
other input tested this project (bare `1`, bare `1+1`, `PRINT 1+1`). That
old screenshot almost certainly came from a `pc1350` test or a stale
build state from earlier in this long-running session, not from the
current `pc1360` driver. **Restated bug scope: `pc1360`'s RUN mode
currently never produces ANY visible output for ANY tested statement or
expression** — not a narrow "calculator immediate-mode" gap. This is
consistent with (and probably explains) the driver's `MACHINE_NOT_WORKING`
flag much more broadly than this doc had been assuming.

**Housekeeping / methodology notes for whoever continues this:**
- `print_trace.log` (from `type_1plus1_trace.lua`) is **misleadingly
  named** — despite the name, it types bare `1+1`, not `PRINT`. Only
  `print_trace2.log` (from `check_bank_6288.lua`) is an actual `PRINT`
  test (`PRINT 1+1`). Don't reuse `print_trace.log` assuming it's a PRINT
  case.
- The ROM patch experiment was done and fully reverted this session —
  `roms/pc1360/b0-1360.rom` and `roms/pc1360.zip` are confirmed back to
  the official, checksummed dump (CRC32 `afe1d3d6`, verified twice). No
  ROM file was left modified. `bare11_pc1360_clean.log` was regenerated
  after the revert and reconfirmed to match the original (pre-patch)
  control-flow findings from earlier in this doc — nothing else in this
  doc needs to be distrusted because of the patch experiment.
- `install_read_tap` fired successfully this session for the first time in
  a while (on `0x556F`, to read the live bank register) after many
  documented failures — still not reliable enough to depend on, but
  apparently not 100% broken either. Keep the existing "don't rely on it"
  guidance, but it's worth one quick retry per address before giving up on
  it, rather than assuming it will always fail.

### BREAKTHROUGH, new session: the bug is a real CPU RESET, not silence —
### found via the user's own full-boot `trace` files (see the "how to
### trace from your own MAME" note two updates back) — and localized to a
### specific, well-characterized stack-corruption mechanism

**The user captured full traces from a fresh boot (breakpoint at `0x0000`,
soft-reset, `trace` started right there) on both `pc1350` and `pc1360`,
typing `7/7`+ENTER on each.** `pc1350` shows the division result normally.
`pc1360` — in the user's words — "restarted itself." That turned out to be
literal, not a figure of speech.

**`0x0000` (the reset vector) appears in the `pc1360` trace exactly once,
never in `pc1350`'s.** The instruction immediately before it is an
ordinary `RTN` (`0116: RTN`) — i.e. **this isn't a hardware reset
instruction firing, it's a normal subroutine return popping a corrupted
return address off the internal call stack, and that corrupted value
happens to be `0x0000`.** Since address `0` is exactly where the ROM
starts after a real reset, the machine boots itself right back up — which
looks, from a screenshot, *identical* to "nothing happened," because the
post-crash boot screen is the same `RUN MODE >` prompt as before. **This
means every "blank screen" result recorded earlier in this doc (bare `1`,
bare `1+1`, `PRINT 1`, `PRINT 1+1`) needs to be treated as a suspected
silent reset, not confirmed silence** — checked this immediately against
this session's own `bare11_pc1360_clean.log` (the "1+1" sandbox trace) and
**it hits `0x0000` too**, at the exact same instruction pair (`0116: RTN`
→ `0000: WAIT e0`). Same bug, same mechanism, confirmed by two independent
tests on two different inputs (`1+1` and `7/7`).

**Localized the mechanism precisely.** The shared statement-dispatch
trampoline this doc has been calling "`0x1283`" gets entered multiple
times per line — once per keystroke (with `LIJ` set to a per-key marker:
`55`, `5b`, `79`, `82`, `cd`, ...), then twice more at the end: once with
`J=0x91` ("bare expression" marker, `CALL 4180`) and once with `J=0x88`
(`CALL 4174`, happens for every input regardless of `PRINT`). **The crash
happens on this final `J=0x88` pass, every time, in both the `1+1` and
`7/7` traces.** Walking `0x1283` in detail (cross-checked byte-for-byte
against `scops.hxx`'s actual opcode semantics, not guessed): it does a
temporary **ROM bank switch** (writes to `0x3400`, the bank-select port,
at `0x1296`), dispatches through `0xFF0D`, does its work, switches the
bank back (`0x12BF/0x12C2`), then — at `0x12B4-0x12BA` — **constructs its
own return address by reading two bytes out of external memory via the
`X` pointer** (`IX; DXL; PUSH; DXL; PUSH; DXL; DX`) instead of using the
address a normal `CALL` would have pushed, then `12C6: RTN`s into
whatever that constructed value is. This is a deliberate, working ROM
idiom elsewhere — but **the path taken to reach `0x12AC` differs between
the two passes**:
- The `J=0x91` pass reaches `0x12AC` via `0x6CD8`'s mechanism (previously
  documented in this doc as "returns normally") — **and succeeds.**
- The `J=0x88` pass reaches `0x12AC` via `0x6297`/`0x628B` — **the
  exact "backward-scan-for-CR-or-space" loop, and the `0x6288` address, a
  prior session had flagged as a dead end.** It is not a dead end — it's
  the road to the crash. This pass is the one that ends up reading `0x00,
  0x00` at `0x12B4-0x12BA` instead of a valid address.

**This means `X` is left pointing at the wrong place by the time the
`0x628B` route reaches `0x12B4`** — almost certainly because the backward
scan (which walks through the typed-line buffer looking for `CR`/space)
leaves `X` at a different offset than the `0x6CD8` route does, and
whatever fixed table or buffer position `0x12B4-0x12BA` expects to read a
real address from isn't there for this route.

**This is now, by a wide margin, the most concrete, most confidently
localized finding of the whole project** — a real, observable, two-line
crash signature (`RTN` → `0000:`), reproduced identically from two
completely independent full-boot traces on two different inputs. This is
almost certainly *the* `MACHINE_NOT_WORKING` bug, not a side issue.

**What's needed to close it out — this needs live register values, which
neither disassembly text nor this doc's trace files carry:** get the
value of `X` (`XL`/`XH`, or however the register pane names it) and `A`
at `0x12B5` and `0x12B7` — ideally on **both** an early, successful pass
(first `0x1283` entry after typing a character, or the `J=0x91` pass) and
the final, crashing `J=0x88` pass, so the two can be compared directly.
The easiest way: breakpoint at `0x12B4`, `go` repeatedly (it's hit ~14
times per line typed), and note `X`/`A` each time — the run right before
the CPU shows `PC=0000` is the crashing one. Also worth capturing: the two
bytes actually sitting in external memory at whatever address `X` resolves
to right before each `PUSH` (a memory-window watch on that address, or
just reading it once `X`'s value is known).

**Likely relevant to the `0x2C04` soft-reset-hang lead below:** that's
also a landed-in-the-wrong-place-after-a-line-was-typed symptom. Worth
re-examining once this mechanism is nailed down — they may be the same
underlying class of bug (`X` or another pointer ending up wrong after a
buffer scan) surfacing two different ways.

### OPEN — new lead from the user, not yet investigated: soft-reset-dependent hang

User's own report from their real MAME debugger session (verbatim
substance): typing `CLS` (as BASIC text) + ENTER sometimes leaves the CPU's
PC stuck around address **`0x2C04`** (inside the LCD/VRAM-mapped
`0x2800-0x37FF` range — i.e., if real, the CPU would be fetching
"instructions" from what should be display memory, which is exactly the kind
of thing a wild/corrupted jump produces) and requires a manual restart to
recover. **Critically: user says this does NOT happen on a fresh boot — it
only appears "after a few soft resets".** This strongly suggests some state
that the cold-boot RAM-init path clears correctly is NOT being reset on a
soft-reset path.

Two reproduction attempts in the sandbox (dedicated CLS key; typing the
letters C-L-S + ENTER) both FAILED to reproduce — PC ended up back in the
normal idle loop (`0x0102`) both times, and `0x2C04` never appeared in either
trace. This is expected given the user says it needs "a few soft resets"
first, which neither sandbox attempt performed (both were tested from a
single fresh boot). **User explicitly said to leave this for now** ("there
is obviously a bigger problem which needs deeper look") — this is flagged as
the most promising next thread but was NOT pursued further this session.

**Next steps for this thread, when resumed:**
1. Reproduce the soft-reset sequence the user actually used (need to ask:
   how do they trigger a "soft reset" in MAME — the EXTRA/"Reset" key
   (KEYCODE_F3, port `EXTRA`/"Reset"), or MAME's own machine reset (F3 by
   MAME convention may collide), or power-cycling via the `DSW0`/`KEY11`
   "Power" dip?). Try several soft-resets via the `EXTRA` "Reset" field
   in the sandbox, THEN type CLS, and see if `0x2C04` shows up.
2. Once reproduced, use a live write-tap / bank-tap (same technique as the
   PRINT investigation) rather than static trace-reading — it's much faster
   and has been the decisive tool every time in this project.
3. Cross-reference against the warm/cold-boot signature check at `0x0022`
   found during the boot-loader trace (see "Key ROM facts" above) — if a
   soft reset takes the "warm start, skip full init" path, some RAM region
   that would normally get zeroed on cold boot might retain garbage that
   later gets misinterpreted as a jump target.
4. This may or may not be the SAME root cause as the PRINT-no-output bug —
   both are "some BASIC command silently does something wrong after the
   machine's been running a while" — but treat that as a hypothesis to test,
   not an assumption.

## Pending decisions / things to ask the user before acting

- Whether to actually deliver a code fix for the `$33` bit-0x04
  graphics-gate finding (e.g., patch the user's own `plot_1360_core.asm`
  library to clear that bit before calling `GFX_PSET_ENTRY`, since it's the
  user's asm code, not the MAME driver, that's missing the precondition) —
  this was reported to the user as a finding but a fix was never requested/
  delivered. Also unresolved: WHY that bit is 1 during normal idle
  (something during boot/idle sets it) — real hardware apparently doesn't
  hit this because real-hardware testing was presumably done from a
  different calling context (e.g. via BASIC's own command dispatch, which
  might clear it as a side effect) rather than a "cold" debugger-injected
  CALL like the sandbox reproduction used.
- The graphics VRAM mapping range in the driver (`0x2800-0x37FF`, i.e. 4096
  bytes) is WIDER than the knowledgebase-documented real VRAM
  (`0x2800-0x31FF`, 2560 bytes) — never reconciled/investigated this
  session. Might be intentional slack, might be a latent bug. Worth a look.

## Files of note

- Modified + delivered: `src/mame/sharp/pocketc.cpp`,
  `src/mame/sharp/pc1360_m.cpp` (both on the user's `C:\mame`, RAM-mapping
  fix only — see "Fixed and delivered").
- User-uploaded reference (their own separate PC-1360 graphics library
  project, read-only reference, NOT part of MAME): `plot_1360_core.asm`
  (LCD_INIT/LCD_CLEAR_ROM/PLOT_PIXEL/CLEAR_PIXEL/TEST_PIXEL/DRAW_LINE),
  `plot_1360_boxes.asm` (DRAW_BOX/DRAW_BOX_FILLED, marked untested by the
  user themselves). Also uploaded: `pc1360_sc61860_full_disassembly.txt`
  (the buggy external disassembly — see "Confirmed NOT a bug" above).
- Sandbox scratch scripts from this session (only exist in `/tmp/mame` in
  THIS session's container — gone in a new chat, but names/techniques are
  reproducible from the methodology notes above): `plot_trace.lua`/`2`/`3`,
  `dasm_probe.lua`/`2`, `type_1plus1.lua`, `type_5.lua`, `type_print.lua`,
  `type_print_trace.lua`, `type_print_str.lua`, `type_print_longenter.lua`,
  `print_tap.lua`, `bank_tap.lua`, `boot_trace.lua`, `dasm_boot.lua`,
  `cls_stuck_check.lua`, `cls_trace.lua`, `type_cls_word.lua`. None of these
  files need to survive — they're simple enough to recreate from the
  patterns documented above if needed again.

## Suggested first move in a new chat

Read this file (`PC1360_DEBUG_STATUS.md`, delivered to the user's `C:\mame`
root), confirm with the user which of the two open threads to resume — the
`PRINT`-produces-no-output bug (next step: disassemble the untaken
`0x6289` branch) or the soft-reset `0x2C04` hang (next step: figure out how
the user actually triggers a soft reset, then reproduce with a live trace/tap)
— then re-set-up the sandbox (fresh MAME clone, clean NVRAM) and continue
using the Lua/debugger techniques documented above rather than re-deriving
them from scratch.

**NOTE (next session, before reading further): the driver has moved on a lot**
since the "Fixed and delivered" / "OPEN" sections above were written — a large
amount of keyboard-matrix and boot-sequence work happened in the gap (see
`pc1360_m.cpp`'s `in_a_r()` comments: the PD/0x3e00 row-decode fix, the
`m_fakey` auto-accept-the-boot-prompt timer, etc.). The PRINT-no-output and
soft-reset-hang threads above are still believed open but were NOT
re-verified this session — the session below was scoped entirely to the
boot-text bug (next section) at the user's request and didn't touch either of
them. Don't assume the old "Key ROM facts" boot-sequence trace or bank
numbers are still exactly accurate; the keyboard scan section in particular
has been substantially rewritten since.

## OPEN — new thread this session: boot-time RAM-CLEAR prompt shows only "K. Y" instead of the full message

**Symptom (user-reported, confirmed in sandbox):** at cold boot the LCD's
first line shows only `K. ?` (then `K. Y` once `m_fakey` auto-presses Y a
couple seconds later) — four characters, left-aligned, with one blank
character cell before the `K`. Screenshot evidence: `/tmp/mame/snap/pc1360/`
(any frame ≥5 in a fresh-NVRAM boot shows it; frame 1 already shows it, i.e.
it's present from the very first rendered frame).

### Root cause, fully confirmed (not a hypothesis)

The full intended prompt is **`RAM CARD S1 CLEAR O.K. ?`** (24 characters).
This string is a literal, null-terminated ASCII string sitting in ROM bank 3
(`b3-1360.rom`) at file offset `0xf23` (CPU address `0x4f23` when bank 3 is
paged in at `0x4000-0x7fff`) — confirmed by extracting the ROM file directly
from the user's `pc1360.zip` and running `strings` on it:

```
b3-1360.rom offsets (file offset : text):
  0xedb: RAM CARD S1 CLEAR O.K. ?
  0xeff: RAM CARD S2 CLEAR O.K. ?
  0xf23: RAM CARD S1 CLEAR O.K. ?      <- the one actually used at boot
  0xf3c: RAM CARD COPY ?
  0xf4c: COPY COMPLETED.
  0xf75: COPYING . . .
```

(Three near-identical string-table entries exist — two "S1" variants and one
"S2" — almost certainly selected by which RAM card slot(s) the self-test
below detects as present. Not yet investigated which of `0xedb`/`0xf23` a
real single-card machine is "supposed" to land on; on this driver it lands on
`0xf23`.)

**The print routine's resolved starting pointer is 20 bytes into this
string, not 0** — i.e. it starts at index 20 (`'K'`) instead of index 0
(`'R'`), which is exactly `"RAM CARD S1 CLEAR O."` (20 chars) short of the
full text, producing exactly the observed `"K. ?"` (indices 20-23 =
`K`,`.`,` `,`?`). Confirmed via a live capture, not just static ROM reading:
a Lua script (`/tmp/mame/msg_ptr3.lua` this session, not preserved — trivial
to recreate from the pattern below) tapped writes to `0x2a90` (a scratch
save-slot the ROM uses around each single-character print call) and, on the
first hit, read the CPU's internal-RAM-resident `A`/`B` register pair (SC61860
internal RAM offsets 2/3, exposed via the `cpu.items["0/m_ram"]`
save-state-item mechanism documented above — **not** reachable via
`prg:read_u8`, same caveat as the P/Q-addressed-RAM note earlier in this
file) at that point. Value seen: `A=0x38 B=0x4f` → pointer `0x4f38`. Reading
ROM from `0x4f38` (bank3 file offset `0xf38`) gives `". ?\0"` — this is
`0xf38 - 0xf23 = 21`, i.e. index 21 of the string (`'.'`), which is exactly
what you'd expect to see the pointer sitting at **after** it already
auto-incremented past the character it just printed (`'K'`, index 20) — see
`sc61860_copy_int()`/opcode `0x35` (`DATA`) in
`src/devices/cpu/sc61860/scops.hxx`, and the `4E00-4E0E` byte-at-a-time
read/terminator-check loop in ROM (disassembled live via `trace`, not from
the buggy external disassembly file). So the loop's true starting pointer,
before any character was consumed, was `0x4f37` = index 20 = `'K'`.

**Leading (strong, but not yet 100% nailed down) lead for *why* the pointer
lands at +20:** immediately after entering banked ROM at boot (`0x4001 ->
0x412d`), there's a hardware self-test loop at ROM `0x4176/0x4189-0x41ac`
(this is the same "hardware self-test/poll loop... expecting a 0x55
response" already noted as unexplored in the "Key ROM facts" section above —
it's now been traced in detail). The loop strobes keyboard rows 1-8 via
`0x3e00` (`INCB` from 1 to 8) and, specifically **at row 5**, compares the
`INA` (Port A) readback against the immediate `0x55` (`41AC: CPIA 55`). Live
trace confirms: in the sandbox this comparison **fails** every time (Port A
never reads back `0x55` at row 5 — this driver's `in_a_r()` has no mechanism
to produce that value; row 5 only ever returns real `KEY4`/"R $" input state
+ `m_outa` bits), which executes `SC` (set-carry, i.e. an explicit
"self-test failed" signal) at `0x41B0`, propagated back up through nested
`RTN`s via `JRCP` carry-checks (`4179: JRCP 4188` confirmed taken). This
strongly suggests `0x55` at keyboard-strobe-row-5 is **not a real key read**
but a hardwired self-test/ID handshake bit sharing the row-5 read path — i.e.
another instance of the same "PD/PA multiplexes something other than a plain
keyboard row" class of gap already found once this project (see the `$33`
bit-0x04 GFX_PSET gate, and the PD/0x3e00 1-indexed-row fix in `in_a_r()`).
**Not yet proven** that this specific self-test's pass/fail state is what
selects the `0xf23`-vs-`0xedb` string and/or the +20 offset into it (traced
the carry flag through a couple of nested returns but lost the thread before
reaching the actual index-selection code around ROM `0x48d7-0x48f4 ->
0x4dad -> ... -> 0x4df1`, where the real message-table lookup happens — see
below). Confirming/refuting this causal link, and finding the actual
index-selection code, is the concrete next step.

### Mechanics of the print routine (for whoever picks this up)

- `sc61860_copy_int()` (opcode `0x35`, disassembled `DATA`) is a general
  "copy N+1 bytes from `internal-RAM[A,B]`-addressed **program space**
  (despite the `/* internal rom! */` comment in `scops.hxx`, it reads
  whatever bank is currently paged in at the target address, confirmed: the
  message table above lives in **banked** ROM 3 at `0x4f23`, not the fixed
  0-0x1fff internal ROM) to `RAM[P]`" primitive, used for two unrelated
  purposes in this code path — vector/pointer-table lookups (2 bytes) *and*
  the font-glyph bitmap copy (6 bytes) inside the per-character print call.
  **`A`/`B` (internal RAM offsets 2/3) get reused for both purposes across
  the same call chain** — don't trust a captured `A`/`B` value unless you
  know exactly which of the two uses you're looking at (this cost real time
  this session; see "iram2/3 = font-table pointer, not message pointer"
  false lead avoided by instead tapping `0x2a90` as a sync point).
- ROM flow (all addresses in whichever bank is active at the time — banked
  ROM 3 for everything below except the actual glyph-table fetch, which
  briefly switches to bank 1 and back, same `0x3400<-01 ... 0x3400<-03`
  pattern documented for `GFX_PSET_ENTRY` above):
  `CALL 48d7` (generic dispatch/mode-check, reused elsewhere — the `CPIA 01`
  it does is comparing against whatever the *caller* just loaded, not a
  fixed self-test outcome; don't assume this is where the self-test result
  is consumed) `-> 48e1 -> 48f2: LIA 03 -> CALL 4dad` (pushes, sets a
  display-status bit at `0x303d`) `-> CALL 4f83` (full VRAM clear — fills
  all five `0x2800/2a00/2c00/2e00/3000` column-groups with 0, confirms
  nothing printed before this point survives on screen) `-> ` toggles
  `LCD_OFF`/`LCD_ON` `-> CALL 4e41` (swaps a word at `0x2a80`/`0x2a84`,
  purpose not yet determined) `-> ` a short keyboard-row-8 probe (`0x3e00<-
  08`, reads `INA`, tests bit `0x40` — **not** wired to anything in this
  driver's `in_a_r()` since row 8 falls outside the `t>=1&&t<=7` branch, so
  this probe's result is effectively always the same fixed value every boot
  in the sandbox; whether that's coincidentally "correct" or itself a latent
  gap is unknown) `-> 4df1: LII 01; LP 02; DATA` (the 2-byte vector fetch
  that resolves `A,B` to the actual message address — **this is the point to
  breakpoint/inspect if you pick this thread back up**, capture `A,B`
  *immediately* after this `DATA` + the following `EXAB`, at ROM address
  `0x4df6`, before anything reuses the registers) `-> ` byte-at-a-time loop
  at `4dff-4e0e` (read one char via `DATA` with `I=0`, `INCA` to manually
  advance the pointer, check for `0x00`/`0x0d` terminators) `-> ` if not a
  terminator, prints the character (`4e10` onward, eventually `CALL 4049 ->
  JP 1a04 -> ...-> 553e...` — the same font-glyph-copy machinery, bank-
  switching to 1 and back) `-> ` loops back to read the next character.
- **Tooling notes for next time:** `bpset(addr, cond, action)` (via
  `cpu.debug:bpset` in Lua) plus `dbg:command("go")` does **not** work as a
  synchronous "run until breakpoint, then inspect" primitive from an
  autoboot script — `dbg:command("go")` is fire-and-forget / non-blocking in
  this context, so a Lua script that calls `bpset` then immediately reads
  state afterward reads *reset-time* state, not breakpoint-hit state (cost
  real time this session before being caught — the read showed all-zero
  registers and `PC=0000`). Likewise a breakpoint `action` string using the
  debugger's own `printf`/`tracelog` commands produced no visible output
  under `-debugger none` headless mode (unclear whether it's silently
  discarded or written somewhere not checked — not investigated further).
  **What did work reliably**: `install_write_tap` on an ordinary
  program-space address that's touched at a known point in the sequence you
  care about, used purely as a synchronization signal, with the actual state
  inspection done via plain Lua reads (`cpu.state[...]`, or
  `emu.item(idx):read(offset)` for internal RAM) *inside* the tap callback
  — taps fire synchronously as part of that write executing, unlike
  breakpoints in this scripting setup.

### Next steps for this thread, when resumed

1. Find the actual `A,B` value the ROM computes at `0x4df6` (right after the
   vector-table fetch, per the tooling note above) across a couple of clean
   runs, and compare against `0x4f23` (start of the `0xf23` "S1 CLEAR"
   string) — confirm it's consistently `0x4f37` (+20) and not something that
   varies run to run (rule out the bank-switch-nondeterminism gremlin flagged
   elsewhere in this doc before concluding it's deterministic).
2. Walk backward from `0x4df1` to find what sets the *original* `A,B` before
   that 2-byte vector fetch (i.e. which slot of what's presumably a small
   jump/offset table is being read) — this is the actual index-selection
   code and hasn't been located yet. `sub_4dad`/`sub_4dc5`(`0x4e41` swap)
   /the row-8 probe right before `4df1` are the immediate suspects.
3. Separately, pin down what `0x55` at keyboard-strobe-row-5 is really
   supposed to mean on real hardware (the self-test at `0x4189-0x41ac`) —
   check the user's local SharpPocketLib knowledgebase / Berwanger-Saretz
   material for this specific self-test sequence before guessing; do **not**
   just make `in_a_r()` return `0x55` unconditionally for row 5 without
   understanding this, since row 5 is also the real `KEY4`/"R $" row and a
   blanket special-case would very likely break real R-key input (see the
   REVERTED CLS-hack precedent above for exactly this class of mistake).
4. Once both are understood, the likely fix is either (a) in `in_a_r()`,
   correctly modeling whatever row-5 hardware signal this self-test is
   really reading, if that's confirmed as the actual cause of the +20 skip,
   or (b) if the self-test turns out to be a red herring, whatever the real
   index-selection code at step 2 turns out to depend on.
5. `RAM CARD S1 CLEAR O.K. ?` also implies a full (not yet built)
   confirmation-echo path: real hardware presumably shows the *complete*
   string then appends the pressed key. Once the truncation is fixed, sanity
   check that `S2`'s variant (`0xeff`) is reachable too if the user ever
   configures a second card, and that the fix doesn't just hardcode string
   `0xf23`.

### UPDATE, same session — hypothesis DISPROVEN by direct test

Implemented the fix step 3/4 above pointed to (`m_selftest55` in
`pc1360.h`/`pc1360_m.cpp`: forces exactly `t==5` reads to return `0x55` for
a short window right after reset, timer-gated the same way `m_fakey` is).
Rebuilt and booted with clean NVRAM. Result: the ROM does **not** produce
the full RAM-CLEAR prompt — instead it enters a **hidden factory/service
menu**:

```
MENU
1:Display   4:Key
2:RAM Card  5:11pin I/O
3:KROM&RAM  6:15pin I/O
```

i.e. forcing the self-test to *pass* diverts the ROM into a diagnostic menu
never seen in normal use, not into a correctly-printed boot prompt. So a
self-test **PASS** is not the normal/intended boot path after all — self-test
**FAILURE** (the original, unmodified `in_a_r()` behavior) is what real
hardware almost certainly does too during ordinary power-on. The `+20`
offset into the RAM-CLEAR string is therefore **not** caused by this
self-test. The experimental code was reverted (behavior confirmed back to
baseline). Refined conclusion at the time: the real index-selection code
feeding the `0x4df1` vector fetch was still unlocated, and might be either a
computed index or a bad/wrong table-slot constant - see the RESOLVED section
below for how that was ultimately settled (it turned out to be neither: a
core CPU-emulation bug, not a ROM/index problem at all).

### RESOLVED, later same session: root cause found and fixed - a core bug in sc61860_copy_int() (the DATA opcode), not anything in pc1360_m.cpp

**tl;dr: the fix is one function in the shared CPU core**
(`src/devices/cpu/sc61860/scops.hxx`, `sc61860_device::sc61860_copy_int()`),
**not the PC-1360 driver at all.** Boot now shows the full, correct text -
confirmed live, screenshot attached
(`snap/pc1360_boot_fixed_full_text.png`):

```
MEM$ = "C"
RAM CARD S1 CLEAR O.K. ?
```

**How it was found.** Picked up exactly where step 2 above left off: wrote a
from-scratch Python transcription of the SC61860 disassembler
(`sc_dasm.py`, not preserved - trivial to recreate from
`src/devices/cpu/sc61860/scdasm.cpp`'s table, or ask a future session to
rebuild it) to statically walk backward from `0x4df1`, found and fixed a
table-offset bug in that transcription along the way (verified against
`sctable.hxx`'s numeric `case` dispatch values), then used it to disassemble
`0x4dad` onward. That function turned out to be a small "beep + pick a
message + print it" helper:

```
4dad: PUSH                     ; save caller's A (message selector)
4dae: LIDP  303d
4db1: ORID  04                 ; hardware side effect, unrelated
4db3: CALL  4f83                ; (a substantial subroutine, not traced further)
4db6: LIDP  303c
4db9: ANID  00
4dbb: LIA   00 ; 4dbd: CAL 13ca ; 4dbf: WAIT e0    ; two-tone beep via internal ROM
4dc1: LIA   01 ; 4dc3: CAL 13ca
4dc5: CALL  4e41
4dc8: POP                       ; A = selector again
4dc9: LIB   00
4dcb: LP 02 ; 4dcc: ADB          ; (A,B) doubled...
4dcd: LP 02 ; 4dce: ADB          ; ...doubled again -> (A,B) = 4*selector
4dcf: ADIA  50                  ; )
4dd1: PUSH                      ; ) (A,B) += 0x4e50  =>  table_addr = 0x4e50 + 4*selector
4dd2: LIA   4e                  ; )
4dd4: LP 03 ; 4dd5: ACDM         ; )
4dd6: LIP   5c ; 4dd8: ANIM 00   ; unrelated scratch-flag clear
4dda: OUTA                      ; outputs IA register, not A -- red herring, ignore
4ddb: LIDP 3e00 ; 4dde: LIA 08 ; 4de0: STD   ; strobes keyboard-line "row 8" (not a real
                                              ; 1-7 keyboard row -- see FIXME already in
                                              ; in_a_r() about PA bits 4-6/extra rows)
4de1: WAIT 42 ; 4de3: INA ; 4de4: TSIA 40     ; tests bit 0x40 of the Port A readback
4de6: POP                       ; A = pointer low byte again (flag from TSIA survives)
4de7: JRZP  4df1                ; if zero, skip the +2 below
4de9: ADIA  02 ; ...             ; else table_addr += 2 (an S1/S2-style variant slot --
                                  ; turned out to be a dead end, see below)
4df1: LII   01
4df3: LP     02
4df4: DATA                      ; <-- fetch 2 bytes from ROM[table_addr] into A,B
4df5: EXAB                      ; swap -> final message pointer
```

So `table_addr = 0x4e50 + 4*selector` (`+2` sometimes, gated on that row-8/PA
0x40 hardware test), and `4df4: DATA` fetches the 2-byte message pointer
*directly into A,B themselves* - i.e. the `DATA` opcode's destination (`P`,
set to `2` = the SC61860 internal-RAM offset of register `A`) **is the same
register pair being used as the source pointer for the fetch.** This is a
real, deliberate ROM idiom (self-referential "fetch a fresh pointer into the
pointer registers"), confirmed live via extensive Lua instrumentation
(`msgindex.lua` through `msgindex6.lua` this session, not preserved - the
technique is: `install_read_tap` at the addresses of interest, and inside
the callback read `A`/`B` via the `m_ram` save-state item, exactly as
documented in the "what worked" note above).

**The actual bug**, found by tracing this at the byte level
(`msgindex6.lua`'s output was the key evidence): `sc61860_copy_int()`
(implements opcode `0x35`/`DATA`) computes the source address for each byte
by calling `READ_RAM(A)|(READ_RAM(B)<<8)` **fresh, every iteration** -
including *after* it has already written that iteration's fetched byte into
the destination via `WRITE_RAM(m_p, t)`. When `m_p` happens to alias `A`
(exactly what this ROM idiom does, `P==2`), the *second* byte of the 2-byte
fetch ends up being read from an address built out of the *first fetched
byte* instead of the original table address:

- table entry for the observed selector (`3`, captured live at the `0x4dad`
  entry point) is at `0x4e5c`, containing bytes `4e d0` (confirmed both
  statically from the ROM file and live via `prg:read_u8`).
- iteration 0 (buggy): fetches `ROM[0x4e5c] = 0x4e`, writes it into `A`
  (since `m_p==A` this iteration) - **A is now `0x4e`, not the original
  low-address-byte `0x5c` any more.** The "auto-increment the pointer for
  next time" step then does `A = READ_RAM(A) + 1`, but `A` was *just*
  overwritten, so this computes `0x4e + 1 = 0x4f`, **not** `0x5c + 1 =
  0x5d`.
- iteration 1 (last, no further increment): fetches from `(A=0x4f,
  B=0x4e) = 0x4e4f`, which is a **completely unrelated ROM byte** (`0x37`,
  which just happens to be sitting inside the *middle* of one of the ROM's
  string tables) - not the intended second byte of the table entry
  (`0x4e5d = 0xd0`).
- Final pointer after `EXAB`: `0x4f37` - this is *exactly* the buggy pointer
  independently confirmed earlier this session (see the `0x2a90`-tap capture
  above, `A=0x38 B=0x4f` mid-string) - same root cause, now fully explained
  down to the byte.

**Proof this is a real bug and not intended ROM behavior:** simulated the
buggy mechanism in Python against all 10 real entries of this message table
(selectors `0`-`9`) - **5 of the 10 collapse to the identical wrong pointer
`0x4f37`**, and the other 5 collapse to a second identical wrong pointer
(`0x504e`). Ten supposedly-distinct system messages cannot legitimately
resolve to only two (both nonsensical, mid-string) addresses - that's a
strong signal the *mechanism*, not just this one selector, is broken.

**The fix** (`sc61860_copy_int()` in `scops.hxx`): track the source address
in a local variable instead of re-deriving it from `A`/`B` each iteration,
and only write the auto-incremented address back into `A` and/or `B` when
this iteration's destination write (`WRITE_RAM(m_p, t)`) didn't *already*
claim that specific register. This leaves the normal (non-aliased) case -
used everywhere else in the ROM, e.g. the per-character print loop at
`0x4dff-0x4e02`, which uses `count=0` with `P` pointed at the `K` register
and manually does `INCA`/`INCB` afterward, never touching this path at all -
completely unchanged, byte-for-byte identical to the original code. It only
changes behavior in the specific aliased case. Re-ran the same
all-10-selectors simulation against the fixed logic: **all 10 now resolve to
10 distinct, valid, readable ROM strings** (`CONVERTING . . .`, `WARNING
:...`, `MEM$ = "C"`+`RAM CARD S1 CLEAR O.K. ?`, `MEM$ = "D"`+..., `RAM CARD
COPY ?`, `COPY COMPLETED.`, `S1:SOURCE,S2:DEST O.`, `COPYING . . .` - real,
sensible PC-1360 RAM-card-operation messages, not a coincidence). Built and
booted live: the LCD now shows the complete, correct
`MEM$ = "C"` / `RAM CARD S1 CLEAR O.K. ?` text from the very first frame it
appears, and the rest of boot (auto-Y-press via `m_fakey`, reaching `RUN
MODE`) is unaffected - confirmed with a 15-second untouched boot run and
several screenshot sweeps at 0.1s granularity through the message's
appearance window.

**The +2/row-8 hardware test (steps 3-4 above) turned out to be a dead
end** for this specific bug - it's real ROM logic (probably a genuine S1/S2
card-variant selector for *some* other selector value), but it doesn't fire
differently in a way that matters for selector `3`/the RAM-clear prompt in
either the buggy or fixed traces captured this session. Left uninvestigated
further; not needed for this fix.

**Caveats / what's NOT yet done:**

- **This changes the shared `sc61860` CPU core**, used by `pc1350.cpp`,
  `pc1401.cpp`, `pc1251.cpp`, and `pc1403.cpp` too, not just `pc1360.cpp`.
  The fix is narrowly scoped (it only changes behavior when `P` aliases `A`
  or `B` during a `DATA` copy - verified the print loop and every other
  `count=0` usage found this session are unaffected), but the other four
  drivers were **not** regression-tested this session (no ROMs available in
  the sandbox for any of them - `cpu.rom`/`basic.rom`/`sc61860.a08`/etc. all
  reported "NOT FOUND"). If the user has those ROM sets, booting each of
  pc1350/pc1401/pc1251/pc1403 briefly and confirming they still reach a
  normal prompt (nothing more rigorous needed) would be a good sanity check
  before considering this fully safe.
- **The `m_selftest55` experiment (row-5 == `0x55` hack) from the DISPROVEN
  update above was never actually committed to `pc1360.h`/`pc1360_m.cpp` on
  `C:\mame`** (confirmed by re-staging those files this session - they were
  still in their pre-experiment form). Nothing to revert there; `in_a_r()`
  is, and remains, in its pre-this-session form (self-test at row 5 still
  legitimately fails, which is correct/intended real-hardware behavior, per
  the disproof above).
- **Not verified against real hardware** - same caveat as always with this
  project. The fix is derived from rigorous internal consistency (10/10
  selectors now resolve to valid distinct ROM strings, vs. 2 degenerate
  collisions before) rather than a hardware reference, so if real PC-1360
  units are ever available to cross-check, this would be worth confirming.
- File changed this session, now live on `C:\mame`:
  `src/devices/cpu/sc61860/scops.hxx` (the fix - `sc61860_copy_int()`). No
  other file needed changes; `pc1360.h`/`pc1360_m.cpp` are untouched.

---

## BREAKTHROUGH, new session: root cause of the silent-reset found — a stack-depth-dependent internal-RAM corruption triggered by the `LOOP` opcode in pc1360-exclusive banked ROM code

**Starting point.** The user did live breakpoint debugging on their own real MAME and pinned the crash to a specific instruction directly: breakpoint on `0x0116` (an `RTN`) during a "7/7"+ENTER test, register dump immediately before (`PC 0116, R 68, P 08, Q 09, X FD33, DP FB13, ...`) and immediately after (`PC 0000, R 6A, ...`). This confirmed, with live hardware-accurate register ground truth, that `0x0116: RTN` is the exact instruction whose `RTN` pops `0x0000` off the internal call stack and reboots the CPU — matching the `0116: RTN` → `0000:` signature found in both the user's own full-boot trace and this project's sandbox traces in earlier sessions. `R` (the CPU's internal-RAM call-stack pointer, exposed by MAME's debugger as `R`) going from `0x68` to `0x6A` — a clean +2 — confirmed the `RTN` mechanics themselves are fine; the bug is that the *bytes stored* at addresses `0x68`/`0x69` are wrong (`0x00,0x00`) rather than a valid return address.

**Instrumentation.** Since `0x0116`'s routine (`0x00B5`-`0x0116`, a generic "scan keyboard, timeout after L attempts, return" utility used constantly by idle/cursor-blink code) is entered via an ordinary `CALL`/`RTN` pair, the natural hypothesis was that something *writes* over the pushed return-address bytes while they're buried under further nested calls. To find it without real hardware access, added temporary instrumentation directly to the shared `sc61860` CPU core (`src/devices/cpu/sc61860/scops.hxx`, `sc61860.h` — **not** committed, fully reverted before this session ended):
- `sc61860_return()`: log `PC`, and the internal-RAM address(es) just popped, whenever the popped value resolves to `0x0000` (the crash signature) — pinpoints exactly *which* stack slot was read.
- `WRITE_RAM()`: log every write to a specific watched address — used in a second pass, watching the exact address the first pass identified.

(Also had to work around an unrelated environment issue: running the sandbox MAME with `-nothrottle` under `-video none` silently stalls the emulation's frame callback after a few hundred frames in this container — switched to normal-speed (`-seconds_to_run 20`, no `-nothrottle`) runs, which work reliably. Worth remembering for future sandbox sessions.)

**What the instrumentation found.** In the sandbox's own bare "1+1"+ENTER reproduction, the crashing `RTN` pops from address `0x62`/`0x63` — and a full-range write-watch confirmed **those addresses are never explicitly written at all**, anywhere in the run, by anything. Since the CPU's internal RAM is `memset` to all-zero at reset (`sc61860.cpp`, `device_reset()`), an address that's never written still reads as `0x00` — exactly matching the popped `0x0000`. So the bug isn't "something corrupts a valid return address" — it's **the CPU's stack pointer (`R`) drifting to an address that was never a live return-address slot in the first place**, and reading virgin zero bytes there.

Tracing the full call chain leading up to the crash (using the freshly-regenerated sandbox trace) found the drift mechanism precisely. The relevant snippet, from real trace output:

```
53B9: CALL  53c8      ; pushes return = 53BC
53C8: CAL   11b7  ... 53CA        ; nested call, returns cleanly
53CE: CAL   1124  ... 53D0        ; nested call, returns cleanly
53D0: LIA   4f        ; A = 0x4F (79 decimal)
53D2: PUSH            ; push A (0x4F) onto the internal stack - a scratch "counter"
53D3: IXL             ; (copy-loop body: advance X, load A)
53D4: IYS             ; (copy-loop body: advance Y, store A)
53D5: LOOP  53d3       ; <-- see below
   ... (repeats) ...
53D7: RTN             ; should return to 53BC
```

`sc61860_loop()`'s actual implementation (`scops.hxx`, unmodified/shared code used by every SC61860-based driver):

```cpp
void sc61860_device::sc61860_loop()
{
    uint16_t adr = m_pc - READ_OP_ARG();
    uint8_t t = READ_RAM(m_r) - 1;   // decrement the byte at the CURRENT stack pointer
    WRITE_RAM(m_r, t);
    m_carry = t == 0xff;
    if (!m_carry) {
        m_pc = adr;                  // branch back (keep looping)
        adr = POP();                 // <-- also pops (R++) on every continuing iteration!
    }
}
```

Because `POP()` runs on *every continuing iteration*, `LOOP` doesn't repeatedly decrement the one byte that was just pushed — after the very first iteration it advances to a *different, adjacent* stack byte each time, decrementing whatever it finds there and continuing until it happens to hit a byte that was already `0` (which then wraps to `0xFF` and stops the walk, leaving `R` sitting at that final address without popping it). In effect, this `PUSH 0x4F` / `LOOP` idiom in the ROM makes the CPU **walk upward through however many live stack bytes happen to sit above the pushed counter**, decrementing each one, until it finds a natural zero.

Confirmed exactly this in the instrumented run: the single write logged in the crash run was `WRITE_RAM[60]=ff (old=00) at PC=53d7`, i.e. the walk found a genuinely-untouched zero byte at `0x60` and stopped there, leaving `R=0x60`. But to get there, the walk necessarily passed *through* `0x62`/`0x63` — which is exactly `53B9`'s own pushed return address (`0x53BC`, low byte `0xBC`, high byte `0x53`) — decrementing both bytes in passing (each becomes stale garbage, not `0x53BC` anymore) before continuing on to find the real zero two bytes further out. The trace confirms the fallout directly: `53D7: RTN` — instead of returning to `53BC` as it should — jumps to **`00FF`**, i.e. it pops `(low=0xFF, high=0x00)`, the poisoned byte at `0x60` plus a genuine zero at `0x61`. `0x00FF` happens to be a valid *mid-routine* address inside the shared `00B5`/`0116` keyboard-scan routine (reached normally only by fall-through, never by `RTN`) — so execution gets hijacked into the *middle* of that routine with the wrong register state, runs through to its own `0116: RTN` exit, and *that* RTN is the one that finally pops genuine `0x0000` (since no real call ever set up a matching frame for this hijacked mid-routine entry) — producing the reset the user observed.

**Why this is pc1360-specific.** This `53B9`-`53D7` routine lives at address `0x53xx`, which is in the *banked external ROM* window (`0x4000`-`0x7FFF`) — code that only exists in pc1360's own ROM banks, not shared with pc1350 at all. Checked directly against the user's own real full-boot traces: `grep -c LOOP` on the pc1350 "7/7" trace returns **0** (pc1350 never executes the `LOOP` opcode at all during this scenario); the equivalent pc1360 trace shows **16** occurrences. So this isn't a case of "shared code reached from different depths" — it's PC1360-exclusive ROM content whose `PUSH`+`LOOP` idiom implicitly assumes a certain amount of zero-padding slack sits just above wherever it's called from before hitting any live stack data. On pc1360, the extra call nesting from the bank-switch dispatch trampoline (`0x1283`/`0x12A9`/`0xFF0D`, previously documented — itself confirmed stack-*balanced* and NOT the culprit, see below) evidently leaves less of that slack than this routine's `0x4F`-iteration cap assumes, so the walk overruns into a live frame.

**Cleared of suspicion this session:** the `0x12AC`-`0x12C6` "construct a jump address by reading 2 bytes via the X pointer, then `PUSH`/`PUSH`/`RTN`" mechanism flagged as suspicious in the previous session (used to redirect execution to `0x53B9` in this trace) is **not** the bug — traced it byte-for-byte and its two explicit `PUSH`es are consumed by exactly one subsequent `RTN` with nothing else touching the stack in between, so `R` is unchanged net. It's a legitimate (if unusual) indirect-jump idiom, not a leak.

**Status / what's NOT yet done:**
- No fix implemented yet. The `sc61860_loop()` semantics (decrement-and-conditionally-pop) are unmodified, shared, long-standing code with no history of being questioned — before touching it, want either (a) real SC61860 hardware/opcode documentation confirming the *intended* semantics (is walking through multiple stack bytes really how real hardware behaves, or is MAME's `adr=POP()` an emulation bug that should only fire on the *terminal* iteration, not every continuing one?), or (b) a regression sweep across every other SC61860 driver (pc1350/pc1401/pc1403/pc1251/pc1450/pc1260/pc1261/pc1245/pc1255) to see whether any of them exercise `LOOP` in a way a semantics change would visibly break. Sandbox has no ROMs for those other machines currently (per earlier session's note), so this needs either the user's own MAME or additional ROM sets.
- A second, real possibility: MAME zero-initializing internal RAM at reset (`memset` in `sc61860.cpp`) may be what makes this deterministic/100%-reproducible in the emulator, where real hardware's powered-on scratch RAM contents are presumably unpredictable garbage that only rarely happens to place a "convenient" zero byte close enough to cause visible corruption. Not verified either way — no real PC-1360 access.
- Debug instrumentation added this session (temporary logging in `scops.hxx`/`sc61860.h`) has been **fully reverted** — current sandbox state of `src/devices/cpu/sc61860/` matches pre-session (only the earlier, already-shipped `sc61860_copy_int` fix remains). Nothing new committed to `C:\mame` this session.
- Next concrete step: pick a specific candidate fix (leading candidate: only call `POP()` when the loop is about to fall through with `carry` clear because the *original* pushed byte reached zero on the very first iteration — i.e. make `LOOP` decrement-in-place without advancing `R`, and let the ROM's own code be responsible for a single explicit `POP` afterward if it wants the slot back) and empirically test it against both the pc1360 "1+1"/"7/7" case and whatever regression coverage is available.

---

## FIX SHIPPED, same session: `sc61860_loop()` corrected, pc1360 arithmetic now works

Implemented and validated the candidate fix flagged above. Final version of `sc61860_loop()` (`src/devices/cpu/sc61860/scops.hxx`):

```cpp
void sc61860_device::sc61860_loop()
{
    uint8_t r0 = m_r;
    uint16_t adr = m_pc - READ_OP_ARG();
    uint8_t t = READ_RAM(r0) - 1;
    WRITE_RAM(r0, t);
    m_zero=t==0;
    m_carry=t==0xff;
    if (!m_carry) {
        m_pc=adr;
        m_icount-=3;
    } else {
        POP();
    }
}
```

Two things changed from the original: (1) the byte being decremented is now pinned to `r0` (the value `m_r` had when the instruction *started*), so every continuing iteration decrements the *same* byte instead of walking to a new one; (2) the `POP()` (which advances `R` past the spent counter) now happens exactly once, on the iteration where the count is used up (`carry` set), instead of on every continuing iteration. Net effect over the whole loop: `R` is completely unchanged while looping, and moves by exactly +1 (undoing the preceding `PUSH`) only when the loop is finished — a clean, balanced `PUSH`/pop pair, and `LOOP` can no longer wander into adjacent stack memory.

**First attempt was wrong and instructive.** Tried removing the `POP()` entirely first (leave `R` untouched always). That *did* stop the reset-to-`0x0000` crash, but left the spent counter byte permanently un-popped, so the very next `RTN` (`0x53D7`, right after this routine, which has no cleanup `POP` of its own) misread the stack anyway — confirmed live: execution wandered off into unmapped memory (`0xFC0Bh` and climbing, decoding as endless `LII 00` from all-zero unmapped space) instead of returning correctly. Moving the `POP()` to fire once, on loop termination, fixed that: it restores the exact stack depth the ROM's parameterless `RTN` expects.

**Validated, this session, in the sandbox:**
- `1+1` + ENTER on pc1360 → displays `2.` (previously: silent reset back to `RUN MODE`).
- `7/7` + ENTER (the user's own original repro case) → displays `1.`.
- `9*6` + ENTER → `54.`; `8-5` + ENTER → `3.`. Four different expressions, four correct results, no resets.
- Regression: booted pc1350, pc1251, pc1401, pc1403, and pc1403h (ROM sets supplied by the user this session, all verify clean) for several seconds each with the fixed core — all boot normally, no crashes/hangs. This is expected to be risk-free for these five specifically: a live boot+idle trace (300k-500k instructions each) shows **zero** `LOOP` executions on every one of them, matching the real "7/7" pc1350 trace from earlier in this project (also zero) — the fixed code path is provably unreachable in their normal operation, not just untested.
- pc1350 also confirmed to boot, accept keystrokes, and reach a stable prompt with the fixed core (exact "1+1=2" screenshot wasn't captured cleanly due to the RAM-clear-confirm key timing being slightly different from pc1360's, but the "does it run/crash" check that actually matters for this fix passed).

**Caveats / what's NOT independently verified:**
- Still no real SC61860 hardware documentation confirming this is the *architecturally correct* semantics rather than a plausible-and-empirically-working guess. The fix is justified by internal consistency (stack balances correctly, matches what the ROM's code structure obviously expects) and by broad empirical testing, not a hardware reference.
- Only 6 of the SC61860 family's machines were regression-tested (pc1350, pc1251, pc1401, pc1403, pc1403h, plus pc1360 itself). Others in the family (pc1450, pc1260, pc1261, pc1245, pc1255) still have no ROMs in the sandbox and were not tested — low risk given the pattern above (LOOP appears rarely, if ever, outside pc1360-specific ROM content) but not proven for these specifically.
- `MACHINE_NOT_WORKING` flag on `pc1360.cpp` has **not** been removed yet - this fix resolves the specific reset-on-arithmetic bug that was this project's main focus, but the driver may still have other rough edges (graphics, banking edge cases, etc.) worth a broader pass before considering the flag safe to lift.

**File changed this session, ready to commit to `C:\mame`:** `src/devices/cpu/sc61860/scops.hxx` (both the `sc61860_loop()` fix above and the earlier `sc61860_copy_int()` fix remain in this file). No other files needed changes for this fix. Temporary debug instrumentation used to find the bug (separate logging code, `sc61860.h` additions) was fully reverted before this fix was written - it is not present in the delivered file.

---

## NEW THREAD, same session: boot-time "RAM CARD S1 CLEAR O.K.?" prompt doesn't respond to Y on cold boot (works after a soft reset)

User-reported, confirmed reproducible in the sandbox: on a fresh cold boot, the RAM-clear confirmation prompt appears but pressing Y (even held for a full second, tested at multiple durations) does nothing; after a manual soft reset the same prompt reappears and Y then works normally.

**Confirmed NOT related to the `sc61860_loop()` fix above** - traced a full cold-boot-through-Y-press window and it contains **zero** `LOOP` opcode executions, so this is a separate, pre-existing bug that just hadn't been noticed/isolated yet (all previous testing focused on typing calculator expressions after already getting past this prompt via the old auto-Y-press hack, which was removed earlier this project for unrelated reasons - see the "REMOVED" note in `pc1360_m.cpp`'s `in_a_r()`).

**Root cause isolated (not yet fully explained):** traced the CPU's execution from boot through the Y-press window. It does exactly one real keyboard-scan attempt (through the same `0x00B5`-`0x0116` routine the `LOOP` fix thread analyzed extensively) shortly after the prompt appears, finds no key pressed (correct, since nothing had been pressed yet), and then falls into what looks like a display/sound housekeeping loop in banked ROM around addresses `0x5940`-`0x596D` (calling into `0x18F6`-`0x1911` and `0x15CE`-`0x15FA` for beep/display-refresh work). Confirmed via `install_read_tap` at `0x5940` that bank `0x03` is active at the moment this loop is entered. **Critically: once in this loop, the trace shows zero further `INA` (read-Port-A) instructions and zero further returns to the `0x0160`/`0x0168`/`0x00B5` keyboard-scan entry points, for the remainder of a very long trace (460k+ further instructions, hundreds of frames)** - i.e. this loop never calls back to check the keyboard again, so no key press, however timed, can ever be detected once it's entered. This matches the user's symptom exactly: it isn't a debounce/timing issue, the polling has permanently stopped.

**Leading hypothesis for why a soft reset "fixes" it, not yet confirmed:** `pc1360_state` (and its base `pocketc_state`) has **no `device_reset()` override** - only `machine_start()`, which runs once at power-on. That means `m_bank`, `m_sysport[]`, `m_strobe`, `m_serial`, and `m_kb_strobe` are never reset back to power-on defaults by a soft reset; they retain whatever values the aborted first boot attempt left them at. Since the trap loop lives in *banked* ROM (address `0x5940` is only reachable through whichever bank is currently selected), if the ROM's cold-boot dispatch logic takes a bank-dependent branch at this point, a soft reset landing with a "dirty" `m_bank` (left over from the interrupted first attempt) could plausibly take a different branch that avoids the trap - explaining why attempt 2 succeeds. **Not yet verified**: attempts to directly compare bank state across a live soft reset hit sandbox tooling issues this session (MAME's Lua `machine:soft_reset()` appears to interact badly with `-autoboot_script` - possibly reloading/restarting the script - which repeatedly truncated log output before it could be captured; separately, combining an `install_read_tap` with the debugger's `reset` command crashed the sandbox MAME process with a segfault). Neither is a real-emulation finding, just sandbox test-harness friction to route around next time (e.g. drive two independent single-purpose runs instead of one run spanning a live reset, or avoid taps across a reset boundary).

**Status:** confirmed real, confirmed reproducible, confirmed distinct from the `LOOP` bug, root symptom (permanent keyboard-poll stop) isolated with a concrete address (`0x5940` entry, bank `0x03`) - but the actual trigger (why does the FIRST scan's "no key found" result lead into a loop that never polls again, when presumably on real hardware or in the intended design it should keep polling / blinking / waiting) is not yet found. Next step: figure out what's SUPPOSED to break the `0x5940`-`0x596D` loop and route back to a keyboard poll - likely a counter or flag check somewhere in that loop that isn't being satisfied - and separately, verify the `device_reset()` hypothesis by cleanly comparing bank/latch state across a soft reset using two independent runs rather than one live-reset run.

---

## CORRECTION + BREAKTHROUGH, same session: the "NEW THREAD" analysis above was chasing a red herring — real finding is a narrow, fixed-length keyboard-scan window right after every reset

Follow-up investigation (`let's investigate further`) initially tried to nail down the `device_reset()` hypothesis above by comparing CPU/bank state across a live soft reset within one running MAME process. This went down a long, confusing road that's worth documenting because the resolution changes the diagnosis substantially.

**Tooling trap found and fixed:** the scripts used to drive a "soft reset" mid-run were calling `dbg:command("reset")`. **`reset` is not a real MAME debugger command** — the actual commands are `softreset` and `hardreset` (confirmed by grepping `src/emu/debug/debugcmd.cpp`'s command registration table; there is no `"reset"` entry). `dbg:command("reset")` silently does nothing (no Lua error, no effect) — every "post-reset" trace/bank-check taken this session before this was caught was actually still observing the *original, never-reset* cold-boot session, which is exactly why bank stayed `0x03` "across the reset" and why the "post-reset" trace showed execution already deep in the `0x5940` idle loop within 10 frames (it had never left). This invalidated essentially all of the "device_reset()/bank hypothesis" experimentation from the previous update — none of it was actually testing a soft reset at all.

Separately, the earlier "long trace" comparisons (`long_trap_trace.log` vs `long_trap_trace_withY.log` vs `long_trap_trace_withY_v2.log`) that seemed to show a Y-press-triggered permanent polling stoppage turned out to be an artifact of *when the debugger `trace` command was turned on* (frame 10 vs frame 60), not of the Y press at all: a same-timing no-press control (`long_trap_trace_nopress_from10.log`, trace-on at frame 10, no key press) came back **byte-for-byte identical** to `long_trap_trace_withY.log` (trace-on at frame 10, Y pressed at frames 100-160). Same total line count (2,105,410), same INA-call counts and positions at every one of the three call sites (`0x0102`, `0x00DA`, `0x0148`). The Y press has **zero observable effect** on that trace. (Turning on `trace` at a different frame does perturb the CPU's own instruction stream measurably — likely a scheduling/cycle-rounding side effect of switching into debugger single-step mode at a different point — but that's a sandbox/tooling quirk, not a driver bug, and it doesn't change any of the conclusions below since it affects both arms of every comparison equally when controlled for.)

**Corrected root cause, now well-supported:** the `0x00FB`-`0x010B` keyboard-scan loop analyzed in the previous update is a **one-shot, fixed-iteration scan with a real hardware-style timeout**, not an indefinite "wait for keypress" loop. Direct disassembly confirms `010B: JRNZM 00fb` branches back to re-poll only while a decrementing counter (loaded via `00FB: LP 0b` / `00FC: LDM`) is nonzero; once it hits zero the code falls straight through to `010D` and, a bit further down, into the `0x5940`-`0x596D` display/idle housekeeping loop that (as previously found) never calls `INA` again for as long as we've ever traced it (confirmed again this session out to 400-600+ frames / roughly a minute of real time past reset).

The decisive experiment: **pressing Y early enough to land inside that scan window works, on both cold boot and a real (`softreset`-command-triggered) soft reset alike.**
- Cold boot, Y held from frame 10 through frame 90 (i.e. from the very start, before the scan's timeout at roughly frame 68): prompt is accepted, machine proceeds straight to `RUN MODE >`. Screenshot confirms.
- `softreset` triggered at frame 200, Y held from frame 210 to 290 (same +10..+90 relative offset from the reset point): **also** accepted, **also** reaches `RUN MODE >`. Screenshot confirms.
- By contrast, pressing Y later — frames 100-160 relative to cold boot (the timing used in essentially all of this project's earlier RAM-clear tests), or frames 500-560 relative to a reset at frame 400 — reliably fails in both cases, matching the originally-reported symptom.

So: **the scan window's timing and behavior is identical after a cold boot and after a soft reset.** It is *not* bank state, `device_reset()`, or anything else driver-latch-related — that hypothesis is now believed to be wrong (also directly checked: `0x3400` bank register reads `0x03` before, immediately after, and well after a genuine `softreset`-triggered reset — unchanged, consistent with the ROM's own boot code re-selecting the same bank early both times regardless of any latch-reset behavior).

At the actual frame rate observed in this sandbox (~9-10 fps under normal/throttled `-video none` operation — confirmed from multiple runs, e.g. 600 frames in 59 real seconds), that scan window is roughly **8-9 real seconds long**, not the sub-second window the "one legitimate scan attempt" framing in the previous update implied. That's a plausible, humanly-reachable reaction window — which points at the most likely explanation for the user's original report: on a **true cold launch** of MAME, several seconds of wall-clock time are consumed by MAME's own startup (window/audio/video init - stderr shows ALSA/XDG warnings during this phase every run) before the user can even see the prompt and react, plausibly eating enough of that ~9-second window that a normal-speed reaction arrives too late. On a **soft reset performed while MAME is already running and responsive**, there's no such startup overhead — the user sees the prompt reappear and can react well inside the window.

**Still open / not yet done:**
- Whether the underlying fixed-timeout-then-never-poll-again behavior is itself a bug (real hardware likely either loops indefinitely on this prompt, or has an interrupt-driven keyboard wake path that lets it notice a key press even after the busy-wait scan gives up) or is accurate original-hardware behavior has **not** been determined — no real-hardware reference available. Worth noting: the SC61860 CPU core in MAME (`src/devices/cpu/sc61860/`) has **no interrupt support implemented at all** (only a stray unused `SC61860_IRQ_STATE` comment) — if real hardware does rely on an interrupt to wake the keyboard scan after this point, that's a much larger gap than a driver-level fix could close.
- No code changes made this session for this thread — this update is purely a corrected diagnosis. Given the uncertainty above about whether "extend/loop the scan indefinitely" is the *correct* fix versus papering over real hardware timing, no fix has been attempted yet; this needs a decision (see next message to the user) on whether to (a) attempt a driver/ROM-interaction-level change that makes this scan retry indefinitely instead of timing out, accepting the risk of deviating from real hardware behavior without a way to verify it, or (b) leave as-is, since a soft reset already provides a reliable, easy user workaround, and document the behavior instead.

---

## Follow-up, same thread: user re-tested and cold boot now responds to Y normally

After the corrected diagnosis above, the user pointed out two things that ruled out my "MAME startup overhead eats the window" explanation: there's no boot delay on either the emulator or real hardware, and the prompt appears instantly either way. In parallel, this session re-tested the "does stale NVRAM change the picture" angle directly in the sandbox: deleted `nvram/pc1360/{cpu_nvram,ram_nvram}` (built up across many earlier test runs this session) and reran the cold-boot/soft-reset Y-timing comparison with genuinely fresh NVRAM. Result: unchanged from before — a late Y press (frames 100-160 relative to boot, matching the timing used throughout this project's testing) still fails identically on both cold boot and a `softreset`-triggered reset, confirmed via `execute_softreset()`'s source (`src/emu/debug/debugcmd.cpp`) calling exactly `m_machine.schedule_soft_reset()`, i.e. the same call MAME's UI Soft Reset hotkey makes — so NVRAM staleness is not the differentiator either.

While waiting on clarification about the user's exact repro steps, the user re-tested on their own real MAME and reported: **it now works — Y is accepted after a cold boot.** No driver change was made between their original report and this retest. This is consistent with (and arguably confirms) the "narrow scan window" finding from the previous update: the keyboard-detect window is real and a Y press landing inside it succeeds regardless of cold-vs-warm boot, so an apparently inconsistent real-world result (fails, then later succeeds, with no code changes) fits a human-reaction-time-vs-fixed-window explanation better than a deterministic cold/warm code-path difference. Under this reading, the original report may simply have been a case of the very first press(es) landing just outside the window, and a subsequent soft reset succeeding because that particular attempt's timing happened to land inside it — not because soft reset structurally behaves differently (sandbox testing this session found no evidence that it does).

**Net status:** no driver bug has been confirmed or fixed for this thread. The `0x00FB`-`0x010B` one-shot scan-then-give-up behavior is real and reproducible in the sandbox, but whether it's an actual defect (vs. accurate original hardware timing) is unresolved, and it may not even be the primary explanation for the user's original symptom given it now reproduces as intermittent rather than deterministic on their machine. No code changes made. Recommend treating this as low-priority/informational unless the user hits it again reliably enough to characterize further.
