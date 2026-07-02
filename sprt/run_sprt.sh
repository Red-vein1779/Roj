#!/usr/bin/env bash
# Roj chess engine — Phase 2, Step 11: self-play SPRT harness (fastchess).
#
# Hardware-tuned per docs/phase2.md §10:
#   - self-play A/B: quiescence search ON vs OFF (one binary, the `Qsearch` UCI option)
#   - TC 8+0.08 (STC workhorse), each engine single-threaded (Roj has no Threads option)
#   - concurrency 6 (of 8 physical cores; lower it if ANY time-loss appears — the
#     invariant is ZERO time-losses, concurrency is the knob)
#   - balanced book 8moves_v3.pgn, paired/colour-swapped games (-repeat -games 2)
#   - pentanomial statistics (-report penta=true)
#   - SPRT bounds [0,10], alpha=beta=0.05 (large Elo gain, still-weak engine)
#
# fastchess, the book and the engine binaries are TEST DATA / TOOLS (originality-
# neutral, §5) and are NOT committed; see README.md for how to fetch them. This
# script IS the committed, reproducible harness.
#
# Usage:  ./run_sprt.sh [concurrency]     (default 6)

set -u
cd "$(dirname "$0")"                     # -> sprt/
ROOT="$(cd .. && pwd)"

CONCURRENCY="${1:-6}"
mkdir -p out
cp "$ROOT/Roj.exe" bin/Roj.exe           # always test the freshly built engine
cd bin

./fastchess.exe \
  -engine cmd=Roj.exe name=Roj_qon  option.Qsearch=true  option.Hash=16 \
  -engine cmd=Roj.exe name=Roj_qoff option.Qsearch=false option.Hash=16 \
  -each proto=uci tc=8+0.08 \
  -openings file=8moves_v3.pgn format=pgn order=random \
  -rounds 20000 -games 2 -repeat -recover -srand 42 \
  -concurrency "$CONCURRENCY" -report penta=true \
  -sprt elo0=0 elo1=10 alpha=0.05 beta=0.05 \
  -pgnout file=../out/roj_qsearch.pgn
