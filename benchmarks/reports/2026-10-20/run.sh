#!/bin/bash
# protoJS on protoCore 2.14.8, master 45658eaf, Release: the standard
# comparison against Node 22 (three rounds) and the structure benchmarks
# (once), each run behind the quiet gate and on a fresh copy of the library.
set -u
WS=/home/gamarino/Documentos/proyectos
OUT=$WS/.agent_scratch/copy/js_suite; mkdir -p $OUT/libs
Q="python3 $WS/protoCore/docs/reports/data/2026-10-12-mark-stack-int-compare/quiet.py"
LIB=$WS/.agent_scratch/installers-oct2026/stage2148/usr/lib/libprotoCore.so.3
B=$WS/protoJS/build_2148/protojs
NODE=$(command -v node || ls -d $HOME/.nvm/versions/node/*/bin/node | tail -1)
export PATH=$(dirname $NODE):$PATH
fresh() { local d=$OUT/libs/$1; mkdir -p $d; cp -L $LIB $d/; export LD_LIBRARY_PATH=$d; }
for r in 1 2 3; do
  $Q > $OUT/quiet_js_r$r.txt 2>&1; fresh js_r$r; echo "$(date -u +%FT%TZ) js r$r"
  ( cd $WS/protoJS && PROTOJS_BIN=$B node tests/benchmarks/run_standard_comparison.js \
      > $OUT/js_r$r.txt 2>&1; cp tests/benchmarks/results/standard_comparison.json $OUT/js_r$r.json 2>/dev/null )
done
for wl in records join doctree wordfreq graph; do
  $Q > $OUT/quiet_struct_$wl.txt 2>&1; fresh struct_$wl; echo "$(date -u +%FT%TZ) struct $wl"
  ( cd $WS/protoJS/benchmarks/structures && python3 run.py --protojs $B --reps 3 \
      --ns 1,2,4,6,12 --scale 1 --heap-cells 40000000 --memory-max 4G --only $wl \
      --out $OUT/structures.jsonl >> $OUT/structures.log 2>&1; echo "rc=$?" >> $OUT/structures.log )
done
echo DONE
