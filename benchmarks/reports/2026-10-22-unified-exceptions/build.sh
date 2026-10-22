#!/bin/bash
set -ux
M=/home/gamarino/Documentos/proyectos/.agent_scratch/meas_1022
for v in before after; do
  cd $M/$v/protoCore && cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release > $M/build_${v}_core_cfg.log 2>&1 && \
   cmake --build build_release --target protoCore -j6 > $M/build_${v}_core.log 2>&1; echo "core $v rc=$?"
done
for v in before after; do
  cd $M/$v/protoJS && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_protoCore=ON > $M/build_${v}_js_cfg.log 2>&1 && \
   cmake --build build --target protojs -j4 > $M/build_${v}_js.log 2>&1; echo "js $v rc=$?"
done
for v in before after; do
  cd $M/$v/protoPython && cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_protoCore=ON > $M/build_${v}_py_cfg.log 2>&1 && \
   cmake --build build_release --target protopy protopyc run_module -j6 > $M/build_${v}_py.log 2>&1; echo "py $v rc=$?"
done
echo BUILD_DONE
