#!/bin/sh
# Renders every major mode headless (Xvfb + Mesa) and fails if any image comes
# out black. Catches GL errors that silently skip draw calls.
# Usage: tools/smoke_render.sh [build-dir]
set -e
BUILD=${1:-build}
OUT=$(mktemp -d)
RUN="xvfb-run -a -s \"-screen 0 480x270x24\" $BUILD/blackhole --size 480x270 --frames 2 --hide-ui"

shot() {  # name, extra args
  eval "$RUN $2 --screenshot $OUT/$1.png" > "$OUT/$1.log" 2>&1 || { cat "$OUT/$1.log"; echo "FAIL $1 (crash)"; exit 1; }
  if grep -q "\[shader\]\|\[gl\]" "$OUT/$1.log"; then cat "$OUT/$1.log"; echo "FAIL $1 (GL/shader error)"; exit 1; fi
  python3 -I - "$OUT/$1.png" "$1" <<'PY'
import sys
from PIL import Image, ImageStat
im = Image.open(sys.argv[1]).convert("L")
peak = im.getextrema()[1]
mean = ImageStat.Stat(im).mean[0]
ok = peak > 60 and mean > 0.2
print(f"{'ok  ' if ok else 'FAIL'} {sys.argv[2]:<28} peak {peak:3d} mean {mean:6.2f}")
sys.exit(0 if ok else 1)
PY
}

shot disk                 "--scenario 0 --warmup 100"
shot waves                "--scenario 1 --warmup 100"
shot star                 "--scenario 2 --warmup 300"
shot lens-disk            "--scenario 0 --lensing --view 0,0.12,45"
shot lens-kerr            "--scenario 0 --lensing --spin 0.9 --view 0,0.12,45"
shot lens-particles       "--scenario 0 --warmup 100 --lensing --lens-source particles --view 0,0.15,60"
shot lens-waves           "--scenario 1 --warmup 100 --lensing"
shot lens-both-kerr       "--scenario 0 --warmup 100 --lensing --lens-source both --spin 0.7 --view 0,0.15,60"

# Video recording path (sub-frames + GPU downsample).
eval "xvfb-run -a -s \"-screen 0 480x270x24\" $BUILD/blackhole --size 480x270 --lensing --record $OUT/rec --record-size 240x136 --duration 0.25 --fps 8 --quality max" > "$OUT/rec.log" 2>&1
python3 -I - "$OUT/rec/frame_00001.jpg" <<'PY'
import sys
from PIL import Image
peak = Image.open(sys.argv[1]).convert("L").getextrema()[1]
print(f"{'ok  ' if peak > 60 else 'FAIL'} {'record --quality max':<28} peak {peak:3d}")
sys.exit(0 if peak > 60 else 1)
PY
echo "all smoke renders passed ($OUT)"
