#!/bin/bash
# Summarizes EDEN_NX2_STUTTER_TRACE lines of one or more eden logs, counting only frames after
# the first frame (boot and precache excluded).
for f in "$@"; do
    ff=$(grep -m1 'event=first_frame' "$f" | grep -oE '^\[ *[0-9.]+' | tr -d '[ ')
    end=$(grep -oE '^\[ *[0-9.]+' "$f" | tail -1 | tr -d '[ ')
    pre=$(grep -oE 'Shader precache: finished, [0-9]+ built[^,]*, [0-9]+ failed[^,]*, [0-9]+ not reached, [0-9.]+ s' "$f" | tail -1)
    gw=$(grep -iE 'graphics warm-up.*(finished|done|built)' "$f" | tail -1 | sed -E 's/.*(Graphics|graphics)/graphics/' | cut -c1-160)
    echo "== $f"
    echo "   first_frame=${ff}s  log_end=${end}s  ${pre}"
    [ -n "$gw" ] && echo "   $gw"
    grep 'Nx2Stutter frame' "$f" | sed -E 's/^\[ *([0-9.]+)\].*ms=([0-9.]+) cause=([a-z_]+).*/\1 \2 \3/' |
        awk -v ff="${ff:-0}" '$1>=ff {n++; t+=$2; c[$3]++; s[$3]+=$2; if($2>m[$3])m[$3]=$2; if($2>100){b++; bb[$3]++}; if($2>mx)mx=$2}
            END{printf "   travadas=%d  >100ms=%d  soma=%.1f s  pior=%.0f ms\n", n, b, t/1000, mx;
                for(k in c) printf "     %-14s n=%4d  >100ms=%3d  media=%6.1f  max=%7.1f\n", k, c[k], bb[k], s[k]/c[k], m[k]}'
    grep 'Nx2StutterSummary' "$f" | grep -oE 'builds=gfx:[0-9]+' | cut -d: -f2 | awk '{s+=$1} END{print "   pipelines graficos compilados em jogo: " s+0}'
done
