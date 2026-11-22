#!/bin/sh
# Serial HD 620 runs of the saturation probes. Run from HD620-full-audit/,
# only when no other GPU job is running.
#   presat = /build/iris-presat (copy of /build/audit-iris taken BEFORE rebuilding)
#   new    = /build/audit-iris  (rebuilt with the saturation change)
set -eu
E=evidence/iris-query-saturation
LIB=src/gallium/targets/dri/libgallium-27.0.0-devel.so
for build in presat:/build/iris-presat new:/build/audit-iris; do
  name=${build%%:*}; dir=${build#*:}
  for probe in gl-query-saturation gl-timer-conversion; do
    log=$E/$name-$probe.log
    docker exec hd620-mesa sha256sum "$dir/$LIB" > "$log"
    legacy=0
    expected_status=0
    expected_failures=0
    if [ "$name" = presat ]; then
      legacy=1
      if [ "$probe" = gl-query-saturation ]; then
        expected_status=1
        expected_failures=4
      fi
    fi
    status=0
    docker exec -i -e "EGL_TEST_LIBRARY=$dir/src/egl/libEGL.so.1.0.0" \
      -e "IRIS_TEST_LEGACY_QUERY_TRUNCATION=$legacy" hd620-mesa \
      python3 - < "$probe.py" >> "$log" 2>&1 || status=$?
    printf 'exit status %s\n' "$status" >> "$log"
    test "$status" -eq "$expected_status"
    grep -q "^$expected_failures check(s) failed" "$log"
    grep -Fxq "Mapped driver library $dir/$LIB" "$log"
  done
done
