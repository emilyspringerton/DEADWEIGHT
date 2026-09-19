# Shared helpers for art recipes. Source, don't execute. Needs ImageMagick 6 `convert` and the NOCK CLI ($NOCK).
W=240; H=336
KIND_NAMES=(offense operations defense)
KIND_FROM=('#E86A5F' '#F2C14E' '#6A9BF5')   # top of gradient (light): Offense red, Operations yellow, Defense blue
KIND_TO=('#6E1512' '#7A5A00' '#182F6B')     # bottom of gradient (dark)
TIER_RIM=('#CD7F32' '#C8CDD3' '#FFD24A')    # bronze, silver, gold

# glyph KIND -> transparent PNG of the kind glyph on a WxH canvas centred at (120,150), white fill
glyph() { # $1 kind  $2 out.png  $3 fill  $4 cx  $5 cy  $6 size(W) $7 size(H)
  local k=$1 out=$2 fill=${3:-white} cx=${4:-120} cy=${5:-150} sw=${6:-$W} sh=${7:-$H}
  local pts
  case $k in
    offense) pts=$(awk -v cx=$cx -v cy=$cy 'BEGIN{n=16;for(i=0;i<n;i++){r=(i%2==0)?78:36;a=3.14159265*2*i/n-1.5708;printf "%d,%d ",cx+r*cos(a),cy+r*sin(a)}}')
           convert -size ${sw}x${sh} xc:none -fill "$fill" -draw "polygon $pts" "$out";;
    operations) pts=$(awk -v cx=$cx -v cy=$cy 'BEGIN{for(i=0;i<6;i++){a=3.14159265*2*i/6-1.5708;printf "%d,%d ",cx+72*cos(a),cy+72*sin(a)}}')
           local in=$(awk -v cx=$cx -v cy=$cy 'BEGIN{for(i=0;i<6;i++){a=3.14159265*2*i/6-1.5708;printf "%d,%d ",cx+40*cos(a),cy+40*sin(a)}}')
           convert -size ${sw}x${sh} xc:none -fill "$fill" -draw "polygon $pts" -fill none -stroke black -strokewidth 5 -draw "polygon $in" "$out";;
    defense) local dx=$((cx-120)) dy=$((cy-150))
           convert -size ${sw}x${sh} xc:none -fill "$fill" -draw "polygon $((120+dx)),$((70+dy)) $((188+dx)),$((92+dy)) $((182+dx)),$((172+dy)) $((120+dx)),$((232+dy)) $((58+dx)),$((172+dy)) $((52+dx)),$((92+dy))" \
             -fill none -stroke black -strokewidth 5 -draw "polyline $((120+dx)),$((92+dy)) $((120+dx)),$((205+dy))" -draw "polyline $((84+dx)),$((130+dy)) $((156+dx)),$((130+dy))" "$out";;
  esac
}
