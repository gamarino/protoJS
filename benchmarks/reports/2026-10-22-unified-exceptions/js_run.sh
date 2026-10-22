#!/bin/bash
# protoJS BEFORE (fb95f3c5d on protoCore cf52f9f5) vs AFTER (40f7d66f3 on
# protoCore 65760e4a), Release, clean worktrees. Each run behind the quiet gate
# and on a fresh copy of the variant's libprotoCore.
set -u
export QUIET_CORES=${QUIET_CORES:-4.0}
WS=/home/gamarino/Documentos/proyectos
M=$WS/.agent_scratch/meas_1022; OUT=$M/js_out; mkdir -p $OUT/libs
Q="python3 $WS/protoCore/docs/reports/data/2026-10-12-mark-stack-int-compare/quiet.py"
NODE=$HOME/.nvm/versions/node/v22.17.0/bin/node; export PATH=$(dirname $NODE):$PATH
bin() { echo $M/$1/protoJS/build/protojs; }
lib() { echo $M/$1/protoCore/build_release/libprotoCore.so.4; }
fresh() { local d=$OUT/libs/$2; mkdir -p $d; cp -L $(lib $1) $d/libprotoCore.so.4; export LD_LIBRARY_PATH=$d
          echo "== $2" >> $OUT/ldd.txt; ldd $(bin $1) | grep -i protocore >> $OUT/ldd.txt; }
# 1. self-report verification (each variant on its own fresh lib)
for v in before after; do fresh $v verify_$v; done
( d1=$OUT/libs/verify_before; d2=$OUT/libs/verify_after
  cat > $OUT/wrap_before.sh <<W
#!/bin/sh
LD_LIBRARY_PATH=$d1 exec $(bin before) "\$@"
W
  cat > $OUT/wrap_after.sh <<W
#!/bin/sh
LD_LIBRARY_PATH=$d2 exec $(bin after) "\$@"
W
  chmod +x $OUT/wrap_*.sh; unset LD_LIBRARY_PATH
  python3 $M/js_verify.py $OUT/wrap_before.sh $OUT/wrap_after.sh > $OUT/verify.txt 2>&1 )
# 2. standard comparison, three rounds, order alternated per round
for r in 1 2 3; do
  order="after before"; [ $r = 2 ] && order="before after"
  for v in $order; do
    $Q > $OUT/quiet_${v}_r$r.txt 2>&1; fresh $v ${v}_r$r; echo "$(date -u +%FT%TZ) std $v r$r"
    ( cd $M/after/protoJS && PROTOJS_BIN=$(bin $v) node tests/benchmarks/run_standard_comparison.js \
        > $OUT/${v}_r$r.txt 2>&1; cp tests/benchmarks/results/standard_comparison.json $OUT/${v}_r$r.json 2>/dev/null )
  done
done
# 3. probes, perf stat -r 3 cycles, interleaved per probe
$Q > $OUT/quiet_probes.txt 2>&1; fresh before probes_before; LB=$LD_LIBRARY_PATH; fresh after probes_after; LA=$LD_LIBRARY_PATH; unset LD_LIBRARY_PATH
echo "$(date -u +%FT%TZ) probes"
for f in $M/js_probes/*.js; do
  for v in before after node; do
    case $v in before) c="env LD_LIBRARY_PATH=$LB $(bin before)";; after) c="env LD_LIBRARY_PATH=$LA $(bin after)";; node) c=node;; esac
    o=$(perf stat -r 3 -x, -e cycles,task-clock $c $f 2>&1 >/dev/null | grep -E 'cycles|task-clock' | cut -d, -f1 | paste -sd' ')
    echo "$(basename $f .js) $v $o" >> $OUT/probes.txt
  done
done
# 4. structure benchmarks
for wl in records join doctree wordfreq graph; do
  for v in before after; do
    $Q > $OUT/quiet_struct_${v}_$wl.txt 2>&1; fresh $v struct_${v}_$wl; echo "$(date -u +%FT%TZ) struct $v $wl"
    ( cd $M/after/protoJS/benchmarks/structures && python3 run.py --protojs $(bin $v) --reps 3 \
        --ns 1,2,4,6,12 --scale 1 --heap-cells 40000000 --memory-max 4G --only $wl \
        --out $OUT/structures_$v.jsonl >> $OUT/structures_$v.log 2>&1; echo "rc=$?" >> $OUT/structures_$v.log )
  done
done
echo DONE
