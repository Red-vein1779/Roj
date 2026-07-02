# Roj — SPRT / test-infrastructure harness (Phase 2, Step 10 track)

This directory is the **committed, reproducible** test harness for Roj: the
fastchess SPRT setup described in `docs/phase2.md` §10. Only the *scripts and
configuration* live in git — the tools, engine binaries, opening book and game
output are downloaded/generated locally and are **git-ignored** (see `.gitignore`).
Per §5 these tools and reference engines are **test data / opponents only**; no
engine code is copied into Roj.

## Layout

```
sprt/
  run_sprt.sh     committed  — the fastchess self-play SPRT invocation
  README.md       committed  — this file
  .gitignore      committed  — excludes bin/ and out/
  bin/            ignored    — fastchess, Stockfish, the book, a copy of Roj.exe
  out/            ignored    — PGN + run logs
```

## Pinned tools & opponents (versions locked)

| Item            | Version / build                                   | Role                         |
|-----------------|---------------------------------------------------|------------------------------|
| fastchess       | **v1.8.0-alpha** (windows-x86-64, CI 20260128)    | match runner / SPRT / compliance |
| opening book    | **8moves_v3.pgn** (official-stockfish/books)      | balanced book, test data     |
| Stockfish       | **sf_18** (windows-x86-64-sse41-popcnt)           | adjustable anchor (UCI_LimitStrength + UCI_Elo) — opponent only |

Reference-engine notes (§10): Stockfish is the *adjustable* anchor — run it with
`option.UCI_LimitStrength=true option.UCI_Elo=<n>` and raise `UCI_Elo` as Roj grows.
A fixed weak pin such as **TSCP** (xboard, `proto=xboard`) is left as a follow-up;
it is not needed for the Step 11 self-play SPRT gate.

## How to fetch the tools (not committed)

```bash
mkdir -p sprt/bin sprt/out
# fastchess (prebuilt Windows binary)
curl -sL -o sprt/bin/fastchess-win.zip \
  https://github.com/Disservin/fastchess/releases/download/v1.8.0-alpha/fastchess-windows-x86-64.zip
#   unzip and place fastchess.exe in sprt/bin/
# opening book
curl -sL -o sprt/bin/8moves_v3.pgn.zip \
  https://github.com/official-stockfish/books/raw/master/8moves_v3.pgn.zip
#   unzip 8moves_v3.pgn into sprt/bin/
# Stockfish (adjustable anchor)
curl -sL -o sprt/bin/sf.zip \
  https://github.com/official-stockfish/Stockfish/releases/download/sf_18/stockfish-windows-x86-64-sse41-popcnt.zip
#   unzip and place stockfish.exe in sprt/bin/
```

## The A/B knob

Roj exposes a **`Qsearch` UCI option** (`type check default true`). It is a test
knob only — default `true` is the normal search. The self-play SPRT pits
quiescence **on vs off** using one binary and two option settings, so the two sides
differ only in the feature under test.

## Running the self-play SPRT

```bash
bash sprt/run_sprt.sh [concurrency]   # default concurrency 6
```

It copies the freshly built `Roj.exe` into `bin/`, then runs quiescence ON vs OFF
at **TC 8+0.08**, balanced book, paired colour-swapped games, **pentanomial**
statistics, SPRT bounds **[0, 10]** (α=β=0.05). Roj is single-threaded by design
(no `Threads` option), so `option.Threads` is intentionally omitted.

**Invariant (§10): zero time-losses.** Start at concurrency 6 (of 8 physical
cores); if any time-loss appears, lower concurrency — it is the knob, zero
time-losses is the gate. Time management (Step 9) was validated at a longer control
before dropping to 8+0.08.

## UCI compliance

```bash
sprt/bin/fastchess.exe --compliance ./Roj.exe
```
