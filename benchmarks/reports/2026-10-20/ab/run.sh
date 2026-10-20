#!/bin/bash
# Same-moment A/B: {protoJS build of 2026-10-18 (build_cmp2146), master build_2148}
# x {protoCore 2.14.7, 2.14.8}, standard comparison, 3 interleaved rounds.
set -u
WS=/home/gamarino/Documentos/proyectos
OUT=$WS/.agent_scratch/copy/js_ab; mkdir -p $OUT/libs
NODE=$(command -v node || ls -d $HOME/.nvm/versions/node/*/bin/node | tail -1); export PATH=$(dirname $NODE):$PATH
L7=$WS/.agent_scratch/installers-oct2026/stage2147/usr/lib/x86_64-linux-gnu/libprotoCore.so.3
L8=$WS/.agent_scratch/installers-oct2026/stage2148/usr/lib/libprotoCore.so.3
for r in 1 2 3; do
  for bin in cmp2146 2148; do
    for lib in 7 8; do
      d=$OUT/libs/${bin}_${lib}_$r; mkdir -p $d; [ $lib = 7 ] && cp -L $L7 $d/ || cp -L $L8 $d/
      export LD_LIBRARY_PATH=$d
      ( cd $WS/protoJS && PROTOJS_BIN=$WS/protoJS/build_$bin/protojs node tests/benchmarks/run_standard_comparison.js > /dev/null 2>&1; \
        cp tests/benchmarks/results/standard_comparison.json $OUT/${bin}_${lib}_r$r.json )
      echo "$bin $lib r$r done"
    done
  done
done
