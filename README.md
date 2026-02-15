# PL_MPEG - MPEG1 Video decoder, MP2 Audio decoder, MPEG-PS demuxer

MPEG1 Decode library for Dreamcast Ver.0.8
	2023/09/19 Tashi


#### FEATURES ####
- MPEG1 video playback with MP2 audio (mono or stereo)
- Supports video resolutions up to 640x480
- Configurable cancel buttons during playback (controller buttons, keyboard keys, button combos)
- Simple, extended, and manual playback APIs
- Overridable memory allocators and file I/O


#### ENCODING FOR DREAMCAST ####

Videos must be MPEG-PS (`.mpg`) with MPEG1 video and MP2 audio. Use constant
bitrate (`-minrate`/`-maxrate`/`-bufsize` matching `-b:v`) to avoid decode
spikes that cause frame drops. Audio at 32kHz keeps CPU overhead low while
sounding good.

**320x240 (4:3) — Standard (Recommended Standard):**
```
ffmpeg -i input.mp4 -vf "fps=24,scale=320:240" -c:v mpeg1video -b:v 1500k -minrate 1500k -maxrate 1500k -bufsize 1500k -ac 2 -ar 48000 -c:a mp2 -b:a 128k -f mpeg 320x240.mpg
```

**368x208 (16:9) — Widescreen (Recommended Widescreen):**
```
ffmpeg -i input.mp4 -vf "fps=24,scale=368:208" -c:v mpeg1video -b:v 1500k -minrate 1500k -maxrate 1500k -bufsize 1500k -ac 2 -ar 48000 -c:a mp2 -b:a 128k -f mpeg 368x208.mpg
```

**448x336 (4:3) — Standard:**
```
ffmpeg -i input.mp4 -vf "fps=24,scale=448:336" -c:v mpeg1video -b:v 1100k -minrate 1100k -maxrate 1100k -bufsize 1100k -ac 2 -ar 32000 -c:a mp2 -b:a 128k -f mpeg 448x336.mpg
```

**512x288 (16:9) — Widescreen:**
```
ffmpeg -i input.mp4 -vf "fps=24,scale=512:288" -c:v mpeg1video -b:v 1100k -minrate 1100k -maxrate 1100k -bufsize 1100k -ac 2 -ar 32000 -c:a mp2 -b:a 128k -f mpeg 512x288.mpg
```

For mono audio, replace `-ac 2 -b:a 128k` with `-ac 1 -b:a 64k`.


#### LICENSE ####
pl_mpeg.h - MIT LICENSE
mpeg.c, mpeg.h - Public Domain


#### THANKS TO ####
Dominic Szablewski (https://phoboslab.org) - Great decoding engine.
Provided advice and ideas:
- Ian Robinson (https://github.com/ianmicheal)
- Andy Barajas (https://github.com/andressbarajas)
- Jnmartin84 (https://github.com/jnmartin84)
- BitBank2 (https://github.com/bitbank2)
- Falco Girgis (https://github.com/gyrovorbis)

with permission https://dcemulation.org/phpBB/viewtopic.php?p=1060259#p1060259

Single-file MIT licensed library for C/C++

See [pl_mpeg.h](https://github.com/phoboslab/pl_mpeg/blob/master/pl_mpeg.h) for
the documentation.


## Why?

This is meant as a simple way to get video playback into your app or game. Other
solutions, such as ffmpeg require huge libraries and a lot of glue code.

MPEG1 is an old and inefficient codec, but it's still good enough for many use
cases. All patents related to MPEG1 and MP2 have expired, so it's completely
free now.

This library does not make use of any SIMD instructions, but because of
the relative simplicity of the codec it still manages to decode 4k60fps video
on a single CPU core (on my i7-6700k at least).


## Example Usage

- [pl_mpeg_extract_frames.c](https://github.com/phoboslab/pl_mpeg/blob/master/pl_mpeg_extract_frames.c)
extracts all frames from a video and saves them as PNG.
 - [pl_mpeg_player.c](https://github.com/phoboslab/pl_mpeg/blob/master/pl_mpeg_player.c)
implements a video player using SDL2 and OpenGL for rendering.



## Encoding for PL_MPEG

Most [MPEG-PS](https://en.wikipedia.org/wiki/MPEG_program_stream) (`.mpg`) files
containing MPEG1 Video ("mpeg1") and MPEG1 Audio Layer II ("mp2") streams should
work with PL_MPEG. Note that `.mpg` files can also contain MPEG2 Video, which is
not supported by this library.

You can encode video in a suitable format using ffmpeg:

```
ffmpeg -i input.mp4 -c:v mpeg1video -c:a mp2 -format mpeg output.mpg
```

If you just want to quickly test the library, try this file:

https://phoboslab.org/files/bjork-all-is-full-of-love.mpg


## Limitations

- no error reporting. PL_MPEG will silently ignore any invalid data.
- the pts (presentation time stamp) for packets in the MPEG-PS container is
ignored. This may cause sync issues with some files.
- no seeking.
- bugs, probably.
