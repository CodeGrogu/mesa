#!/bin/sh
# Serial HD 620 runs: each probe on each pinned build, one at a time.
# Run from HD620-full-audit/.
set -u
E=evidence/iris-timer-conversion
for probe in gl-timer-conversion gl-timer-ordering; do
  for build in original:/build/iris-timer-baseline order-only:/build/iris-timer-order-only new:/build/audit-iris; do
    name=${build%%:*}; dir=${build#*:}
    docker exec hd620-mesa sha256sum $dir/src/gallium/targets/dri/libgallium-27.0.0-devel.so > $E/$name-$probe.log
    docker exec -i -e EGL_TEST_LIBRARY=$dir/src/egl/libEGL.so.1.0.0 hd620-mesa python3 - < $probe.py >> $E/$name-$probe.log 2>&1
    echo "exit status $?" >> $E/$name-$probe.log
  done
done
docker exec -i -e EGL_TEST_LIBRARY=/build/audit-iris/src/egl/libEGL.so.1.0.0 hd620-mesa python3 - < gl-compute-timing.py > $E/new-gl-compute-timing.log 2>&1
echo "exit status $?" >> $E/new-gl-compute-timing.log
