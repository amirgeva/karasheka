#!/usr/bin/env bash
# Records the gameplay clip and encodes docs/gameplay.mp4 (1280x720, 30 fps)
# and docs/gameplay.gif (640 px, 15 fps), then renders the cover image
# docs/cover.png (1280x640, GitHub's social preview size) and a screenshot
# docs/screenshot.png (1280x720).
# Needs ffmpeg (with libx264) on the PATH.
#
# Usage: tools/make_media.sh [path/to/karasheka]
#   defaults to build/Release/karasheka (Visual Studio) or build/karasheka
set -euo pipefail

cd "$(dirname "$0")/.."
GAME=${1:-}
if [[ -z "$GAME" ]]; then
  if [[ -x build/Release/karasheka || -x build/Release/karasheka.exe ]]; then
    GAME=build/Release/karasheka
  else
    GAME=build/karasheka
  fi
fi

W=1280
H=720
FPS=30
GIF_W=640
GIF_FPS=15
OUT=docs
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT"

echo "Recording gameplay..."
"$GAME" --record - --size "${W}x${H}" --fps "$FPS" |
  ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgb24 -s "${W}x${H}" -r "$FPS" -i - \
    -c:v ffv1 "$WORK/clip.mkv"

echo "Encoding $OUT/gameplay.mp4..."
ffmpeg -y -loglevel error -i "$WORK/clip.mkv" \
  -c:v libx264 -preset slow -crf 18 -pix_fmt yuv420p -movflags +faststart "$OUT/gameplay.mp4"

echo "Encoding $OUT/gameplay.gif..."
# The game is drawn at 320x180, so a 2x nearest-neighbour GIF is pixel exact.
ffmpeg -y -loglevel error -i "$WORK/clip.mkv" \
  -vf "fps=${GIF_FPS},scale=${GIF_W}:-1:flags=neighbor,split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];[b][p]paletteuse=dither=none:diff_mode=rectangle" \
  -loop 0 "$OUT/gameplay.gif"

echo "Rendering $OUT/cover.png and $OUT/screenshot.png..."
"$GAME" --screenshot "$WORK/cover.bmp" --scene cover --size 1280x640 --time 12
ffmpeg -y -loglevel error -i "$WORK/cover.bmp" "$OUT/cover.png"
"$GAME" --screenshot "$WORK/shot.bmp" --scene fight --size 1280x720 --time 14
ffmpeg -y -loglevel error -i "$WORK/shot.bmp" "$OUT/screenshot.png"

ls -l "$OUT/gameplay.mp4" "$OUT/gameplay.gif" "$OUT/cover.png" "$OUT/screenshot.png"
