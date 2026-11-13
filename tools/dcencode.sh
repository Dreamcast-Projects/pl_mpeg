#!/bin/sh
# dcencode.sh [-l] INPUT OUTPUT.mpg
#
# Convert any video ffmpeg can read into an MPEG-1 program stream for the Dreamcast player,
# and report anything about the input that needed changing or that the script cannot fix.
#
#   - Size: fitted inside 640x480 with the picture's shape kept, never enlarged, both sides
#     rounded to a multiple of 16.
#   - Frame rate: at most 30. Anything above 30 fps is halved (60 -> 30, 50 -> 25), then set
#     to the nearest rate MPEG-1 has (23.976, 24, 25, 29.97 or 30).
#   - Video: peaks capped at 1.8 Mbit/s, no B-pictures, an I-picture every 15 frames.
#   - Audio: stereo MP2, 44.1 kHz, 192 kbit/s.
#   - Interlaced input is deinterlaced.
#
# 30 fps video larger than about 640x400 needs the player's frame queue
# (mpeg_player_options_t.frame_queue = 4) to play without late frames. -l lowers the cap to
# 1.5 Mbit/s for players that leave the queue off.
#
# Not handled: HDR sources (they come out washed out).
#
# Needs ffmpeg and ffprobe.

RATE="-b:v 1500k -maxrate 1800k -bufsize 500k"
LOW=0
if [ "$1" = "-l" ]; then
    RATE="-b:v 1300k -maxrate 1500k -bufsize 400k"
    LOW=1
    shift
fi

if [ $# -ne 2 ]; then
    echo "usage: $0 [-l] INPUT OUTPUT.mpg" >&2
    exit 1
fi
IN=$1
OUT=$2

note() { echo "dcencode: $*" >&2; }
fail() { echo "dcencode: error: $*" >&2; exit 1; }

command -v ffmpeg >/dev/null 2>&1 || fail "ffmpeg not found"
command -v ffprobe >/dev/null 2>&1 || fail "ffprobe not found"
[ -f "$IN" ] || fail "no such file: $IN"

# One field of the first video (v:0) or audio (a:0) stream; empty if there is none
probe() {
    ffprobe -v error -select_streams "$1" -show_entries "stream=$2" \
        -of default=nw=1:nk=1 "$IN" 2>/dev/null | head -n 1
}

W=$(probe v:0 width)
H=$(probe v:0 height)
[ -n "$W" ] && [ -n "$H" ] || fail "no video in $IN"
FPS=$(probe v:0 r_frame_rate)
AVG=$(probe v:0 avg_frame_rate)
FIELD=$(probe v:0 field_order)
TRC=$(probe v:0 color_transfer)
ACH=$(probe a:0 channels)

# ---- What the input needs ----

DEINT=""
case "$FIELD" in
    tt|bb|tb|bt)
        note "input is interlaced: deinterlacing it"
        DEINT="bwdif=mode=send_frame,"
        ;;
esac

case "$TRC" in
    smpte2084|arib-std-b67)
        note "warning: input is HDR, which this script does not convert: colours will look washed out"
        ;;
esac

# Frame rates as numbers: "30000/1001" -> 29.970
num() { echo "$1" | awk -F/ '{ if ($2 > 0) printf "%.3f", $1 / $2; else if ($1 > 0) printf "%.3f", $1; else print 0 }'; }
FPSN=$(num "$FPS")
AVGN=$(num "$AVG")
if awk "BEGIN { exit !($FPSN > 30.5) }"; then
    note "input is $FPSN fps: every second frame is dropped to get to 30 fps or less"
elif awk "BEGIN { exit !($FPSN > 0 && $FPSN < 23.9) }"; then
    note "input is $FPSN fps, lower than MPEG-1 allows: frames are repeated to make 23.976 fps"
fi
if awk "BEGIN { exit !($AVGN > 0 && ($FPSN / $AVGN > 1.05 || $AVGN / $FPSN > 1.05)) }"; then
    note "warning: input has a variable frame rate ($AVGN fps average): motion may look uneven"
fi

if [ -z "$ACH" ]; then
    note "input has no audio: the output has none either"
    AUDIO="-an"
else
    [ "$ACH" -gt 2 ] && note "input has $ACH audio channels: mixed down to stereo"
    AUDIO="-c:a mp2 -ar 44100 -ac 2 -b:a 192k"
fi

# ---- Encode ----

# The frame rate after halving, used four times below
F="source_fps/ceil(source_fps/30.5)"

ffmpeg -hide_banner -i "$IN" \
    -vf "${DEINT}scale=iw*sar:ih,setsar=1,scale='min(640,iw)':'min(480,ih)':force_original_aspect_ratio=decrease:force_divisible_by=16:flags=lanczos:out_color_matrix=bt601,fps='if(lt($F,23.99),24000/1001,if(lt($F,24.5),24,if(lt($F,27),25,if(lt($F,29.98),30000/1001,30))))',format=yuv420p" \
    -c:v mpeg1video $RATE -g 15 -bf 0 -mbd rd -trellis 1 \
    $AUDIO \
    -f mpeg "$OUT" || fail "ffmpeg failed"

# ---- Check the result against what the Dreamcast player needs ----

IN=$OUT
OW=$(probe v:0 width)
OH=$(probe v:0 height)
# (r_frame_rate reports the field rate, twice the frame rate, for MPEG-1)
OFPS=$(num "$(probe v:0 avg_frame_rate)")
[ -n "$OW" ] && [ -n "$OH" ] || fail "could not read back $OUT"

note "input ${W}x${H} at $FPSN fps -> output ${OW}x${OH} at $OFPS fps"

BAD=0
if [ "$OW" -gt 640 ] || [ "$OH" -gt 480 ]; then
    note "error: output is larger than 640x480"
    BAD=1
fi
if [ $((OW % 16)) -ne 0 ] || [ $((OH % 16)) -ne 0 ]; then
    note "error: output sides are not multiples of 16"
    BAD=1
fi
if awk "BEGIN { exit !($OFPS > 30.01) }"; then
    note "error: output is above 30 fps"
    BAD=1
fi
[ $BAD -eq 0 ] || exit 1

if [ $LOW -eq 0 ] && awk "BEGIN { exit !($OFPS > 27 && $OW * $OH > 640 * 400) }"; then
    note "this size at $OFPS fps needs the player's frame queue (frame_queue = 4);"
    note "with the queue off, encode with -l instead"
fi
exit 0
