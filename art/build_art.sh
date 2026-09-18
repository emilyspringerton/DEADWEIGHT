#!/usr/bin/env bash
# Regenerate every DEADWEIGHT art PNG. Uses the NOCK CLI (layered project engine, ImageMagick 6 backend) against a
# THROWAWAY data dir + DB in a temp directory -- never IDUNA's live data. No PARENA texture generation, no FFI.
# Usage: art/build_art.sh            (needs `convert`; builds NOCK from ../../IDUNA unless $NOCK is set)
set -euo pipefail
cd "$(dirname "$0")"
ART=$PWD
. recipes/lib.sh
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
export NOCK_DATA_DIR="$TMP/projects" NOCK_DB_PATH="$TMP/nock.db"
mkdir -p "$NOCK_DATA_DIR" out
if [ -z "${NOCK:-}" ]; then
  IDUNA="${IDUNA_DIR:-$ART/../../IDUNA}"
  (cd "$IDUNA" && GOWORK=off go build -o "$TMP/nock" ./cmd/nock 2>/dev/null || go build -o "$TMP/nock" ./cmd/nock)
  NOCK="$TMP/nock"
fi
N() { "$NOCK" "$@" >/dev/null; }
# clip an exported card to its rounded-corner outline (transparent corners) -- NOCK layers are full-canvas rectangles
round() { convert "$1" \( -size ${W}x${H} xc:none -fill white -draw "roundrectangle 0,0 $((W-1)),$((H-1)) 24,24" \) -alpha set -compose DstIn -composite "$1"; }

card() { # $1 id
  local id=$1 k=$((id/3)) t=$((id%3)) p="card$id"
  local kn=${KIND_NAMES[$k]}
  N init -project $p -width $W -height $H
  N gradient -project $p -layer bg -from "${KIND_FROM[$k]}" -to "${KIND_TO[$k]}" -direction vertical
  # faint diagonal hatch for texture
  local hd=(); for i in $(seq -340 24 240); do hd+=(-draw "line $i,340 $((i+340)),0"); done
  convert -size ${W}x${H} xc:none -stroke white -strokewidth 2 "${hd[@]}" "$TMP/hatch$id.png"
  N layer-add -project $p -layer hatch -file "$TMP/hatch$id.png"
  N layer-opacity -project $p -layer hatch -opacity 12
  glyph $kn "$TMP/glyph$id.png" white 120 160
  N layer-add -project $p -layer glyph -file "$TMP/glyph$id.png"
  # tier pips (tier+1 diamonds) along the bottom, in the tier rim colour
  local n=$((t+1)); local x0=$((120-(n-1)*18)); local td=()
  for i in $(seq 0 $((n-1))); do x=$((x0+i*36)); td+=(-draw "polygon $x,290 $((x+11)),302 $x,314 $((x-11)),302"); done
  convert -size ${W}x${H} xc:none -fill "${TIER_RIM[$t]}" -stroke black -strokewidth 2 "${td[@]}" "$TMP/tier$id.png"
  N layer-add -project $p -layer tier -file "$TMP/tier$id.png"
  # frame: outer white rim + tier-coloured inner rim, rounded corners
  convert -size ${W}x${H} xc:none -fill none -stroke white -strokewidth 6 -draw "roundrectangle 3,3 $((W-4)),$((H-4)) 22,22" \
    -stroke "${TIER_RIM[$t]}" -strokewidth 5 -draw "roundrectangle 12,12 $((W-13)),$((H-13)) 16,16" "$TMP/frame$id.png"
  N layer-add -project $p -layer frame -file "$TMP/frame$id.png"
  N export -project $p -out "$ART/out/card_$id.png"; round "$ART/out/card_$id.png"
}
for id in 0 1 2 3 4 5 6 7 8; do card $id; done

# card back: dark gradient + concentric emblem
N init -project back -width $W -height $H
N gradient -project back -layer bg -from '#2B2F3A' -to '#0E1016' -direction vertical
convert -size ${W}x${H} xc:none -fill none -stroke '#8892A6' -strokewidth 4 -draw "circle 120,168 120,110" -draw "circle 120,168 120,80" -stroke '#FFD24A' -strokewidth 3 -draw "circle 120,168 120,50" \
  -fill '#8892A6' -stroke none -draw "polygon 120,132 132,168 120,204 108,168" "$TMP/emblem.png"
N layer-add -project back -layer emblem -file "$TMP/emblem.png"
convert -size ${W}x${H} xc:none -fill none -stroke white -strokewidth 6 -draw "roundrectangle 3,3 $((W-4)),$((H-4)) 22,22" "$TMP/backframe.png"
N layer-add -project back -layer frame -file "$TMP/backframe.png"
N export -project back -out "$ART/out/card_back.png"; round "$ART/out/card_back.png"

# 128x128 icons (transparent): kind glyphs in kind colour, hull, energy
icon() { # $1 name  $2 path-to-transparent-png-on-128 canvas
  N init -project i_$1 -width 128 -height 128
  N layer-add -project i_$1 -layer g -file "$2"
  N export -project i_$1 -out "$ART/out/icon_$1.png"
}
for k in 0 1 2; do
  kn=${KIND_NAMES[$k]}
  # scale glyph coordinates: draw on 240x336 then crop/resize to a square
  glyph $kn "$TMP/ig$k.png" "${KIND_FROM[$k]}" 120 150
  convert "$TMP/ig$k.png" -crop 200x200+20+50 +repage -resize 128x128 "$TMP/ik$k.png"
  icon $kn "$TMP/ik$k.png"
done
# hull: rivetted plate; energy: bolt
convert -size 128x128 xc:none -fill '#9AA5B8' -stroke white -strokewidth 4 -draw "roundrectangle 14,22 114,106 14,14" -fill white -stroke none \
  -draw "circle 30,38 30,42" -draw "circle 98,38 98,42" -draw "circle 30,90 30,94" -draw "circle 98,90 98,94" \
  -fill '#E86A5F' -draw "rectangle 56,44 72,84" -draw "rectangle 44,56 84,72" "$TMP/hull.png"
icon hull "$TMP/hull.png"
convert -size 128x128 xc:none -fill '#FFD24A' -stroke '#7A5A00' -strokewidth 4 -draw "polygon 72,8 30,70 60,70 50,120 100,52 68,52" "$TMP/energy.png"
icon energy "$TMP/energy.png"
echo "art: $(ls out | wc -l) files in art/out"

# ship into the Android app (drawable-nodpi: drawn scaled by CardView, no per-density copies needed)
DST="$ART/../android/src/main/res/drawable-nodpi"; mkdir -p "$DST"; cp out/*.png "$DST/"
echo "art: copied to android/src/main/res/drawable-nodpi"
