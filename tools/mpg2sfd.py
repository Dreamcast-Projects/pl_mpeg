#!/usr/bin/env python3
"""mpg2sfd.py [options] IN.mpg OUT.sfd

Make a Sofdec-style file from an MPEG-1 program stream: the video stream is kept as it is and
the MP2 audio is replaced with CRI ADX (4-bit ADPCM). pl_mpeg decodes ADX for about a third of
the CPU time of MP2; the price is about twice the audio bitrate (432 kbit/s at 48 kHz stereo).

    python3 mpg2sfd.py movie.mpg movie.sfd
    python3 mpg2sfd.py --audio movie.wav movie.mpg movie.sfd     (better: no MP2 generation loss)
    python3 mpg2sfd.py --rate 32000 movie.mpg movie.sfd

Options:
    --audio FILE   take the sound from FILE (anything ffmpeg reads) instead of IN.mpg's MP2
    --rate HZ      resample to HZ (default: the source's rate)
    --mono         mix down to one channel
    --ffmpeg PATH  the ffmpeg to use (default: "ffmpeg" from PATH)

Needs ffmpeg, which decodes the source audio and encodes the ADX.

What is written:
- a 2048-byte block starting with "Sofdec Stream", by which ffmpeg/ffprobe know the audio as
  ADX; pl_mpeg skips it. The other fields CRI's tools put there are not written, and the file
  has not been tried with CRI's own player.
- IN.mpg's packs, system headers and video packets unchanged.
- in place of the MP2 packets (stream 0xC0): the ADX data, spread in proportion to time, in
  packets of at most 2000 bytes without timestamps.
- every ADX block's scale one lower than ffmpeg wrote it: ffmpeg's encoder means
  "sample = nibble * scale", ADX decoders (pl_mpeg, and real Sofdec files) use scale + 1.
"""
import os, struct, subprocess, sys, tempfile

def usage(msg=None):
    if msg:
        sys.stderr.write('mpg2sfd: ' + msg + '\n')
    sys.stderr.write(__doc__.split('\n\n')[0] + '\n(--help for the options)\n')
    sys.exit(2)

def parse_args(argv):
    opt = {'audio': None, 'rate': None, 'mono': False, 'ffmpeg': 'ffmpeg'}
    pos = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ('-h', '--help'):
            print(__doc__); sys.exit(0)
        elif a == '--mono':
            opt['mono'] = True
        elif a in ('--audio', '--rate', '--ffmpeg'):
            if i + 1 >= len(argv): usage(a + ' needs a value')
            opt[a[2:]] = argv[i + 1]; i += 1
        elif a.startswith('--'):
            usage('unknown option ' + a)
        else:
            pos.append(a)
        i += 1
    if len(pos) != 2: usage('give the input and the output file')
    if opt['rate'] is not None:
        try: opt['rate'] = int(opt['rate'])
        except ValueError: usage('--rate wants a number')
    return opt, pos[0], pos[1]

def encode_adx(opt, src):
    """The source's audio as an ADX file (bytes), via ffmpeg."""
    fd, tmp = tempfile.mkstemp(suffix='.adx'); os.close(fd)
    cmd = [opt['ffmpeg'], '-v', 'error', '-y', '-i', opt['audio'] or src, '-vn', '-c:a', 'adpcm_adx']
    if opt['rate']: cmd += ['-ar', str(opt['rate'])]
    if opt['mono']: cmd += ['-ac', '1']
    cmd += ['-f', 'adx', tmp]
    try:
        try:
            r = subprocess.run(cmd, capture_output=True, text=True)
        except FileNotFoundError:
            sys.exit('mpg2sfd: cannot run "%s" (install ffmpeg, or name it with --ffmpeg)' % opt['ffmpeg'])
        if r.returncode != 0 or os.path.getsize(tmp) < 64:
            sys.exit('mpg2sfd: ffmpeg could not make the ADX audio:\n' + r.stderr.strip())
        return bytearray(open(tmp, 'rb').read())
    finally:
        if os.path.exists(tmp): os.remove(tmp)

