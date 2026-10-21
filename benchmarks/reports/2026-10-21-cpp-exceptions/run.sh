#!/bin/bash
# protoJS perf/cpp-exceptions (fb95f3c5d, JavaScript exceptions as C++
# exceptions) against its base perf/fast-paths (8182924ea), Release, protoCore
# 3.0.0: the standard comparison against Node 22 (three rounds, the two builds
# interleaved), the exception probes and the structure benchmarks (once each),
# each run behind the quiet gate and on a fresh copy of the library.
set -u
WS=/home/gamarino/Documentos/proyectos
OUT=$WS/.agent_scratch/js_suite_final; mkdir -p $OUT/libs
Q="python3 $WS/protoCore/docs/reports/data/2026-10-12-mark-stack-int-compare/quiet.py"
LIB=$WS/.agent_scratch/installers-oct2026/stage300/usr/lib/libprotoCore.so.4
NEW=$WS/.agent_scratch/js_wt_final/build/protojs
BASE=$WS/protoJS/build_300/protojs
NODE=$(command -v node || ls -d $HOME/.nvm/versions/node/*/bin/node | tail -1)
export PATH=$(dirname $NODE):$PATH
fresh() { local d=$OUT/libs/$1; mkdir -p $d; cp -L $LIB $d/; export LD_LIBRARY_PATH=$d; }
for r in 1 2 3; do
  for v in new base; do
    B=$NEW; [ $v = base ] && B=$BASE
    $Q > $OUT/quiet_${v}_r$r.txt 2>&1; fresh ${v}_r$r; echo "$(date -u +%FT%TZ) $v r$r"
    ( cd $WS/protoJS && PROTOJS_BIN=$B node tests/benchmarks/run_standard_comparison.js \
        > $OUT/${v}_r$r.txt 2>&1; cp tests/benchmarks/results/standard_comparison.json $OUT/${v}_r$r.json 2>/dev/null )
  done
done
P=$(dirname $0)/probes
$Q > $OUT/quiet_probes.txt 2>&1; fresh probes
for f in $P/*.js; do
  for v in new base node; do
    B=$NEW; [ $v = base ] && B=$BASE; [ $v = node ] && B=node
    c=$(perf stat -r 3 -x, -e cycles $B $f 2>&1 >/dev/null | cut -d, -f1)
    echo "$(basename $f .js) $v $c" >> $OUT/probes.txt
  done
done
for v in new base; do
  B=$NEW; [ $v = base ] && B=$BASE
  for wl in records join doctree wordfreq graph; do
    $Q > $OUT/quiet_struct_${v}_$wl.txt 2>&1; fresh struct_${v}_$wl; echo "$(date -u +%FT%TZ) struct $v $wl"
    ( cd $WS/protoJS/benchmarks/structures && python3 run.py --protojs $B --reps 3 \
        --ns 1,2,4,6,12 --scale 1 --heap-cells 40000000 --memory-max 4G --only $wl \
        --out $OUT/structures_$v.jsonl >> $OUT/structures_$v.log 2>&1; echo "rc=$?" >> $OUT/structures_$v.log )
  done
done
echo DONE
