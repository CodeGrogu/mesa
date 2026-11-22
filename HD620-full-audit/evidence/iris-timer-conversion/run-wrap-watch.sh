#!/bin/sh
# Wait until ~90 s before the next 36-bit CS timestamp rollover, then loop
# GPU-path query-buffer resolves for 240 s on the new build.  Run from
# HD620-full-audit/.
set -u
E=evidence/iris-timer-conversion
info=$(docker exec hd620-mesa /build/mi-test/rawts)
echo "$info" > $E/new-wrap-watch.log
wait_s=$(echo "$info" | sed 's/.*seconds_to_wrap=\([0-9.]*\).*/\1/' | awk '{s=int($1)-90; print (s>0)?s:0}')
echo "sleeping $wait_s s" >> $E/new-wrap-watch.log
sleep "$wait_s"
docker exec hd620-mesa /build/mi-test/rawts >> $E/new-wrap-watch.log
docker exec hd620-mesa sha256sum /build/audit-iris/src/gallium/targets/dri/libgallium-27.0.0-devel.so >> $E/new-wrap-watch.log
docker exec -i -e EGL_TEST_LIBRARY=/build/audit-iris/src/egl/libEGL.so.1.0.0 hd620-mesa python3 - --wrap 240 < gl-timer-conversion.py >> $E/new-wrap-watch.log 2>&1
echo "exit status $?" >> $E/new-wrap-watch.log
docker exec hd620-mesa /build/mi-test/rawts >> $E/new-wrap-watch.log