def fix_adx(adx):
    """Store every scale one lower and make sure the stream ends with an end marker."""
    if adx[0:2] != b'\x80\x00' or adx[4] != 3 or adx[5] != 18 or adx[6] != 4:
        sys.exit('mpg2sfd: unexpected ADX header from ffmpeg')
    p = struct.unpack('>H', adx[2:4])[0] + 4
    channels = adx[7]; rate = struct.unpack('>I', adx[8:12])[0]
    blocks = 0
    while p + 18 <= len(adx):
        scale = struct.unpack('>H', adx[p:p + 2])[0]
        if scale & 0x8000: break
        if scale:
            adx[p:p + 2] = struct.pack('>H', scale - 1)
        elif any(adx[p + 2:p + 18]):
            sys.exit('mpg2sfd: ADX block with scale 0 and samples that are not (not from ffmpeg\'s encoder?)')
        # scale 0 with all samples 0 is ffmpeg's silent block: it decodes the same either way
        blocks += 1; p += 18
    else:
        adx += b'\x80\x01' + bytes(16)
    return channels, rate, blocks

def packets(src):
    """(offset, length, stream id, audio payload bytes) for every pack header and packet."""
    out = []; i = 0; n = len(src)
    while i + 4 <= n:
        if src[i:i + 3] != b'\x00\x00\x01':
            sys.exit('mpg2sfd: not a plain MPEG-1 program stream (lost sync at byte %d)' % i)
        c = src[i + 3]
        if c == 0xba:
            if src[i + 4] & 0xf0 != 0x20:
                sys.exit('mpg2sfd: this is an MPEG-2 program stream; make the file with "-f mpeg"')
            length = 12
        elif c == 0xb9:
            length = 4
        else:
            length = 6 + struct.unpack('>H', src[i + 4:i + 6])[0]
        es = 0
        if c == 0xc0:
            j = i + 6
            while src[j] == 0xff: j += 1
            if src[j] & 0xc0 == 0x40: j += 2
            j += 5 if src[j] & 0xf0 == 0x20 else 10 if src[j] & 0xf0 == 0x30 else 1
            es = i + length - j
        out.append((i, length, c, es)); i += length
    return out

def main():
    opt, src_path, out_path = parse_args(sys.argv[1:])
    try:
        src = open(src_path, 'rb').read()
    except OSError as e:
        sys.exit('mpg2sfd: cannot read %s: %s' % (src_path, e.strerror))
    pk = packets(src)
    total = sum(es for _, _, c, es in pk if c == 0xc0)
    if not any(c == 0xe0 for _, _, c, _ in pk): sys.exit('mpg2sfd: no video stream (0xE0) in ' + src_path)
    if total == 0: sys.exit('mpg2sfd: no MPEG audio stream (0xC0) in %s to take the place of' % src_path)
    if any(0xc1 <= c <= 0xdf for _, _, c, _ in pk): sys.stderr.write('mpg2sfd: more audio streams found; only 0xC0 is replaced\n')

    adx = encode_adx(opt, src_path)
    channels, rate, blocks = fix_adx(adx)

    out = bytearray(b'Sofdec Stream'.ljust(32, b' ') + bytes(2048 - 32))
    done = 0; seen = 0
    def emit(upto):
        nonlocal done
        while done < upto:
            k = min(2000, upto - done)
            out.extend(b'\x00\x00\x01\xc0' + struct.pack('>H', k + 1) + b'\x0f' + adx[done:done + k]); done += k
    for i, length, c, es in pk:
        if c == 0xc0:
            seen += es; emit(len(adx) * seen // total)
        elif c == 0xb9:
            emit(len(adx)); out.extend(src[i:i + length])
        else:
            out.extend(src[i:i + length])
    emit(len(adx))
    open(out_path, 'wb').write(out)

    seconds = blocks / channels * 32 / rate
    print('%s: %d bytes; ADX %d Hz, %d channel%s, %.1f s, %d kbit/s (was %d bytes of MP2, now %d)' % (
        out_path, len(out), rate, channels, '' if channels == 1 else 's', seconds, rate * 18 * 8 * channels // 32 // 1000, total, len(adx)))

if __name__ == '__main__':
    main()
