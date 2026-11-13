#!/bin/sh
# dcencode.sh [-l] INPUT OUTPUT.mpg
#
# Convert any video ffmpeg can read into an MPEG-1 program stream for the Dreamcast player.
#
#   - Size: fitted inside 640x480 with the picture's shape kept, never enlarged, both sides
#     rounded to a multiple of 16.
#   - Frame rate: anything above 30 fps is halved (60 -> 30, 50 -> 25), then set to the nearest
#     rate MPEG-1 has (23.976, 24, 25, 29.97 or 30).
#   - Video: peaks capped at 1.8 Mbit/s, no B-pictures, an I-picture every 15 frames.
#   - Audio: stereo MP2, 44.1 kHz, 192 kbit/s.
#
# 30 fps video larger than about 640x400 needs the player's frame queue
# (mpeg_player_options_t.frame_queue = 4) to play without late frames. -l lowers the cap to
# 1.5 Mbit/s for players that leave the queue off.
#
# Not handled: HDR sources (they come out washed out) and interlaced sources (add a deinterlace
# filter such as bwdif in front of the scale filters).

RATE="-b:v 1500k -maxrate 1800k -bufsize 500k"
if [ "$1" = "-l" ]; then
    RATE="-b:v 1300k -maxrate 1500k -bufsize 400k"
    shift
fi

if [ $# -ne 2 ]; then
    echo "usage: $0 [-l] INPUT OUTPUT.mpg" >&2
    exit 1
fi

# The frame rate after halving, used four times below
F="source_fps/ceil(source_fps/30.5)"

exec ffmpeg -i "$1" \
    -vf "scale=iw*sar:ih,setsar=1,scale='min(640,iw)':'min(480,ih)':force_original_aspect_ratio=decrease:force_divisible_by=16:flags=lanczos:out_color_matrix=bt601,fps='if(lt($F,23.99),24000/1001,if(lt($F,24.5),24,if(lt($F,27),25,if(lt($F,29.98),30000/1001,30))))',format=yuv420p" \
    -c:v mpeg1video $RATE -g 15 -bf 0 -mbd rd -trellis 1 \
    -c:a mp2 -ar 44100 -ac 2 -b:a 192k \
    -f mpeg "$2"
