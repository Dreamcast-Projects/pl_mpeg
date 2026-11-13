/*
PL_MPEG - MPEG1 Video decoder, MP2 Audio decoder, MPEG-PS demuxer
SPDX-License-Identifier: MIT

Dominic Szablewski - https://phoboslab.org


-- Synopsis

// Define `PL_MPEG_IMPLEMENTATION` in *one* C/C++ file before including this
// library to create the implementation.

#define PL_MPEG_IMPLEMENTATION
#include "plmpeg.h"

// This function gets called for each decoded video frame
void my_video_callback(plm_t *plm, plm_frame_t *frame, void *user) {
	// Do something with frame->y.data, frame->cr.data, frame->cb.data
}

// This function gets called for each decoded audio frame
void my_audio_callback(plm_t *plm, plm_samples_t *frame, void *user) {
	// Do something with samples->interleaved
}

// Load a .mpg (MPEG Program Stream) file
plm_t *plm = plm_create_with_filename("some-file.mpg");

// Install the video & audio decode callbacks
plm_set_video_decode_callback(plm, my_video_callback, my_data);
plm_set_audio_decode_callback(plm, my_audio_callback, my_data);


// Decode
do {
	plm_decode(plm, time_since_last_call);
} while (!plm_has_ended(plm));

// All done
plm_destroy(plm);



-- Documentation

This library provides several interfaces to load, demux and decode MPEG video
and audio data. A high-level API combines the demuxer, video & audio decoders
in an easy to use wrapper.

Lower-level APIs for accessing the demuxer, video decoder and audio decoder,
as well as providing different data sources are also available.

Interfaces are written in an object oriented style, meaning you create object
instances via various different constructor functions (plm_*create()),
do some work on them and later dispose them via plm_*destroy().

plm_* ......... the high-level interface, combining demuxer and decoders
plm_buffer_* .. the data source used by all interfaces
plm_demux_* ... the MPEG-PS demuxer
plm_video_* ... the MPEG1 Video ("mpeg1") decoder
plm_audio_* ... the MPEG1 Audio Layer II ("mp2") decoder


With the high-level interface you have two options to decode video & audio:

 1. Use plm_decode() and just hand over the delta time since the last call.
    It will decode everything needed and call your callbacks (specified through
    plm_set_{video|audio}_decode_callback()) any number of times.

 2. Use plm_decode_video() and plm_decode_audio() to decode exactly one
    frame of video or audio data at a time. How you handle the synchronization
    of both streams is up to you.

If you only want to decode video *or* audio through these functions, you should
disable the other stream (plm_set_{video|audio}_enabled(FALSE))

Video data is decoded into a struct with all 3 planes (Y, Cr, Cb) stored in
separate buffers. You can either convert this to RGB on the CPU (slow) via the
plm_frame_to_rgb() function or do it on the GPU with the following matrix:

mat4 bt601 = mat4(
	1.16438,  0.00000,  1.59603, -0.87079,
	1.16438, -0.39176, -0.81297,  0.52959,
	1.16438,  2.01723,  0.00000, -1.08139,
	0, 0, 0, 1
);
gl_FragColor = vec4(y, cb, cr, 1.0) * bt601;

Audio data is decoded into a struct with either one single float array with the
samples for the left and right channel interleaved, or if the
PLM_AUDIO_SEPARATE_CHANNELS is defined *before* including this library, into
two separate float arrays - one for each channel.


Data can be supplied to the high level interface, the demuxer and the decoders
in three different ways:

 1. Using plm_create_from_filename() or with a file handle with
    plm_create_from_file().

 2. Using plm_create_with_memory() and supplying a pointer to memory that
    contains the whole file.

 3. Using plm_create_with_buffer(), supplying your own plm_buffer_t instance and
    periodically writing to this buffer.

When using your own plm_buffer_t instance, you can fill this buffer using
plm_buffer_write(). You can either monitor plm_buffer_get_remaining() and push
data when appropriate, or install a callback on the buffer with
plm_buffer_set_load_callback() that gets called whenever the buffer needs more
data.

A buffer created with plm_buffer_create_with_capacity() is treated as a ring
buffer, meaning that data that has already been read, will be discarded. In
contrast, a buffer created with plm_buffer_create_for_appending() will keep all
data written to it in memory. This enables seeking in the already loaded data.


There should be no need to use the lower level plm_demux_*, plm_video_* and
plm_audio_* functions, if all you want to do is read/decode an MPEG-PS file.
However, if you get raw mpeg1video data or raw mp2 audio data from a different
source, these functions can be used to decode the raw data directly. Similarly,
if you only want to analyze an MPEG-PS file or extract raw video or audio
packets from it, you can use the plm_demux_* functions.


This library uses malloc(), realloc() and free() to manage memory. Typically
all allocation happens up-front when creating the interface. However, the
default buffer size may be too small for certain inputs. In these cases plmpeg
will realloc() the buffer with a larger size whenever needed. You can configure
the default buffer size by defining PLM_BUFFER_DEFAULT_SIZE *before*
including this library.

You can also define PLM_MALLOC, PLM_REALLOC and PLM_FREE to provide your own
memory management functions.


See below for detailed the API documentation.

*/

#ifndef PL_MPEG_H
#define PL_MPEG_H

#include <stdint.h>
#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif

#ifndef TRUE
    #define TRUE 1
    #define FALSE 0
#endif

#ifndef PLM_FILE_OPEN
    #define PLM_FILE_TYPE                 MPEG_FILE_TYPE
    #define PLM_FILE_INVALID_HANDLE       MPEG_FILE_INVALID_HANDLE
    #define PLM_FILE_OPEN(fn)             MPEG_FILE_OPEN((fn))
    #define PLM_FILE_CLOSE(fh)            MPEG_FILE_CLOSE((fh))
    #define PLM_FILE_SEEK(fh, off, st)    MPEG_FILE_SEEK((fh), (off), (st))
    #define PLM_FILE_READ(fh, buf, size)  MPEG_FILE_READ((fh), (buf), (size))
    #define PLM_FILE_TELL(fh)             MPEG_FILE_TELL((fh))
#endif

#ifndef PLM_MALLOC
	#define PLM_MALLOC(sz)       MPEG_MALLOC(sz)
	#define PLM_FREE(p)          MPEG_FREE(p)
	#define PLM_REALLOC(p, sz)   MPEG_REALLOC((p), (sz))
	#define PLM_MEMZERO(p, sz)   MPEG_MEMZERO((p), (sz))
	#define PLM_MEMALIGN(a, sz)  MPEG_MEMALIGN((a), (sz))
#endif

#define PLM_UNUSED(expr) (void)(expr)

#define PLM_MIN(a,b) ((a) < (b) ? (a) : (b))

#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

// -----------------------------------------------------------------------------
// Public Data Types


// Object types for the various interfaces

typedef struct plm_t plm_t;
typedef struct plm_buffer_t plm_buffer_t;
typedef struct plm_demux_t plm_demux_t;
typedef struct plm_video_t plm_video_t;
typedef struct plm_audio_t plm_audio_t;


// Demuxed MPEG PS packet
// The type maps directly to the various MPEG-PES start codes. PTS is the
// presentation time stamp of the packet in seconds. Note that not all packets
// have a PTS value, indicated by PLM_PACKET_INVALID_TS.

#define PLM_PACKET_INVALID_TS -1

typedef struct {
    int type;
    double pts;

    size_t length;      // Total bytes (len0 + len1)

    uint8_t *data0;     // Ring buffer: first span
    size_t   len0;

    uint8_t *data1;     // Ring buffer: second span (NULL if not wrapped)
    size_t   len1;
} plm_packet_t;

// Decoded Video Plane
// The byte length of the data is width * height. Note that different planes
// have different sizes: the Luma plane (Y) is double the size of each of
// the two Chroma planes (Cr, Cb) - i.e. 4 times the byte length.
// Also note that the size of the plane does *not* denote the size of the
// displayed frame. The sizes of planes are always rounded up to the nearest
// macroblock (16px).

typedef struct {
	unsigned int width;
	unsigned int height;
	uint8_t *data;
} plm_plane_t;


// Decoded Video Frame
// width and height denote the desired display size of the frame. This may be
// different from the internal size of the 3 planes.

typedef struct {
	double time;
	unsigned int width;
	unsigned int height;
	plm_plane_t y;
	plm_plane_t cr;
	plm_plane_t cb;
	uint32_t *display;
	unsigned int display_stride_mbs;   // macroblocks per row of display (>= width / 16; padding is never written)
} plm_frame_t;


// Callback function type for decoded video frames used by the high-level
// plm_* interface

typedef void(*plm_video_decode_callback)
	(plm_t *self, plm_frame_t *frame, void *user);


// Decoded Audio Samples
// Samples are signed 16-bit PCM interleaved as L, R.
// The `count` is always PLM_AUDIO_SAMPLES_PER_FRAME (per channel) and just
// there for convenience.

#define PLM_AUDIO_SAMPLES_PER_FRAME 1152

typedef struct {
	double time;
	unsigned int count;
	short pcm[PLM_AUDIO_SAMPLES_PER_FRAME * 2] __attribute__((aligned(32)));
} plm_samples_t;


// Callback function type for decoded audio samples used by the high-level
// plm_* interface

typedef void(*plm_audio_decode_callback)
	(plm_t *self, plm_samples_t *samples, void *user);


// Callback function for plm_buffer when it needs more data

typedef void(*plm_buffer_load_callback)(plm_buffer_t *self, void *user);

// Callback function for plm_buffer when it needs to seek

typedef void(*plm_buffer_seek_callback)(plm_buffer_t *self, size_t offset, void *user);


// Callback function for plm_buffer when it needs to tell the position

typedef size_t(*plm_buffer_tell_callback)(plm_buffer_t *self, void *user);

// -----------------------------------------------------------------------------
// plm_* public API
// High-Level API for loading/demuxing/decoding MPEG-PS data


// Create a plmpeg instance with a filename. Returns NULL if the file could not
// be opened.

plm_t *plm_create_with_filename(const char *filename);


// Create a plmpeg instance with a file handle. Pass TRUE to close_when_done to
// let plmpeg call fclose() on the handle when plm_destroy() is called.

plm_t *plm_create_with_file(PLM_FILE_TYPE fh, int close_when_done);


// Create a plmpeg instance with a pointer to memory as source. This assumes the
// whole file is in memory. The memory is not copied. Pass TRUE to
// free_when_done to let plmpeg call free() on the pointer when plm_destroy()
// is called.

plm_t *plm_create_with_memory(uint8_t *bytes, size_t length, int free_when_done);


// Create a plmpeg instance with a plm_buffer as source. Pass TRUE to
// destroy_when_done to let plmpeg call plm_buffer_destroy() on the buffer when
// plm_destroy() is called.

plm_t *plm_create_with_buffer(plm_buffer_t *buffer, int destroy_when_done);


// Destroy a plmpeg instance and free all data.

void plm_destroy(plm_t *self);


// Get whether we have headers on all available streams and we can accurately
// report the number of video/audio streams, video dimensions, framerate and
// audio samplerate.
// This returns FALSE if the file is not an MPEG-PS file or - when not using a
// file as source - when not enough data is available yet.

int plm_has_headers(plm_t *self);

// Probe the MPEG-PS data to find the actual number of video and audio streams
// within the buffer. For certain files (e.g. VideoCD) this can be more accurate
// than just reading the number of streams from the headers.
// This should only be used when the underlying plm_buffer is seekable, i.e. for
// files, fixed memory buffers or _for_appending buffers. If used with dynamic
// memory buffers it will skip decoding the probesize!
// The necessary probesize is dependent on the files you expect to read. Usually
// a few hundred KB should be enough to find all streams.
// Use plm_get_num_{audio|video}_streams() afterwards to get the number of
// streams in the file.
// Returns TRUE if any streams were found within the probesize.

int plm_probe(plm_t *self, size_t probesize);


// Get or set whether video decoding is enabled. Default TRUE.

int plm_get_video_enabled(plm_t *self);
void plm_set_video_enabled(plm_t *self, int enabled);


// Skip B-pictures while set: they are not decoded (no other picture refers to them) and no frame
// is returned for them, but the video clock advances as if they had been. A player that has
// fallen behind real time can set this to catch up and clear it again once it has.

void plm_set_video_skip_b_frames(plm_t *self, int skip);


// Skip every picture up to the next I-picture (and the B-pictures right after it, whose forward
// reference was skipped). The last resort for catching up, e.g. in streams with no B-pictures.
// Clears itself once the decoder is back on a complete reference chain.

void plm_set_video_skip_to_intra(plm_t *self);


// Get the number of video streams (0--1) reported in the system header.

int plm_get_num_video_streams(plm_t *self);


// Get the display width/height of the video stream.

int plm_get_width(plm_t *self);
int plm_get_height(plm_t *self);
double plm_get_pixel_aspect_ratio(plm_t *self);

// Get the framerate of the video stream in frames per second.

double plm_get_framerate(plm_t *self);


// Get or set whether audio decoding is enabled. Default TRUE.

int plm_get_audio_enabled(plm_t *self);
void plm_set_audio_enabled(plm_t *self, int enabled);


// Get the number of audio streams (0--4) reported in the system header.

int plm_get_num_audio_streams(plm_t *self);


// Set the desired audio stream (0--3). Default 0.

void plm_set_audio_stream(plm_t *self, int stream_index);


// Get the samplerate of the audio stream in samples per second.

int plm_get_samplerate(plm_t *self);


// Get or set the audio lead time in seconds - the time in which audio samples
// are decoded in advance (or behind) the video decode time. Typically this
// should be set to the duration of the buffer of the audio API that you use
// for output. E.g. for SDL2: (SDL_AudioSpec.samples / samplerate)

double plm_get_audio_lead_time(plm_t *self);
void plm_set_audio_lead_time(plm_t *self, double lead_time);


// Get the current internal time in seconds.

double plm_get_time(plm_t *self);


// Get the video duration of the underlying source in seconds.

double plm_get_duration(plm_t *self);


// Rewind all buffers back to the beginning.

void plm_rewind(plm_t *self);


// Get or set looping. Default FALSE.

int plm_get_loop(plm_t *self);
void plm_set_loop(plm_t *self, int loop);


// Get whether the file has ended. If looping is enabled, this will always
// return FALSE.

int plm_has_ended(plm_t *self);


// Set the callback for decoded video frames used with plm_decode(). If no
// callback is set, video data will be ignored and not be decoded. The *user
// Parameter will be passed to your callback.

void plm_set_video_decode_callback(plm_t *self, plm_video_decode_callback fp, void *user);


// Set the callback for decoded audio samples used with plm_decode(). If no
// callback is set, audio data will be ignored and not be decoded. The *user
// Parameter will be passed to your callback.

void plm_set_audio_decode_callback(plm_t *self, plm_audio_decode_callback fp, void *user);


// Advance the internal timer by seconds and decode video/audio up to this time.
// This will call the video_decode_callback and audio_decode_callback any number
// of times. A frame-skip is not implemented, i.e. everything up to current time
// will be decoded.

void plm_decode(plm_t *self, double seconds);


// Decode and return one video frame. Returns NULL if no frame could be decoded
// (either because the source ended or data is corrupt). If you only want to
// decode video, you should disable audio via plm_set_audio_enabled().
// The returned plm_frame_t is valid until the next call to plm_decode_video()
// or until plm_destroy() is called.

plm_frame_t *plm_decode_video(plm_t *self);


// Decode and return one audio frame. Returns NULL if no frame could be decoded
// (either because the source ended or data is corrupt). If you only want to
// decode audio, you should disable video via plm_set_video_enabled().
// The returned plm_samples_t is valid until the next call to plm_decode_audio()
// or until plm_destroy() is called.

plm_samples_t *plm_decode_audio(plm_t *self);


// Seek to the specified time, clamped between 0 -- duration. This can only be
// used when the underlying plm_buffer is seekable, i.e. for files, fixed
// memory buffers or _for_appending buffers.
// If seek_exact is TRUE this will seek to the exact time, otherwise it will
// seek to the last intra frame just before the desired time. Exact seeking can
// be slow, because all frames up to the seeked one have to be decoded on top of
// the previous intra frame.
// If seeking succeeds, this function will call the video_decode_callback
// exactly once with the target frame. If audio is enabled, it will also call
// the audio_decode_callback any number of times, until the audio_lead_time is
// satisfied.
// Returns TRUE if seeking succeeded or FALSE if no frame could be found.

int plm_seek(plm_t *self, double time, int seek_exact);


// Similar to plm_seek(), but will not call the video_decode_callback,
// audio_decode_callback or make any attempts to sync audio.
// Returns the found frame or NULL if no frame could be found.

plm_frame_t *plm_seek_frame(plm_t *self, double time, int seek_exact);



// -----------------------------------------------------------------------------
// plm_buffer public API
// Provides the data source for all other plm_* interfaces


// The default size for buffers created from files or by the high-level API. They
// MUST BE A POWER OF 2 for the internal ring buffer implementation to work correctly.
#ifndef PLM_BUFFER_DEFAULT_SIZE
#define PLM_BUFFER_DEFAULT_SIZE (32 * 1024)
#endif

// The default size for vid buffers to save ourselves from many reallocations.
// At minimum should be 128 * 1024 because that is what 320x240 videos require.
#ifndef PLM_VID_BUFFER_DEFAULT_SIZE
#define PLM_VID_BUFFER_DEFAULT_SIZE (128 * 1024)
#endif

// Bytes we keep available for fast “peek” reads.
// We maintain a small mirrored/guard region so hot-path bit reads can grab
// up to PLM_PEEK_SIZE bytes linearly without doing ring wrap math.
#define PLM_PEEK_SIZE 4

// Create a buffer instance with a filename. Returns NULL if the file could not
// be opened.

plm_buffer_t *plm_buffer_create_with_filename(const char *filename);


// Create a buffer instance with a file handle. Pass TRUE to close_when_done
// to let plmpeg call fclose() on the handle when plm_destroy() is called.

plm_buffer_t *plm_buffer_create_with_file(PLM_FILE_TYPE fh, int close_when_done);


// Create a buffer instance with a pointer to memory as source. This assumes
// the whole file is in memory. The bytes are not copied. Pass 1 to
// free_when_done to let plmpeg call free() on the pointer when plm_destroy()
// is called.

plm_buffer_t *plm_buffer_create_with_memory(uint8_t *bytes, size_t length, int free_when_done);


// Create an empty buffer with an initial capacity. The buffer will grow
// as needed. Data that has already been read, will be discarded.

plm_buffer_t *plm_buffer_create_with_capacity(size_t capacity);


// Create an empty buffer with an initial capacity. The buffer will grow
// as needed. Decoded data will *not* be discarded. This can be used when
// loading a file over the network, without needing to throttle the download.
// It also allows for seeking in the already loaded data.

plm_buffer_t *plm_buffer_create_for_appending(size_t initial_capacity);


// Destroy a buffer instance and free all data

void plm_buffer_destroy(plm_buffer_t *self);


// Copy data into the buffer. If the data to be written is larger than the
// available space, the buffer will realloc() with a larger capacity.
// Returns the number of bytes written. This will always be the same as the
// passed in length, except when the buffer was created _with_memory() for
// which _write() is forbidden.

size_t plm_buffer_write(plm_buffer_t *self, uint8_t *bytes, size_t length);


// Mark the current byte length as the end of this buffer and signal that no
// more data is expected to be written to it. This function should be called
// just after the last plm_buffer_write().
// For _with_capacity buffers, this is cleared on a plm_buffer_rewind().

void plm_buffer_signal_end(plm_buffer_t *self);


// Set a callback that is called whenever the buffer needs more data

void plm_buffer_set_load_callback(plm_buffer_t *self, plm_buffer_load_callback fp, void *user);


// Rewind the buffer back to the beginning. When loading from a file handle,
// this also seeks to the beginning of the file.

void plm_buffer_rewind(plm_buffer_t *self);


// Get the total size. For files, this returns the file size. For all other
// types it returns the number of bytes currently in the buffer.

size_t plm_buffer_get_size(plm_buffer_t *self);


// Get the number of remaining (yet unread) bytes in the buffer. This can be
// useful to throttle writing.

size_t plm_buffer_get_remaining(plm_buffer_t *self);


// Get whether the read position of the buffer is at the end and no more data
// is expected.

int plm_buffer_has_ended(plm_buffer_t *self);



// -----------------------------------------------------------------------------
// plm_demux public API
// Demux an MPEG Program Stream (PS) data into separate packages


// Various Packet Types

static const int PLM_DEMUX_PACKET_PRIVATE = 0xBD;
static const int PLM_DEMUX_PACKET_AUDIO_1 = 0xC0;
static const int PLM_DEMUX_PACKET_AUDIO_2 = 0xC1;
static const int PLM_DEMUX_PACKET_AUDIO_3 = 0xC2;
static const int PLM_DEMUX_PACKET_AUDIO_4 = 0xC3;
static const int PLM_DEMUX_PACKET_VIDEO_1 = 0xE0;


// Create a demuxer with a plm_buffer as source. This will also attempt to read
// the pack and system headers from the buffer.

plm_demux_t *plm_demux_create(plm_buffer_t *buffer, int destroy_when_done);


// Destroy a demuxer and free all data.

void plm_demux_destroy(plm_demux_t *self);


// Returns TRUE/FALSE whether pack and system headers have been found. This will
// attempt to read the headers if non are present yet.

int plm_demux_has_headers(plm_demux_t *self);

// Probe the file for the actual number of video/audio streams. See
// plm_probe() for the details.

int plm_demux_probe(plm_demux_t *self, size_t probesize);


// Returns the number of video streams found in the system header. This will
// attempt to read the system header if non is present yet.

int plm_demux_get_num_video_streams(plm_demux_t *self);


// Returns the number of audio streams found in the system header. This will
// attempt to read the system header if non is present yet.

int plm_demux_get_num_audio_streams(plm_demux_t *self);


// Rewind the internal buffer. See plm_buffer_rewind().

void plm_demux_rewind(plm_demux_t *self);


// Get whether the file has ended. This will be cleared on seeking or rewind.

int plm_demux_has_ended(plm_demux_t *self);


// Seek to a packet of the specified type with a PTS just before specified time.
// If force_intra is TRUE, only packets containing an intra frame will be
// considered - this only makes sense when the type is PLM_DEMUX_PACKET_VIDEO_1.
// Note that the specified time is considered 0-based, regardless of the first
// PTS in the data source.

plm_packet_t *plm_demux_seek(plm_demux_t *self, double time, int type, int force_intra);


// Get the PTS of the first packet of this type. Returns PLM_PACKET_INVALID_TS
// if not packet of this packet type can be found.

double plm_demux_get_start_time(plm_demux_t *self, int type);


// Get the duration for the specified packet type - i.e. the span between the
// the first PTS and the last PTS in the data source. This only makes sense when
// the underlying data source is a file or fixed memory.

double plm_demux_get_duration(plm_demux_t *self, int type);


// Decode and return the next packet. The returned packet_t is valid until
// the next call to plm_demux_decode() or until the demuxer is destroyed.

plm_packet_t *plm_demux_decode(plm_demux_t *self);



// -----------------------------------------------------------------------------
// plm_video public API
// Decode MPEG1 Video ("mpeg1") data into raw YCrCb frames


// Create a video decoder with a plm_buffer as source.

plm_video_t *plm_video_create_with_buffer(plm_buffer_t *buffer, int destroy_when_done);


// Destroy a video decoder and free all data.

void plm_video_destroy(plm_video_t *self);


// Get whether a sequence header was found and we can accurately report on
// dimensions and framerate.

int plm_video_has_header(plm_video_t *self);


// Get the framerate in frames per second.

double plm_video_get_framerate(plm_video_t *self);
double plm_video_get_pixel_aspect_ratio(plm_video_t *self);


// Get the display width/height.

int plm_video_get_width(plm_video_t *self);
int plm_video_get_height(plm_video_t *self);


// Set "no delay" mode. When enabled, the decoder assumes that the video does
// *not* contain any B-Frames. This is useful for reducing lag when streaming.
// The default is FALSE.

void plm_video_set_no_delay(plm_video_t *self, int no_delay);


// See plm_set_video_skip_b_frames().

void plm_video_set_skip_b_frames(plm_video_t *self, int skip);
void plm_video_set_skip_to_intra(plm_video_t *self);


// Set a function that is called after every slice while a picture is decoded (several times per
// picture), or NULL for none. The Dreamcast player shows queued frames and polls the sound stream
// from it, so a picture that takes longer than a frame period does not hold them up. The function
// must not call into the decoder or the demuxer.

typedef void(*plm_video_slice_callback)(plm_video_t *self, void *user);
void plm_video_set_slice_callback(plm_video_t *self, plm_video_slice_callback fp, void *user);


// Lay out the rows of plm_frame_t.display with exactly width / 16 macroblocks (exact = TRUE)
// instead of padding them to the next power of two (the default). For a PVR texture with a
// stride, whose rows are as long as the video is wide. Call it before the first picture is
// decoded.

void plm_video_set_display_stride_exact(plm_video_t *self, int exact);


// Get the current internal time in seconds.

double plm_video_get_time(plm_video_t *self);


// Set the current internal time in seconds. This is only useful when you
// manipulate the underlying video buffer and want to enforce a correct
// timestamps.

void plm_video_set_time(plm_video_t *self, double time);


// Rewind the internal buffer. See plm_buffer_rewind().

void plm_video_rewind(plm_video_t *self);


// Get whether the file has ended. This will be cleared on rewind.

int plm_video_has_ended(plm_video_t *self);


// Decode and return one frame of video and advance the internal time by
// 1/framerate seconds. The returned frame_t is valid until the next call of
// plm_video_decode() or until the video decoder is destroyed.

plm_frame_t *plm_video_decode(plm_video_t *self);


// Convert the YCrCb data of a frame into interleaved R G B data. The stride
// specifies the width in bytes of the destination buffer. I.e. the number of
// bytes from one line to the next. The stride must be at least
// (frame->width * bytes_per_pixel). The buffer pointed to by *dest must have a
// size of at least (stride * frame->height).
// Note that the alpha component of the dest buffer is always left untouched.

void plm_frame_to_rgb(plm_frame_t *frame, uint8_t *dest, int stride);
void plm_frame_to_bgr(plm_frame_t *frame, uint8_t *dest, int stride);
void plm_frame_to_rgba(plm_frame_t *frame, uint8_t *dest, int stride);
void plm_frame_to_bgra(plm_frame_t *frame, uint8_t *dest, int stride);
void plm_frame_to_argb(plm_frame_t *frame, uint8_t *dest, int stride);
void plm_frame_to_abgr(plm_frame_t *frame, uint8_t *dest, int stride);


// -----------------------------------------------------------------------------
// plm_audio public API
// Decode MPEG-1 Audio Layer II ("mp2") data into raw samples


// Create an audio decoder with a plm_buffer as source.

plm_audio_t *plm_audio_create_with_buffer(plm_buffer_t *buffer, int destroy_when_done);


// Destroy an audio decoder and free all data.

void plm_audio_destroy(plm_audio_t *self);


// Get whether a frame header was found and we can accurately report on
// samplerate.

int plm_audio_has_header(plm_audio_t *self);


// Get the samplerate in samples per second.

int plm_audio_get_samplerate(plm_audio_t *self);


// Get the current internal time in seconds.

double plm_audio_get_time(plm_audio_t *self);


// Set the current internal time in seconds. This is only useful when you
// manipulate the underlying video buffer and want to enforce a correct
// timestamps.

void plm_audio_set_time(plm_audio_t *self, double time);


// Rewind the internal buffer. See plm_buffer_rewind().

void plm_audio_rewind(plm_audio_t *self);


// Get whether the file has ended. This will be cleared on rewind.

int plm_audio_has_ended(plm_audio_t *self);


// Decode and return one "frame" of audio and advance the internal time by
// (PLM_AUDIO_SAMPLES_PER_FRAME/samplerate) seconds. The returned samples_t
// is valid until the next call of plm_audio_decode() or until the audio
// decoder is destroyed.

plm_samples_t *plm_audio_decode(plm_audio_t *self);



#ifdef __cplusplus
}
#endif

#endif // PL_MPEG_H





#ifdef PL_MPEG_IMPLEMENTATION
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// IMPLEMENTATION

#include <kos.h>
#include <string.h>
#include <stdlib.h>

// ---- shared definitions ----
#define PLM_AVG4(a, b) (((a) | (b)) - ((((a) ^ (b)) >> 1) & 0x7f7f7f7f))   // four bytes, rounding up


// -----------------------------------------------------------------------------
// plm (high-level interface) implementation

struct plm_t {
	plm_demux_t *demux;
	double time;
	int has_ended;
	int loop;
	int has_decoders;

	int video_enabled;
	int video_packet_type;
	plm_buffer_t *video_buffer;
	plm_video_t *video_decoder;

	int audio_enabled;
	int audio_stream_index;
	int audio_packet_type;
	double audio_lead_time;
	plm_buffer_t *audio_buffer;
	plm_audio_t *audio_decoder;

	plm_video_decode_callback video_decode_callback;
	void *video_decode_callback_user_data;

	plm_audio_decode_callback audio_decode_callback;
	void *audio_decode_callback_user_data;
};

int plm_init_decoders(plm_t *self);
void plm_handle_end(plm_t *self);
void plm_read_video_packet(plm_buffer_t *buffer, void *user);
void plm_read_audio_packet(plm_buffer_t *buffer, void *user);
void plm_read_packets(plm_t *self, int requested_type);

plm_t *plm_create_with_filename(const char *filename) {
	plm_buffer_t *buffer = plm_buffer_create_with_filename(filename);
	if (!buffer) {
		return NULL;
	}
	return plm_create_with_buffer(buffer, TRUE);
}

plm_t *plm_create_with_file(PLM_FILE_TYPE fh, int close_when_done) {
	plm_buffer_t *buffer = plm_buffer_create_with_file(fh, close_when_done);
	if (!buffer) {
		return NULL;
	}
	return plm_create_with_buffer(buffer, TRUE);
}

plm_t *plm_create_with_memory(uint8_t *bytes, size_t length, int free_when_done) {
	plm_buffer_t *buffer = plm_buffer_create_with_memory(bytes, length, free_when_done);
	if (!buffer) {
		return NULL;
	}
	return plm_create_with_buffer(buffer, TRUE);
}

plm_t *plm_create_with_buffer(plm_buffer_t *buffer, int destroy_when_done) {
	if (!buffer) {
		return NULL;
	}

	plm_t *self = (plm_t *)PLM_MALLOC(sizeof(plm_t));
	if(!self) {
		fprintf(stderr, "Out of memory for self. [plm_create_with_buffer]\n");
		if (destroy_when_done) {
			plm_buffer_destroy(buffer);
		}
		return NULL;
	}
	PLM_MEMZERO(self, sizeof(plm_t));

	self->demux = plm_demux_create(buffer, destroy_when_done);
	if (!self->demux) {
		if (destroy_when_done) {
			plm_buffer_destroy(buffer);
		}
		PLM_FREE(self);
		return NULL;
	}
	self->video_enabled = TRUE;
	self->audio_enabled = TRUE;
	plm_init_decoders(self);

	return self;
}

int plm_init_decoders(plm_t *self) {
	if (self->has_decoders) {
		return TRUE;
	}

	if (!plm_demux_has_headers(self->demux)) {
		return FALSE;
	}

	if (plm_demux_get_num_video_streams(self->demux) > 0) {
		if (self->video_enabled) {
			self->video_packet_type = PLM_DEMUX_PACKET_VIDEO_1;
		}
		if (!self->video_decoder) {
			self->video_buffer = plm_buffer_create_with_capacity(PLM_VID_BUFFER_DEFAULT_SIZE);
			if (!self->video_buffer) {
				return FALSE;
			}
			plm_buffer_set_load_callback(self->video_buffer, plm_read_video_packet, self);
			self->video_decoder = plm_video_create_with_buffer(self->video_buffer, TRUE);
			if (!self->video_decoder) {
				plm_buffer_destroy(self->video_buffer);
				self->video_buffer = NULL;
				return FALSE;
			}
		}
	}

	if (plm_demux_get_num_audio_streams(self->demux) > 0) {
		if (self->audio_enabled) {
			self->audio_packet_type = PLM_DEMUX_PACKET_AUDIO_1 + self->audio_stream_index;
		}
		if (!self->audio_decoder) {
			self->audio_buffer = plm_buffer_create_with_capacity(PLM_BUFFER_DEFAULT_SIZE);
			if (!self->audio_buffer) {
				return FALSE;
			}
			plm_buffer_set_load_callback(self->audio_buffer, plm_read_audio_packet, self);
			self->audio_decoder = plm_audio_create_with_buffer(self->audio_buffer, TRUE);
			if (!self->audio_decoder) {
				plm_buffer_destroy(self->audio_buffer);
				self->audio_buffer = NULL;
				return FALSE;
			}
		}
	}

	self->has_decoders = TRUE;
	return TRUE;
}

void plm_destroy(plm_t *self) {
	if(!self)
		return;

	if (self->video_decoder) {
		plm_video_destroy(self->video_decoder);
		self->video_decoder = NULL;
		self->video_buffer = NULL;
	}
	else if (self->video_buffer) {
		plm_buffer_destroy(self->video_buffer);
		self->video_buffer = NULL;
	}
	if (self->audio_decoder) {
		plm_audio_destroy(self->audio_decoder);
		self->audio_decoder = NULL;
		self->audio_buffer = NULL;
	}
	else if (self->audio_buffer) {
		plm_buffer_destroy(self->audio_buffer);
		self->audio_buffer = NULL;
	}

	plm_demux_destroy(self->demux);
	PLM_FREE(self);
}

int plm_get_audio_enabled(plm_t *self) {
	return self->audio_enabled;
}

int plm_has_headers(plm_t *self) {
	if (!plm_demux_has_headers(self->demux)) {
		return FALSE;
	}

	if (!plm_init_decoders(self)) {
		return FALSE;
	}

	if (
		(self->video_decoder && !plm_video_has_header(self->video_decoder)) ||
		(self->audio_decoder && !plm_audio_has_header(self->audio_decoder))
	) {
		return FALSE;
	}

	return TRUE;
}

int plm_probe(plm_t *self, size_t probesize) {
	int found_streams = plm_demux_probe(self->demux, probesize);
	if (!found_streams) {
		return FALSE;
	}

	// Re-init decoders
	self->has_decoders = FALSE;
	self->video_packet_type = 0;
	self->audio_packet_type = 0;
	return plm_init_decoders(self);
}

void plm_set_audio_enabled(plm_t *self, int enabled) {
	self->audio_enabled = enabled;

	if (!enabled) {
		self->audio_packet_type = 0;
		return;
	}

	self->audio_packet_type = (plm_init_decoders(self) && self->audio_decoder)
		? PLM_DEMUX_PACKET_AUDIO_1 + self->audio_stream_index
		: 0;
}

void plm_set_audio_stream(plm_t *self, int stream_index) {
	if (stream_index < 0 || stream_index > 3) {
		return;
	}
	self->audio_stream_index = stream_index;

	// Set the correct audio_packet_type
	plm_set_audio_enabled(self, self->audio_enabled);
}

int plm_get_video_enabled(plm_t *self) {
	return self->video_enabled;
}

void plm_set_video_skip_to_intra(plm_t *self) {
	if (self->video_decoder) {
		plm_video_set_skip_to_intra(self->video_decoder);
	}
}

void plm_set_video_skip_b_frames(plm_t *self, int skip) {
	if (self->video_decoder) {
		plm_video_set_skip_b_frames(self->video_decoder, skip);
	}
}

void plm_set_video_enabled(plm_t *self, int enabled) {
	self->video_enabled = enabled;

	if (!enabled) {
		self->video_packet_type = 0;
		return;
	}

	self->video_packet_type = (plm_init_decoders(self) && self->video_decoder)
		? PLM_DEMUX_PACKET_VIDEO_1
		: 0;
}

int plm_get_num_video_streams(plm_t *self) {
	return plm_demux_get_num_video_streams(self->demux);
}

int plm_get_width(plm_t *self) {
	return (plm_init_decoders(self) && self->video_decoder)
		? plm_video_get_width(self->video_decoder)
		: 0;
}

int plm_get_height(plm_t *self) {
	return (plm_init_decoders(self) && self->video_decoder)
		? plm_video_get_height(self->video_decoder)
		: 0;
}

double plm_get_framerate(plm_t *self) {
	return (plm_init_decoders(self) && self->video_decoder)
		? plm_video_get_framerate(self->video_decoder)
		: 0;
}

double plm_get_pixel_aspect_ratio(plm_t *self) {
	return (plm_init_decoders(self) && self->video_decoder)
		? plm_video_get_pixel_aspect_ratio(self->video_decoder)
		: 0;
}

int plm_get_num_audio_streams(plm_t *self) {
	return plm_demux_get_num_audio_streams(self->demux);
}

int plm_get_samplerate(plm_t *self) {
	return (plm_init_decoders(self) && self->audio_decoder)
		? plm_audio_get_samplerate(self->audio_decoder)
		: 0;
}

double plm_get_audio_lead_time(plm_t *self) {
	return self->audio_lead_time;
}

void plm_set_audio_lead_time(plm_t *self, double lead_time) {
	self->audio_lead_time = lead_time;
}

double plm_get_time(plm_t *self) {
	return self->time;
}

double plm_get_duration(plm_t *self) {
	return plm_demux_get_duration(self->demux, PLM_DEMUX_PACKET_VIDEO_1);
}

void plm_rewind(plm_t *self) {
	if (self->video_decoder) {
		plm_video_rewind(self->video_decoder);
	}

	if (self->audio_decoder) {
		plm_audio_rewind(self->audio_decoder);
	}

	plm_demux_rewind(self->demux);
	self->time = 0;
	self->has_ended = FALSE;
}

int plm_get_loop(plm_t *self) {
	return self->loop;
}

void plm_set_loop(plm_t *self, int loop) {
	self->loop = loop;
}

int plm_has_ended(plm_t *self) {
	return self->has_ended;
}

void plm_set_video_decode_callback(plm_t *self, plm_video_decode_callback fp, void *user) {
	self->video_decode_callback = fp;
	self->video_decode_callback_user_data = user;
}

void plm_set_audio_decode_callback(plm_t *self, plm_audio_decode_callback fp, void *user) {
	self->audio_decode_callback = fp;
	self->audio_decode_callback_user_data = user;
}

void plm_decode(plm_t *self, double tick) {
	if (!plm_init_decoders(self)) {
		return;
	}

	int decode_video = (self->video_decode_callback && self->video_packet_type);
	int decode_audio = (self->audio_decode_callback && self->audio_packet_type);

	if (!decode_video && !decode_audio) {
		// Nothing to do here
		return;
	}

	int did_decode = FALSE;
	int decode_video_failed = FALSE;
	int decode_audio_failed = FALSE;

	double video_target_time = self->time + tick;
	double audio_target_time = self->time + tick + self->audio_lead_time;

	do {
		did_decode = FALSE;

		if (decode_video && plm_video_get_time(self->video_decoder) < video_target_time) {
			plm_frame_t *frame = plm_video_decode(self->video_decoder);
			if (frame) {
				self->video_decode_callback(self, frame, self->video_decode_callback_user_data);
				did_decode = TRUE;
			}
			else {
				decode_video_failed = TRUE;
			}
		}

		if (decode_audio && plm_audio_get_time(self->audio_decoder) < audio_target_time) {
			plm_samples_t *samples = plm_audio_decode(self->audio_decoder);
			if (samples) {
				self->audio_decode_callback(self, samples, self->audio_decode_callback_user_data);
				did_decode = TRUE;
			}
			else {
				decode_audio_failed = TRUE;
			}
		}
	} while (did_decode);

	// Did all sources we wanted to decode fail and the demuxer is at the end?
	if (
		(!decode_video || decode_video_failed) &&
		(!decode_audio || decode_audio_failed) &&
		plm_demux_has_ended(self->demux)
	) {
		plm_handle_end(self);
		return;
	}

	self->time += tick;
}

plm_frame_t *plm_decode_video(plm_t *self) {
	if (!plm_init_decoders(self)) {
		return NULL;
	}

	if (!self->video_packet_type) {
		return NULL;
	}

	plm_frame_t *frame = plm_video_decode(self->video_decoder);
	if (frame) {
		self->time = frame->time;
	}
	else if (plm_demux_has_ended(self->demux)) {
		plm_handle_end(self);
	}
	return frame;
}

plm_samples_t *plm_decode_audio(plm_t *self) {
	if (!plm_init_decoders(self)) {
		return NULL;
	}

	if (!self->audio_packet_type) {
		return NULL;
	}

	plm_samples_t *samples = plm_audio_decode(self->audio_decoder);
	if (samples) {
		self->time = samples->time;
	}
	else if (plm_demux_has_ended(self->demux)) {
		plm_handle_end(self);
	}
	return samples;
}

void plm_handle_end(plm_t *self) {
	if (self->loop) {
		plm_rewind(self);
	}
	else {
		self->has_ended = TRUE;
	}
}

void plm_read_video_packet(plm_buffer_t *buffer, void *user) {
	PLM_UNUSED(buffer);
	plm_t *self = (plm_t *)user;
	plm_read_packets(self, self->video_packet_type);
}

void plm_read_audio_packet(plm_buffer_t *buffer, void *user) {
	PLM_UNUSED(buffer);
	plm_t *self = (plm_t *)user;
	plm_read_packets(self, self->audio_packet_type);
}

void plm_read_packets(plm_t *self, int requested_type) {
	plm_packet_t *packet;
	while ((packet = plm_demux_decode(self->demux))) {
		if (packet->type == self->video_packet_type) {
			plm_buffer_write(self->video_buffer, packet->data0, packet->len0);
			if(packet->data1)
				plm_buffer_write(self->video_buffer, packet->data1, packet->len1);
		}
		else if (packet->type == self->audio_packet_type) {
			plm_buffer_write(self->audio_buffer, packet->data0, packet->len0);
			if(packet->data1)
				plm_buffer_write(self->audio_buffer, packet->data1, packet->len1);
		}

		if (packet->type == requested_type) {
			return;
		}
	}

	if (plm_demux_has_ended(self->demux)) {
		if (self->video_buffer) {
			plm_buffer_signal_end(self->video_buffer);
		}
		if (self->audio_buffer) {
			plm_buffer_signal_end(self->audio_buffer);
		}
	}
}

plm_frame_t *plm_seek_frame(plm_t *self, double time, int seek_exact) {
	if (!plm_init_decoders(self)) {
		return NULL;
	}

	if (!self->video_packet_type) {
		return NULL;
	}

	int type = self->video_packet_type;

	double start_time = plm_demux_get_start_time(self->demux, type);
	double duration = plm_demux_get_duration(self->demux, type);

	if (time < 0) {
		time = 0;
	}
	else if (time > duration) {
		time = duration;
	}

	plm_packet_t *packet = plm_demux_seek(self->demux, time, type, TRUE);
	if (!packet) {
		return NULL;
	}

	// Disable writing to the audio buffer while decoding video
	int previous_audio_packet_type = self->audio_packet_type;
	self->audio_packet_type = 0;

	// Clear video buffer and decode the found packet
	plm_video_rewind(self->video_decoder);
	plm_video_set_time(self->video_decoder, packet->pts - start_time);
	plm_buffer_write(self->video_buffer, packet->data0, packet->len0);
	if(packet->data1)
		plm_buffer_write(self->video_buffer, packet->data1, packet->len1);
	plm_frame_t *frame = plm_video_decode(self->video_decoder);

	// If we want to seek to an exact frame, we have to decode all frames
	// on top of the intra frame we just jumped to.
	if (seek_exact) {
		while (frame && frame->time < time) {
			frame = plm_video_decode(self->video_decoder);
		}
	}

	// Enable writing to the audio buffer again?
	self->audio_packet_type = previous_audio_packet_type;

	if (frame) {
		self->time = frame->time;
	}

	self->has_ended = FALSE;
	return frame;
}

int plm_seek(plm_t *self, double time, int seek_exact) {
	plm_frame_t *frame = plm_seek_frame(self, time, seek_exact);

	if (!frame) {
		return FALSE;
	}

	if (self->video_decode_callback) {
		self->video_decode_callback(self, frame, self->video_decode_callback_user_data);
	}

	// If audio is not enabled we are done here.
	if (!self->audio_packet_type) {
		return TRUE;
	}

	// Sync up Audio. This demuxes more packets until the first audio packet
	// with a PTS greater than the current time is found. plm_decode() is then
	// called to decode enough audio data to satisfy the audio_lead_time.

	double start_time = plm_demux_get_start_time(self->demux, self->video_packet_type);
	plm_audio_rewind(self->audio_decoder);

	plm_packet_t *packet = NULL;
	while ((packet = plm_demux_decode(self->demux))) {
		if (packet->type == self->video_packet_type) {
			plm_buffer_write(self->video_buffer, packet->data0, packet->len0);
			if(packet->data1)
				plm_buffer_write(self->video_buffer, packet->data1, packet->len1);
		}
		else if (
			packet->type == self->audio_packet_type &&
			packet->pts - start_time > self->time
		) {
			plm_audio_set_time(self->audio_decoder, packet->pts - start_time);
			plm_buffer_write(self->audio_buffer, packet->data0, packet->len0);
			if(packet->data1)
				plm_buffer_write(self->audio_buffer, packet->data1, packet->len1);
			plm_decode(self, 0);
			break;
		}
	}

	return TRUE;
}

// -----------------------------------------------------------------------------
// plm_buffer implementation

enum plm_buffer_mode {
	PLM_BUFFER_MODE_FILE,
	PLM_BUFFER_MODE_FIXED_MEM,
	PLM_BUFFER_MODE_RING,
	PLM_BUFFER_MODE_APPEND
};

struct plm_buffer_t {
	size_t bit_index;
	size_t capacity;
	size_t length;
	size_t total_size;
	size_t read_byte_pos;     // Byte index of first unread byte
    size_t write_byte_pos;    // Byte index one past last written byte
	int discard_read_bytes;
	int has_ended;
	int free_when_done;
	int close_when_done;
	PLM_FILE_TYPE fh;

	plm_buffer_load_callback load_callback;
	plm_buffer_seek_callback seek_callback;
	plm_buffer_tell_callback tell_callback;

	void *load_callback_user_data;
	uint8_t *bytes;
	enum plm_buffer_mode mode;
};

typedef struct {
	int16_t index;
	int16_t value;
} plm_vlc_t;

typedef struct {
	int16_t index;
	uint16_t value;
} plm_vlc_uint_t;


void plm_buffer_seek(plm_buffer_t *self, size_t pos);
size_t plm_buffer_tell(plm_buffer_t *self);
void plm_buffer_discard_read_bytes(plm_buffer_t *self);

void plm_buffer_load_file_callback(plm_buffer_t *self, void *user);
void plm_buffer_seek_file_callback(plm_buffer_t *self, size_t offset, void *user);
size_t plm_buffer_tell_file_callback(plm_buffer_t *self, void *user);

static inline int plm_buffer_has(plm_buffer_t *self, size_t count);
static inline uint32_t plm_buffer_read(plm_buffer_t *self, int count);
static inline void plm_buffer_align(plm_buffer_t *self);
static inline void plm_buffer_skip(plm_buffer_t *self, size_t count);
static inline int plm_buffer_skip_bytes(plm_buffer_t *self, uint8_t v);
static inline int plm_buffer_next_start_code(plm_buffer_t *self);
static inline int plm_buffer_find_start_code(plm_buffer_t *self, int code);
static inline int16_t plm_buffer_read_vlc(plm_buffer_t *self, const plm_vlc_t *table);
static inline uint16_t plm_buffer_read_vlc_uint(plm_buffer_t *self, const plm_vlc_uint_t *table);

plm_buffer_t *plm_buffer_create_with_filename(const char *filename) {
	PLM_FILE_TYPE fh = PLM_FILE_OPEN(filename);
	if (fh == PLM_FILE_INVALID_HANDLE) {
		fprintf(stderr, "Can not open file: %s\n", filename);
		return NULL;
	}
	return plm_buffer_create_with_file(fh, TRUE);
}

plm_buffer_t *plm_buffer_create_with_file(PLM_FILE_TYPE fh, int close_when_done) {
	plm_buffer_t *self = plm_buffer_create_with_capacity(PLM_BUFFER_DEFAULT_SIZE);
	if (!self) {
		if (close_when_done && fh != PLM_FILE_INVALID_HANDLE) {
			PLM_FILE_CLOSE(fh);
		}
		return NULL;
	}
	self->fh = fh;
	self->close_when_done = close_when_done;
	self->mode = PLM_BUFFER_MODE_FILE;
	self->discard_read_bytes = TRUE;

	PLM_FILE_SEEK(self->fh, 0, SEEK_END);
	self->total_size = PLM_FILE_TELL(self->fh);
	PLM_FILE_SEEK(self->fh, 0, SEEK_SET);

	self->load_callback = plm_buffer_load_file_callback;
	self->seek_callback = plm_buffer_seek_file_callback;
	self->tell_callback = plm_buffer_tell_file_callback;

	return self;
}

plm_buffer_t *plm_buffer_create_with_memory(uint8_t *bytes, size_t length, int free_when_done) {
	plm_buffer_t *self = (plm_buffer_t *)PLM_MALLOC(sizeof(plm_buffer_t));
	if(!self) {
		fprintf(stderr, "Out of memory for self. [plm_buffer_create_with_memory]\n");
		return NULL;
	}

	PLM_MEMZERO(self, sizeof(plm_buffer_t));
	// Reads index the bytes with (pos & (capacity - 1)), the ring buffers' wrap. A fixed buffer
	// must never wrap, so its capacity is the next power of two >= length (only length is read).
	// With capacity = length (a file size, rarely a power of two) memory playback read wrong bytes.
	size_t cap = 1;
	while (cap < length) {
		cap <<= 1;
	}
	self->capacity = cap;
	self->length = length;
	self->total_size = length;
	self->free_when_done = free_when_done;
	self->bytes = bytes;
	self->mode = PLM_BUFFER_MODE_FIXED_MEM;
	self->discard_read_bytes = FALSE;
	return self;
}

plm_buffer_t *plm_buffer_create_with_capacity(size_t capacity) {
	plm_buffer_t *self = (plm_buffer_t *)PLM_MALLOC(sizeof(plm_buffer_t));
	if(!self) {
		fprintf(stderr, "Out of memory for self. [plm_buffer_create_with_capacity]\n");
		return NULL;
	}

	PLM_MEMZERO(self, sizeof(plm_buffer_t));
	self->capacity = capacity;
	self->free_when_done = TRUE;
	self->total_size = 0;
	self->bytes = (uint8_t *)PLM_MEMALIGN(32, capacity + PLM_PEEK_SIZE);
	if(!self->bytes) {
		fprintf(stderr, "Out of memory for bytes. [plm_buffer_create_with_capacity]\n");
		PLM_FREE(self);
		return NULL;
	}
	self->mode = PLM_BUFFER_MODE_RING;
	self->discard_read_bytes = TRUE;
	return self;
}

plm_buffer_t *plm_buffer_create_for_appending(size_t initial_capacity) {
	plm_buffer_t *self = plm_buffer_create_with_capacity(initial_capacity);
	if (!self) {
		return NULL;
	}
	self->mode = PLM_BUFFER_MODE_APPEND;
	self->discard_read_bytes = FALSE;
	return self;
}

void plm_buffer_destroy(plm_buffer_t *self) {
	if(!self)
		return;

	if ((self->fh != PLM_FILE_INVALID_HANDLE) && self->close_when_done) {
		PLM_FILE_CLOSE(self->fh);
	}
	if (self->free_when_done) {
		PLM_FREE(self->bytes);
		self->bytes = NULL;
	}
	PLM_FREE(self);
}

size_t plm_buffer_get_size(plm_buffer_t *self) {
	return (self->mode == PLM_BUFFER_MODE_FILE)
		? self->total_size
		: self->length;
}

size_t plm_buffer_get_remaining(plm_buffer_t *self) {
	return self->length - (self->bit_index >> 3);
}

static inline size_t plm_buffer_get_space(plm_buffer_t *self) {
    return self->capacity - self->length;
}

// How many bytes are contiguous from pos until end of buffer
static inline size_t plm_buffer_bytes_until_wrap(plm_buffer_t *self, size_t pos) {
    return self->capacity - pos;
}

// (self->capacity - 1) is mod[%] trick that can only be used with capacity that are
// powers of 2.
static inline uint8_t *plm_buffer_ptr_from_read(const plm_buffer_t *self, size_t byte_off) {
    size_t pos = (self->read_byte_pos + byte_off) & (self->capacity - 1);
    return &self->bytes[pos]; // Safe for bytes[0..3] because of guard bytes
}

// Keep a 4-byte guard at the end of the allocation in sync with bytes[0..3].
// This lets hot-path bit reads grab a 32-bit window (s[0..3]) without needing
// any ring wrap checks when the read crosses the end of the buffer.
static inline void plm_buffer_ring_sync_guard(plm_buffer_t *self) {
    self->bytes[self->capacity + 0] = self->bytes[0];
    self->bytes[self->capacity + 1] = self->bytes[1];
    self->bytes[self->capacity + 2] = self->bytes[2];
    self->bytes[self->capacity + 3] = self->bytes[3];
}

// An ordinary cached copy. (Store-queue writes into the ring bypass the D-cache and gave wrong
// pictures on hardware.)
static inline void plm_sq_copy_bytes(void *dest, const void *src, size_t length) {
    memcpy(dest, src, length);
}

int plm_buffer_ring_grow_memalign(plm_buffer_t *self, size_t new_capacity) {
    uint8_t *old_bytes = self->bytes;
    uint8_t *new_bytes = (uint8_t *)PLM_MEMALIGN(32, new_capacity + PLM_PEEK_SIZE);
	if (!new_bytes) {
        fprintf(stderr,
            "PLM_MEMALIGN failed when trying to resize to %zu bytes [plm_buffer_ring_grow_memalign]\n",
            new_capacity);
        return -1;
    }

    // Copy unread data in order into new_bytes[0..length-1]
    size_t first = PLM_MIN(self->length, self->capacity - self->read_byte_pos);
    plm_sq_copy_bytes(new_bytes, old_bytes + self->read_byte_pos, first);
    if (self->length > first) {
        plm_sq_copy_bytes(new_bytes + first, old_bytes, self->length - first);
    }

    PLM_FREE(old_bytes);
    self->bytes = new_bytes;
    self->capacity = new_capacity;
    self->read_byte_pos = 0;
    self->write_byte_pos = self->length;

    if (self->length > 0) {
        plm_buffer_ring_sync_guard(self);
    }

    return 0;
}

int plm_buffer_ring_grow_realloc(plm_buffer_t *self, size_t new_capacity) {
    size_t old_capacity = self->capacity;
    size_t old_write = self->write_byte_pos;

    uint8_t *new_bytes = (uint8_t *)PLM_REALLOC(self->bytes, new_capacity + PLM_PEEK_SIZE);
    if (!new_bytes) {
        fprintf(stderr,
            "PLM_REALLOC failed when trying to resize to %zu bytes [plm_buffer_ring_grow_realloc]\n",
            new_capacity);
        return -1;
    }

    self->bytes = new_bytes;

    // Wrapped unread data: [read..old_capacity) + [0..old_write)
    if (old_write < self->read_byte_pos) {
        plm_sq_copy_bytes(self->bytes + old_capacity, self->bytes, old_write);
        self->write_byte_pos = old_capacity + old_write;
    }

    self->capacity = new_capacity;

    return 0;
}

// Copy up to len bytes into plm_buffer_t. Returns bytes written.
static inline size_t plm_buffer_ring_write(plm_buffer_t *self, uint8_t *bytes, size_t length) {
    size_t first = PLM_MIN(length, plm_buffer_bytes_until_wrap(self, self->write_byte_pos));

    plm_sq_copy_bytes(&self->bytes[self->write_byte_pos], bytes, first);

	self->write_byte_pos += first;
	if (self->write_byte_pos == self->capacity) {
        self->write_byte_pos = 0;
    }

    size_t remaining = length - first;
    if (remaining) {

        plm_sq_copy_bytes(&self->bytes[0], bytes + first, remaining);

		self->write_byte_pos = remaining;
    }

    self->length += length;
	self->has_ended = FALSE;

	plm_buffer_ring_sync_guard(self);

    return length;
}

static inline int plm_buffer_ring_fs_read_into(plm_buffer_t *self, size_t want) {
    // One contiguous span until we wrap
    size_t bytes_until_wrap = plm_buffer_bytes_until_wrap(self, self->write_byte_pos);
    size_t first_chunk_want = (want < bytes_until_wrap) ? want : bytes_until_wrap;

    int first_chunk_read = PLM_FILE_READ(self->fh, self->bytes + self->write_byte_pos, first_chunk_want);
    if (first_chunk_read <= 0) {
        self->has_ended = TRUE;
        return first_chunk_read; // 0 = EOF, <0 = error
    }

    // Commit first chunk
    self->write_byte_pos += (size_t)first_chunk_read;
    if (self->write_byte_pos >= self->capacity) {
        self->write_byte_pos -= self->capacity;
    }
    self->length += (size_t)first_chunk_read;

    size_t second_chunk_want = want - (size_t)first_chunk_read;
    if (second_chunk_want == 0) {
        return first_chunk_read;
    }

    // Wrapped chunk
    int second_chunk_read = PLM_FILE_READ(self->fh, self->bytes, second_chunk_want);
    if (second_chunk_read <= 0) {
        return first_chunk_read; // keep what we got
    }

    // Commit second chunk
    self->write_byte_pos = (size_t)second_chunk_read;
    self->length += (size_t)second_chunk_read;

    plm_buffer_ring_sync_guard(self);

    return first_chunk_read + second_chunk_read;
}

size_t plm_buffer_write(plm_buffer_t *self, uint8_t *bytes, size_t length) {
	if (self->mode == PLM_BUFFER_MODE_FIXED_MEM) {
		return 0;
	}

	if (self->discard_read_bytes) {
		plm_buffer_discard_read_bytes(self);
		if (self->mode == PLM_BUFFER_MODE_RING) {
			self->total_size = 0;
		}
	}

	// Do we have to resize to fit the new data?
	size_t bytes_available = plm_buffer_get_space(self);
	if (bytes_available < length) {
		size_t new_size = self->capacity;
		do {
			new_size *= 2;
		} while (new_size - self->length < length);

		int result = plm_buffer_ring_grow_memalign(self, new_size);
		if (result < 0)
			return 0;
	}

	plm_buffer_ring_write(self, bytes, length);

	return length;
}

void plm_buffer_signal_end(plm_buffer_t *self) {
	self->total_size = self->length;
}

void plm_buffer_set_load_callback(plm_buffer_t *self, plm_buffer_load_callback fp, void *user) {
	self->load_callback = fp;
	self->load_callback_user_data = user;
}

void plm_buffer_rewind(plm_buffer_t *self) {
	plm_buffer_seek(self, 0);
}

void plm_buffer_seek(plm_buffer_t *self, size_t pos) {
	self->has_ended = FALSE;

	if (self->seek_callback) {
		self->seek_callback(self, pos, self->load_callback_user_data);
		self->bit_index = 0;
		self->length = 0;
		self->read_byte_pos = 0;
		self->write_byte_pos = 0;
	}
	else if (self->mode == PLM_BUFFER_MODE_RING) {
		if (pos != 0) {
			// Seeking to non-0 is forbidden for dynamic-mem buffers
			return;
		}
		self->bit_index = 0;
		self->length = 0;
		self->total_size = 0;
		self->read_byte_pos = 0;
		self->write_byte_pos = 0;
	}
	else if (pos < self->length) {
		self->bit_index = (uint32_t)(pos << 3);
	}
}

size_t plm_buffer_tell(plm_buffer_t *self) {
	return self->tell_callback
		? self->tell_callback(self, self->load_callback_user_data) + (self->bit_index >> 3) - self->length
		: self->bit_index >> 3;
}

void plm_buffer_discard_read_bytes(plm_buffer_t *self) {
    size_t byte_pos = self->bit_index >> 3;
    if (byte_pos == 0) return;

    // Advance read position with wrap
	self->read_byte_pos = (self->read_byte_pos + byte_pos) & (self->capacity - 1);

    // Decrease buffered length
    self->length -= byte_pos;

    // Keep only remaining bit offset within current byte
    self->bit_index &= 7;

    // If empty, normalize
    if (self->length == 0) {
        self->bit_index = 0;
        self->read_byte_pos = 0;
        self->write_byte_pos = 0;
    }
}

void plm_buffer_load_file_callback(plm_buffer_t *self, void *user) {
	PLM_UNUSED(user);

	if (self->discard_read_bytes) {
		plm_buffer_discard_read_bytes(self);
	}

	size_t bytes_available = self->capacity - self->length;
	int bytes_read = plm_buffer_ring_fs_read_into(self, bytes_available);

	if (bytes_read <= 0) {
		self->has_ended = TRUE;
	}
}

void plm_buffer_seek_file_callback(plm_buffer_t *self, size_t offset, void *user) {
	PLM_UNUSED(user);
	PLM_FILE_SEEK(self->fh, offset, SEEK_SET);
}

size_t plm_buffer_tell_file_callback(plm_buffer_t *self, void *user) {
	PLM_UNUSED(user);
	return PLM_FILE_TELL(self->fh);
}

inline int plm_buffer_has_ended(plm_buffer_t *self) {
	return self->has_ended;
}

static inline int plm_buffer_has(plm_buffer_t *self, size_t count) {
	if (((self->length << 3) - self->bit_index) >= count) {
		return TRUE;
	}

	if (self->load_callback) {
		self->load_callback(self, self->load_callback_user_data);

		if (((self->length << 3) - self->bit_index) >= count) {
			return TRUE;
		}
	}

	if (self->total_size != 0 && self->length == self->total_size) {
		self->has_ended = TRUE;
	}
	return FALSE;
}

static inline uint32_t plm_buffer_load_u32be(const uint8_t *s) {
	return
		((uint32_t)s[0] << 24) |
		((uint32_t)s[1] << 16) |
		((uint32_t)s[2] <<  8) |
		((uint32_t)s[3] <<  0);
}

// Gain 12%: https://github.com/bitbank2/pl_mpeg/blob/master/pl_mpeg.h
static inline uint32_t plm_buffer_read(plm_buffer_t *self, int count) {
	uint32_t value = 0;
    uint32_t bit_index = (uint32_t)self->bit_index;
    size_t byte_off = (size_t)(bit_index >> 3);
    const uint8_t *s = plm_buffer_ptr_from_read(self, byte_off);

	// Safe unaligned load (big-endian)
    uint32_t word_be = plm_buffer_load_u32be(s);

    value = word_be << (bit_index & 7);
    bit_index += (uint32_t)count;
    value >>= (32 - (uint32_t)count);

    self->bit_index = bit_index;
    return value;
}

static inline void plm_buffer_align(plm_buffer_t *self) {
	self->bit_index = ((self->bit_index + 7) >> 3) << 3; // Align to next byte
}

static inline void plm_buffer_skip(plm_buffer_t *self, size_t count) {
	if (plm_buffer_has(self, count)) {
		self->bit_index += count;
	}
}

static inline int plm_buffer_skip_bytes(plm_buffer_t *self, uint8_t v) {
    plm_buffer_align(self);

    int skipped = 0;
    while (TRUE) {
        size_t avail_bits = (self->length << 3) - self->bit_index;
        if (avail_bits < 8 && !plm_buffer_has(self, 8)) {
            break;
        }

        size_t byte_off = (size_t)(self->bit_index >> 3);
        const uint8_t *p = plm_buffer_ptr_from_read(self, byte_off);

        if (*p != v) {
            break;
        }

        self->bit_index += 8;
        skipped++;
    }

    return skipped;
}

// Number of the n aligned words at w, from the first, that contain no zero byte. A start code
// (00 00 01) cannot begin inside such a word.
static inline size_t plm_words_without_zero(const uint32_t *w, size_t n) {
	const uint32_t *q = w;
	uint32_t x;
	if (!n) {
		return 0;
	}
#ifdef __SH4__
	__asm__(
		"1:\n\t"
		"mov.l	@%0+,%1\n\t"
		"cmp/str	%3,%1\n\t"
		"bt	2f\n\t"
		"dt	%2\n\t"
		"bf	1b\n\t"
		"bra	3f\n\t"
		"nop\n"
		"2:\n\t"
		"add	#-4,%0\n"
		"3:\n"
		: "+r"(q), "=&r"(x), "+r"(n) : "r"(0) : "t", "memory");
#else
	while (n--) {
		x = *q;
		if ((x - 0x01010101u) & ~x & 0x80808080u) {
			break;
		}
		q++;
	}
#endif
	return (size_t)(q - w);
}

static inline int plm_buffer_next_start_code(plm_buffer_t *self) {
    plm_buffer_align(self);

    while (TRUE) {
        size_t avail_bits = (self->length << 3) - self->bit_index;
        if (avail_bits < (5u << 3) && !plm_buffer_has(self, (5u << 3))) {
            return -1;
        }

        size_t byte_off = (size_t)(self->bit_index >> 3);
        const uint8_t *p = plm_buffer_ptr_from_read(self, byte_off);
        if (((uintptr_t)p & 3) == 0) {
            // whole words before the end of the ring's storage, keeping the last 5 bytes for the
            // exact check (which also refills)
            size_t left = self->length - byte_off;
            size_t room = (size_t)(self->bytes + self->capacity - p);
            if (left > room) {
                left = room;
            }
            if (left >= 9) {
                size_t k = plm_words_without_zero((const uint32_t *)p, (left - 5) >> 2);
                if (k) {
                    self->bit_index += (uint32_t)(k << 5);
                    continue;
                }
            }
        }

        // Thanks to the 4-byte guard, p[0..3] is always safe here
        if (p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x01) {
            self->bit_index = (uint32_t)((byte_off + 4) << 3);
            return p[3];
        }

        self->bit_index += 8;
    }
}

static inline int plm_buffer_find_start_code(plm_buffer_t *self, int code) {
	int current = 0;
	while (TRUE) {
		current = plm_buffer_next_start_code(self);
		if (current == code || current == -1) {
			return current;
		}
	}
	return -1;
}

static inline int plm_buffer_has_start_code(plm_buffer_t *self, int code) {
	size_t previous_bit_index = self->bit_index;
	int previous_discard_read_bytes = self->discard_read_bytes;

	self->discard_read_bytes = FALSE;
	int current = plm_buffer_find_start_code(self, code);

	self->bit_index = previous_bit_index;
	self->discard_read_bytes = previous_discard_read_bytes;
	return current;
}

static inline int plm_buffer_peek_non_zero(plm_buffer_t *self, int bit_count) {
	size_t avail_bits = (self->length << 3) - self->bit_index;
	if (avail_bits < (size_t)bit_count && !plm_buffer_has(self, bit_count)) {
		return FALSE;
	}

	uint32_t bit_index = (uint32_t)self->bit_index;
	size_t byte_off = (size_t)(bit_index >> 3);
	const uint8_t *s = plm_buffer_ptr_from_read(self, byte_off);
	uint32_t word_be = plm_buffer_load_u32be(s);
	uint32_t val = (word_be << (bit_index & 7)) >> (32 - (uint32_t)bit_count);
	return val != 0;
}

static inline int16_t plm_buffer_read_vlc(plm_buffer_t *self, const plm_vlc_t *table) {
	plm_vlc_t state = {0, 0};
	uint32_t bit_index = (uint32_t)self->bit_index;
	size_t byte_off = (size_t)(bit_index >> 3);
	const uint8_t *s = plm_buffer_ptr_from_read(self, byte_off);
	uint32_t bits = plm_buffer_load_u32be(s) << (bit_index & 7);

	while (TRUE) {
		state = table[state.index + (int)(bits >> 31)];
		bit_index++;

		if (state.index <= 0) {
			break;
		}

		if ((bit_index & 7) == 0) {
			byte_off = (size_t)(bit_index >> 3);
			s = plm_buffer_ptr_from_read(self, byte_off);
			bits = plm_buffer_load_u32be(s);
		}
		else {
			bits <<= 1;
		}
	}

	self->bit_index = bit_index;
	return state.value;
}

static inline uint16_t plm_buffer_read_vlc_uint(plm_buffer_t *self, const plm_vlc_uint_t *table) {
	return (uint16_t)plm_buffer_read_vlc(self, (const plm_vlc_t *)table);
}

// Table-driven VLC decoding. A lookup table built from a plm_vlc_t tree resolves every code of up to
// PLM_VLC_LUT_BITS bits with one lookup; longer codes continue the bit-serial tree walk from the
// node the table stopped at. Results are identical to plm_buffer_read_vlc by construction.
#define PLM_VLC_LUT_BITS 10
__attribute__((noinline, cold)) static int16_t plm_buffer_read_vlc_tail(plm_buffer_t *self, const plm_vlc_t *table,
		int index, uint32_t bit_index);

typedef struct {
	int16_t value;   // leaf: the decoded value; otherwise: tree index to continue from
	uint8_t len;     // bits consumed
	uint8_t leaf;
} plm_vlc_lut_t;

static void plm_vlc_lut_build_n(const plm_vlc_t *table, plm_vlc_lut_t *lut, int lut_bits) {
	for (int p = 0; p < (1 << lut_bits); p++) {
		int index = 0;
		plm_vlc_lut_t e = {0, (uint8_t)lut_bits, 0};
		for (int k = 0; k < lut_bits; k++) {
			int bit = (p >> (lut_bits - 1 - k)) & 1;
			plm_vlc_t st = table[index + bit];
			if (st.index <= 0) {
				e.value = st.value;
				e.len = (uint8_t)(k + 1);
				e.leaf = 1;
				break;
			}
			index = st.index;
		}
		if (!e.leaf) {
			e.value = (int16_t)index;
		}
		lut[p] = e;
	}
}

static void plm_vlc_lut_build(const plm_vlc_t *table, plm_vlc_lut_t *lut) {
	plm_vlc_lut_build_n(table, lut, PLM_VLC_LUT_BITS);
}

// DCT coefficient table with the sign bit and the end-of-block bit folded in. Entry for a 9-bit
// window: run, level and total bits (code + sign), or a kind for end_of_block, escape, or a code
// longer than the window.
// 'first' tables are for coefficient 0 (where '1s' is run 0 / level 1), 'rest' for later ones
// (where '10' is end_of_block and '11s' is run 0 / level 1).
#ifndef PLM_COEF_LUT_BITS
#define PLM_COEF_LUT_BITS 9   // window of the sign/EOB-folded coefficient tables (2 KB each at 9)
#endif
// Entry: four bytes, in the order the assembly loads them (mov.b @r14+, sign-extending):
//   run4  4 * run: the step to this coefficient's entry in the per-index table (plm_video_t.qz_*)
//   lenf  bits to skip (code + sign), or with bit 7 set one of the kinds below (run4 is 0 then)
//   mag   2|level| + 1, the non-intra dequantiser's factor (at most 81 for the table codes)
//   sign  0, or -1 for a negative level
#define PLM_COEF_EOB  0x81   // end_of_block
#define PLM_COEF_ESC  0x82   // escape
#define PLM_COEF_LONG 0x84   // code longer than the window: second-level table
#define PLM_COEF_L3   0x88   // second-level miss: third-level table
#define PLM_COEF_INV  0x90   // third-level miss: not a valid code
typedef struct {
	uint8_t run4;
	uint8_t lenf;
	uint8_t mag;
	int8_t sign;
} plm_coef_lut_t;

static void plm_coef_lut_build(const plm_vlc_t *tree, plm_coef_lut_t *lut, int rest) {
	const int B = PLM_COEF_LUT_BITS;
	for (int p = 0; p < (1 << B); p++) {
		plm_coef_lut_t e = {0, PLM_COEF_LONG, 0, 0};
		int index = 0, len = 0;
		uint16_t coeff = 0;
		int leaf = 0;
		for (int k = 0; k < B; k++) {
			plm_vlc_t st = tree[index + ((p >> (B - 1 - k)) & 1)];
			if (st.index <= 0) {
				coeff = (uint16_t)st.value;
				len = k + 1;
				leaf = 1;
				break;
			}
			index = st.index;
		}
		if (leaf) {
			#define PLM_BIT_AT(pos) ((p >> (B - 1 - (pos))) & 1)
			if (coeff == 0xffff) {
				e.lenf = PLM_COEF_ESC;
			}
			else if (rest && coeff == 0x0001) {
				// len == 1 here; the next bit decides
				if (PLM_BIT_AT(1) == 0) {
					e.lenf = PLM_COEF_EOB;
				}
				else if (3 <= B) {
					e.mag = 3;
					e.sign = PLM_BIT_AT(2) ? -1 : 0;
					e.lenf = 3;
				}
			}
			else if (len + 1 <= B) {
				int lv = coeff & 0xff;
				e.run4 = (uint8_t)((coeff >> 8) * 4);
				e.mag = (uint8_t)(2 * lv + 1);
				e.sign = (lv && PLM_BIT_AT(len)) ? -1 : 0;   // level 0 (an invalid code) has no sign
				e.lenf = (uint8_t)(len + 1);
			}
			#undef PLM_BIT_AT
		}
		lut[p] = e;
	}
}

// Second level for codes that start with six zeros (all codes longer than 8 bits + sign): entry for
// the next PLM_COEF_L2_BITS bits, same format, length counted from the start of the code. Codes
// whose sign is outside the 6 + PLM_COEF_L2_BITS window get `miss` (the next level, or invalid).
#ifndef PLM_COEF_L2_BITS
#define PLM_COEF_L2_BITS 8   // 1 KB; covers the 10-, 12- and 13-bit codes
#endif
static __attribute__((unused)) void plm_coef_ln_build(const plm_vlc_t *tree, plm_coef_lut_t *lut, int zeros, int bits, int miss) {
	const int B = zeros + bits;
	for (int p = 0; p < (1 << bits); p++) {   // the B-bit window is `zeros` zero bits, then p
		plm_coef_lut_t e = {0, (uint8_t)miss, 0, 0};
		int index = 0;
		for (int k = 0; k < B; k++) {
			plm_vlc_t st = tree[index + ((p >> (B - 1 - k)) & 1)];
			if (st.index <= 0) {
				uint16_t coeff = (uint16_t)st.value;
				int len = k + 1;
				if (coeff != 0xffff && coeff != 0x0001 && len + 1 <= B) {
					int lv = coeff & 0xff;
					e.run4 = (uint8_t)((coeff >> 8) * 4);
					e.mag = (uint8_t)(2 * lv + 1);
					e.sign = (lv && ((p >> (B - 1 - len)) & 1)) ? -1 : 0;
					e.lenf = (uint8_t)(len + 1);
				}
				break;
			}
			index = st.index;
		}
		lut[p] = e;
	}
}

// Same as plm_buffer_read_vlc_lut for a table built with plm_vlc_lut_build_n(..., lut_bits).
static inline int16_t plm_buffer_read_vlc_lut_n(plm_buffer_t *self, const plm_vlc_t *table, const plm_vlc_lut_t *lut, int lut_bits) {
	uint32_t bit_index = (uint32_t)self->bit_index;
	const uint8_t *s = plm_buffer_ptr_from_read(self, (size_t)(bit_index >> 3));
	uint32_t bits = plm_buffer_load_u32be(s) << (bit_index & 7);
	plm_vlc_lut_t e = lut[bits >> (32 - lut_bits)];
	if (e.leaf) {
		self->bit_index = bit_index + e.len;
		return e.value;
	}
	return plm_buffer_read_vlc_tail(self, table, e.value, bit_index + (uint32_t)lut_bits);
}

// Codes longer than a lookup table: finish the bit-serial tree walk from 'index', starting at
// bit_index. Out of line and cold so the inlined table readers stay small (SH-4 I-cache is 8 KB).
__attribute__((noinline, cold)) static int16_t plm_buffer_read_vlc_tail(plm_buffer_t *self, const plm_vlc_t *table,
		int index, uint32_t bit_index) {
	const uint8_t *s = plm_buffer_ptr_from_read(self, (size_t)(bit_index >> 3));
	uint32_t bits = plm_buffer_load_u32be(s) << (bit_index & 7);
	plm_vlc_t state = {(int16_t)index, 0};
	while (TRUE) {
		state = table[state.index + (int)(bits >> 31)];
		bit_index++;
		if (state.index <= 0) {
			break;
		}
		if ((bit_index & 7) == 0) {
			s = plm_buffer_ptr_from_read(self, (size_t)(bit_index >> 3));
			bits = plm_buffer_load_u32be(s);
		}
		else {
			bits <<= 1;
		}
	}
	self->bit_index = bit_index;
	return state.value;
}

static inline int16_t plm_buffer_read_vlc_lut(plm_buffer_t *self, const plm_vlc_t *table, const plm_vlc_lut_t *lut) {
	uint32_t bit_index = (uint32_t)self->bit_index;
	const uint8_t *s = plm_buffer_ptr_from_read(self, (size_t)(bit_index >> 3));
	uint32_t bits = plm_buffer_load_u32be(s) << (bit_index & 7);   // >= 25 valid bits
	plm_vlc_lut_t e = lut[bits >> (32 - PLM_VLC_LUT_BITS)];
	if (e.leaf) {
		self->bit_index = bit_index + e.len;
		return e.value;
	}
	// Long code: continue the tree walk from the node reached after PLM_VLC_LUT_BITS bits.
	return plm_buffer_read_vlc_tail(self, table, e.value, bit_index + PLM_VLC_LUT_BITS);
}



// ----------------------------------------------------------------------------
// plm_demux implementation

static const int PLM_START_PACK = 0xBA;
static const int PLM_START_END = 0xB9;
static const int PLM_START_SYSTEM = 0xBB;

struct plm_demux_t {
	plm_buffer_t *buffer;
	int destroy_buffer_when_done;
	double system_clock_ref;

	size_t last_file_size;
	double last_decoded_pts;
	double start_time;
	double duration;

	int start_code;
	int has_pack_header;
	int has_system_header;
	int has_headers;

	int num_audio_streams;
	int num_video_streams;
	plm_packet_t current_packet;
	plm_packet_t next_packet;
};


void plm_demux_buffer_seek(plm_demux_t *self, size_t pos);
double plm_demux_decode_time(plm_demux_t *self);
plm_packet_t *plm_demux_decode_packet(plm_demux_t *self, int type);
plm_packet_t *plm_demux_get_packet(plm_demux_t *self);

plm_demux_t *plm_demux_create(plm_buffer_t *buffer, int destroy_when_done) {
	plm_demux_t *self = (plm_demux_t *)PLM_MALLOC(sizeof(plm_demux_t));
	if(!self) {
		fprintf(stderr, "Out of memory for self. [plm_demux_create]\n");
		return NULL;
	}
	PLM_MEMZERO(self, sizeof(plm_demux_t));

	self->buffer = buffer;
	self->destroy_buffer_when_done = destroy_when_done;

	self->start_time = PLM_PACKET_INVALID_TS;
	self->duration = PLM_PACKET_INVALID_TS;
	self->start_code = -1;

	plm_demux_has_headers(self);
	return self;
}

void plm_demux_destroy(plm_demux_t *self) {
	if(!self)
		return;

	if (self->destroy_buffer_when_done) {
		plm_buffer_destroy(self->buffer);
	}
	PLM_FREE(self);
}

int plm_demux_has_headers(plm_demux_t *self) {
	if (self->has_headers) {
		return TRUE;
	}

	// Decode pack header
	if (!self->has_pack_header) {
		if (
			self->start_code != PLM_START_PACK &&
			plm_buffer_find_start_code(self->buffer, PLM_START_PACK) == -1
		) {
			return FALSE;
		}

		self->start_code = PLM_START_PACK;
		if (!plm_buffer_has(self->buffer, 64)) {
			return FALSE;
		}
		self->start_code = -1;

		if (plm_buffer_read(self->buffer, 4) != 0x02) {
			return FALSE;
		}

		self->system_clock_ref = plm_demux_decode_time(self);
		plm_buffer_skip(self->buffer, 1);
		plm_buffer_skip(self->buffer, 22); // mux_rate * 50
		plm_buffer_skip(self->buffer, 1);

		self->has_pack_header = TRUE;
	}

	// Decode system header
	if (!self->has_system_header) {
		if (
			self->start_code != PLM_START_SYSTEM &&
			plm_buffer_find_start_code(self->buffer, PLM_START_SYSTEM) == -1
		) {
			return FALSE;
		}

		self->start_code = PLM_START_SYSTEM;
		if (!plm_buffer_has(self->buffer, 56)) {
			return FALSE;
		}
		self->start_code = -1;

		plm_buffer_skip(self->buffer, 16); // header_length
		plm_buffer_skip(self->buffer, 24); // rate bound
		self->num_audio_streams = plm_buffer_read(self->buffer, 6);
		plm_buffer_skip(self->buffer, 5); // misc flags
		self->num_video_streams = plm_buffer_read(self->buffer, 5);

		self->has_system_header = TRUE;
	}

	self->has_headers = TRUE;
	return TRUE;
}

int plm_demux_probe(plm_demux_t *self, size_t probesize) {
	int previous_pos = plm_buffer_tell(self->buffer);

	int video_stream = FALSE;
	int audio_streams[4] = {FALSE, FALSE, FALSE, FALSE};
	do {
		self->start_code = plm_buffer_next_start_code(self->buffer);
		if (self->start_code == PLM_DEMUX_PACKET_VIDEO_1) {
			video_stream = TRUE;
		}
		else if (
			self->start_code >= PLM_DEMUX_PACKET_AUDIO_1 &&
			self->start_code <= PLM_DEMUX_PACKET_AUDIO_4
		) {
			audio_streams[self->start_code - PLM_DEMUX_PACKET_AUDIO_1] = TRUE;
		}
	} while (
		self->start_code != -1 &&
		plm_buffer_tell(self->buffer) - previous_pos < probesize
	);

	self->num_video_streams = video_stream ? 1 : 0;
	self->num_audio_streams = 0;
	for (int i = 0; i < 4; i++) {
		if (audio_streams[i]) {
			self->num_audio_streams++;
		}
	}

	plm_demux_buffer_seek(self, previous_pos);
	return (self->num_video_streams || self->num_audio_streams);
}

int plm_demux_get_num_video_streams(plm_demux_t *self) {
	return plm_demux_has_headers(self)
		? self->num_video_streams
		: 0;
}

int plm_demux_get_num_audio_streams(plm_demux_t *self) {
	return plm_demux_has_headers(self)
		? self->num_audio_streams
		: 0;
}

void plm_demux_rewind(plm_demux_t *self) {
	plm_buffer_rewind(self->buffer);
	self->current_packet.length = 0;
	self->next_packet.length = 0;
	self->start_code = -1;
}

int plm_demux_has_ended(plm_demux_t *self) {
	return plm_buffer_has_ended(self->buffer);
}

void plm_demux_buffer_seek(plm_demux_t *self, size_t pos) {
	plm_buffer_seek(self->buffer, pos);
	self->current_packet.length = 0;
	self->next_packet.length = 0;
	self->start_code = -1;
}

double plm_demux_get_start_time(plm_demux_t *self, int type) {
	if (self->start_time != PLM_PACKET_INVALID_TS) {
		return self->start_time;
	}

	int previous_pos = plm_buffer_tell(self->buffer);
	int previous_start_code = self->start_code;

	// Find first video PTS
	plm_demux_rewind(self);
	do {
		plm_packet_t *packet = plm_demux_decode(self);
		if (!packet) {
			break;
		}
		if (packet->type == type) {
			self->start_time = packet->pts;
		}
	} while (self->start_time == PLM_PACKET_INVALID_TS);

	plm_demux_buffer_seek(self, previous_pos);
	self->start_code = previous_start_code;
	return self->start_time;
}

double plm_demux_get_duration(plm_demux_t *self, int type) {
	size_t file_size = plm_buffer_get_size(self->buffer);

	if (
		self->duration != PLM_PACKET_INVALID_TS &&
		self->last_file_size == file_size
	) {
		return self->duration;
	}

	size_t previous_pos = plm_buffer_tell(self->buffer);
	int previous_start_code = self->start_code;

	// Find last video PTS. Start searching 64kb from the end and go further
	// back if needed.
	long start_range = 64 * 1024;
	long max_range = 4096 * 1024;
	for (long range = start_range; range <= max_range; range *= 2) {
		long seek_pos = file_size - range;
		if (seek_pos < 0) {
			seek_pos = 0;
			range = max_range; // Make sure to bail after this round
		}
		plm_demux_buffer_seek(self, seek_pos);
		self->current_packet.length = 0;

		double last_pts = PLM_PACKET_INVALID_TS;
		plm_packet_t *packet = NULL;
		while ((packet = plm_demux_decode(self))) {
			if (packet->pts != PLM_PACKET_INVALID_TS && packet->type == type) {
				last_pts = packet->pts;
			}
		}
		if (last_pts != PLM_PACKET_INVALID_TS) {
			self->duration = last_pts - plm_demux_get_start_time(self, type);
			break;
		}
	}

	plm_demux_buffer_seek(self, previous_pos);
	self->start_code = previous_start_code;
	self->last_file_size = file_size;
	return self->duration;
}

plm_packet_t *plm_demux_seek(plm_demux_t *self, double seek_time, int type, int force_intra) {
	if (!plm_demux_has_headers(self)) {
		return NULL;
	}

	// Using the current time, current byte position and the average bytes per
	// second for this file, try to jump to a byte position that hopefully has
	// packets containing timestamps within one second before to the desired
	// seek_time.

	// If we hit close to the seek_time scan through all packets to find the
	// last one (just before the seek_time) containing an intra frame.
	// Otherwise we should at least be closer than before. Calculate the bytes
	// per second for the jumped range and jump again.

	// The number of retries here is hard-limited to a generous amount. Usually
	// the correct range is found after 1--5 jumps, even for files with very
	// variable bitrates. If significantly more jumps are needed, there's
	// probably something wrong with the file and we just avoid getting into an
	// infinite loop. 32 retries should be enough for anybody.

	double duration = plm_demux_get_duration(self, type);
	long file_size = plm_buffer_get_size(self->buffer);
	long byterate = file_size / duration;

	double cur_time = self->last_decoded_pts;
	double scan_span = 1;

	if (seek_time > duration) {
		seek_time = duration;
	}
	else if (seek_time < 0) {
		seek_time = 0;
	}
	seek_time += self->start_time;

	for (int retry = 0; retry < 32; retry++) {
		int found_packet_with_pts = FALSE;
		int found_packet_in_range = FALSE;
		long last_valid_packet_start = -1;
		double first_packet_time = PLM_PACKET_INVALID_TS;

		long cur_pos = plm_buffer_tell(self->buffer);

		// Estimate byte offset and jump to it.
		long offset = (seek_time - cur_time - scan_span) * byterate;
		long seek_pos = cur_pos + offset;
		if (seek_pos < 0) {
			seek_pos = 0;
		}
		else if (seek_pos > file_size - 256) {
			seek_pos = file_size - 256;
		}

		plm_demux_buffer_seek(self, seek_pos);

		// Scan through all packets up to the seek_time to find the last packet
		// containing an intra frame.
		while (plm_buffer_find_start_code(self->buffer, type) != -1) {
			long packet_start = plm_buffer_tell(self->buffer);
			plm_packet_t *packet = plm_demux_decode_packet(self, type);

			// Skip packet if it has no PTS
			if (!packet || packet->pts == PLM_PACKET_INVALID_TS) {
				continue;
			}

			// Bail scanning through packets if we hit one that is outside
			// seek_time - scan_span.
			// We also adjust the cur_time and byterate values here so the next
			// iteration can be a bit more precise.
			if (packet->pts > seek_time || packet->pts < seek_time - scan_span) {
				found_packet_with_pts = TRUE;
				byterate = (seek_pos - cur_pos) / (packet->pts - cur_time);
				cur_time = packet->pts;
				break;
			}

			// If we are still here, it means this packet is in close range to
			// the seek_time. If this is the first packet for this jump position
			// record the PTS. If we later have to back off, when there was no
			// intra frame in this range, we can lower the seek_time to not scan
			// this range again.
			if (!found_packet_in_range) {
				found_packet_in_range = TRUE;
				first_packet_time = packet->pts;
			}

			// Check if this is an intra frame packet. If so, record the buffer
			// position of the start of this packet. We want to jump back to it
			// later, when we know it's the last intra frame before desired
			// seek time.
			//
			if (force_intra) {
				const uint8_t *a = packet->data0;
				size_t a_len = packet->len0;

				const uint8_t *b = packet->data1;
				size_t b_len = packet->len1;

				// total bytes == a_len + b_len
				size_t total = packet->length;

				// --- 1) Scan windows fully inside span 0 ---
				for (size_t i = 0; i + 6 <= a_len; i++) {
					if (a[i] == 0x00 && a[i + 1] == 0x00 && a[i + 2] == 0x01 && a[i + 3] == 0x00) {
						if ((a[i + 5] & 0x38) == 0x08) {
							last_valid_packet_start = packet_start;
						}
						goto found_picture;
					}
				}

				// --- 2) Scan windows that cross the span boundary (at most 5 starts) ---
				if (b_len) {
					size_t start = (a_len >= 5) ? (a_len - 5) : 0;
					for (size_t i = start; i < a_len; i++) {
						if (i + 6 > total) break; // not enough bytes left in packet

						// Pull the 6-byte window across the boundary (small loop, not hot)
						uint8_t w0 = a[i + 0];
						uint8_t w1 = (i + 1 < a_len) ? a[i + 1] : b[(i + 1) - a_len];
						uint8_t w2 = (i + 2 < a_len) ? a[i + 2] : b[(i + 2) - a_len];
						uint8_t w3 = (i + 3 < a_len) ? a[i + 3] : b[(i + 3) - a_len];

						if (w0 == 0x00 && w1 == 0x00 && w2 == 0x01 && w3 == 0x00) {
							uint8_t w5 = (i + 5 < a_len) ? a[i + 5] : b[(i + 5) - a_len];
							if ((w5 & 0x38) == 0x08) {
								last_valid_packet_start = packet_start;
							}
							goto found_picture;
						}
					}
				}

				// --- 3) Scan windows fully inside span 1 ---
				if (b_len) {
					for (size_t j = 0; j + 6 <= b_len; j++) {
						if (b[j] == 0x00 && b[j + 1] == 0x00 && b[j + 2] == 0x01 && b[j + 3] == 0x00) {
							if ((b[j + 5] & 0x38) == 0x08) {
								last_valid_packet_start = packet_start;
							}
							goto found_picture;
						}
					}
				}

			found_picture:
				;
			}

			// If we don't want intra frames, just use the last PTS found.
			else {
				last_valid_packet_start = packet_start;
			}
		}

		// If there was at least one intra frame in the range scanned above,
		// our search is over. Jump back to the packet and decode it again.
		if (last_valid_packet_start != -1) {
			plm_demux_buffer_seek(self, last_valid_packet_start);
			return plm_demux_decode_packet(self, type);
		}

		// If we hit the right range, but still found no intra frame, we have
		// to increases the scan_span. This is done exponentially to also handle
		// video files with very few intra frames.
		else if (found_packet_in_range) {
			scan_span *= 2;
			seek_time = first_packet_time;
		}

		// If we didn't find any packet with a PTS, it probably means we reached
		// the end of the file. Estimate byterate and cur_time accordingly.
		else if (!found_packet_with_pts) {
			byterate = (seek_pos - cur_pos) / (duration - cur_time);
			cur_time = duration;
		}
	}

	return NULL;
}

plm_packet_t *plm_demux_decode(plm_demux_t *self) {
	if (!plm_demux_has_headers(self)) {
		return NULL;
	}

	if (self->current_packet.length) {
		size_t bits_till_next_packet = self->current_packet.length << 3;
		if (!plm_buffer_has(self->buffer, bits_till_next_packet)) {
			return NULL;
		}
		plm_buffer_skip(self->buffer, bits_till_next_packet);
		self->current_packet.length = 0;
	}

	// Pending packet waiting for data?
	if (self->next_packet.length) {
		return plm_demux_get_packet(self);
	}

	// Pending packet waiting for header?
	if (self->start_code != -1) {
		return plm_demux_decode_packet(self, self->start_code);
	}

	do {
		self->start_code = plm_buffer_next_start_code(self->buffer);
		if (
			self->start_code == PLM_DEMUX_PACKET_VIDEO_1 ||
			self->start_code == PLM_DEMUX_PACKET_PRIVATE || (
				self->start_code >= PLM_DEMUX_PACKET_AUDIO_1 &&
				self->start_code <= PLM_DEMUX_PACKET_AUDIO_4
			)
		) {
			return plm_demux_decode_packet(self, self->start_code);
		}
	} while (self->start_code != -1);

	return NULL;
}

double plm_demux_decode_time(plm_demux_t *self) {
	int64_t clock = plm_buffer_read(self->buffer, 3) << 30;
	plm_buffer_skip(self->buffer, 1);
	clock |= plm_buffer_read(self->buffer, 15) << 15;
	plm_buffer_skip(self->buffer, 1);
	clock |= plm_buffer_read(self->buffer, 15);
	plm_buffer_skip(self->buffer, 1);
	return (double)clock / 90000.0;
}

plm_packet_t *plm_demux_decode_packet(plm_demux_t *self, int type) {
	if (!plm_buffer_has(self->buffer, 16 << 3)) {
		return NULL;
	}

	self->start_code = -1;

	self->next_packet.type = type;
	self->next_packet.length = plm_buffer_read(self->buffer, 16);
	self->next_packet.length -= plm_buffer_skip_bytes(self->buffer, 0xff); // stuffing

	// skip P-STD
	if (plm_buffer_read(self->buffer, 2) == 0x01) {
		plm_buffer_skip(self->buffer, 16);
		self->next_packet.length -= 2;
	}

	int pts_dts_marker = plm_buffer_read(self->buffer, 2);
	if (pts_dts_marker == 0x03) {
		self->next_packet.pts = plm_demux_decode_time(self);
		self->last_decoded_pts = self->next_packet.pts;
		plm_buffer_skip(self->buffer, 40); // skip dts
		self->next_packet.length -= 10;
	}
	else if (pts_dts_marker == 0x02) {
		self->next_packet.pts = plm_demux_decode_time(self);
		self->last_decoded_pts = self->next_packet.pts;
		self->next_packet.length -= 5;
	}
	else if (pts_dts_marker == 0x00) {
		self->next_packet.pts = PLM_PACKET_INVALID_TS;
		plm_buffer_skip(self->buffer, 4);
		self->next_packet.length -= 1;
	}
	else {
		return NULL; // invalid
	}

	return plm_demux_get_packet(self);
}

plm_packet_t *plm_demux_get_packet(plm_demux_t *self) {
    plm_buffer_t *b = self->buffer;
    size_t need_bytes = self->next_packet.length;

    if (!plm_buffer_has(b, need_bytes << 3)) {
        return NULL;
    }

    size_t byte_off = (size_t)(b->bit_index >> 3);
    uint8_t *p = plm_buffer_ptr_from_read(b, byte_off);

    size_t pos = (size_t)(p - b->bytes);
    size_t contig = plm_buffer_bytes_until_wrap(b, pos);

    self->current_packet.type = self->next_packet.type;
    self->current_packet.pts = self->next_packet.pts;
    self->current_packet.length = need_bytes;

    if (need_bytes <= contig) {
        // Not wrapped: single span
        self->current_packet.data0 = p;
        self->current_packet.len0  = need_bytes;
        self->current_packet.data1 = NULL;
        self->current_packet.len1  = 0;
    } else {
        // Wrapped: two spans
        self->current_packet.data0 = p;
        self->current_packet.len0  = contig;
        self->current_packet.data1 = b->bytes;              // wraps to start
        self->current_packet.len1  = need_bytes - contig;
    }

    self->next_packet.length = 0;
    return &self->current_packet;
}



// -----------------------------------------------------------------------------
// plm_video implementation

// Inspired by Java MPEG-1 Video Decoder and Player by Zoltan Korandi
// https://sourceforge.net/projects/javampeg1video/

static const int PLM_VIDEO_PICTURE_TYPE_INTRA = 1;
static const int PLM_VIDEO_PICTURE_TYPE_PREDICTIVE = 2;
static const int PLM_VIDEO_PICTURE_TYPE_B = 3;

static const int PLM_START_SEQUENCE = 0xB3;
static const int PLM_START_SLICE_FIRST = 0x01;
static const int PLM_START_SLICE_LAST = 0xAF;
static const int PLM_START_PICTURE = 0x00;
static const int PLM_START_EXTENSION = 0xB5;
static const int PLM_START_USER_DATA = 0xB2;

#define PLM_START_IS_SLICE(c) \
	(c >= PLM_START_SLICE_FIRST && c <= PLM_START_SLICE_LAST)

static const float PLM_VIDEO_PIXEL_ASPECT_RATIO[] = {
	1.0000, // square pixels
	0.6735, // 3:4?
	0.7031, // MPEG-1 / MPEG-2 video encoding divergence?
	0.7615, 0.8055, 0.8437, 0.8935, 0.9157, 0.9815,
	1.0255, 1.0695, 1.0950, 1.1575, 1.2051,
};

 __attribute__((aligned(32))) static const float PLM_VIDEO_PICTURE_RATE[] = {
	0.000, 23.976, 24.000, 25.000, 29.970, 30.000, 50.000, 59.940,
	60.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000
};

 __attribute__((aligned(32))) static const uint8_t PLM_VIDEO_ZIG_ZAG[] = {
	 0,  1,  8, 16,  9,  2,  3, 10,
	17, 24, 32, 25, 18, 11,  4,  5,
	12, 19, 26, 33, 40, 48, 41, 34,
	27, 20, 13,  6,  7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36,
	29, 22, 15, 23, 30, 37, 44, 51,
	58, 59, 52, 45, 38, 31, 39, 46,
	53, 60, 61, 54, 47, 55, 62, 63
};

 __attribute__((aligned(32))) static const uint8_t PLM_VIDEO_INTRA_QUANT_MATRIX[] = {
	 8, 16, 19, 22, 26, 27, 29, 34,
	16, 16, 22, 24, 27, 29, 34, 37,
	19, 22, 26, 27, 29, 34, 34, 38,
	22, 22, 26, 27, 29, 34, 37, 40,
	22, 26, 27, 29, 32, 35, 40, 48,
	26, 27, 29, 32, 35, 40, 48, 58,
	26, 27, 29, 34, 38, 46, 56, 69,
	27, 29, 35, 38, 46, 56, 69, 83
};

 __attribute__((aligned(32))) static const uint8_t PLM_VIDEO_NON_INTRA_QUANT_MATRIX[] = {
	16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16
};

 __attribute__((aligned(32))) static const plm_vlc_t PLM_VIDEO_MACROBLOCK_ADDRESS_INCREMENT[] = {
	{  1 << 1,    0}, {       0,    1},  //   0: x
	{  2 << 1,    0}, {  3 << 1,    0},  //   1: 0x
	{  4 << 1,    0}, {  5 << 1,    0},  //   2: 00x
	{       0,    3}, {       0,    2},  //   3: 01x
	{  6 << 1,    0}, {  7 << 1,    0},  //   4: 000x
	{       0,    5}, {       0,    4},  //   5: 001x
	{  8 << 1,    0}, {  9 << 1,    0},  //   6: 0000x
	{       0,    7}, {       0,    6},  //   7: 0001x
	{ 10 << 1,    0}, { 11 << 1,    0},  //   8: 0000 0x
	{ 12 << 1,    0}, { 13 << 1,    0},  //   9: 0000 1x
	{ 14 << 1,    0}, { 15 << 1,    0},  //  10: 0000 00x
	{ 16 << 1,    0}, { 17 << 1,    0},  //  11: 0000 01x
	{ 18 << 1,    0}, { 19 << 1,    0},  //  12: 0000 10x
	{       0,    9}, {       0,    8},  //  13: 0000 11x
	{      -1,    0}, { 20 << 1,    0},  //  14: 0000 000x
	{      -1,    0}, { 21 << 1,    0},  //  15: 0000 001x
	{ 22 << 1,    0}, { 23 << 1,    0},  //  16: 0000 010x
	{       0,   15}, {       0,   14},  //  17: 0000 011x
	{       0,   13}, {       0,   12},  //  18: 0000 100x
	{       0,   11}, {       0,   10},  //  19: 0000 101x
	{ 24 << 1,    0}, { 25 << 1,    0},  //  20: 0000 0001x
	{ 26 << 1,    0}, { 27 << 1,    0},  //  21: 0000 0011x
	{ 28 << 1,    0}, { 29 << 1,    0},  //  22: 0000 0100x
	{ 30 << 1,    0}, { 31 << 1,    0},  //  23: 0000 0101x
	{ 32 << 1,    0}, {      -1,    0},  //  24: 0000 0001 0x
	{      -1,    0}, { 33 << 1,    0},  //  25: 0000 0001 1x
	{ 34 << 1,    0}, { 35 << 1,    0},  //  26: 0000 0011 0x
	{ 36 << 1,    0}, { 37 << 1,    0},  //  27: 0000 0011 1x
	{ 38 << 1,    0}, { 39 << 1,    0},  //  28: 0000 0100 0x
	{       0,   21}, {       0,   20},  //  29: 0000 0100 1x
	{       0,   19}, {       0,   18},  //  30: 0000 0101 0x
	{       0,   17}, {       0,   16},  //  31: 0000 0101 1x
	{       0,   35}, {      -1,    0},  //  32: 0000 0001 00x
	{      -1,    0}, {       0,   34},  //  33: 0000 0001 11x
	{       0,   33}, {       0,   32},  //  34: 0000 0011 00x
	{       0,   31}, {       0,   30},  //  35: 0000 0011 01x
	{       0,   29}, {       0,   28},  //  36: 0000 0011 10x
	{       0,   27}, {       0,   26},  //  37: 0000 0011 11x
	{       0,   25}, {       0,   24},  //  38: 0000 0100 00x
	{       0,   23}, {       0,   22},  //  39: 0000 0100 01x
};

 __attribute__((aligned(32))) static const plm_vlc_t PLM_VIDEO_MACROBLOCK_TYPE_INTRA[] = {
	{  1 << 1,    0}, {       0,  0x01},  //   0: x
	{      -1,    0}, {       0,  0x11},  //   1: 0x
};

 __attribute__((aligned(32))) static const plm_vlc_t PLM_VIDEO_MACROBLOCK_TYPE_PREDICTIVE[] = {
	{  1 << 1,    0}, {       0, 0x0a},  //   0: x
	{  2 << 1,    0}, {       0, 0x02},  //   1: 0x
	{  3 << 1,    0}, {       0, 0x08},  //   2: 00x
	{  4 << 1,    0}, {  5 << 1,    0},  //   3: 000x
	{  6 << 1,    0}, {       0, 0x12},  //   4: 0000x
	{       0, 0x1a}, {       0, 0x01},  //   5: 0001x
	{      -1,    0}, {       0, 0x11},  //   6: 0000 0x
};

 __attribute__((aligned(32))) static const plm_vlc_t PLM_VIDEO_MACROBLOCK_TYPE_B[] = {
	{  1 << 1,    0}, {  2 << 1,    0},  //   0: x
	{  3 << 1,    0}, {  4 << 1,    0},  //   1: 0x
	{       0, 0x0c}, {       0, 0x0e},  //   2: 1x
	{  5 << 1,    0}, {  6 << 1,    0},  //   3: 00x
	{       0, 0x04}, {       0, 0x06},  //   4: 01x
	{  7 << 1,    0}, {  8 << 1,    0},  //   5: 000x
	{       0, 0x08}, {       0, 0x0a},  //   6: 001x
	{  9 << 1,    0}, { 10 << 1,    0},  //   7: 0000x
	{       0, 0x1e}, {       0, 0x01},  //   8: 0001x
	{      -1,    0}, {       0, 0x11},  //   9: 0000 0x
	{       0, 0x16}, {       0, 0x1a},  //  10: 0000 1x
};

 __attribute__((aligned(32))) static const plm_vlc_t *PLM_VIDEO_MACROBLOCK_TYPE[] = {
	NULL,
	PLM_VIDEO_MACROBLOCK_TYPE_INTRA,
	PLM_VIDEO_MACROBLOCK_TYPE_PREDICTIVE,
	PLM_VIDEO_MACROBLOCK_TYPE_B
};

 __attribute__((aligned(32))) static const plm_vlc_t PLM_VIDEO_CODE_BLOCK_PATTERN[] = {
	{  1 << 1,    0}, {  2 << 1,    0},  //   0: x
	{  3 << 1,    0}, {  4 << 1,    0},  //   1: 0x
	{  5 << 1,    0}, {  6 << 1,    0},  //   2: 1x
	{  7 << 1,    0}, {  8 << 1,    0},  //   3: 00x
	{  9 << 1,    0}, { 10 << 1,    0},  //   4: 01x
	{ 11 << 1,    0}, { 12 << 1,    0},  //   5: 10x
	{ 13 << 1,    0}, {       0,   60},  //   6: 11x
	{ 14 << 1,    0}, { 15 << 1,    0},  //   7: 000x
	{ 16 << 1,    0}, { 17 << 1,    0},  //   8: 001x
	{ 18 << 1,    0}, { 19 << 1,    0},  //   9: 010x
	{ 20 << 1,    0}, { 21 << 1,    0},  //  10: 011x
	{ 22 << 1,    0}, { 23 << 1,    0},  //  11: 100x
	{       0,   32}, {       0,   16},  //  12: 101x
	{       0,    8}, {       0,    4},  //  13: 110x
	{ 24 << 1,    0}, { 25 << 1,    0},  //  14: 0000x
	{ 26 << 1,    0}, { 27 << 1,    0},  //  15: 0001x
	{ 28 << 1,    0}, { 29 << 1,    0},  //  16: 0010x
	{ 30 << 1,    0}, { 31 << 1,    0},  //  17: 0011x
	{       0,   62}, {       0,    2},  //  18: 0100x
	{       0,   61}, {       0,    1},  //  19: 0101x
	{       0,   56}, {       0,   52},  //  20: 0110x
	{       0,   44}, {       0,   28},  //  21: 0111x
	{       0,   40}, {       0,   20},  //  22: 1000x
	{       0,   48}, {       0,   12},  //  23: 1001x
	{ 32 << 1,    0}, { 33 << 1,    0},  //  24: 0000 0x
	{ 34 << 1,    0}, { 35 << 1,    0},  //  25: 0000 1x
	{ 36 << 1,    0}, { 37 << 1,    0},  //  26: 0001 0x
	{ 38 << 1,    0}, { 39 << 1,    0},  //  27: 0001 1x
	{ 40 << 1,    0}, { 41 << 1,    0},  //  28: 0010 0x
	{ 42 << 1,    0}, { 43 << 1,    0},  //  29: 0010 1x
	{       0,   63}, {       0,    3},  //  30: 0011 0x
	{       0,   36}, {       0,   24},  //  31: 0011 1x
	{ 44 << 1,    0}, { 45 << 1,    0},  //  32: 0000 00x
	{ 46 << 1,    0}, { 47 << 1,    0},  //  33: 0000 01x
	{ 48 << 1,    0}, { 49 << 1,    0},  //  34: 0000 10x
	{ 50 << 1,    0}, { 51 << 1,    0},  //  35: 0000 11x
	{ 52 << 1,    0}, { 53 << 1,    0},  //  36: 0001 00x
	{ 54 << 1,    0}, { 55 << 1,    0},  //  37: 0001 01x
	{ 56 << 1,    0}, { 57 << 1,    0},  //  38: 0001 10x
	{ 58 << 1,    0}, { 59 << 1,    0},  //  39: 0001 11x
	{       0,   34}, {       0,   18},  //  40: 0010 00x
	{       0,   10}, {       0,    6},  //  41: 0010 01x
	{       0,   33}, {       0,   17},  //  42: 0010 10x
	{       0,    9}, {       0,    5},  //  43: 0010 11x
	{      -1,    0}, { 60 << 1,    0},  //  44: 0000 000x
	{ 61 << 1,    0}, { 62 << 1,    0},  //  45: 0000 001x
	{       0,   58}, {       0,   54},  //  46: 0000 010x
	{       0,   46}, {       0,   30},  //  47: 0000 011x
	{       0,   57}, {       0,   53},  //  48: 0000 100x
	{       0,   45}, {       0,   29},  //  49: 0000 101x
	{       0,   38}, {       0,   26},  //  50: 0000 110x
	{       0,   37}, {       0,   25},  //  51: 0000 111x
	{       0,   43}, {       0,   23},  //  52: 0001 000x
	{       0,   51}, {       0,   15},  //  53: 0001 001x
	{       0,   42}, {       0,   22},  //  54: 0001 010x
	{       0,   50}, {       0,   14},  //  55: 0001 011x
	{       0,   41}, {       0,   21},  //  56: 0001 100x
	{       0,   49}, {       0,   13},  //  57: 0001 101x
	{       0,   35}, {       0,   19},  //  58: 0001 110x
	{       0,   11}, {       0,    7},  //  59: 0001 111x
	{       0,   39}, {       0,   27},  //  60: 0000 0001x
	{       0,   59}, {       0,   55},  //  61: 0000 0010x
	{       0,   47}, {       0,   31},  //  62: 0000 0011x
};

 __attribute__((aligned(32))) static const plm_vlc_t PLM_VIDEO_MOTION[] = {
	{  1 << 1,    0}, {       0,    0},  //   0: x
	{  2 << 1,    0}, {  3 << 1,    0},  //   1: 0x
	{  4 << 1,    0}, {  5 << 1,    0},  //   2: 00x
	{       0,    1}, {       0,   -1},  //   3: 01x
	{  6 << 1,    0}, {  7 << 1,    0},  //   4: 000x
	{       0,    2}, {       0,   -2},  //   5: 001x
	{  8 << 1,    0}, {  9 << 1,    0},  //   6: 0000x
	{       0,    3}, {       0,   -3},  //   7: 0001x
	{ 10 << 1,    0}, { 11 << 1,    0},  //   8: 0000 0x
	{ 12 << 1,    0}, { 13 << 1,    0},  //   9: 0000 1x
	{      -1,    0}, { 14 << 1,    0},  //  10: 0000 00x
	{ 15 << 1,    0}, { 16 << 1,    0},  //  11: 0000 01x
	{ 17 << 1,    0}, { 18 << 1,    0},  //  12: 0000 10x
	{       0,    4}, {       0,   -4},  //  13: 0000 11x
	{      -1,    0}, { 19 << 1,    0},  //  14: 0000 001x
	{ 20 << 1,    0}, { 21 << 1,    0},  //  15: 0000 010x
	{       0,    7}, {       0,   -7},  //  16: 0000 011x
	{       0,    6}, {       0,   -6},  //  17: 0000 100x
	{       0,    5}, {       0,   -5},  //  18: 0000 101x
	{ 22 << 1,    0}, { 23 << 1,    0},  //  19: 0000 0011x
	{ 24 << 1,    0}, { 25 << 1,    0},  //  20: 0000 0100x
	{ 26 << 1,    0}, { 27 << 1,    0},  //  21: 0000 0101x
	{ 28 << 1,    0}, { 29 << 1,    0},  //  22: 0000 0011 0x
	{ 30 << 1,    0}, { 31 << 1,    0},  //  23: 0000 0011 1x
	{ 32 << 1,    0}, { 33 << 1,    0},  //  24: 0000 0100 0x
	{       0,   10}, {       0,  -10},  //  25: 0000 0100 1x
	{       0,    9}, {       0,   -9},  //  26: 0000 0101 0x
	{       0,    8}, {       0,   -8},  //  27: 0000 0101 1x
	{       0,   16}, {       0,  -16},  //  28: 0000 0011 00x
	{       0,   15}, {       0,  -15},  //  29: 0000 0011 01x
	{       0,   14}, {       0,  -14},  //  30: 0000 0011 10x
	{       0,   13}, {       0,  -13},  //  31: 0000 0011 11x
	{       0,   12}, {       0,  -12},  //  32: 0000 0100 00x
	{       0,   11}, {       0,  -11},  //  33: 0000 0100 01x
};

 __attribute__((aligned(32))) static const plm_vlc_t PLM_VIDEO_DCT_SIZE_LUMINANCE[] = {
	{  1 << 1,    0}, {  2 << 1,    0},  //   0: x
	{       0,    1}, {       0,    2},  //   1: 0x
	{  3 << 1,    0}, {  4 << 1,    0},  //   2: 1x
	{       0,    0}, {       0,    3},  //   3: 10x
	{       0,    4}, {  5 << 1,    0},  //   4: 11x
	{       0,    5}, {  6 << 1,    0},  //   5: 111x
	{       0,    6}, {  7 << 1,    0},  //   6: 1111x
	{       0,    7}, {  8 << 1,    0},  //   7: 1111 1x
	{       0,    8}, {      -1,    0},  //   8: 1111 11x
};

 __attribute__((aligned(32))) static const plm_vlc_t PLM_VIDEO_DCT_SIZE_CHROMINANCE[] = {
	{  1 << 1,    0}, {  2 << 1,    0},  //   0: x
	{       0,    0}, {       0,    1},  //   1: 0x
	{       0,    2}, {  3 << 1,    0},  //   2: 1x
	{       0,    3}, {  4 << 1,    0},  //   3: 11x
	{       0,    4}, {  5 << 1,    0},  //   4: 111x
	{       0,    5}, {  6 << 1,    0},  //   5: 1111x
	{       0,    6}, {  7 << 1,    0},  //   6: 1111 1x
	{       0,    7}, {  8 << 1,    0},  //   7: 1111 11x
	{       0,    8}, {      -1,    0},  //   8: 1111 111x
};

static const plm_vlc_t *PLM_VIDEO_DCT_SIZE[] = {
	PLM_VIDEO_DCT_SIZE_LUMINANCE,
	PLM_VIDEO_DCT_SIZE_CHROMINANCE,
	PLM_VIDEO_DCT_SIZE_CHROMINANCE
};


//  dct_coeff bitmap:
//    0xff00  run
//    0x00ff  level

//  Decoded values are unsigned. Sign bit follows in the stream.

 __attribute__((aligned(32))) static const plm_vlc_uint_t PLM_VIDEO_DCT_COEFF[] = {
	{  1 << 1,        0}, {       0,   0x0001},  //   0: x
	{  2 << 1,        0}, {  3 << 1,        0},  //   1: 0x
	{  4 << 1,        0}, {  5 << 1,        0},  //   2: 00x
	{  6 << 1,        0}, {       0,   0x0101},  //   3: 01x
	{  7 << 1,        0}, {  8 << 1,        0},  //   4: 000x
	{  9 << 1,        0}, { 10 << 1,        0},  //   5: 001x
	{       0,   0x0002}, {       0,   0x0201},  //   6: 010x
	{ 11 << 1,        0}, { 12 << 1,        0},  //   7: 0000x
	{ 13 << 1,        0}, { 14 << 1,        0},  //   8: 0001x
	{ 15 << 1,        0}, {       0,   0x0003},  //   9: 0010x
	{       0,   0x0401}, {       0,   0x0301},  //  10: 0011x
	{ 16 << 1,        0}, {       0,   0xffff},  //  11: 0000 0x
	{ 17 << 1,        0}, { 18 << 1,        0},  //  12: 0000 1x
	{       0,   0x0701}, {       0,   0x0601},  //  13: 0001 0x
	{       0,   0x0102}, {       0,   0x0501},  //  14: 0001 1x
	{ 19 << 1,        0}, { 20 << 1,        0},  //  15: 0010 0x
	{ 21 << 1,        0}, { 22 << 1,        0},  //  16: 0000 00x
	{       0,   0x0202}, {       0,   0x0901},  //  17: 0000 10x
	{       0,   0x0004}, {       0,   0x0801},  //  18: 0000 11x
	{ 23 << 1,        0}, { 24 << 1,        0},  //  19: 0010 00x
	{ 25 << 1,        0}, { 26 << 1,        0},  //  20: 0010 01x
	{ 27 << 1,        0}, { 28 << 1,        0},  //  21: 0000 000x
	{ 29 << 1,        0}, { 30 << 1,        0},  //  22: 0000 001x
	{       0,   0x0d01}, {       0,   0x0006},  //  23: 0010 000x
	{       0,   0x0c01}, {       0,   0x0b01},  //  24: 0010 001x
	{       0,   0x0302}, {       0,   0x0103},  //  25: 0010 010x
	{       0,   0x0005}, {       0,   0x0a01},  //  26: 0010 011x
	{ 31 << 1,        0}, { 32 << 1,        0},  //  27: 0000 0000x
	{ 33 << 1,        0}, { 34 << 1,        0},  //  28: 0000 0001x
	{ 35 << 1,        0}, { 36 << 1,        0},  //  29: 0000 0010x
	{ 37 << 1,        0}, { 38 << 1,        0},  //  30: 0000 0011x
	{ 39 << 1,        0}, { 40 << 1,        0},  //  31: 0000 0000 0x
	{ 41 << 1,        0}, { 42 << 1,        0},  //  32: 0000 0000 1x
	{ 43 << 1,        0}, { 44 << 1,        0},  //  33: 0000 0001 0x
	{ 45 << 1,        0}, { 46 << 1,        0},  //  34: 0000 0001 1x
	{       0,   0x1001}, {       0,   0x0502},  //  35: 0000 0010 0x
	{       0,   0x0007}, {       0,   0x0203},  //  36: 0000 0010 1x
	{       0,   0x0104}, {       0,   0x0f01},  //  37: 0000 0011 0x
	{       0,   0x0e01}, {       0,   0x0402},  //  38: 0000 0011 1x
	{ 47 << 1,        0}, { 48 << 1,        0},  //  39: 0000 0000 00x
	{ 49 << 1,        0}, { 50 << 1,        0},  //  40: 0000 0000 01x
	{ 51 << 1,        0}, { 52 << 1,        0},  //  41: 0000 0000 10x
	{ 53 << 1,        0}, { 54 << 1,        0},  //  42: 0000 0000 11x
	{ 55 << 1,        0}, { 56 << 1,        0},  //  43: 0000 0001 00x
	{ 57 << 1,        0}, { 58 << 1,        0},  //  44: 0000 0001 01x
	{ 59 << 1,        0}, { 60 << 1,        0},  //  45: 0000 0001 10x
	{ 61 << 1,        0}, { 62 << 1,        0},  //  46: 0000 0001 11x
	{      -1,        0}, { 63 << 1,        0},  //  47: 0000 0000 000x
	{ 64 << 1,        0}, { 65 << 1,        0},  //  48: 0000 0000 001x
	{ 66 << 1,        0}, { 67 << 1,        0},  //  49: 0000 0000 010x
	{ 68 << 1,        0}, { 69 << 1,        0},  //  50: 0000 0000 011x
	{ 70 << 1,        0}, { 71 << 1,        0},  //  51: 0000 0000 100x
	{ 72 << 1,        0}, { 73 << 1,        0},  //  52: 0000 0000 101x
	{ 74 << 1,        0}, { 75 << 1,        0},  //  53: 0000 0000 110x
	{ 76 << 1,        0}, { 77 << 1,        0},  //  54: 0000 0000 111x
	{       0,   0x000b}, {       0,   0x0802},  //  55: 0000 0001 000x
	{       0,   0x0403}, {       0,   0x000a},  //  56: 0000 0001 001x
	{       0,   0x0204}, {       0,   0x0702},  //  57: 0000 0001 010x
	{       0,   0x1501}, {       0,   0x1401},  //  58: 0000 0001 011x
	{       0,   0x0009}, {       0,   0x1301},  //  59: 0000 0001 100x
	{       0,   0x1201}, {       0,   0x0105},  //  60: 0000 0001 101x
	{       0,   0x0303}, {       0,   0x0008},  //  61: 0000 0001 110x
	{       0,   0x0602}, {       0,   0x1101},  //  62: 0000 0001 111x
	{ 78 << 1,        0}, { 79 << 1,        0},  //  63: 0000 0000 0001x
	{ 80 << 1,        0}, { 81 << 1,        0},  //  64: 0000 0000 0010x
	{ 82 << 1,        0}, { 83 << 1,        0},  //  65: 0000 0000 0011x
	{ 84 << 1,        0}, { 85 << 1,        0},  //  66: 0000 0000 0100x
	{ 86 << 1,        0}, { 87 << 1,        0},  //  67: 0000 0000 0101x
	{ 88 << 1,        0}, { 89 << 1,        0},  //  68: 0000 0000 0110x
	{ 90 << 1,        0}, { 91 << 1,        0},  //  69: 0000 0000 0111x
	{       0,   0x0a02}, {       0,   0x0902},  //  70: 0000 0000 1000x
	{       0,   0x0503}, {       0,   0x0304},  //  71: 0000 0000 1001x
	{       0,   0x0205}, {       0,   0x0107},  //  72: 0000 0000 1010x
	{       0,   0x0106}, {       0,   0x000f},  //  73: 0000 0000 1011x
	{       0,   0x000e}, {       0,   0x000d},  //  74: 0000 0000 1100x
	{       0,   0x000c}, {       0,   0x1a01},  //  75: 0000 0000 1101x
	{       0,   0x1901}, {       0,   0x1801},  //  76: 0000 0000 1110x
	{       0,   0x1701}, {       0,   0x1601},  //  77: 0000 0000 1111x
	{ 92 << 1,        0}, { 93 << 1,        0},  //  78: 0000 0000 0001 0x
	{ 94 << 1,        0}, { 95 << 1,        0},  //  79: 0000 0000 0001 1x
	{ 96 << 1,        0}, { 97 << 1,        0},  //  80: 0000 0000 0010 0x
	{ 98 << 1,        0}, { 99 << 1,        0},  //  81: 0000 0000 0010 1x
	{100 << 1,        0}, {101 << 1,        0},  //  82: 0000 0000 0011 0x
	{102 << 1,        0}, {103 << 1,        0},  //  83: 0000 0000 0011 1x
	{       0,   0x001f}, {       0,   0x001e},  //  84: 0000 0000 0100 0x
	{       0,   0x001d}, {       0,   0x001c},  //  85: 0000 0000 0100 1x
	{       0,   0x001b}, {       0,   0x001a},  //  86: 0000 0000 0101 0x
	{       0,   0x0019}, {       0,   0x0018},  //  87: 0000 0000 0101 1x
	{       0,   0x0017}, {       0,   0x0016},  //  88: 0000 0000 0110 0x
	{       0,   0x0015}, {       0,   0x0014},  //  89: 0000 0000 0110 1x
	{       0,   0x0013}, {       0,   0x0012},  //  90: 0000 0000 0111 0x
	{       0,   0x0011}, {       0,   0x0010},  //  91: 0000 0000 0111 1x
	{104 << 1,        0}, {105 << 1,        0},  //  92: 0000 0000 0001 00x
	{106 << 1,        0}, {107 << 1,        0},  //  93: 0000 0000 0001 01x
	{108 << 1,        0}, {109 << 1,        0},  //  94: 0000 0000 0001 10x
	{110 << 1,        0}, {111 << 1,        0},  //  95: 0000 0000 0001 11x
	{       0,   0x0028}, {       0,   0x0027},  //  96: 0000 0000 0010 00x
	{       0,   0x0026}, {       0,   0x0025},  //  97: 0000 0000 0010 01x
	{       0,   0x0024}, {       0,   0x0023},  //  98: 0000 0000 0010 10x
	{       0,   0x0022}, {       0,   0x0021},  //  99: 0000 0000 0010 11x
	{       0,   0x0020}, {       0,   0x010e},  // 100: 0000 0000 0011 00x
	{       0,   0x010d}, {       0,   0x010c},  // 101: 0000 0000 0011 01x
	{       0,   0x010b}, {       0,   0x010a},  // 102: 0000 0000 0011 10x
	{       0,   0x0109}, {       0,   0x0108},  // 103: 0000 0000 0011 11x
	{       0,   0x0112}, {       0,   0x0111},  // 104: 0000 0000 0001 000x
	{       0,   0x0110}, {       0,   0x010f},  // 105: 0000 0000 0001 001x
	{       0,   0x0603}, {       0,   0x1002},  // 106: 0000 0000 0001 010x
	{       0,   0x0f02}, {       0,   0x0e02},  // 107: 0000 0000 0001 011x
	{       0,   0x0d02}, {       0,   0x0c02},  // 108: 0000 0000 0001 100x
	{       0,   0x0b02}, {       0,   0x1f01},  // 109: 0000 0000 0001 101x
	{       0,   0x1e01}, {       0,   0x1d01},  // 110: 0000 0000 0001 110x
	{       0,   0x1c01}, {       0,   0x1b01},  // 111: 0000 0000 0001 111x
};

typedef struct {
	int full_px;
	int is_set;
	int r_size;
	int h;
	int v;
} plm_video_motion_t;

typedef struct {
	uint32_t P;              //  0 in/out: absolute bit position (read_byte_pos * 8 + bit_index)
	const uint8_t *base;     //  4 ring buffer
	uint32_t mask;           //  8 capacity - 1
	const void *first;       // 12 FIRST table
	const void *rest;        // 16 REST table
	const void *unused20;    // 20
	const int16_t *qz;       // 24 per coefficient index: quantizer_scale * matrix, block offset (qz_* below)
	int32_t ni;              // 28 0 (intra) or -1
	uint32_t cbp;            // 32 in/out: blocks still to decode, the next one in bit 31
	float *blocks;           // 36 6 x 64 floats
	int *dcp;                // 40 dc predictors [3]
	const void *dclut;       // 44 PLM_VIDEO_DCT_SIZE_LUT
	int32_t b;               // 48 in/out: current block (-1 to start)
	int32_t n;               // 52 in/out: coefficient index when resuming
	int32_t resume;          // 56 1: continue block b's AC tokens at n
	const void *l2;          // 60 second-level table for codes that start 000000
	uint8_t nb[8];           // 64 out
} plm_mbc_t;
struct plm_video_t {
	// --- Hot: accessed for every block (target: 1 cache line) ---
	plm_buffer_t *buffer;
	int quantizer_scale;
	int16_t qm_cached_scale[2];          // quantizer_scale qz_non_intra [0] / qz_intra [1] were built for, -1 = stale
	// Per coefficient index n (zigzag order): [0] quantizer_scale * matrix, [1] byte offset of the
	// coefficient in the float block (4 * PLM_FIDCT_POS), -4 for n >= 64. The token loop walks these
	// with a pointer, so a run that leaves the block lands on an entry marked invalid.
	int16_t qz_intra[128][2];
	int16_t qz_non_intra[128][2];
	int macroblock_intra;
	int dc_predictor[3];
	int macroblock_address;
	int macroblock_type;

	// --- Hot: accessed every macroblock (target: 1 cache line) ---
	int picture_type;
	int mb_row;
	int mb_col;
	int display_stride_mbs;
	int slice_begin;
	int mb_width;
	int mb_size;
	int luma_width;
	int chroma_width;

	// --- Warm: motion vectors + predict_macroblock fields ---
	plm_video_motion_t motion_forward;
	plm_video_motion_t motion_backward;
	int mb_height;

	// --- Frame data (frame_current is hot for display/scatter) ---
	plm_frame_t frame_current;
	plm_frame_t frame_forward;
	plm_frame_t frame_backward;

	// --- Large arrays ---
	int block_data[64] __attribute__((aligned(8)));   // float coefficients in the FTRV layout
	uint8_t intra_quant_matrix[64] __attribute__((aligned(32)));
	uint8_t non_intra_quant_matrix[64] __attribute__((aligned(32)));

	// --- Cold: accessed once per frame or during init only ---
	double framerate;
	double pixel_aspect_ratio;
	double time;
	int frames_decoded;
	int width;
	int height;
	int luma_height;
	int chroma_height;
	int start_code;
	int has_sequence_header;
	int destroy_buffer_when_done;
	uint8_t *frames_data;
	int has_reference_frame;
	int assume_no_b_frames;
	int skip_b_frames;
	int skip_to_intra;   // 2: skip until an I-picture; 1: then skip B-pictures until the next anchor
	int picture_skipped;
	plm_mbc_t mbc;
	float mb_blocks[6][64] __attribute__((aligned(32)));   // coefficients of one macroblock; kept zero between uses
	plm_video_slice_callback slice_callback;   // after the hot data so nothing above moves
	void *slice_callback_user_data;
	int display_stride_exact;
	void *alloc_base;        // what to free: the struct sits at a chosen data-cache offset inside it
};

// DCL Gives 6% speedup...(https://github.com/bitbank2/pl_mpeg/blob/master/pl_mpeg.h)
static const uint8_t the_clamp_table[] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // never goes below -15
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
    0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
    0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,
    0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,
    0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,
    0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f,
    0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x6f,
    0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x7b,0x7c,0x7d,0x7e,0x7f,
    0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x8b,0x8c,0x8d,0x8e,0x8f,
    0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f,
    0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xab,0xac,0xad,0xae,0xaf,
    0xb0,0xb1,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf,
    0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xce,0xcf,
    0xd0,0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf,
    0xe0,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xeb,0xec,0xed,0xee,0xef,
    0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff, // or above 264
};
static const uint8_t *clamp_table = &the_clamp_table[16];

// ---------------------------------------------------------------------------------------------
// Floating-point IDCT (FTRV). Coefficients are stored as floats at PLM_FIDCT_POS(natural index);
// see plm_fidct_sh4 below for the layout and method.
// XMTRX belongs to the application between calls (KOS's mat_load/mat_apply keep their matrix there),
// so plm_video_decode saves it on entry and restores it on exit.
static inline __attribute__((always_inline)) void plm_xmtrx_save(float *m) {
	float *e = m + 16;
	__asm__ volatile("fschg\n\tfmov xd14,@-%0\n\tfmov xd12,@-%0\n\tfmov xd10,@-%0\n\tfmov xd8,@-%0\n\t"
		"fmov xd6,@-%0\n\tfmov xd4,@-%0\n\tfmov xd2,@-%0\n\tfmov xd0,@-%0\n\tfschg\n" : "+r"(e) : : "memory");
}
static inline __attribute__((always_inline)) void plm_xmtrx_restore(const float *m) {
	__asm__ volatile("fschg\n\tfmov @%0+,xd0\n\tfmov @%0+,xd2\n\tfmov @%0+,xd4\n\tfmov @%0+,xd6\n\t"
		"fmov @%0+,xd8\n\tfmov @%0+,xd10\n\tfmov @%0+,xd12\n\tfmov @%0+,xd14\n\tfschg\n" : "+r"(m) : : "memory");
}
typedef float plm_float_alias __attribute__((may_alias));   // block_data is declared int
#define PLM_FIDCT_POS(i) ((((i) >> 3) & 1) * 32 + ((i) & 7) * 4 + ((i) >> 4))


#define PLM_FIDCT_MAGIC_BITS 0x4b400000u

/* XMTRX element XF[c * 4 + r] = M[r][c]. E[k][j] = C(2j)/2 cos((2k+1) 2j pi/16), O[k][j] = 1/2 cos((2k+1)(2j+1) pi/16). */
static void plm_fidct_init(float *mtx) {
	for (int k = 0; k < 4; k++)
		for (int j = 0; j < 4; j++) {
			double ce = (j == 0 ? sqrt(0.5) : 1.0) * 0.5 * cos((2 * k + 1) * (2 * j) * M_PI / 16);
			double co = 0.5 * cos((2 * k + 1) * (2 * j + 1) * M_PI / 16);
			mtx[j * 4 + k] = (float)ce;
			mtx[16 + j * 4 + k] = (float)co;
		}
	mtx[32] = 12582912.0f;
}


void plm_fidct_sh4_v2(float *blk, float *tmp, uint32_t *out, const float *mtx);

/* Float IDCT, in place (out may equal blk). Pass 1: even and odd halves through FTRV (E and O
   matrices), then the column butterflies into tmp (transposed). Pass 2: the same for the rows, plus
   the 12582912.0 bias that leaves each result's integer in the low mantissa bits. Software-pipelined
   with the ornio.nilware.io SH7091 pipeline model: each in-place FTRV pass loads the next vector
   before transforming the current one; the column step loads the next column's e values in its idle
   load/store slots; pass 2 is unrolled (8 rows) and each row loads three of the next row's o values
   into the registers the previous row freed (fr4-fr7, fr9-fr11 rotate). For each butterfly
   a = e + o and b = 2e - a, as before, so results are unchanged. Pass 1 reads 16 bytes past the
   even halves (blk+128..143, inside the block). r0-r7, fr0-fr11 and XMTRX are clobbered; FPSCR.SZ is
   toggled and restored. */
__asm__(
	"\t.section .text.plm_hot,\"ax\"\n"
	"\t.align 5\n"
	"\t.global _plm_fidct_sh4_v2\n"
	"_plm_fidct_sh4_v2:\n\t"
	"mov.l r8,@-r15\n\t"
	"mov.l r9,@-r15\n\t"
	"mov #16,r8\n\t"
	"mov.l r10,@-r15\n\t"
	"mov #32,r9\n\t"
	"mov.l r11,@-r15\n\t"
	"mov #48,r10\n\t"
	"mov.l r12,@-r15\n\t"
	"mov #64,r11\n\t"
	"mov.l r13,@-r15\n\t"
	"mov #80,r12\n\t"
	"mov.l r14,@-r15\n\t"
	"mov #96,r13\n\t"
	"fmov.s fr12,@-r15\n\t"
	"mov #112,r14\n\t"
	"fmov.s fr13,@-r15\n\t"
	"fmov.s fr14,@-r15\n\t"
	"fmov.s fr15,@-r15\n\t"
	"mov.l r7,@-r15\n\t"
	"mov.l r6,@-r15\n\t"
	"mov.l r5,@-r15\n\t"
	"fschg\n\t"
	"fmov @r7+,xd0\n\t"
	"fmov @r7+,xd2\n\t"
	"fmov @r7+,xd4\n\t"
	"fmov @r7+,xd6\n\t"
	"fmov @r7+,xd8\n\t"
	"fmov @r7+,xd10\n\t"
	"fmov @r7+,xd12\n\t"
	"fmov @r7+,xd14\n\t"
	"mov r4,r2\n\t"
	"mov r4,r3\n\t"
	"add #16,r3\n\t"
	"fmov @r2+,dr0\n\t"
	"fmov @r2+,dr2\n\t"
	"fmov @r2+,dr4\n\t"
	"fmov @r2+,dr6\n\t"
	"ftrv xmtrx,fv0\n\t"
	"fmov @r2+,dr8\n\t"
	"fmov @r2+,dr10\n\t"
	"ftrv xmtrx,fv4\n\t"
	"fmov @r2+,dr12\n\t"
	"fmov @r2+,dr14\n\t"
	"ftrv xmtrx,fv8\n\t"
	"fmov dr2,@-r3\n\t"
	"fmov dr0,@-r3\n\t"
	"add #32,r3\n\t"
	"fmov @r2+,dr0\n\t"
	"fmov @r2+,dr2\n\t"
	"ftrv xmtrx,fv12\n\t"
	"fmov dr6,@-r3\n\t"
	"fmov dr4,@-r3\n\t"
	"add #32,r3\n\t"
	"fmov @r2+,dr4\n\t"
	"fmov @r2+,dr6\n\t"
	"ftrv xmtrx,fv0\n\t"
	"fmov dr10,@-r3\n\t"
	"fmov dr8,@-r3\n\t"
	"add #32,r3\n\t"
	"fmov @r2+,dr8\n\t"
	"fmov @r2+,dr10\n\t"
	"ftrv xmtrx,fv4\n\t"
	"fmov dr14,@-r3\n\t"
	"fmov dr12,@-r3\n\t"
	"add #32,r3\n\t"
	"fmov @r2+,dr12\n\t"
	"fmov @r2+,dr14\n\t"
	"ftrv xmtrx,fv8\n\t"
	"fmov dr2,@-r3\n\t"
	"fmov dr0,@-r3\n\t"
	"add #32,r3\n\t"
	"ftrv xmtrx,fv12\n\t"
	"fmov dr6,@-r3\n\t"
	"fmov dr4,@-r3\n\t"
	"add #32,r3\n\t"
	"fmov dr10,@-r3\n\t"
	"fmov dr8,@-r3\n\t"
	"add #32,r3\n\t"
	"fmov dr14,@-r3\n\t"
	"fmov dr12,@-r3\n\t"
	"fmov @r7+,xd0\n\t"
	"fmov @r7+,xd2\n\t"
	"fmov @r7+,xd4\n\t"
	"fmov @r7+,xd6\n\t"
	"fmov @r7+,xd8\n\t"
	"fmov @r7+,xd10\n\t"
	"fmov @r7+,xd12\n\t"
	"fmov @r7+,xd14\n\t"
	"fschg\n\t"
	"mov r4,r2\n\t"
	"mov r4,r3\n\t"
	"add #127,r3\n\t"
	"add #1,r3\n\t"
	"mov r5,r4\n\t"
	"mov r5,r6\n\t"
	"add #127,r6\n\t"
	"add #1,r6\n\t"
	"fmov.s @r3+,fr4\n\t"
	"fmov.s @r3+,fr5\n\t"
	"fmov.s @r3+,fr6\n\t"
	"fmov.s @r3+,fr7\n\t"
	"ftrv xmtrx,fv4\n\t"
	"mov #4,r1\n\t"
	"fmov.s @r2+,fr0\n\t"
	"fmov.s @r2+,fr1\n\t"
	"fmov.s @r2+,fr2\n\t"
	"fmov.s @r2+,fr3\n\t"
	"\n5:\n\t"
	"fmov.s @r3+,fr12\n\t"
	"fadd fr0,fr4\n\t"
	"fmov.s @r3+,fr13\n\t"
	"fadd fr1,fr5\n\t"
	"fmov.s @r3+,fr14\n\t"
	"fadd fr2,fr6\n\t"
	"fmov.s @r3+,fr15\n\t"
	"fadd fr3,fr7\n\t"
	"mov r4,r0\n\t"
	"fadd fr0,fr0\n\t"
	"fmov.s @r2+,fr8\n\t"
	"fadd fr1,fr1\n\t"
	"fmov.s @r2+,fr9\n\t"
	"fadd fr2,fr2\n\t"
	"fmov.s @r2+,fr10\n\t"
	"fadd fr3,fr3\n\t"
	"fmov.s fr4,@r0\n\t"
	"fsub fr4,fr0\n\t"
	"fmov.s fr5,@(r0,r8)\n\t"
	"fsub fr5,fr1\n\t"
	"fmov.s fr6,@(r0,r9)\n\t"
	"fsub fr6,fr2\n\t"
	"fmov.s fr7,@(r0,r10)\n\t"
	"fsub fr7,fr3\n\t"
	"fmov.s @r2+,fr11\n\t"
	"ftrv xmtrx,fv12\n\t"
	"fmov.s fr0,@(r0,r14)\n\t"
	"fmov.s fr1,@(r0,r13)\n\t"
	"fmov.s fr2,@(r0,r12)\n\t"
	"fmov.s fr3,@(r0,r11)\n\t"
	"fmov.s @r3+,fr4\n\t"
	"fadd fr8,fr12\n\t"
	"fmov.s @r3+,fr5\n\t"
	"fadd fr9,fr13\n\t"
	"fmov.s @r3+,fr6\n\t"
	"fadd fr10,fr14\n\t"
	"fmov.s @r3+,fr7\n\t"
	"fadd fr11,fr15\n\t"
	"mov r6,r0\n\t"
	"fadd fr8,fr8\n\t"
	"fmov.s @r2+,fr0\n\t"
	"fadd fr9,fr9\n\t"
	"fmov.s @r2+,fr1\n\t"
	"fadd fr10,fr10\n\t"
	"fmov.s @r2+,fr2\n\t"
	"fadd fr11,fr11\n\t"
	"fmov.s fr12,@r0\n\t"
	"fsub fr12,fr8\n\t"
	"fmov.s fr13,@(r0,r8)\n\t"
	"fsub fr13,fr9\n\t"
	"fmov.s fr14,@(r0,r9)\n\t"
	"fsub fr14,fr10\n\t"
	"fmov.s fr15,@(r0,r10)\n\t"
	"fsub fr15,fr11\n\t"
	"fmov.s @r2+,fr3\n\t"
	"ftrv xmtrx,fv4\n\t"
	"fmov.s fr8,@(r0,r14)\n\t"
	"fmov.s fr9,@(r0,r13)\n\t"
	"fmov.s fr10,@(r0,r12)\n\t"
	"fmov.s fr11,@(r0,r11)\n\t"
	"add #4,r4\n\t"
	"dt r1\n\t"
	"bf/s 5b\n\t"
	"add #4,r6\n\t"
	"fschg\n\t"
	"mov.l @r15,r5\n\t"
	"mov r5,r2\n\t"
	"add #127,r2\n\t"
	"add #1,r2\n\t"
	"mov r2,r3\n\t"
	"add #16,r3\n\t"
	"fmov @r2+,dr0\n\t"
	"fmov @r2+,dr2\n\t"
	"fmov @r2+,dr4\n\t"
	"fmov @r2+,dr6\n\t"
	"ftrv xmtrx,fv0\n\t"
	"fmov @r2+,dr8\n\t"
	"fmov @r2+,dr10\n\t"
	"ftrv xmtrx,fv4\n\t"
	"fmov @r2+,dr12\n\t"
	"fmov @r2+,dr14\n\t"
	"ftrv xmtrx,fv8\n\t"
	"fmov dr2,@-r3\n\t"
	"fmov dr0,@-r3\n\t"
	"add #32,r3\n\t"
	"fmov @r2+,dr0\n\t"
	"fmov @r2+,dr2\n\t"
	"ftrv xmtrx,fv12\n\t"
	"fmov dr6,@-r3\n\t"
	"fmov dr4,@-r3\n\t"
	"add #32,r3\n\t"
	"fmov @r2+,dr4\n\t"
	"fmov @r2+,dr6\n\t"
	"ftrv xmtrx,fv0\n\t"
	"fmov dr10,@-r3\n\t"
	"fmov dr8,@-r3\n\t"
	"add #32,r3\n\t"
	"fmov @r2+,dr8\n\t"
	"fmov @r2+,dr10\n\t"
	"ftrv xmtrx,fv4\n\t"
	"fmov dr14,@-r3\n\t"
	"fmov dr12,@-r3\n\t"
	"add #32,r3\n\t"
	"fmov @r2+,dr12\n\t"
	"fmov @r2+,dr14\n\t"
	"ftrv xmtrx,fv8\n\t"
	"fmov dr2,@-r3\n\t"
	"fmov dr0,@-r3\n\t"
	"add #32,r3\n\t"
	"ftrv xmtrx,fv12\n\t"
	"fmov dr6,@-r3\n\t"
	"fmov dr4,@-r3\n\t"
	"add #32,r3\n\t"
	"fmov dr10,@-r3\n\t"
	"fmov dr8,@-r3\n\t"
	"add #32,r3\n\t"
	"fmov dr14,@-r3\n\t"
	"fmov dr12,@-r3\n\t"
	"mov.l @(8,r15),r7\n\t"
	"fmov @r7+,xd0\n\t"
	"fmov @r7+,xd2\n\t"
	"fmov @r7+,xd4\n\t"
	"fmov @r7+,xd6\n\t"
	"fmov @r7+,xd8\n\t"
	"fmov @r7+,xd10\n\t"
	"fmov @r7+,xd12\n\t"
	"fmov @r7+,xd14\n\t"
	"fschg\n\t"
	"add #64,r7\n\t"
	"fmov.s @r7,fr8\n\t"
	"mov r5,r2\n\t"
	"mov r5,r3\n\t"
	"add #127,r3\n\t"
	"add #1,r3\n\t"
	"mov.l @(4,r15),r6\n\t"
	"add #32,r6\n\t"
	"fmov.s @r2+,fr0\n\t"
	"fmov.s @r2+,fr1\n\t"
	"fmov.s @r2+,fr2\n\t"
	"fmov.s @r2+,fr3\n\t"
	"ftrv xmtrx,fv0\n\t"
	"fmov.s @r3+,fr4\n\t"
	"fmov.s @r3+,fr5\n\t"
	"fmov.s @r3+,fr6\n\t"
	"fadd fr0,fr4\n\t"
	"fmov.s @r3+,fr7\n\t"
	"fadd fr1,fr5\n\t"
	"fmov.s @r2+,fr12\n\t"
	"fadd fr2,fr6\n\t"
	"fmov.s @r2+,fr13\n\t"
	"fadd fr3,fr7\n\t"
	"fmov.s @r2+,fr14\n\t"
	"fadd fr0,fr0\n\t"
	"fmov.s @r2+,fr15\n\t"
	"fadd fr1,fr1\n\t"
	"fmov.s @r3+,fr9\n\t"
	"fadd fr2,fr2\n\t"
	"fmov.s @r3+,fr10\n\t"
	"fadd fr3,fr3\n\t"
	"fmov.s @r3+,fr11\n\t"
	"fsub fr4,fr0\n\t"
	"fsub fr5,fr1\n\t"
	"fsub fr6,fr2\n\t"
	"fsub fr7,fr3\n\t"
	"fadd fr8,fr7\n\t"
	"fadd fr8,fr6\n\t"
	"fadd fr8,fr5\n\t"
	"fadd fr8,fr4\n\t"
	"fadd fr8,fr0\n\t"
	"mov r6,r7\n\t"
	"add #-16,r7\n\t"
	"fadd fr8,fr1\n\t"
	"fmov.s fr7,@-r7\n\t"
	"fadd fr8,fr2\n\t"
	"fmov.s fr6,@-r7\n\t"
	"fadd fr8,fr3\n\t"
	"fmov.s fr5,@-r7\n\t"
	"fmov.s fr4,@-r7\n\t"
	"ftrv xmtrx,fv12\n\t"
	"fmov.s fr0,@-r6\n\t"
	"fmov.s fr1,@-r6\n\t"
	"fmov.s fr2,@-r6\n\t"
	"fmov.s fr3,@-r6\n\t"
	"add #48,r6\n\t"
	"fadd fr12,fr9\n\t"
	"fmov.s @r3+,fr4\n\t"
	"fadd fr13,fr10\n\t"
	"fmov.s @r2+,fr0\n\t"
	"fadd fr14,fr11\n\t"
	"fmov.s @r2+,fr1\n\t"
	"fadd fr15,fr4\n\t"
	"fmov.s @r2+,fr2\n\t"
	"fadd fr12,fr12\n\t"
	"fmov.s @r2+,fr3\n\t"
	"fadd fr13,fr13\n\t"
	"fmov.s @r3+,fr5\n\t"
	"fadd fr14,fr14\n\t"
	"fmov.s @r3+,fr6\n\t"
	"fadd fr15,fr15\n\t"
	"fmov.s @r3+,fr7\n\t"
	"fsub fr9,fr12\n\t"
	"fsub fr10,fr13\n\t"
	"fsub fr11,fr14\n\t"
	"fsub fr4,fr15\n\t"
	"fadd fr8,fr4\n\t"
	"fadd fr8,fr11\n\t"
	"fadd fr8,fr10\n\t"
	"fadd fr8,fr9\n\t"
	"fadd fr8,fr12\n\t"
	"mov r6,r7\n\t"
	"add #-16,r7\n\t"
	"fadd fr8,fr13\n\t"
	"fmov.s fr4,@-r7\n\t"
	"fadd fr8,fr14\n\t"
	"fmov.s fr11,@-r7\n\t"
	"fadd fr8,fr15\n\t"
	"fmov.s fr10,@-r7\n\t"
	"fmov.s fr9,@-r7\n\t"
	"ftrv xmtrx,fv0\n\t"
	"fmov.s fr12,@-r6\n\t"
	"fmov.s fr13,@-r6\n\t"
	"fmov.s fr14,@-r6\n\t"
	"fmov.s fr15,@-r6\n\t"
	"add #48,r6\n\t"
	"fadd fr0,fr5\n\t"
	"fmov.s @r3+,fr9\n\t"
	"fadd fr1,fr6\n\t"
	"fmov.s @r2+,fr12\n\t"
	"fadd fr2,fr7\n\t"
	"fmov.s @r2+,fr13\n\t"
	"fadd fr3,fr9\n\t"
	"fmov.s @r2+,fr14\n\t"
	"fadd fr0,fr0\n\t"
	"fmov.s @r2+,fr15\n\t"
	"fadd fr1,fr1\n\t"
	"fmov.s @r3+,fr4\n\t"
	"fadd fr2,fr2\n\t"
	"fmov.s @r3+,fr10\n\t"
	"fadd fr3,fr3\n\t"
	"fmov.s @r3+,fr11\n\t"
	"fsub fr5,fr0\n\t"
	"fsub fr6,fr1\n\t"
	"fsub fr7,fr2\n\t"
	"fsub fr9,fr3\n\t"
	"fadd fr8,fr9\n\t"
	"fadd fr8,fr7\n\t"
	"fadd fr8,fr6\n\t"
	"fadd fr8,fr5\n\t"
	"fadd fr8,fr0\n\t"
	"mov r6,r7\n\t"
	"add #-16,r7\n\t"
	"fadd fr8,fr1\n\t"
	"fmov.s fr9,@-r7\n\t"
	"fadd fr8,fr2\n\t"
	"fmov.s fr7,@-r7\n\t"
	"fadd fr8,fr3\n\t"
	"fmov.s fr6,@-r7\n\t"
	"fmov.s fr5,@-r7\n\t"
	"ftrv xmtrx,fv12\n\t"
	"fmov.s fr0,@-r6\n\t"
	"fmov.s fr1,@-r6\n\t"
	"fmov.s fr2,@-r6\n\t"
	"fmov.s fr3,@-r6\n\t"
	"add #48,r6\n\t"
	"fadd fr12,fr4\n\t"
	"fmov.s @r3+,fr5\n\t"
	"fadd fr13,fr10\n\t"
	"fmov.s @r2+,fr0\n\t"
	"fadd fr14,fr11\n\t"
	"fmov.s @r2+,fr1\n\t"
	"fadd fr15,fr5\n\t"
	"fmov.s @r2+,fr2\n\t"
	"fadd fr12,fr12\n\t"
	"fmov.s @r2+,fr3\n\t"
	"fadd fr13,fr13\n\t"
	"fmov.s @r3+,fr6\n\t"
	"fadd fr14,fr14\n\t"
	"fmov.s @r3+,fr7\n\t"
	"fadd fr15,fr15\n\t"
	"fmov.s @r3+,fr9\n\t"
	"fsub fr4,fr12\n\t"
	"fsub fr10,fr13\n\t"
	"fsub fr11,fr14\n\t"
	"fsub fr5,fr15\n\t"
	"fadd fr8,fr5\n\t"
	"fadd fr8,fr11\n\t"
	"fadd fr8,fr10\n\t"
	"fadd fr8,fr4\n\t"
	"fadd fr8,fr12\n\t"
	"mov r6,r7\n\t"
	"add #-16,r7\n\t"
	"fadd fr8,fr13\n\t"
	"fmov.s fr5,@-r7\n\t"
	"fadd fr8,fr14\n\t"
	"fmov.s fr11,@-r7\n\t"
	"fadd fr8,fr15\n\t"
	"fmov.s fr10,@-r7\n\t"
	"fmov.s fr4,@-r7\n\t"
	"ftrv xmtrx,fv0\n\t"
	"fmov.s fr12,@-r6\n\t"
	"fmov.s fr13,@-r6\n\t"
	"fmov.s fr14,@-r6\n\t"
	"fmov.s fr15,@-r6\n\t"
	"add #48,r6\n\t"
	"fadd fr0,fr6\n\t"
	"fmov.s @r3+,fr4\n\t"
	"fadd fr1,fr7\n\t"
	"fmov.s @r2+,fr12\n\t"
	"fadd fr2,fr9\n\t"
	"fmov.s @r2+,fr13\n\t"
	"fadd fr3,fr4\n\t"
	"fmov.s @r2+,fr14\n\t"
	"fadd fr0,fr0\n\t"
	"fmov.s @r2+,fr15\n\t"
	"fadd fr1,fr1\n\t"
	"fmov.s @r3+,fr5\n\t"
	"fadd fr2,fr2\n\t"
	"fmov.s @r3+,fr10\n\t"
	"fadd fr3,fr3\n\t"
	"fmov.s @r3+,fr11\n\t"
	"fsub fr6,fr0\n\t"
	"fsub fr7,fr1\n\t"
	"fsub fr9,fr2\n\t"
	"fsub fr4,fr3\n\t"
	"fadd fr8,fr4\n\t"
	"fadd fr8,fr9\n\t"
	"fadd fr8,fr7\n\t"
	"fadd fr8,fr6\n\t"
	"fadd fr8,fr0\n\t"
	"mov r6,r7\n\t"
	"add #-16,r7\n\t"
	"fadd fr8,fr1\n\t"
	"fmov.s fr4,@-r7\n\t"
	"fadd fr8,fr2\n\t"
	"fmov.s fr9,@-r7\n\t"
	"fadd fr8,fr3\n\t"
	"fmov.s fr7,@-r7\n\t"
	"fmov.s fr6,@-r7\n\t"
	"ftrv xmtrx,fv12\n\t"
	"fmov.s fr0,@-r6\n\t"
	"fmov.s fr1,@-r6\n\t"
	"fmov.s fr2,@-r6\n\t"
	"fmov.s fr3,@-r6\n\t"
	"add #48,r6\n\t"
	"fadd fr12,fr5\n\t"
	"fmov.s @r3+,fr6\n\t"
	"fadd fr13,fr10\n\t"
	"fmov.s @r2+,fr0\n\t"
	"fadd fr14,fr11\n\t"
	"fmov.s @r2+,fr1\n\t"
	"fadd fr15,fr6\n\t"
	"fmov.s @r2+,fr2\n\t"
	"fadd fr12,fr12\n\t"
	"fmov.s @r2+,fr3\n\t"
	"fadd fr13,fr13\n\t"
	"fmov.s @r3+,fr4\n\t"
	"fadd fr14,fr14\n\t"
	"fmov.s @r3+,fr7\n\t"
	"fadd fr15,fr15\n\t"
	"fmov.s @r3+,fr9\n\t"
	"fsub fr5,fr12\n\t"
	"fsub fr10,fr13\n\t"
	"fsub fr11,fr14\n\t"
	"fsub fr6,fr15\n\t"
	"fadd fr8,fr6\n\t"
	"fadd fr8,fr11\n\t"
	"fadd fr8,fr10\n\t"
	"fadd fr8,fr5\n\t"
	"fadd fr8,fr12\n\t"
	"mov r6,r7\n\t"
	"add #-16,r7\n\t"
	"fadd fr8,fr13\n\t"
	"fmov.s fr6,@-r7\n\t"
	"fadd fr8,fr14\n\t"
	"fmov.s fr11,@-r7\n\t"
	"fadd fr8,fr15\n\t"
	"fmov.s fr10,@-r7\n\t"
	"fmov.s fr5,@-r7\n\t"
	"ftrv xmtrx,fv0\n\t"
	"fmov.s fr12,@-r6\n\t"
	"fmov.s fr13,@-r6\n\t"
	"fmov.s fr14,@-r6\n\t"
	"fmov.s fr15,@-r6\n\t"
	"add #48,r6\n\t"
	"fadd fr0,fr4\n\t"
	"fmov.s @r3+,fr5\n\t"
	"fadd fr1,fr7\n\t"
	"fmov.s @r2+,fr12\n\t"
	"fadd fr2,fr9\n\t"
	"fmov.s @r2+,fr13\n\t"
	"fadd fr3,fr5\n\t"
	"fmov.s @r2+,fr14\n\t"
	"fadd fr0,fr0\n\t"
	"fmov.s @r2+,fr15\n\t"
	"fadd fr1,fr1\n\t"
	"fmov.s @r3+,fr6\n\t"
	"fadd fr2,fr2\n\t"
	"fmov.s @r3+,fr10\n\t"
	"fadd fr3,fr3\n\t"
	"fmov.s @r3+,fr11\n\t"
	"fsub fr4,fr0\n\t"
	"fsub fr7,fr1\n\t"
	"fsub fr9,fr2\n\t"
	"fsub fr5,fr3\n\t"
	"fadd fr8,fr5\n\t"
	"fadd fr8,fr9\n\t"
	"fadd fr8,fr7\n\t"
	"fadd fr8,fr4\n\t"
	"fadd fr8,fr0\n\t"
	"mov r6,r7\n\t"
	"add #-16,r7\n\t"
	"fadd fr8,fr1\n\t"
	"fmov.s fr5,@-r7\n\t"
	"fadd fr8,fr2\n\t"
	"fmov.s fr9,@-r7\n\t"
	"fadd fr8,fr3\n\t"
	"fmov.s fr7,@-r7\n\t"
	"fmov.s fr4,@-r7\n\t"
	"ftrv xmtrx,fv12\n\t"
	"fmov.s fr0,@-r6\n\t"
	"fmov.s fr1,@-r6\n\t"
	"fmov.s fr2,@-r6\n\t"
	"fmov.s fr3,@-r6\n\t"
	"add #48,r6\n\t"
	"fadd fr12,fr6\n\t"
	"fmov.s @r3+,fr4\n\t"
	"fadd fr13,fr10\n\t"
	"fadd fr14,fr11\n\t"
	"fadd fr15,fr4\n\t"
	"fadd fr12,fr12\n\t"
	"fadd fr13,fr13\n\t"
	"fadd fr14,fr14\n\t"
	"fadd fr15,fr15\n\t"
	"fsub fr6,fr12\n\t"
	"fsub fr10,fr13\n\t"
	"fsub fr11,fr14\n\t"
	"fsub fr4,fr15\n\t"
	"fadd fr8,fr4\n\t"
	"fadd fr8,fr11\n\t"
	"fadd fr8,fr10\n\t"
	"fadd fr8,fr6\n\t"
	"fadd fr8,fr12\n\t"
	"mov r6,r7\n\t"
	"add #-16,r7\n\t"
	"fadd fr8,fr13\n\t"
	"fmov.s fr4,@-r7\n\t"
	"fadd fr8,fr14\n\t"
	"fmov.s fr11,@-r7\n\t"
	"fadd fr8,fr15\n\t"
	"fmov.s fr10,@-r7\n\t"
	"fmov.s fr6,@-r7\n\t"
	"fmov.s fr12,@-r6\n\t"
	"fmov.s fr13,@-r6\n\t"
	"fmov.s fr14,@-r6\n\t"
	"fmov.s fr15,@-r6\n\t"
	"add #12,r15\n\t"
	"fmov.s @r15+,fr15\n\t"
	"fmov.s @r15+,fr14\n\t"
	"fmov.s @r15+,fr13\n\t"
	"fmov.s @r15+,fr12\n\t"
	"mov.l @r15+,r14\n\t"
	"mov.l @r15+,r13\n\t"
	"mov.l @r15+,r12\n\t"
	"mov.l @r15+,r11\n\t"
	"mov.l @r15+,r10\n\t"
	"mov.l @r15+,r9\n\t"
	"rts\n\t"
	"mov.l @r15+,r8\n"
	"\t.text\n"
);



static inline uint8_t plm_clamp(int n) {
	return clamp_table[n];
}

int plm_video_decode_sequence_header(plm_video_t *self);
void plm_video_init_frame(plm_video_t *self, plm_frame_t *frame, uint8_t *base);
void plm_video_decode_picture(plm_video_t *self);
void plm_video_decode_slice(plm_video_t *self, int slice);
void plm_video_decode_macroblock(plm_video_t *self);
void plm_video_decode_motion_vectors(plm_video_t *self);
void plm_video_predict_macroblock(plm_video_t *self);
void plm_video_copy_macroblock(uint32_t *dest, plm_frame_t *reference, int motion_h, int motion_v);
void plm_video_interpolate_macroblock(uint32_t *dest, plm_frame_t *reference, int motion_h, int motion_v);
// Dequantise one AC coefficient (or a non-intra DC); qm = quantizer_scale * matrix entry.
static inline int plm_dequant(int level, int qm, int non_intra) {
	// ISO/IEC 11172-2: (2 level + Sign(level)*non_intra) * q * W / 16 with truncation toward zero,
	// then even values move one step toward zero, then clip to [-2048, 2047]
	int neg = level < 0;
	int m = neg ? -level : level;
	m = ((2 * m + non_intra) * qm) >> 4;
	if (m > 0 && (m & 1) == 0) {
		m--;
	}
	if (m > 2047) {
		m = neg ? 2048 : 2047;
	}
	return neg ? -m : m;
}
static int plm_video_decode_mb_coefs(plm_video_t *self, int cbp);
static void plm_video_output_block(plm_video_t *self, int block, uint32_t *mb_display, float *coefs, int n);
#ifndef PLM_HOT
// Per-macroblock functions share one section so they sit together and contend for the SH-4's
// 8 KB direct-mapped instruction cache predictably instead of being scattered across .text.
#define PLM_HOT __attribute__((section(".text.plm_hot")))
#endif

// Allocate the 12 cache lines of a macroblock's 384-byte display entry without reading memory
// (SH-4 movca.l). Only for entries that are then completely overwritten: motion-compensated
// prediction and intra macroblocks (all six blocks coded). Saves 12 line fills per macroblock.
static inline __attribute__((always_inline)) void plm_mb_alloc_lines(uint32_t *d) {
	register uint32_t z __asm__("r0") = 0;
	for (int i = 0; i < 96; i += 8) {
		__asm__ __volatile__("movca.l %0,@%1" : : "z"(z), "r"(d + i) : "memory");
	}
}


static inline void plm_video_advance_macroblock(plm_video_t *self) {
	self->macroblock_address++;
	self->mb_col++;
	if (self->mb_col >= self->mb_width) {
		self->mb_col = 0;
		self->mb_row++;
	}
}


// ---------------------------------------------------------------------------
// SH-4 assembly output stage for one 8x8 block (64 contiguous bytes at d) with the float IDCT.
// cl is the clamp table pointer biased by -0x4b400000, r the IDCT results as float bit
// patterns 0x4b400000 + value (zeroed afterwards with 64-bit stores, ready for the next block):
//   plm_sh4_out_add(d, r, cl):  d[i] = cl[d[i] + r[i]]   (predicted block plus residual)
//   plm_sh4_out_put(d, r, cl):  d[i] = cl[r[i]]          (intra block)
//   plm_sh4_out_dc(d, clv):     d[i] = clv[d[i]]         (DC-only residual; clv = clamp + DC value)
// Two pixels are interleaved so the indexed clamp loads (which need r0) do not serialise the loop.
void plm_sh4_out_add(uint8_t *d, uint32_t *r, const uint8_t *cl);
void plm_sh4_out_put(uint8_t *d, uint32_t *r, const uint8_t *cl);
void plm_sh4_out_dc(uint8_t *d, const uint8_t *clv);
#define PLM_SH4_ZERO256 /* r5 = end of the 256-byte block: 32 paired zero stores */ \
"	fschg\n" \
"	fldi0	fr0\n" \
"	fldi0	fr1\n" \
"	mov	#8,r1\n" \
"1:\n" \
"	fmov	dr0,@-r5\n" \
"	fmov	dr0,@-r5\n" \
"	dt	r1\n" \
"	fmov	dr0,@-r5\n" \
"	bf/s	1b\n" \
"	fmov	dr0,@-r5\n" \
"	fschg\n"
__asm__(
"	.section .text.plm_hot,\"ax\"\n"
"	.align	5\n"
"	.global	_plm_sh4_out_add\n"
"_plm_sh4_out_add:\n"
/* 4 pixels per iteration. The four prediction bytes come from one word (r1, shifted down a byte at
   a time); each clamp load is indexed, r0 = residual plus (cl + prediction), so one add per pixel.
   The results (r8-r11) are stored during the next iteration, backwards from the group's end (r12),
   which lets the stores pair with the next group's arithmetic: the first group is computed before
   the loop and the last one stored after it. Order from the SH7091 pipeline model (16 cycles per
   iteration; the previous loop was 19). */
"	mov.l	r8,@-r15\n"
"	mov.l	r9,@-r15\n"
"	mov.l	r10,@-r15\n"
"	mov.l	r11,@-r15\n"
"	mov.l	r12,@-r15\n"
"	mov	r4,r12\n"
"	add	#4,r12\n"
"	mov	#15,r7\n"
"	mov.l	@r4+,r1\n"
"	mov.l	@r5+,r0\n"
"	extu.b	r1,r2\n"
"	shlr8	r1\n"
"	add	r6,r2\n"
"	mov.b	@(r0,r2),r8\n"
"	mov.l	@r5+,r0\n"
"	extu.b	r1,r3\n"
"	shlr8	r1\n"
"	add	r6,r3\n"
"	mov.b	@(r0,r3),r9\n"
"	mov.l	@r5+,r0\n"
"	extu.b	r1,r2\n"
"	shlr8	r1\n"
"	add	r6,r2\n"
"	mov.b	@(r0,r2),r10\n"
"	mov.l	@r5+,r0\n"
"	add	r6,r1\n"
"	mov.b	@(r0,r1),r11\n"
"2:\n"
"	mov.l	@r4+,r1\n"
"	mov.l	@r5+,r0\n"
"	extu.b	r1,r2\n"
"	mov.b	r11,@-r12\n"
"	mov.b	r10,@-r12\n"
"	shlr8	r1\n"
"	add	r6,r2\n"
"	mov.b	r9,@-r12\n"
"	mov.b	r8,@-r12\n"
"	extu.b	r1,r3\n"
"	shlr8	r1\n"
"	mov.b	@(r0,r2),r8\n"
"	mov.l	@r5+,r0\n"
"	add	#8,r12\n"
"	add	r6,r3\n"
"	dt	r7\n"
"	mov.b	@(r0,r3),r9\n"
"	extu.b	r1,r2\n"
"	mov.l	@r5+,r0\n"
"	add	r6,r2\n"
"	shlr8	r1\n"
"	mov.b	@(r0,r2),r10\n"
"	mov.l	@r5+,r0\n"
"	add	r6,r1\n"
"	bf/s	2b\n"
"	mov.b	@(r0,r1),r11\n"
"	mov.b	r11,@-r12\n"
"	mov.b	r10,@-r12\n"
"	mov.b	r9,@-r12\n"
"	mov.b	r8,@-r12\n"
PLM_SH4_ZERO256
"	mov.l	@r15+,r12\n"
"	mov.l	@r15+,r11\n"
"	mov.l	@r15+,r10\n"
"	mov.l	@r15+,r9\n"
"	rts\n"
"	mov.l	@r15+,r8\n"
"	.align	5\n"
"	.global	_plm_sh4_out_put\n"
"_plm_sh4_out_put:\n"
/* 4 pixels per iteration: r0-r3 hold residual, then clamp address, then result. Order from the
   SH7091 pipeline model (12 cycles per iteration). */
"	mov	#16,r7\n"
"2:\n"
"	mov.l	@r5+,r0\n"
"	dt	r7\n"
"	mov.l	@r5+,r1\n"
"	add	r6,r0\n"
"	mov.l	@r5+,r2\n"
"	add	r6,r1\n"
"	mov.l	@r5+,r3\n"
"	mov.b	@r0,r0\n"
"	add	r6,r2\n"
"	mov.b	@r1,r1\n"
"	mov.b	r0,@r4\n"
"	add	#1,r4\n"
"	mov.b	r1,@r4\n"
"	add	#1,r4\n"
"	mov.b	@r2,r2\n"
"	add	r6,r3\n"
"	mov.b	@r3,r3\n"
"	mov.b	r2,@r4\n"
"	add	#1,r4\n"
"	mov.b	r3,@r4\n"
"	bf/s	2b\n"
"	add	#1,r4\n"
PLM_SH4_ZERO256
"	rts\n"
"	nop\n"
"	.align	5\n"
"	.global	_plm_sh4_out_dc\n"
"_plm_sh4_out_dc:\n"
/* 4 pixels per iteration, as plm_sh4_out_add: the prediction bytes come from one word (r1), each
   clamp load is indexed with the byte in r0 (the last one through r1 itself), and the results
   (r2, r3, r8, r9) are stored during the next iteration, backwards from the group's end (r6).
   12 cycles per iteration in the SH7091 pipeline model; the previous loop was 15. */
"	mov.l	r8,@-r15\n"
"	mov.l	r9,@-r15\n"
"	mov	r4,r6\n"
"	add	#4,r6\n"
"	mov	#15,r7\n"
"	mov.l	@r4+,r1\n"
"	extu.b	r1,r0\n"
"	shlr8	r1\n"
"	mov.b	@(r0,r5),r2\n"
"	extu.b	r1,r0\n"
"	shlr8	r1\n"
"	mov.b	@(r0,r5),r3\n"
"	extu.b	r1,r0\n"
"	shlr8	r1\n"
"	mov.b	@(r0,r5),r8\n"
"	add	r5,r1\n"
"	mov.b	@r1,r9\n"
"2:\n"
"	mov.l	@r4+,r1\n"
"	mov.b	r9,@-r6\n"
"	mov.b	r8,@-r6\n"
"	dt	r7\n"
"	mov.b	r3,@-r6\n"
"	extu.b	r1,r0\n"
"	shlr8	r1\n"
"	mov.b	r2,@-r6\n"
"	mov.b	@(r0,r5),r2\n"
"	extu.b	r1,r0\n"
"	shlr8	r1\n"
"	mov.b	@(r0,r5),r3\n"
"	extu.b	r1,r0\n"
"	shlr8	r1\n"
"	mov.b	@(r0,r5),r8\n"
"	add	#8,r6\n"
"	add	r5,r1\n"
"	bf/s	2b\n"
"	mov.b	@r1,r9\n"
"	mov.b	r9,@-r6\n"
"	mov.b	r8,@-r6\n"
"	mov.b	r3,@-r6\n"
"	mov.b	r2,@-r6\n"
"	mov.l	@r15+,r9\n"
"	rts\n"
"	mov.l	@r15+,r8\n"
"	.text\n"
);

// ---------------------------------------------------------------------------
// SH-4 assembly: all DCT coefficients of one macroblock. For every block set in cbp
// it decodes the intra DC (size code, differential, predictor) or the first non-intra token, then
// the AC tokens up to end_of_block, dequantised exactly as the C loop and stored as floats in
// blocks[b] (float-IDCT layout); nb[b] = index after the last coefficient. The bit reader keeps
// two 32-bit words. Codes longer than the 9-bit table return status 1 with b, n and P
// at the code; C decodes that token and calls again with resume = 1. 0 = done, -1 = invalid.
int plm_sh4_mb_coefs(plm_mbc_t *st);
__asm__(
"	.section .text.plm_hot,\"ax\"\n"
"	.align	5\n"
"	.global	_plm_sh4_mb_coefs\n"
"_plm_sh4_mb_coefs:\n"
"	mov.l	r8,@-r15\n"
"	mov.l	r9,@-r15\n"
"	mov.l	r10,@-r15\n"
"	mov.l	r11,@-r15\n"
"	mov.l	r12,@-r15\n"
"	mov.l	r13,@-r15\n"
"	mov.l	r14,@-r15\n"
"	mov.l	r4,@-r15\n"          /* @r15 = ctx */
"	mov.l	@(0,r4),r2\n"        /* P */
"	mov.l	@(4,r4),r6\n"        /* base */
"	mov.l	@(8,r4),r7\n"        /* mask */
"	mov	r2,r0\n"
"	shlr2	r0\n"
"	shlr2	r0\n"
"	shlr	r0\n"
"	shll2	r0\n"                /* byte index of the word holding bit P */
"	mov	r0,r8\n"
"	and	r7,r0\n"
"	mov.l	@(r0,r6),r1\n"
"	mov	r8,r0\n"
"	add	#4,r0\n"
"	and	r7,r0\n"
"	mov.l	@(r0,r6),r0\n"
"	swap.b	r1,r1\n"
"	swap.w	r1,r1\n"
"	swap.b	r1,r1\n"
"	mov	#31,r3\n"
"	and	r2,r3\n"             /* bits consumed in that word */
"	shld	r3,r1\n"             /* cur */
"	add	#-32,r3\n"           /* c */
"	mov	r0,r2\n"
"	swap.b	r2,r2\n"
"	swap.w	r2,r2\n"
"	swap.b	r2,r2\n"             /* nxt */
"	add	#8,r8\n"             /* widx */
"	mov.l	@(16,r4),r9\n"        /* REST */
"	mov.w	.Lm_2047,r11\n"
"	mov	#-23,r12\n"
"	mov.l	@(28,r4),r13\n"       /* ni */
"	mov.l	@(56,r4),r0\n"
"	tst	r0,r0\n"
"	bt	.Lm_next\n"
"	mov.l	@(48,r4),r0\n"        /* resume block b at n */
"	shll8	r0\n"
"	mov.l	@(36,r4),r10\n"
"	add	r0,r10\n"
"	mov.l	@(52,r4),r5\n"
"	mov.l	@(24,r4),r0\n"
"	shll2	r5\n"
"	add	r0,r5\n"                /* qz + 4n */
"	bra	.Lm_first\n"
"	mov	r9,r4\n"
"	.align	2\n"
".Lm_2047:\n"
"	.short	2047\n"
"	.short	0\n"
".Lm_done_near:\n"
"	bra	.Lm_done\n"
"	nop\n"
".Lm_next:\n"                 /* next coded block */
"	mov.l	@r15,r4\n"
"	mov.l	@(48,r4),r0\n"
".Lm_next2:\n"                /* r4 = ctx, r0 = b */
"	mov.l	@(32,r4),r6\n"       /* blocks still to decode, the next one in bit 31 */
"	tst	r6,r6\n"
"	bt	.Lm_done_near\n"
"1:\n"
"	shll	r6\n"
"	bf/s	1b\n"
"	add	#1,r0\n"
"	mov.l	r6,@(32,r4)\n"
"	mov.l	r0,@(48,r4)\n"
"	mov	r0,r7\n"
"	shll8	r7\n"
"	mov.l	@(36,r4),r10\n"
"	add	r7,r10\n"               /* blocks[b] */
"	mov.l	@(24,r4),r5\n"        /* qz entry of coefficient 0 */
"	tst	r13,r13\n"
"	bt/s	.Lm_first\n"           /* non-intra (r13 = 0): first token from the FIRST table */
"	mov.l	@(12,r4),r4\n"
/* intra DC: plane = b < 4 ? 0 : b - 3 */
"	mov	#0,r5\n"
"	mov	#3,r7\n"
"	cmp/gt	r7,r0\n"
"	bf	1f\n"
"	mov	r0,r5\n"
"	add	#-3,r5\n"
"1:\n"
"	mov.l	@r15,r4\n"
"	mov.l	@(44,r4),r7\n"
"	mov	r5,r0\n"
"	shll8	r0\n"
"	shll2	r0\n"
"	add	r0,r7\n"                /* this plane's 256-entry dct_dc_size table */
"	shll2	r5\n"                 /* predictor offset */
"	mov	r2,r0\n"
"	shld	r3,r0\n"
"	or	r1,r0\n"
"	shlr16	r0\n"
"	shlr8	r0\n"
"	shll2	r0\n"
"	mov.l	@(r0,r7),r7\n"        /* size | len << 16 | leaf << 24 */
"	extu.b	r7,r6\n"             /* dct_dc_size */
"	shlr16	r7\n"
"	extu.b	r7,r0\n"             /* code length */
"	add	r0,r3\n"
"	cmp/pz	r3\n"
"	bt/s	.Lm_x1\n"
"	shld	r0,r1\n"
".Lm_c1:\n"
"	mov	#0,r7\n"                /* differential */
"	tst	r6,r6\n"
"	bt	.Lm_dcz\n"
"	mov	r2,r0\n"
"	shld	r3,r0\n"
"	or	r1,r0\n"
"	mov	r6,r14\n"
"	add	#-32,r14\n"
"	shld	r14,r0\n"              /* the top dct_dc_size bits */
"	mov	r0,r7\n"
"	mov	r6,r14\n"
"	neg	r14,r14\n"
"	add	#1,r14\n"
"	shld	r14,r0\n"              /* their leading bit */
"	tst	#1,r0\n"
"	bf	.Lm_dcp\n"
"	mov	#1,r14\n"
"	shld	r6,r14\n"
"	sub	r14,r7\n"
"	add	#1,r7\n"                /* leading 0: bits - (2^size - 1) */
".Lm_dcp:\n"
"	mov	r6,r0\n"
"	add	r0,r3\n"
"	cmp/pz	r3\n"
"	bt/s	.Lm_x2\n"
"	shld	r0,r1\n"
".Lm_dcz:\n"
"	mov.l	@r15,r4\n"
"	mov.l	@(40,r4),r14\n"
"	mov	r5,r0\n"
"	mov.l	@(r0,r14),r6\n"
"	add	r7,r6\n"                /* dc = predictor + differential */
"	mov.l	r6,@(r0,r14)\n"
"	shll2	r6\n"
"	add	r6,r6\n"                /* * 8 */
"	lds	r6,fpul\n"
"	mov.l	@(24,r4),r5\n"
"	float	fpul,fr0\n"
"	add	#4,r5\n"                /* qz entry of coefficient 1 */
"	fmov.s	fr0,@r10\n"
"	mov	r9,r4\n"
".Lm_first:\n"                /* look up the next token with table r4: r14 = its entry */
"	mov	r2,r14\n"
"	shld	r3,r14\n"
"	or	r1,r14\n"
"	shld	r12,r14\n"
"	shll2	r14\n"
"	add	r4,r14\n"
/* A token with nothing pending (the first of a block, or one found in the second or third level):
   load its entry as .Lm_tok does. */
".Lm_tok_np:\n"
"	mov.b	@r14+,r7\n"           /* 4 * run */
"	add	r7,r5\n"                /* this coefficient's qz entry */
"	mov.b	@r14+,r7\n"           /* length, or kind */
"	mov.b	@r14+,r6\n"           /* 2|level| + 1 */
"	cmp/pz	r7\n"
"	bt/s	.Lm_tokx\n"
"	mov.b	@r14+,r4\n"           /* sign */
".Lm_special:\n"              /* r7 = kind; nothing consumed, nothing pending */
"	mov	r7,r0\n"
"	tst	#1,r0\n"
"	bt	.Lm_special2\n"
"	mov	#2,r0\n"                /* end_of_block: consume 2 bits, record n, next block */
"	add	r0,r3\n"
"	cmp/pz	r3\n"
"	bt/s	.Lm_x3\n"
"	shld	r0,r1\n"
".Lm_c3:\n"
"	mov.l	@r15,r4\n"
"	mov.l	@(24,r4),r0\n"
"	mov	r4,r6\n"
"	sub	r0,r5\n"
"	mov.l	@(48,r4),r0\n"
"	shlr2	r5\n"                 /* n = index after the last coefficient */
"	add	#64,r6\n"
"	bra	.Lm_next2\n"
"	mov.b	r5,@(r0,r6)\n"
/* The token loop, one coefficient per pass, in the order the SH7091 pipeline model scores best
   (26 cycles). MPEG-1: v = (2|level| + non_intra) * q * W / 16 (truncating), even v -> v - 1 (0
   stays 0), sign restored, clipped to [-2048, 2047].
   The previous coefficient is finished while this token's table entry loads: on entry r6 = its
   magnitude after oddification and the clip test, r4 = its sign, r0 = its block offset; the sign,
   the conversion to float and the store pair with the four byte loads. The store is done before
   the special-entry branch, so every exit from the loop leaves nothing pending.
   r5 walks the qz table (quantiser product, block offset per coefficient index); an index past 63
   reads an entry whose offset is negative. r14 holds the entry pointer, then the next token's
   window. r11 = 2047, r12 = -23. */
".Lm_tok:\n"
"	xor	r4,r6\n"
"	sub	r4,r6\n"                /* sign */
"	mov.b	@r14+,r7\n"           /* 4 * run */
"	lds	r6,fpul\n"
"	add	r7,r5\n"                /* this coefficient's qz entry */
"	mov.b	@r14+,r7\n"           /* length, or kind */
"	float	fpul,fr0\n"
"	mov.b	@r14+,r6\n"           /* 2|level| + 1 */
"	mov.b	@r14+,r4\n"           /* sign */
"	cmp/pz	r7\n"
"	fmov.s	fr0,@(r0,r10)\n"
"	bf	.Lm_special\n"
".Lm_tokx:\n"                 /* r7 = length, r6 = 2|level| + 1, r4 = sign, r5 = qz entry */
"	mov.w	@r5+,r0\n"            /* q * W */
"	add	r7,r3\n"
"	cmp/pz	r3\n"
"	add	r13,r6\n"               /* 2|level| + 1, or 2|level| intra (r13 = -1) */
"	mul.l	r0,r6\n"
"	bt/s	.Lm_cross\n"
"	shld	r7,r1\n"
".Lm_consumed:\n"
"	mov	r2,r14\n"               /* look up the next token now; it is loaded at the top of the next pass */
"	sts	macl,r6\n"
"	shld	r3,r14\n"
"	mov.w	@r5+,r0\n"            /* block offset; r5 = next index's entry */
"	or	r1,r14\n"
"	cmp/pz	r0\n"                /* negative: coefficient index past 63 */
"	shlr2	r6\n"
"	shlr2	r6\n"
"	bf	.Lm_inv_near\n"
"	cmp/pl	r6\n"
"	shld	r12,r14\n"
"	movt	r7\n"
"	sub	r7,r6\n"
"	or	r7,r6\n"                 /* oddify toward zero */
"	shll2	r14\n"
"	cmp/gt	r11,r6\n"
"	bt	.Lm_hi\n"
".Lm_hi_back:\n"
"	bra	.Lm_tok\n"
"	add	r9,r14\n"
".Lm_hi:\n"                   /* magnitude that the sign step turns into 2047, or -2048 */
"	mov	r11,r6\n"
"	bra	.Lm_hi_back\n"
"	sub	r4,r6\n"
".Lm_inv_near:\n"
"	bra	.Lm_invalid\n"
"	nop\n"
".Lm_cross:\n"
"	mov	r2,r1\n"
"	shld	r3,r1\n"
"	mov.l	@r15,r2\n"
"	mov.l	@(8,r2),r0\n"
"	mov.l	@(4,r2),r2\n"
"	and	r8,r0\n"
"	mov.l	@(r0,r2),r2\n"
"	add	#4,r8\n"
"	swap.b	r2,r2\n"
"	swap.w	r2,r2\n"
"	swap.b	r2,r2\n"
"	bra	.Lm_consumed\n"
"	add	#-32,r3\n"
".Lm_x1:\n"
"	mov	r2,r1\n"
"	shld	r3,r1\n"
"	mov.l	@r15,r2\n"
"	mov.l	@(8,r2),r0\n"
"	mov.l	@(4,r2),r2\n"
"	and	r8,r0\n"
"	mov.l	@(r0,r2),r2\n"
"	add	#4,r8\n"
"	swap.b	r2,r2\n"
"	swap.w	r2,r2\n"
"	swap.b	r2,r2\n"
"	bra	.Lm_c1\n"
"	add	#-32,r3\n"
".Lm_x2:\n"
"	mov	r2,r1\n"
"	shld	r3,r1\n"
"	mov.l	@r15,r2\n"
"	mov.l	@(8,r2),r0\n"
"	mov.l	@(4,r2),r2\n"
"	and	r8,r0\n"
"	mov.l	@(r0,r2),r2\n"
"	add	#4,r8\n"
"	swap.b	r2,r2\n"
"	swap.w	r2,r2\n"
"	swap.b	r2,r2\n"
"	bra	.Lm_dcz\n"
"	add	#-32,r3\n"
".Lm_x3:\n"
"	mov	r2,r1\n"
"	shld	r3,r1\n"
"	mov.l	@r15,r2\n"
"	mov.l	@(8,r2),r0\n"
"	mov.l	@(4,r2),r2\n"
"	and	r8,r0\n"
"	mov.l	@(r0,r2),r2\n"
"	add	#4,r8\n"
"	swap.b	r2,r2\n"
"	swap.w	r2,r2\n"
"	swap.b	r2,r2\n"
"	bra	.Lm_c3\n"
"	add	#-32,r3\n"
".Lm_special2:\n"             /* r0 = kind */
"	tst	#2,r0\n"
"	bf	.Lm_esc\n"
"	tst	#4,r0\n"
"	bf	.Lm_long\n"
"	tst	#8,r0\n"
"	bf	.Lm_l3\n"
"	mov.l	@r15,r4\n"            /* not a valid code: C reports it */
"	mov.l	@(24,r4),r0\n"
"	sub	r0,r5\n"
"	shlr2	r5\n"
"	mov.l	r5,@(52,r4)\n"
"	bra	.Lm_exit\n"
"	mov	#1,r6\n"
".Lm_esc:\n"                  /* escape: 6 bits, run 6 bits, level 8 or 16 bits */
"	mov	r2,r0\n"
"	shld	r3,r0\n"
"	or	r1,r0\n"
"	mov	r0,r6\n"
"	shlr8	r6\n"
"	shlr2	r6\n"
"	shlr2	r6\n"
"	extu.b	r6,r14\n"
"	shlr8	r6\n"
"	mov	#63,r7\n"
"	and	r6,r7\n"
"	shll2	r7\n"
"	add	r7,r5\n"                /* this coefficient's qz entry */
"	tst	r14,r14\n"
"	bt	.Lm_esc0\n"
"	exts.b	r14,r6\n"
"	cmp/pz	r6\n"
"	bt	.Lm_esc_pos\n"
"	mov	r6,r14\n"
"	mov	#-128,r6\n"
"	cmp/eq	r6,r14\n"
"	bt	.Lm_esc128\n"
".Lm_esc_pos:\n"
"	mov	#20,r7\n"
".Lm_esc_enc:\n"              /* r14 = signed level, r7 = length */
"	mov	r14,r4\n"
"	shll	r4\n"
"	subc	r4,r4\n"               /* sign */
"	mov	r14,r6\n"
"	xor	r4,r6\n"
"	sub	r4,r6\n"                /* |level| */
"	add	r6,r6\n"
"	bra	.Lm_tokx\n"
"	add	#1,r6\n"
".Lm_esc0:\n"
"	shlr2	r0\n"
"	shlr2	r0\n"
"	extu.b	r0,r14\n"
"	bra	.Lm_esc_enc\n"
"	mov	#28,r7\n"
".Lm_esc128:\n"
"	shlr2	r0\n"
"	shlr2	r0\n"
"	extu.b	r0,r14\n"
"	mov.w	.Lm_256,r6\n"
"	sub	r6,r14\n"
"	bra	.Lm_esc_enc\n"
"	mov	#28,r7\n"
".Lm_long:\n"                 /* the code starts 000000: second-level table on the next 8 bits */
"	mov	r2,r0\n"
"	shld	r3,r0\n"
"	or	r1,r0\n"
"	mov.l	@r15,r4\n"
"	shll2	r0\n"
"	shll2	r0\n"
"	shll2	r0\n"
"	mov.l	@(60,r4),r7\n"
"	shlr16	r0\n"
"	shlr8	r0\n"
"	shll2	r0\n"
"	add	r7,r0\n"
"	bra	.Lm_tok_np\n"           /* up to 13 bits + sign: an ordinary token */
"	mov	r0,r14\n"
".Lm_l3:\n"                   /* starts with nine zeros: third level on bits 9..16 (l2 + 1024) */
"	mov	r2,r0\n"
"	shld	r3,r0\n"
"	or	r1,r0\n"
"	mov.l	@r15,r4\n"
"	shll8	r0\n"
"	shll	r0\n"
"	mov.l	@(60,r4),r7\n"
"	shlr16	r0\n"
"	shlr8	r0\n"
"	mov.w	.Lm_1024,r6\n"
"	shll2	r0\n"
"	add	r6,r7\n"
"	add	r7,r0\n"
"	bra	.Lm_tok_np\n"           /* 14 to 16 bits + sign */
"	mov	r0,r14\n"
".Lm_invalid:\n"
"	bra	.Lm_exit\n"
"	mov	#-1,r6\n"
".Lm_done:\n"
"	mov	#0,r6\n"
".Lm_exit:\n"
"	mov.l	@r15+,r4\n"
"	add	#-8,r8\n"
"	shll2	r8\n"
"	shll	r8\n"                  /* (widx - 8) * 8 */
"	add	#32,r3\n"
"	add	r3,r8\n"
"	mov.l	r8,@(0,r4)\n"         /* P */
"	mov	r6,r0\n"
"	mov.l	@r15+,r14\n"
"	mov.l	@r15+,r13\n"
"	mov.l	@r15+,r12\n"
"	mov.l	@r15+,r11\n"
"	mov.l	@r15+,r10\n"
"	mov.l	@r15+,r9\n"
"	rts\n"
"	mov.l	@r15+,r8\n"
"	.align	2\n"
".Lm_1024:\n"
"	.short	1024\n"
".Lm_256:\n"
"	.short	256\n"
"	.text\n"
);

// Macroblock-level codes: 8-bit tables (1 KB each) resolve almost every code in one lookup.
#define PLM_MB_LUT_BITS 8
// Tables used for every macroblock, in one block at a fixed place in the 16 KB direct-mapped
// D-cache. Without this, adding or resizing any static moved them, and when one landed on the same
// cache lines as the stack (which KOS puts just below 0x8d000000, the top of the cache's 16 KB
// window) decoding was up to 10% slower with no code change. The block takes cache offsets
// 0..~13.3 KB; the top 2.7 KB are left to the stack of a decoder running on the main thread.
#ifndef PLM_HOT_TABLE_ALIGN
#define PLM_HOT_TABLE_ALIGN 16384
#endif
typedef struct {
	plm_coef_lut_t coef_rest[1 << PLM_COEF_LUT_BITS];     // 0x0000 2 KB
	plm_coef_lut_t coef_first[1 << PLM_COEF_LUT_BITS];    // 0x0800 2 KB
	plm_coef_lut_t coef_l2[1 << PLM_COEF_L2_BITS];        // 0x1000 1 KB
	plm_coef_lut_t coef_l3[256];                          // 0x1400 1 KB, must follow coef_l2 (the assembly uses l2 + 1024)
	uint8_t clamp_wide[1024];                             // 0x1800 clamps -384..639
	plm_vlc_lut_t dct_size[3][1 << PLM_MB_LUT_BITS];      // 0x1c00 3 KB
	plm_vlc_lut_t cbp[1 << PLM_MB_LUT_BITS];              // 0x2800
	plm_vlc_lut_t mb_addr_inc[1 << PLM_MB_LUT_BITS];      // 0x2c00
	plm_vlc_lut_t motion[1 << PLM_MB_LUT_BITS];           // 0x3000
	float fidct_mtx[36] __attribute__((aligned(32)));     // 0x3400
	uint8_t fidct_zz_pos[128];                            // 0x3490 zigzag scan index -> float block position; 64..127: 0x80 (invalid)
} plm_hot_tables_t;
static plm_hot_tables_t plm_hot_tables __attribute__((aligned(PLM_HOT_TABLE_ALIGN)));
#define PLM_VIDEO_COEF_REST_LUT (plm_hot_tables.coef_rest)
#define PLM_VIDEO_COEF_FIRST_LUT (plm_hot_tables.coef_first)
#define PLM_VIDEO_COEF_L2_LUT (plm_hot_tables.coef_l2)
#define PLM_VIDEO_COEF_L3_LUT (plm_hot_tables.coef_l3)
#define plm_clamp_wide_table (plm_hot_tables.clamp_wide)
#define PLM_VIDEO_DCT_SIZE_LUT (plm_hot_tables.dct_size)
#define PLM_VIDEO_CBP_LUT (plm_hot_tables.cbp)
#define PLM_VIDEO_MB_ADDR_INC_LUT (plm_hot_tables.mb_addr_inc)
#define PLM_VIDEO_MOTION_LUT (plm_hot_tables.motion)
#define plm_fidct_mtx (plm_hot_tables.fidct_mtx)
#define plm_fidct_zz_pos (plm_hot_tables.fidct_zz_pos)
// Used once per picture type, or only for the rare codes longer than the coefficient tables.
__attribute__((aligned(32))) static plm_vlc_lut_t PLM_VIDEO_MB_TYPE_LUT[4][1 << PLM_MB_LUT_BITS];
__attribute__((aligned(32))) static plm_vlc_lut_t PLM_VIDEO_DCT_COEFF_LUT[1 << PLM_VLC_LUT_BITS];
static int plm_video_luts_ready = 0;

static void plm_video_init_luts(void) {
	if (plm_video_luts_ready) {
		return;
	}
	plm_vlc_lut_build((const plm_vlc_t *)PLM_VIDEO_DCT_COEFF, PLM_VIDEO_DCT_COEFF_LUT);
	plm_fidct_init(plm_fidct_mtx);
	for (int n = 0; n < 128; n++) plm_fidct_zz_pos[n] = n < 64 ? PLM_FIDCT_POS(PLM_VIDEO_ZIG_ZAG[n]) : 0x80;
	for (int v = 0; v < 1024; v++) plm_clamp_wide_table[v] = v < 384 ? 0 : v > 384 + 255 ? 255 : v - 384;
	plm_coef_lut_build((const plm_vlc_t *)PLM_VIDEO_DCT_COEFF, PLM_VIDEO_COEF_FIRST_LUT, 0);
	plm_coef_lut_build((const plm_vlc_t *)PLM_VIDEO_DCT_COEFF, PLM_VIDEO_COEF_REST_LUT, 1);
	plm_coef_ln_build((const plm_vlc_t *)PLM_VIDEO_DCT_COEFF, PLM_VIDEO_COEF_L2_LUT, 6, PLM_COEF_L2_BITS, PLM_COEF_L3);
	plm_coef_ln_build((const plm_vlc_t *)PLM_VIDEO_DCT_COEFF, PLM_VIDEO_COEF_L3_LUT, 9, 8, PLM_COEF_INV);   // 14- to 16-bit codes
	plm_vlc_lut_build_n(PLM_VIDEO_MACROBLOCK_ADDRESS_INCREMENT, PLM_VIDEO_MB_ADDR_INC_LUT, PLM_MB_LUT_BITS);
	for (int t = 1; t < 4; t++) {
		plm_vlc_lut_build_n(PLM_VIDEO_MACROBLOCK_TYPE[t], PLM_VIDEO_MB_TYPE_LUT[t], PLM_MB_LUT_BITS);
	}
	plm_vlc_lut_build_n(PLM_VIDEO_CODE_BLOCK_PATTERN, PLM_VIDEO_CBP_LUT, PLM_MB_LUT_BITS);
	plm_vlc_lut_build_n(PLM_VIDEO_MOTION, PLM_VIDEO_MOTION_LUT, PLM_MB_LUT_BITS);
	for (int t = 0; t < 3; t++) {
		plm_vlc_lut_build_n(PLM_VIDEO_DCT_SIZE[t], PLM_VIDEO_DCT_SIZE_LUT[t], PLM_MB_LUT_BITS);
	}
	plm_video_luts_ready = 1;
}

#ifndef PLM_VIDEO_DCACHE_OFFSET
#if defined(__sh__)
#define PLM_VIDEO_DCACHE_OFFSET 0x2000   // a multiple of 32; -1: wherever the allocator puts it
#else
#define PLM_VIDEO_DCACHE_OFFSET -1
#endif
#endif
#if PLM_VIDEO_DCACHE_OFFSET >= 0
typedef char plm_video_dcache_offset_check[(PLM_VIDEO_DCACHE_OFFSET % 32 == 0 && PLM_VIDEO_DCACHE_OFFSET < 16384) ? 1 : -1];
#endif
plm_video_t * plm_video_create_with_buffer(plm_buffer_t *buffer, int destroy_when_done) {
	plm_video_init_luts();
	// 32-byte aligned: mb_blocks' cache lines are allocated with movca.l, which must not share a
	// line with other fields
	// PLM_VIDEO_DCACHE_OFFSET: the struct (its tables and mb_blocks are read for every block) is put
	// at this offset within the SH-4's 16 KB direct-mapped data cache, because where malloc happens
	// to put it changes decode time by up to 15%. Measured on one 640x368 stream, decoding on the
	// main thread, struct start at each 1 KB step: 24.9 ms per frame from 0x1b00 to 0x2600,
	// 28.3-28.8 ms at 0x3000-0x3400, 25.4-27.0 ms elsewhere. Costs 16 KB.
	size_t place = 0;
#if PLM_VIDEO_DCACHE_OFFSET >= 0
	place = 16384;
#endif
	uint8_t *base = (uint8_t *)PLM_MEMALIGN(32, sizeof(plm_video_t) + place);
	if(!base) {
		fprintf(stderr, "Out of memory for self. [plm_video_create_with_buffer]\n");
		return NULL;
	}
	plm_video_t *self = (plm_video_t *)base;
#if PLM_VIDEO_DCACHE_OFFSET >= 0
	self = (plm_video_t *)(base + (((uintptr_t)PLM_VIDEO_DCACHE_OFFSET - (uintptr_t)base) & 16383));
#endif
	PLM_MEMZERO(self, sizeof(plm_video_t));
	self->alloc_base = base;
	for (int n = 0; n < 128; n++) {
		int16_t z4 = (int16_t)(n < 64 ? 4 * PLM_FIDCT_POS(PLM_VIDEO_ZIG_ZAG[n]) : -4);
		self->qz_intra[n][1] = z4;
		self->qz_non_intra[n][1] = z4;
	}
	self->qm_cached_scale[0] = self->qm_cached_scale[1] = -1;
	// The token decoder's context: the parts that never change
	self->mbc.first = PLM_VIDEO_COEF_FIRST_LUT;
	self->mbc.rest = PLM_VIDEO_COEF_REST_LUT;
	self->mbc.blocks = &self->mb_blocks[0][0];
	self->mbc.dcp = self->dc_predictor;
	self->mbc.dclut = PLM_VIDEO_DCT_SIZE_LUT;
	self->mbc.l2 = PLM_VIDEO_COEF_L2_LUT;

	self->buffer = buffer;
	self->destroy_buffer_when_done = destroy_when_done;

	// Attempt to decode the sequence header
	self->start_code = plm_buffer_find_start_code(self->buffer, PLM_START_SEQUENCE);
	if (self->start_code != -1) {
		plm_video_decode_sequence_header(self);
	}
	return self;
}

void plm_video_destroy(plm_video_t *self) {
	if(!self)
		return;

	if (self->destroy_buffer_when_done) {
		plm_buffer_destroy(self->buffer);
	}

	if (self->has_sequence_header) {
		PLM_FREE(self->frames_data);
		self->frames_data = NULL;
	}

	PLM_FREE(self->alloc_base);
}

double plm_video_get_framerate(plm_video_t *self) {
	return plm_video_has_header(self)
		? self->framerate
		: 0;
}

double plm_video_get_pixel_aspect_ratio(plm_video_t *self) {
	return plm_video_has_header(self)
		? self->pixel_aspect_ratio
		: 0;
}

int plm_video_get_width(plm_video_t *self) {
	return plm_video_has_header(self)
		? self->width
		: 0;
}

int plm_video_get_height(plm_video_t *self) {
	return plm_video_has_header(self)
		? self->height
		: 0;
}

void plm_video_set_skip_b_frames(plm_video_t *self, int skip) {
	self->skip_b_frames = skip;
}

void plm_video_set_skip_to_intra(plm_video_t *self) {
	self->skip_to_intra = 2;
}

void plm_video_set_slice_callback(plm_video_t *self, plm_video_slice_callback fp, void *user) {
	self->slice_callback = fp;
	self->slice_callback_user_data = user;
}

void plm_video_set_display_stride_exact(plm_video_t *self, int exact) {
	self->display_stride_exact = exact;
	if (!self->has_sequence_header) {
		return;   // applied when the sequence header is decoded
	}

	// The frames were allocated for the padded layout, which is the larger one
	int stride = self->mb_width;
	while (!exact && (stride & (stride - 1))) {
		stride += stride & -stride;
	}
	self->display_stride_mbs = stride;
	self->frame_current.display_stride_mbs = (unsigned int)stride;
	self->frame_forward.display_stride_mbs = (unsigned int)stride;
	self->frame_backward.display_stride_mbs = (unsigned int)stride;
}

void plm_video_set_no_delay(plm_video_t *self, int no_delay) {
	self->assume_no_b_frames = no_delay;
}

double plm_video_get_time(plm_video_t *self) {
	return self->time;
}

void plm_video_set_time(plm_video_t *self, double time) {
	self->frames_decoded = self->framerate * time;
	self->time = time;
}

void plm_video_rewind(plm_video_t *self) {
	plm_buffer_rewind(self->buffer);
	self->time = 0;
	self->frames_decoded = 0;
	self->has_reference_frame = FALSE;
	self->start_code = -1;
}

int plm_video_has_ended(plm_video_t *self) {
	return plm_buffer_has_ended(self->buffer);
}

static plm_frame_t *plm_video_decode_keep_xmtrx(plm_video_t *self);
plm_frame_t *plm_video_decode(plm_video_t *self) {
	float saved[16] __attribute__((aligned(8)));
	plm_xmtrx_save(saved);
	plm_frame_t *frame = plm_video_decode_keep_xmtrx(self);
	plm_xmtrx_restore(saved);
	return frame;
}
static plm_frame_t *plm_video_decode_keep_xmtrx(plm_video_t *self) {
	if (!plm_video_has_header(self)) {
		return NULL;
	}

	plm_frame_t *frame = NULL;
	do {
		if (self->start_code != PLM_START_PICTURE) {
			self->start_code = plm_buffer_find_start_code(self->buffer, PLM_START_PICTURE);

			if (self->start_code == -1) {
				// If we reached the end of the file and the previously decoded
				// frame was a reference frame, we still have to return it.
				if (
					self->has_reference_frame &&
					!self->assume_no_b_frames &&
					plm_buffer_has_ended(self->buffer) && (
						self->picture_type == PLM_VIDEO_PICTURE_TYPE_INTRA ||
						self->picture_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE
					)
				) {
					self->has_reference_frame = FALSE;
					frame = &self->frame_backward;
					break;
				}

				return NULL;
			}
		}

		// Make sure we have a full picture in the buffer before attempting to
		// decode it. Sadly, this can only be done by seeking for the start code
		// of the next picture. Also, if we didn't find the start code for the
		// next picture, but the source has ended, we assume that this last
		// picture is in the buffer.
		// With a load callback this scan is also what pulls the whole picture into the ring, which
		// the assembly bit reader (it never refills) relies on, and the end-of-stream handling
		// depends on its side effects. Only a buffer with no callback whose source has ended holds
		// all the data already; the scan cannot fail then and is skipped. It reads the whole
		// picture a byte or word at a time, about 0.7 ms per frame at 7.5 Mbit/s.
		if (
			!(plm_buffer_has_ended(self->buffer) && !self->buffer->load_callback) &&
			plm_buffer_has_start_code(self->buffer, PLM_START_PICTURE) == -1 &&
			!plm_buffer_has_ended(self->buffer)
		) {
			return NULL;
		}
		plm_buffer_discard_read_bytes(self->buffer);

		plm_video_decode_picture(self);

		if (self->picture_skipped) {
			// its display slot passes without a frame
			self->picture_skipped = FALSE;
			self->frames_decoded++;
			self->time = (double)self->frames_decoded / self->framerate;
			continue;
		}

		if (self->assume_no_b_frames) {
			frame = &self->frame_backward;
		}
		else if (self->picture_type == PLM_VIDEO_PICTURE_TYPE_B) {
			frame = &self->frame_current;
		}
		else if (self->has_reference_frame) {
			frame = &self->frame_forward;
		}
		else {
			self->has_reference_frame = TRUE;
		}
	} while (!frame);

	frame->time = self->time;
	self->frames_decoded++;
	self->time = (double)self->frames_decoded / self->framerate;

	return frame;
}

int plm_video_has_header(plm_video_t *self) {
	if (self->has_sequence_header) {
		return TRUE;
	}

	if (self->start_code != PLM_START_SEQUENCE) {
		self->start_code = plm_buffer_find_start_code(self->buffer, PLM_START_SEQUENCE);
	}
	if (self->start_code == -1) {
		return FALSE;
	}

	if (!plm_video_decode_sequence_header(self)) {
		return FALSE;
	}

	return TRUE;
}

int plm_video_decode_sequence_header(plm_video_t *self) {
	int max_header_size = 64 + 2 * 64 * 8; // 64 bit header + 2x 64 byte matrix
	if (!plm_buffer_has(self->buffer, max_header_size)) {
		return FALSE;
	}

	self->width = plm_buffer_read(self->buffer, 12);
	self->height = plm_buffer_read(self->buffer, 12);

	if (self->width <= 0 || self->height <= 0) {
		return FALSE;
	}

	// Get pixel aspect ratio
	int pixel_aspect_ratio_code;
	pixel_aspect_ratio_code = plm_buffer_read(self->buffer, 4);
	pixel_aspect_ratio_code -= 1;
	if (pixel_aspect_ratio_code < 0) {
		pixel_aspect_ratio_code = 0;
	}
	int par_last = (sizeof(PLM_VIDEO_PIXEL_ASPECT_RATIO) /
			sizeof(PLM_VIDEO_PIXEL_ASPECT_RATIO[0]) - 1);
	if (pixel_aspect_ratio_code > par_last) {
		pixel_aspect_ratio_code = par_last;
	}
	self->pixel_aspect_ratio =
		PLM_VIDEO_PIXEL_ASPECT_RATIO[pixel_aspect_ratio_code];

	self->framerate = PLM_VIDEO_PICTURE_RATE[plm_buffer_read(self->buffer, 4)];

	// Skip bit_rate, marker, buffer_size and constrained bit
	plm_buffer_skip(self->buffer, 18 + 1 + 10 + 1);

	// Load custom intra quant matrix?
	if (plm_buffer_read(self->buffer, 1)) {
		for (int i = 0; i < 64; i++) {
			int idx = PLM_VIDEO_ZIG_ZAG[i];
			self->intra_quant_matrix[idx] = plm_buffer_read(self->buffer, 8);
		}
	}
	else {
		plm_sq_copy_bytes(self->intra_quant_matrix, PLM_VIDEO_INTRA_QUANT_MATRIX, 64);
	}

	// Load custom non intra quant matrix?
	if (plm_buffer_read(self->buffer, 1)) {
		for (int i = 0; i < 64; i++) {
			int idx = PLM_VIDEO_ZIG_ZAG[i];
			self->non_intra_quant_matrix[idx] = plm_buffer_read(self->buffer, 8);
		}
	}
	else {
		plm_sq_copy_bytes(self->non_intra_quant_matrix, PLM_VIDEO_NON_INTRA_QUANT_MATRIX, 64);
	}

	self->mb_width = (self->width + 15) >> 4;
	self->mb_height = (self->height + 15) >> 4;
	self->mb_size = self->mb_width * self->mb_height;

	self->luma_width = self->mb_width << 4;
	self->luma_height = self->mb_height << 4;

	self->chroma_width = self->mb_width << 3;
	self->chroma_height = self->mb_height << 3;


	// Allocate one big chunk of data for all 3 frames = 9 planes
	size_t luma_plane_size = self->luma_width * self->luma_height;
	size_t chroma_plane_size = self->chroma_width * self->chroma_height;
	size_t frame_data_size = (luma_plane_size + 2 * chroma_plane_size);

	// Display rows (macroblock layout) are display_stride_mbs macroblocks apart: the texture's width
	// in macroblocks (next power of two), so the player can send a whole frame to the PVR YUV
	// converter in one DMA. The padding blocks at each row's end are part of the buffer and are
	// never written.
	// (plm_video_set_display_stride_exact() asks for unpadded rows; the memory is the same.)
	int stride = self->mb_width;
	while (stride & (stride - 1)) {
		stride += stride & -stride;
	}
	self->display_stride_mbs = self->display_stride_exact ? self->mb_width : stride;
	size_t display_size = (size_t)self->mb_height * (size_t)stride * 384;
	size_t frame_bytes = frame_data_size + display_size;

	// DCL DIFF
	self->frames_data = (uint8_t *)PLM_MEMALIGN(32, frame_bytes * 3);
	if(!self->frames_data) {
		fprintf(stderr, "Out of memory for self->frames_data. [plm_video_decode_sequence_header]\n");
		return FALSE;
	}

	plm_video_init_frame(self, &self->frame_current, self->frames_data + frame_bytes * 0);
	plm_video_init_frame(self, &self->frame_forward, self->frames_data + frame_bytes * 1);
	plm_video_init_frame(self, &self->frame_backward, self->frames_data + frame_bytes * 2);

	self->qm_cached_scale[0] = self->qm_cached_scale[1] = -1;   // matrices may have changed
	self->has_sequence_header = TRUE;
	return TRUE;
}

void plm_video_init_frame(plm_video_t *self, plm_frame_t *frame, uint8_t *base) {
	size_t luma_plane_size = self->luma_width * self->luma_height;
	size_t chroma_plane_size = self->chroma_width * self->chroma_height;

	frame->width = self->width;
	frame->height = self->height;
	frame->y.width = self->luma_width;
	frame->y.height = self->luma_height;
	frame->y.data = base;

	frame->cr.width = self->chroma_width;
	frame->cr.height = self->chroma_height;
	frame->cr.data = base + luma_plane_size;

	frame->cb.width = self->chroma_width;
	frame->cb.height = self->chroma_height;
	frame->cb.data = base + luma_plane_size + chroma_plane_size;

	// DCL DIFF
	frame->display = (uint32_t *)(base + luma_plane_size + chroma_plane_size * 2);
	frame->display_stride_mbs = (unsigned int)self->display_stride_mbs;
}

void plm_video_decode_picture(plm_video_t *self) {
	plm_buffer_skip(self->buffer, 10); // skip temporalReference
	self->picture_type = plm_buffer_read(self->buffer, 3);
	plm_buffer_skip(self->buffer, 16); // skip vbv_delay

	// D frames or unknown coding type
	if (self->picture_type <= 0 || self->picture_type > PLM_VIDEO_PICTURE_TYPE_B) {
		return;
	}

	// Dropped picture: plm_video_decode looks for the next one
	if (self->skip_to_intra == 2 && self->picture_type == PLM_VIDEO_PICTURE_TYPE_INTRA) {
		self->skip_to_intra = 1;
	}
	if (self->skip_to_intra == 1 && self->picture_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE) {
		self->skip_to_intra = 0;
	}
	if (
		self->skip_to_intra == 2 ||
		(self->picture_type == PLM_VIDEO_PICTURE_TYPE_B && (self->skip_b_frames || self->skip_to_intra))
	) {
		self->picture_skipped = TRUE;
		self->start_code = -1;
		return;
	}

	// Forward full_px, f_code
	if (
		self->picture_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE ||
		self->picture_type == PLM_VIDEO_PICTURE_TYPE_B
	) {
		self->motion_forward.full_px = plm_buffer_read(self->buffer, 1);
		int f_code = plm_buffer_read(self->buffer, 3);
		if (f_code == 0) {
			// Ignore picture with zero f_code
			return;
		}
		self->motion_forward.r_size = f_code - 1;
	}

	// Backward full_px, f_code
	if (self->picture_type == PLM_VIDEO_PICTURE_TYPE_B) {
		self->motion_backward.full_px = plm_buffer_read(self->buffer, 1);
		int f_code = plm_buffer_read(self->buffer, 3);
		if (f_code == 0) {
			// Ignore picture with zero f_code
			return;
		}
		self->motion_backward.r_size = f_code - 1;
	}

	plm_frame_t frame_temp = self->frame_forward;
	if (
		self->picture_type == PLM_VIDEO_PICTURE_TYPE_INTRA ||
		self->picture_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE
	) {
		self->frame_forward = self->frame_backward;
	}

	// Find first slice start code; skip extension and user data
	do {
		self->start_code = plm_buffer_next_start_code(self->buffer);
	} while (
		self->start_code == PLM_START_EXTENSION ||
		self->start_code == PLM_START_USER_DATA
	);

	// Decode all slices
	while (PLM_START_IS_SLICE(self->start_code)) {
		plm_video_decode_slice(self, self->start_code & 0x000000FF);
		if (self->slice_callback) {
			self->slice_callback(self, self->slice_callback_user_data);
		}
		if (self->macroblock_address >= self->mb_size - 1) {
			break;
		}
		self->start_code = plm_buffer_next_start_code(self->buffer);
	}

	// If this is a reference picture rotate the prediction pointers
	if (
		self->picture_type == PLM_VIDEO_PICTURE_TYPE_INTRA ||
		self->picture_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE
	) {
		self->frame_backward = self->frame_current;
		self->frame_current = frame_temp;
	}
}

PLM_HOT void plm_video_decode_slice(plm_video_t *self, int slice) {
	self->slice_begin = TRUE;
	self->macroblock_address = (slice - 1) * self->mb_width - 1;
	self->mb_row = slice - 1;
	self->mb_col = -1;

	// Reset motion vectors and DC predictors
	self->motion_backward.h = self->motion_forward.h = 0;
	self->motion_backward.v = self->motion_forward.v = 0;
	self->dc_predictor[0] = 128;
	self->dc_predictor[1] = 128;
	self->dc_predictor[2] = 128;

	self->quantizer_scale = plm_buffer_read(self->buffer, 5);

	// Skip extra
	while (plm_buffer_read(self->buffer, 1)) {
		plm_buffer_skip(self->buffer, 8);
	}

	do {
		plm_video_decode_macroblock(self);
	} while (
		self->macroblock_address < self->mb_size - 1 &&
		plm_buffer_peek_non_zero(self->buffer, 23)
	);
}

PLM_HOT void plm_video_decode_macroblock(plm_video_t *self) {
	// Decode increment
	int increment = 0;
	int t = plm_buffer_read_vlc_lut_n(self->buffer, PLM_VIDEO_MACROBLOCK_ADDRESS_INCREMENT, PLM_VIDEO_MB_ADDR_INC_LUT, PLM_MB_LUT_BITS);

	while (t == 34) {
		// macroblock_stuffing
		t = plm_buffer_read_vlc_lut_n(self->buffer, PLM_VIDEO_MACROBLOCK_ADDRESS_INCREMENT, PLM_VIDEO_MB_ADDR_INC_LUT, PLM_MB_LUT_BITS);
	}
	while (t == 35) {
		// macroblock_escape
		increment += 33;
		t = plm_buffer_read_vlc_lut_n(self->buffer, PLM_VIDEO_MACROBLOCK_ADDRESS_INCREMENT, PLM_VIDEO_MB_ADDR_INC_LUT, PLM_MB_LUT_BITS);
	}
	increment += t;

	// Process any skipped macroblocks
	if (self->slice_begin) {
		// The first increment of each slice is relative to beginning of the
		// previous row, not the previous macroblock
		self->slice_begin = FALSE;
		self->macroblock_address += increment;
		int col = self->mb_col + increment;
		self->mb_row += col / self->mb_width;
		self->mb_col = col % self->mb_width;
	}
	else {
		if (self->macroblock_address + increment >= self->mb_size) {
			return; // invalid
		}
		if (increment > 1) {
			// Skipped macroblocks reset DC predictors
			self->dc_predictor[0] = 128;
			self->dc_predictor[1] = 128;
			self->dc_predictor[2] = 128;

			// Skipped macroblocks in P-pictures reset motion vectors
			if (self->picture_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE) {
				self->motion_forward.h = 0;
				self->motion_forward.v = 0;
			}
		}

		// Predict skipped macroblocks
		while (increment > 1) {
			plm_video_advance_macroblock(self);
		plm_video_predict_macroblock(self);
			increment--;
		}
		plm_video_advance_macroblock(self);
	}

	if (
		self->mb_col < 0 ||
		self->mb_col >= self->mb_width ||
		self->mb_row < 0 ||
		self->mb_row >= self->mb_height
	) {
		return; // corrupt stream;
	}

	// Process the current macroblock
	const plm_vlc_t *table = PLM_VIDEO_MACROBLOCK_TYPE[self->picture_type];
	self->macroblock_type = plm_buffer_read_vlc_lut_n(self->buffer, table, PLM_VIDEO_MB_TYPE_LUT[self->picture_type], PLM_MB_LUT_BITS);

	self->macroblock_intra = (self->macroblock_type & 0x01);
	self->motion_forward.is_set = (self->macroblock_type & 0x08);
	self->motion_backward.is_set = (self->macroblock_type & 0x04);

	// Quantizer scale
	if ((self->macroblock_type & 0x10) != 0) {
		self->quantizer_scale = plm_buffer_read(self->buffer, 5);
	}

	if (self->macroblock_intra) {
		// Intra-coded macroblocks reset motion vectors
		self->motion_backward.h = self->motion_forward.h = 0;
		self->motion_backward.v = self->motion_forward.v = 0;
	}
	else {
		// Non-intra macroblocks reset DC predictors
		self->dc_predictor[0] = 128;
		self->dc_predictor[1] = 128;
		self->dc_predictor[2] = 128;

		plm_video_decode_motion_vectors(self);
		plm_video_predict_macroblock(self);
	}

	// Decode blocks
	int cbp = ((self->macroblock_type & 0x02) != 0)
		? plm_buffer_read_vlc_lut_n(self->buffer, PLM_VIDEO_CODE_BLOCK_PATTERN, PLM_VIDEO_CBP_LUT, PLM_MB_LUT_BITS)
		: (self->macroblock_intra ? 0x3f : 0);

	if (cbp != 0) {
		uint32_t *mb_display = self->frame_current.display + (self->mb_row * self->display_stride_mbs + self->mb_col) * 96;
		if (self->macroblock_intra) {
			plm_mb_alloc_lines(mb_display);   // intra: all six blocks overwrite the entry
		}
		if (!plm_video_decode_mb_coefs(self, cbp)) {
			return;   // invalid
		}
		for (int block = 0, mask = 0x20; block < 6; block++, mask >>= 1) {
			if ((cbp & mask) != 0) {
				plm_video_output_block(self, block, mb_display, self->mb_blocks[block], self->mbc.nb[block]);
			}
		}
	}
}

static __attribute__((noinline)) PLM_HOT int plm_video_decode_motion_vector(plm_video_t *self, int r_size, int motion) {   // one copy, not four inlined
	int fscale = 1 << r_size;
	int m_code = plm_buffer_read_vlc_lut_n(self->buffer, PLM_VIDEO_MOTION, PLM_VIDEO_MOTION_LUT, PLM_MB_LUT_BITS);
	int r = 0;
	int d;

	if ((m_code != 0) && (fscale != 1)) {
		r = plm_buffer_read(self->buffer, r_size);
		int m_code_abs = m_code < 0 ? -m_code : m_code;
		d = ((m_code_abs - 1) << r_size) + r + 1;
		if (m_code < 0) {
			d = -d;
		}
	}
	else {
		d = m_code;
	}

	motion += d;
	if (motion > (fscale << 4) - 1) {
		motion -= fscale << 5;
	}
	else if (motion < (int)((unsigned)(-fscale) << 4)) {
		motion += fscale << 5;
	}

	return motion;
}

PLM_HOT void plm_video_decode_motion_vectors(plm_video_t *self) {

	// Forward
	if (self->motion_forward.is_set) {
		int r_size = self->motion_forward.r_size;
		self->motion_forward.h = plm_video_decode_motion_vector(self, r_size, self->motion_forward.h);
		self->motion_forward.v = plm_video_decode_motion_vector(self, r_size, self->motion_forward.v);
	}
	else if (self->picture_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE) {
		// No motion information in P-picture, reset vectors
		self->motion_forward.h = 0;
		self->motion_forward.v = 0;
	}

	if (self->motion_backward.is_set) {
		int r_size = self->motion_backward.r_size;
		self->motion_backward.h = plm_video_decode_motion_vector(self, r_size, self->motion_backward.h);
		self->motion_backward.v = plm_video_decode_motion_vector(self, r_size, self->motion_backward.v);
	}
}

// DCL DIFF
PLM_HOT void plm_video_predict_macroblock(plm_video_t *self) {
	uint32_t *d = self->frame_current.display + (self->mb_row * self->display_stride_mbs + self->mb_col) * 96;
	plm_mb_alloc_lines(d);
	int fw_h = self->motion_forward.h;
	int fw_v = self->motion_forward.v;

	if (self->motion_forward.full_px) {
		fw_h <<= 1;
		fw_v <<= 1;
	}
	// DCL
	fw_h += fw_h < 0 ? ((self->mb_col - self->mb_width) << 5) : (self->mb_col << 5);
	fw_v += fw_v < 0 ? ((self->mb_row - self->mb_height) << 5) : (self->mb_row << 5);

	if (self->picture_type == PLM_VIDEO_PICTURE_TYPE_B) {
		int bw_h = self->motion_backward.h;
		int bw_v = self->motion_backward.v;

		if (self->motion_backward.full_px) {
			bw_h <<= 1;
			bw_v <<= 1;
		}
		// DCL
		bw_h += bw_h < 0 ? ((self->mb_col - self->mb_width) << 5) : (self->mb_col << 5);
		bw_v += bw_v < 0 ? ((self->mb_row - self->mb_height) << 5) : (self->mb_row << 5);

		if (self->motion_forward.is_set) {
			plm_video_copy_macroblock(d, &self->frame_forward, fw_h, fw_v);
			if (self->motion_backward.is_set) {
				plm_video_interpolate_macroblock(d, &self->frame_backward, bw_h, bw_v);
			}
		}
		else {
			plm_video_copy_macroblock(d, &self->frame_backward, bw_h, bw_v);
		}
	}
	else {
		plm_video_copy_macroblock(d, &self->frame_forward, fw_h, fw_v);
	}
}

// DCL DIFF
// Reference frames are read from their macroblock-ordered display buffer (U, V, Y0 Y1 Y2 Y3 per
// macroblock, each 8x8 block 64 contiguous bytes), so the planar Y/Cb/Cr copies are not needed.
// Any 8-pixel row inside an 8x8 block is 8 contiguous, 8-byte-aligned bytes.
static inline __attribute__((always_inline)) const uint32_t *plm_mb_luma_row(const uint32_t *disp, int mbw, int tx, int y) {
	const uint8_t *e = (const uint8_t *)(disp + ((y >> 4) * mbw + (tx >> 1)) * 96);
	return (const uint32_t *)(e + 128 + ((((y >> 3) & 1) << 1) + (tx & 1)) * 64 + (y & 7) * 8);
}
static inline __attribute__((always_inline)) const uint32_t *plm_mb_chroma_row(const uint32_t *disp, int mbw, int tx, int y, int plane) {
	const uint8_t *e = (const uint8_t *)(disp + ((y >> 3) * mbw + tx) * 96);
	return (const uint32_t *)(e + plane * 64 + (y & 7) * 8);
}
// Copy n 8-byte rows from one, two or three 8x8 blocks (rows 8 bytes apart) into a scratch with a
// 24- or 16-byte stride, 64 bits per move (FPSCR.SZ=1). dend points one past the first scratch row's
// last copied byte; stores go backwards, then adv moves to the next row's end.
#define PLM_GATHER_ASM_HEAD "fschg\n1:\n\t"
#define PLM_GATHER_ASM_TAIL "bf/s 1b\n\tadd %[adv],%[d]\n\tfschg\n"
static inline __attribute__((always_inline)) void plm_gather3(const uint8_t *a, const uint8_t *b, const uint8_t *c, uint8_t *d, int n, int adv) {
	__asm__ volatile(PLM_GATHER_ASM_HEAD
		"fmov @%[a]+,dr0\n\tfmov @%[b]+,dr2\n\tfmov @%[c]+,dr4\n\tdt %[n]\n\t"
		"fmov dr4,@-%[d]\n\tfmov dr2,@-%[d]\n\tfmov dr0,@-%[d]\n\t" PLM_GATHER_ASM_TAIL
		: [a]"+r"(a), [b]"+r"(b), [c]"+r"(c), [d]"+r"(d), [n]"+r"(n) : [adv]"r"(adv)
		: "fr0", "fr1", "fr2", "fr3", "fr4", "fr5", "memory", "t");
}
static inline __attribute__((always_inline)) void plm_gather2(const uint8_t *a, const uint8_t *b, uint8_t *d, int n, int adv) {
	__asm__ volatile(PLM_GATHER_ASM_HEAD
		"fmov @%[a]+,dr0\n\tfmov @%[b]+,dr2\n\tdt %[n]\n\t"
		"fmov dr2,@-%[d]\n\tfmov dr0,@-%[d]\n\t" PLM_GATHER_ASM_TAIL
		: [a]"+r"(a), [b]"+r"(b), [d]"+r"(d), [n]"+r"(n) : [adv]"r"(adv)
		: "fr0", "fr1", "fr2", "fr3", "memory", "t");
}
static inline __attribute__((always_inline)) void plm_gather1(const uint8_t *a, uint8_t *d, int n, int adv) {
	__asm__ volatile(PLM_GATHER_ASM_HEAD
		"fmov @%[a]+,dr0\n\tdt %[n]\n\t"
		"fmov dr0,@-%[d]\n\t" PLM_GATHER_ASM_TAIL
		: [a]"+r"(a), [d]"+r"(d), [n]"+r"(n) : [adv]"r"(adv)
		: "fr0", "fr1", "memory", "t");
}
// Luma source rows vp..vp+rows-1, block columns hp>>3 onward (ncols = 2 or 3), into dst (stride 24).
// Block rows are walked band by band (8 rows of one block row), so addresses are computed once per band.
static void plm_mb_gather_luma(const uint32_t *disp, int mbw, int hp, int vp, int rows, int ncols, uint8_t *dst) {
	int tx = hp >> 3;
	int bottom = (vp >> 3) & 1;
	const uint8_t *base = (const uint8_t *)disp + ((vp >> 4) * mbw + (tx >> 1)) * 384 + 128 + ((bottom << 1) + (tx & 1)) * 64;
	int s1 = (tx & 1) ? 320 : 64;     // column tx -> tx+1: next block in the macroblock, or the next macroblock's
	int s2 = 384 - s1;                // column tx+1 -> tx+2
	int down = mbw * 384 - 128;       // bottom band -> top band of the next macroblock row
	int r0 = vp & 7;
	for (;;) {
		int n = 8 - r0;
		if (n > rows) n = rows;
		const uint8_t *a = base + r0 * 8;
		if (ncols == 3) plm_gather3(a, a + s1, a + s1 + s2, dst + 24, n, 48);
		else plm_gather2(a, a + s1, dst + 16, n, 40);
		rows -= n;
		if (!rows) break;
		dst += n * 24;
		base += bottom ? down : 128;
		bottom ^= 1;
		r0 = 0;
	}
}
// Chroma rows of both planes (U at +0, V at +64 in each entry) into du / dv (stride 16).
static void plm_mb_gather_chroma(const uint32_t *disp, int mbw, int hp, int vp, int rows, int ncols, uint8_t *du, uint8_t *dv) {
	const uint8_t *base = (const uint8_t *)disp + ((vp >> 3) * mbw + (hp >> 3)) * 384;
	int down = mbw * 384;
	int r0 = vp & 7;
	for (;;) {
		int n = 8 - r0;
		if (n > rows) n = rows;
		const uint8_t *a = base + r0 * 8;
		if (ncols == 2) {
			plm_gather2(a, a + 384, du + 16, n, 32);
			plm_gather2(a + 64, a + 448, dv + 16, n, 32);
		} else {
			plm_gather1(a, du + 8, n, 24);
			plm_gather1(a + 64, dv + 8, n, 24);
		}
		rows -= n;
		if (!rows) break;
		du += n * 16;
		dv += n * 16;
		base += down;
		r0 = 0;
	}
}

// 8x8 prediction kernels: s = first source row at the prediction's offset, d = 64-byte destination
// block, stride = source row stride. copy, h: (a + b + 1) >> 1 across, v: down, hv: (a+b+c+d+2) >> 2.
typedef void (*plm_mc8_fn)(const uint8_t *s, uint8_t *d, int stride);
void plm_mc8_copy(const uint8_t *s, uint8_t *d, int stride);
void plm_mc8_h(const uint8_t *s, uint8_t *d, int stride);
void plm_mc8_v(const uint8_t *s, uint8_t *d, int stride);
void plm_mc8_hv(const uint8_t *s, uint8_t *d, int stride);
// The same four kernels on 32-bit words, four pixels at a time. Rows are read as aligned words and
// lined up with SH-4 shld, whose semantics make one formula cover every byte offset r: shld by -8r
// (0 when r == 0) and by 32 - 8r, or by -32 when r == 0 (shld by -32 gives 0; by 32 it would shift
// by 0). The pixel-to-the-right words come from the aligned ones with fixed shifts. Exact:
//   h, v:  (a + b + 1) >> 1 = (a | b) - (((a ^ b) >> 1) & 0x7f7f7f7f)   per byte
//   hv:    (a + b + c + d + 2) >> 2 = sum(x >> 2) + ((sum(x & 3) + 2) >> 2)   per byte
// They read up to 3 words of each row from the aligned address below s (bytes that the gather may
// not have written are shifted out). Rows: copy and h 8, v and hv 9.
static inline __attribute__((always_inline)) uint32_t plm_shld(uint32_t x, int n) {
	__asm__("shld %1,%0" : "+r"(x) : "r"(n));
	return x;
}
#define PLM_AVGU4(a, b) (((a) | (b)) - ((((a) ^ (b)) >> 1) & 0x7f7f7f7f))
#define PLM_MC8_SETUP \
	int r = (int)((uintptr_t)s & 3); \
	const uint32_t *p = (const uint32_t *)(s - r); \
	int ra = -8 * r, la = r ? 32 - 8 * r : -32; \
	int ws = stride >> 2; \
	uint32_t *o = (uint32_t *)d;
#define PLM_MC8_ROW(A0, A1, A2) do { \
	uint32_t w0_ = p[0], w1_ = p[1], w2_ = p[2]; \
	A0 = plm_shld(w0_, ra) | plm_shld(w1_, la); \
	A1 = plm_shld(w1_, ra) | plm_shld(w2_, la); \
	A2 = plm_shld(w2_, ra); \
	p += ws; \
} while (0)
static PLM_HOT void plm_mc8_copy_w(const uint8_t *s, uint8_t *d, int stride) {
	PLM_MC8_SETUP
	for (int y = 0; y < 8; y++) {
		uint32_t a0, a1, a2;
		PLM_MC8_ROW(a0, a1, a2);
		(void)a2;
		o[0] = a0; o[1] = a1; o += 2;
	}
}
static PLM_HOT void plm_mc8_h_w(const uint8_t *s, uint8_t *d, int stride) {
	PLM_MC8_SETUP
	for (int y = 0; y < 8; y++) {
		uint32_t a0, a1, a2;
		PLM_MC8_ROW(a0, a1, a2);
		uint32_t b0 = (a0 >> 8) | (a1 << 24), b1 = (a1 >> 8) | (a2 << 24);
		o[0] = PLM_AVGU4(a0, b0); o[1] = PLM_AVGU4(a1, b1); o += 2;
	}
}
static PLM_HOT void plm_mc8_v_w(const uint8_t *s, uint8_t *d, int stride) {
	PLM_MC8_SETUP
	uint32_t a0, a1, a2;
	PLM_MC8_ROW(a0, a1, a2);
	(void)a2;
	for (int y = 0; y < 8; y++) {
		uint32_t c0, c1, c2;
		PLM_MC8_ROW(c0, c1, c2);
		(void)c2;
		o[0] = PLM_AVGU4(a0, c0); o[1] = PLM_AVGU4(a1, c1); o += 2;
		a0 = c0; a1 = c1;
	}
}
static PLM_HOT void plm_mc8_hv_w(const uint8_t *s, uint8_t *d, int stride) {
	PLM_MC8_SETUP
	const uint32_t m3f = 0x3f3f3f3f, m03 = 0x03030303;
	uint32_t a0, a1, a2;
	PLM_MC8_ROW(a0, a1, a2);
	uint32_t b0 = (a0 >> 8) | (a1 << 24), b1 = (a1 >> 8) | (a2 << 24);
	uint32_t h0 = ((a0 >> 2) & m3f) + ((b0 >> 2) & m3f), l0 = (a0 & m03) + (b0 & m03);
	uint32_t h1 = ((a1 >> 2) & m3f) + ((b1 >> 2) & m3f), l1 = (a1 & m03) + (b1 & m03);
	for (int y = 0; y < 8; y++) {
		PLM_MC8_ROW(a0, a1, a2);
		b0 = (a0 >> 8) | (a1 << 24); b1 = (a1 >> 8) | (a2 << 24);
		uint32_t g0 = ((a0 >> 2) & m3f) + ((b0 >> 2) & m3f), k0 = (a0 & m03) + (b0 & m03);
		uint32_t g1 = ((a1 >> 2) & m3f) + ((b1 >> 2) & m3f), k1 = (a1 & m03) + (b1 & m03);
		o[0] = h0 + g0 + (((l0 + k0 + 0x02020202) >> 2) & m03);
		o[1] = h1 + g1 + (((l1 + k1 + 0x02020202) >> 2) & m03);
		o += 2;
		h0 = g0; l0 = k0; h1 = g1; l1 = k1;
	}
}
#undef PLM_MC8_ROW
#undef PLM_MC8_SETUP
static const plm_mc8_fn plm_mc8_tab[4] = {plm_mc8_copy_w, plm_mc8_h_w, plm_mc8_v_w, plm_mc8_hv_w};

PLM_HOT void plm_video_copy_macroblock(
	uint32_t *dest, plm_frame_t *reference, int motion_h, int motion_v
) {
	int dw = reference->y.width;
	int dh = reference->y.height;
	int hp = motion_h < 0 ? (dw + (motion_h >> 1)) : (motion_h >> 1);
	int vp = motion_v < 0 ? (dh + (motion_v >> 1)) : (motion_v >> 1);
	int odd_h = (motion_h & 1) == 1;
	int odd_v = (motion_v & 1) == 1;

	// A motion vector of whole macroblocks (zero motion included) predicts exactly another
	// macroblock of the reference frame, which its display buffer already holds as 384 contiguous
	// bytes in this layout (I and P frames write every entry; the player only reads it). One
	// contiguous copy reads 12 cache lines instead of ~32 half-used ones from the planes.
	if (!odd_h && !odd_v && !(hp & 15) && !(vp & 15)) {
		const uint32_t *ms = reference->display + ((vp >> 4) * (int)reference->display_stride_mbs + (hp >> 4)) * 96;
		// 12 lines, 64 bits per move (FPSCR.SZ=1); stores run backwards from each line's end.
		const uint32_t *pf = ms + 16;
		uint32_t *de = dest + 8;
		int n = 12;
		__asm__ volatile("fschg\n1:\n\t"
			"pref @%[pf]\n\t"
			"fmov @%[s]+,dr0\n\tfmov @%[s]+,dr2\n\tfmov @%[s]+,dr4\n\tfmov @%[s]+,dr6\n\t"
			"add #32,%[pf]\n\tdt %[n]\n\t"
			"fmov dr6,@-%[d]\n\tfmov dr4,@-%[d]\n\tfmov dr2,@-%[d]\n\tfmov dr0,@-%[d]\n\t"
			"bf/s 1b\n\tadd #64,%[d]\n\tfschg\n"
			: [s]"+r"(ms), [d]"+r"(de), [pf]"+r"(pf), [n]"+r"(n) :
			: "fr0", "fr1", "fr2", "fr3", "fr4", "fr5", "fr6", "fr7", "memory", "t");
		return;
	}

	uint8_t lscratch[18 * 24] __attribute__((aligned(8)));
	plm_mb_gather_luma(reference->display, (int)reference->display_stride_mbs, hp, vp, 16 + odd_v, ((hp & 7) | odd_h) ? 3 : 2, lscratch);
	int dw_real = dw;
	{
		const uint8_t *ls = lscratch + (hp & 7);
		uint8_t *yd = (uint8_t *)dest + 128;
		plm_mc8_fn k = plm_mc8_tab[(odd_v << 1) | odd_h];
		k(ls, yd, 24);
		k(ls + 8, yd + 64, 24);
		k(ls + 8 * 24, yd + 128, 24);
		k(ls + 8 * 24 + 8, yd + 192, 24);
		int cdw = dw_real >> 1, cdh = dh >> 1;
		int cmh = motion_h / 2, cmv = motion_v / 2;
		int chp = cmh < 0 ? (cdw + (cmh >> 1)) : (cmh >> 1);
		int cvp = cmv < 0 ? (cdh + (cmv >> 1)) : (cmv >> 1);
		int codd_h = (cmh & 1) == 1, codd_v = (cmv & 1) == 1;
		uint8_t cus[9 * 16] __attribute__((aligned(8)));
		uint8_t cvs[9 * 16] __attribute__((aligned(8)));
		plm_mb_gather_chroma(reference->display, (int)reference->display_stride_mbs, chp, cvp, 8 + codd_v, ((chp & 7) | codd_h) ? 2 : 1, cus, cvs);
		k = plm_mc8_tab[(codd_v << 1) | codd_h];
		k(cus + (chp & 7), (uint8_t *)dest, 16);
		k(cvs + (chp & 7), (uint8_t *)dest + 64, 16);
		return;
	}
	dw = 24;                       // the kernels below read the scratch with this stride
	uint8_t *src = lscratch;
	unsigned int si = hp & 7;

	// Y block
	dest += 32;
	__asm__("pref @%0" : : "r"(dest));

	if (odd_h && odd_v)
	{
		uint8_t *d = (uint8_t *)dest;
		uint8_t *s1 = (uint8_t *)(src + si);
		uint8_t *s2 = (uint8_t *)(src + si + dw);
		int scan = dw;
		for (int i = 2; i; i--)
		{
			for (int j = 8; j; j--)
			{
				d[0] = (s1[0] + s1[1] + s2[0] + s2[1] + 2) >> 2;
				d[1] = (s1[1] + s1[2] + s2[1] + s2[2] + 2) >> 2;
				d[2] = (s1[2] + s1[3] + s2[2] + s2[3] + 2) >> 2;
				d[3] = (s1[3] + s1[4] + s2[3] + s2[4] + 2) >> 2;
				d[4] = (s1[4] + s1[5] + s2[4] + s2[5] + 2) >> 2;
				d[5] = (s1[5] + s1[6] + s2[5] + s2[6] + 2) >> 2;
				d[6] = (s1[6] + s1[7] + s2[6] + s2[7] + 2) >> 2;
				d[7] = (s1[7] + s1[8] + s2[7] + s2[8] + 2) >> 2;
				d[64] = (s1[8] + s1[9] + s2[8] + s2[9] + 2) >> 2;
				d[65] = (s1[9] + s1[10] + s2[9] + s2[10] + 2) >> 2;
				d[66] = (s1[10] + s1[11] + s2[10] + s2[11] + 2) >> 2;
				d[67] = (s1[11] + s1[12] + s2[11] + s2[12] + 2) >> 2;
				d[68] = (s1[12] + s1[13] + s2[12] + s2[13] + 2) >> 2;
				d[69] = (s1[13] + s1[14] + s2[13] + s2[14] + 2) >> 2;
				d[70] = (s1[14] + s1[15] + s2[14] + s2[15] + 2) >> 2;
				d[71] = (s1[15] + s1[16] + s2[15] + s2[16] + 2) >> 2;
				d += 8;
				s1 += scan;
				s2 += scan;
			}
			d += 64;
		}
	}
	else if (odd_v)
	{
		uint8_t *d = (uint8_t *)dest;
		uint8_t *s1 = (uint8_t *)(src + si);
		uint8_t *s2 = (uint8_t *)(src + si + dw);
		int scan = dw;
		for (int i = 2; i; i--)
		{
			for (int j = 8; j; j--)
			{
				d[0] = (s1[0] + s2[0] + 1) >> 1;
				d[1] = (s1[1] + s2[1] + 1) >> 1;
				d[2] = (s1[2] + s2[2] + 1) >> 1;
				d[3] = (s1[3] + s2[3] + 1) >> 1;
				d[4] = (s1[4] + s2[4] + 1) >> 1;
				d[5] = (s1[5] + s2[5] + 1) >> 1;
				d[6] = (s1[6] + s2[6] + 1) >> 1;
				d[7] = (s1[7] + s2[7] + 1) >> 1;
				d[64] = (s1[8] + s2[8] + 1) >> 1;
				d[65] = (s1[9] + s2[9] + 1) >> 1;
				d[66] = (s1[10] + s2[10] + 1) >> 1;
				d[67] = (s1[11] + s2[11] + 1) >> 1;
				d[68] = (s1[12] + s2[12] + 1) >> 1;
				d[69] = (s1[13] + s2[13] + 1) >> 1;
				d[70] = (s1[14] + s2[14] + 1) >> 1;
				d[71] = (s1[15] + s2[15] + 1) >> 1;
				d += 8;
				s1 += scan;
				s2 += scan;
			}
			d += 64;
		}
	}
	else if (odd_h)
	{
		uint8_t *d = (uint8_t *)dest;
		uint8_t *s = (uint8_t *)(src + si);
		int scan = dw;
		for (int i = 2; i; i--)
		{
			for (int j = 8; j; j--)
			{
				d[0] = (s[0] + s[1] + 1) >> 1;
				d[1] = (s[1] + s[2] + 1) >> 1;
				d[2] = (s[2] + s[3] + 1) >> 1;
				d[3] = (s[3] + s[4] + 1) >> 1;
				d[4] = (s[4] + s[5] + 1) >> 1;
				d[5] = (s[5] + s[6] + 1) >> 1;
				d[6] = (s[6] + s[7] + 1) >> 1;
				d[7] = (s[7] + s[8] + 1) >> 1;
				d[64] = (s[8] + s[9] + 1) >> 1;
				d[65] = (s[9] + s[10] + 1) >> 1;
				d[66] = (s[10] + s[11] + 1) >> 1;
				d[67] = (s[11] + s[12] + 1) >> 1;
				d[68] = (s[12] + s[13] + 1) >> 1;
				d[69] = (s[13] + s[14] + 1) >> 1;
				d[70] = (s[14] + s[15] + 1) >> 1;
				d[71] = (s[15] + s[16] + 1) >> 1;
				d += 8;
				s += scan;
			}
			d += 64;
		}
	}
	else
	{
		// whole-pixel, any alignment: byte copy (aligned whole-MB moves take the display-copy path)
		{
			uint8_t *d = (uint8_t *)dest;
			uint8_t *s = (uint8_t *)(src + si);
			int scan = dw;
			for (int i = 2; i; i--)
			{
				for (int j = 8; j; j--)
				{
					d[0] = s[0];
					d[1] = s[1];
					d[2] = s[2];
					d[3] = s[3];
					d[4] = s[4];
					d[5] = s[5];
					d[6] = s[6];
					d[7] = s[7];
					d[64] = s[8];
					d[65] = s[9];
					d[66] = s[10];
					d[67] = s[11];
					d[68] = s[12];
					d[69] = s[13];
					d[70] = s[14];
					d[71] = s[15];
					d += 8;
					s += scan;
				}
				d += 64;
			}
		}
	}

	// Cb, Cr blocks
	dest -= 32;
	__asm__("pref @%0" : : "r"(dest));
	dw = dw_real;
	src = reference->cb.data;
	dw >>= 1;
	dh >>= 1;
	motion_h /= 2;
	motion_v /= 2;
	hp = motion_h < 0 ? (dw + (motion_h >> 1)) : (motion_h >> 1);
	vp = motion_v < 0 ? (dh + (motion_v >> 1)) : (motion_v >> 1);
	odd_h = (motion_h & 1) == 1;
	odd_v = (motion_v & 1) == 1;
	si = vp * dw + hp;
	uint8_t *src_cr = reference->cr.data;
	uint8_t uscratch[9 * 16] __attribute__((aligned(8)));
	uint8_t vscratch[9 * 16] __attribute__((aligned(8)));
	plm_mb_gather_chroma(reference->display, (int)reference->display_stride_mbs, hp, vp, 8 + odd_v, ((hp & 7) | odd_h) ? 2 : 1, uscratch, vscratch);
	src = uscratch;
	src_cr = vscratch;
	si = hp & 7;
	dw = 16;

	for (int i = 2; i; i--)
	{
		if (odd_h && odd_v)
		{
			uint8_t *d = (uint8_t *)dest;
			uint8_t *s1 = (uint8_t *)(src + si);
			uint8_t *s2 = (uint8_t *)(src + si + dw);
			int scan = dw;
			for (int j = 8; j; j--)
			{
				d[0] = (s1[0] + s1[1] + s2[0] + s2[1] + 2) >> 2;
				d[1] = (s1[1] + s1[2] + s2[1] + s2[2] + 2) >> 2;
				d[2] = (s1[2] + s1[3] + s2[2] + s2[3] + 2) >> 2;
				d[3] = (s1[3] + s1[4] + s2[3] + s2[4] + 2) >> 2;
				d[4] = (s1[4] + s1[5] + s2[4] + s2[5] + 2) >> 2;
				d[5] = (s1[5] + s1[6] + s2[5] + s2[6] + 2) >> 2;
				d[6] = (s1[6] + s1[7] + s2[6] + s2[7] + 2) >> 2;
				d[7] = (s1[7] + s1[8] + s2[7] + s2[8] + 2) >> 2;
				d += 8;
				s1 += scan;
				s2 += scan;
			}
		}
		else if (odd_v)
		{
			uint8_t *d = (uint8_t *)dest;
			uint8_t *s1 = (uint8_t *)(src + si);
			uint8_t *s2 = (uint8_t *)(src + si + dw);
			int scan = dw;
			for (int j = 8; j; j--)
			{
				d[0] = (s1[0] + s2[0] + 1) >> 1;
				d[1] = (s1[1] + s2[1] + 1) >> 1;
				d[2] = (s1[2] + s2[2] + 1) >> 1;
				d[3] = (s1[3] + s2[3] + 1) >> 1;
				d[4] = (s1[4] + s2[4] + 1) >> 1;
				d[5] = (s1[5] + s2[5] + 1) >> 1;
				d[6] = (s1[6] + s2[6] + 1) >> 1;
				d[7] = (s1[7] + s2[7] + 1) >> 1;
				d += 8;
				s1 += scan;
				s2 += scan;
			}
		}
		else if (odd_h)
		{
			uint8_t *d = (uint8_t *)dest;
			uint8_t *s = (uint8_t *)(src + si);
			int scan = dw;
			for (int j = 8; j; j--)
			{
				d[0] = (s[0] + s[1] + 1) >> 1;
				d[1] = (s[1] + s[2] + 1) >> 1;
				d[2] = (s[2] + s[3] + 1) >> 1;
				d[3] = (s[3] + s[4] + 1) >> 1;
				d[4] = (s[4] + s[5] + 1) >> 1;
				d[5] = (s[5] + s[6] + 1) >> 1;
				d[6] = (s[6] + s[7] + 1) >> 1;
				d[7] = (s[7] + s[8] + 1) >> 1;
				d += 8;
				s += scan;
			}
		}
		else
		{
			// whole-pixel, any alignment: byte copy
			{
				uint8_t *d = (uint8_t *)dest;
				uint8_t *s = (uint8_t *)(src + si);
				int scan = dw;
				for (int j = 8; j; j--)
				{
					d[0] = s[0];
					d[1] = s[1];
					d[2] = s[2];
					d[3] = s[3];
					d[4] = s[4];
					d[5] = s[5];
					d[6] = s[6];
					d[7] = s[7];
					d += 8;
					s += scan;
				}
			}
		}
		dest += 16;
		__asm__("pref @%0" : : "r"(dest));
		src = src_cr;
	}
}

// DCL DIFF
PLM_HOT void plm_video_interpolate_macroblock(
	uint32_t *dest, plm_frame_t *reference, int motion_h, int motion_v
) {
	__attribute__((aligned(8))) static uint32_t buffer[96];

	plm_video_copy_macroblock(buffer, reference, motion_h, motion_v);
	__asm__("pref @%0" : : "r"(dest));
	__asm__("pref @%0" : : "r"(buffer));
	for (int i = 0; i < 96; i += 8) {
		__asm__("pref @%0" : : "r"(dest + i + 8));
		__asm__("pref @%0" : : "r"(buffer + i + 8));
		dest[i + 0] = PLM_AVG4(dest[i + 0], buffer[i + 0]);
		dest[i + 1] = PLM_AVG4(dest[i + 1], buffer[i + 1]);
		dest[i + 2] = PLM_AVG4(dest[i + 2], buffer[i + 2]);
		dest[i + 3] = PLM_AVG4(dest[i + 3], buffer[i + 3]);
		dest[i + 4] = PLM_AVG4(dest[i + 4], buffer[i + 4]);
		dest[i + 5] = PLM_AVG4(dest[i + 5], buffer[i + 5]);
		dest[i + 6] = PLM_AVG4(dest[i + 6], buffer[i + 6]);
		dest[i + 7] = PLM_AVG4(dest[i + 7], buffer[i + 7]);
	}
}

// One token whose code is longer than the 9-bit table (never end_of_block or escape), at bit m->P,
// for block m->b at coefficient index m->n: the C tree walk and the same dequantisation as the assembly.
static int plm_video_long_token(plm_video_t *self, plm_mbc_t *m) {
	uint32_t P = m->P;
	const uint8_t *bb = m->base;
	uint32_t mask = m->mask;
	uint32_t byte = P >> 3, sh = P & 7;
	uint32_t win = plm_buffer_load_u32be(&bb[byte & mask]) << sh;
	if (sh) {
		win |= (uint32_t)bb[(byte + 4) & mask] >> (8 - sh);
	}
	plm_vlc_lut_t e = PLM_VIDEO_DCT_COEFF_LUT[win >> (32 - PLM_VLC_LUT_BITS)];
	int len;
	uint16_t coeff;
	if (e.leaf) {
		coeff = (uint16_t)e.value;
		len = e.len;
	}
	else {
		const plm_vlc_t *tree = (const plm_vlc_t *)PLM_VIDEO_DCT_COEFF;
		int index = e.value;
		plm_vlc_t st;
		len = PLM_VLC_LUT_BITS;
		while (TRUE) {
			st = tree[index + (int)((win >> (31 - len)) & 1)];
			len++;
			if (st.index <= 0) {
				break;
			}
			index = st.index;
		}
		coeff = (uint16_t)st.value;
	}
	if (coeff == 0xffff || coeff == 0x0001) {
		return 0;   // cannot happen: the assembly decodes these
	}
	int run = coeff >> 8;
	int level = coeff & 0xff;
	if ((win >> (31 - len)) & 1) {
		level = -level;
	}
	len++;
	int n = m->n + run;
	if (n < 0 || n >= 64) {
		return 0;
	}
	int z = plm_fidct_zz_pos[n];
	level = plm_dequant(level, m->qz[2 * n], !self->macroblock_intra);
	((plm_float_alias *)self->mb_blocks[m->b])[z] = (float)level;
	m->n = n + 1;
	m->P = P + len;
	return 1;
}

// All coefficients of the current macroblock into self->mb_blocks (see plm_sh4_mb_coefs).
static int plm_video_decode_mb_coefs(plm_video_t *self, int cbp) {
	int quantizer_scale = self->quantizer_scale;
	int intra = self->macroblock_intra != 0;
	int16_t (*qz)[2] = intra ? self->qz_intra : self->qz_non_intra;
	if (quantizer_scale != self->qm_cached_scale[intra]) {
		// Only this macroblock type's table: the other one is rebuilt if and when it is needed
		const uint8_t *w = intra ? self->intra_quant_matrix : self->non_intra_quant_matrix;
		for (int n = 0; n < 64; n++) {
			qz[n][0] = (int16_t)(quantizer_scale * w[PLM_VIDEO_ZIG_ZAG[n]]);
		}
		self->qm_cached_scale[intra] = (int16_t)quantizer_scale;
	}
	plm_buffer_t *buf = self->buffer;
	plm_mbc_t *m = &self->mbc;   // first, rest, blocks, dcp, dclut and l2 are set once, at creation
	uint32_t base_bits = (uint32_t)buf->read_byte_pos * 8;
	m->P = base_bits + (uint32_t)buf->bit_index;
	m->base = buf->bytes;
	m->mask = (uint32_t)(buf->capacity - 1);
	m->qz = &qz[0][0];
	m->ni = -intra;   // added to 2|level| + 1
	m->cbp = (uint32_t)cbp << 26;   // block 0 in bit 31; the assembly shifts the blocks out
	m->b = -1;
	m->resume = 0;
	int ok = 1;
	while (TRUE) {
		int status = plm_sh4_mb_coefs(m);
		if (status == 0) {
			break;
		}
		if (status < 0 || !plm_video_long_token(self, m)) {
			ok = 0;
			break;
		}
		m->resume = 1;
	}
	buf->bit_index = m->P - base_bits;
	return ok;
}

// IDCT of a block whose only coefficients are (0,0), (0,1) and (1,0) (n <= 3: 37-46% of the coded
// non-DC blocks at 4 Mbit/s). The same operations as plm_fidct_sh4_v2 on that input, with each FTRV
// reduced to its one non-zero product: pass 1 columns 0 and 1, pass 2 rows with the butterfly
// a = e + o, b = 2e - a and the magic number added last. No multiply-add contraction, so every
// product and sum is rounded as the assembly rounds it. Output in place, as the full IDCT's.
// Pass 2 when pass 1 left only columns 0 and 1 non-zero: every row's even and odd FTRV inputs have
// one non-zero entry, so each reduces to one product (as the full kernel rounds it).
static inline __attribute__((always_inline)) void plm_fidct_pass2_c01(const float *y0, const float *y1, float *blk, const float *mtx) {
	const float E0 = mtx[0], E1 = mtx[1], E2 = mtx[2], E3 = mtx[3];
	const float O0 = mtx[16], O1 = mtx[17], O2 = mtx[18], O3 = mtx[19];
	const float magic = mtx[32];
	plm_float_alias *out = (plm_float_alias *)blk;
	for (int r = 0; r < 8; r++, out += 8) {
		float u = y0[r], v = y1[r];
		float e0 = E0 * u, e1 = E1 * u, e2 = E2 * u, e3 = E3 * u;
		float o0 = O0 * v, o1 = O1 * v, o2 = O2 * v, o3 = O3 * v;
		float a0 = e0 + o0, a1 = e1 + o1, a2 = e2 + o2, a3 = e3 + o3;
		float b0 = (e0 + e0) - a0, b1 = (e1 + e1) - a1, b2 = (e2 + e2) - a2, b3 = (e3 + e3) - a3;
		out[0] = a0 + magic; out[1] = a1 + magic; out[2] = a2 + magic; out[3] = a3 + magic;
		out[7] = b0 + magic; out[6] = b1 + magic; out[5] = b2 + magic; out[4] = b3 + magic;
	}
}
static PLM_HOT __attribute__((optimize("fp-contract=off"))) void plm_fidct_sparse3(float *blk, const float *mtx) {
	const float E0 = mtx[0], E1 = mtx[1], E2 = mtx[2], E3 = mtx[3];
	const float O0 = mtx[16], O1 = mtx[17], O2 = mtx[18], O3 = mtx[19];
	float f00 = blk[PLM_FIDCT_POS(0)], f01 = blk[PLM_FIDCT_POS(1)], f10 = blk[PLM_FIDCT_POS(8)];
	float y0[8], y1[8];
#define PLM_SP3_P1(K, EK, OK) do { \
		float e = EK * f00, o = OK * f10, a = e + o, e1 = EK * f01; \
		y0[K] = a; y0[7 - K] = (e + e) - a; \
		y1[K] = e1; y1[7 - K] = (e1 + e1) - e1; \
	} while (0)
	PLM_SP3_P1(0, E0, O0); PLM_SP3_P1(1, E1, O1); PLM_SP3_P1(2, E2, O2); PLM_SP3_P1(3, E3, O3);
#undef PLM_SP3_P1
	plm_fidct_pass2_c01(y0, y1, blk, mtx);
}

PLM_HOT static void plm_video_output_block(plm_video_t *self, int block, uint32_t *mb_display, float *coefs, int n) {
	int block_offset = (block < 4) ? (32 + (block << 4)) : (block == 4 ? 0 : 16);
	uint32_t *display = mb_display + block_offset;
	plm_float_alias *fs = (plm_float_alias *)coefs;
	const uint8_t *clamp = plm_clamp_wide_table + 384;
	uint8_t *d = (uint8_t *)display;
	if (n == 1) {
		// DC only: every pixel is DC / 8, rounded half up
		int value = ((int)fs[0] + 4) >> 3;
		((uint32_t *)coefs)[0] = 0;
		if (self->macroblock_intra) {
			int clamped = clamp[value];
			clamped |= (clamped << 24) | (clamped << 16) | (clamped << 8);
			for (int y = 8; y; y--) {
				*display++ = clamped;
				*display++ = clamped;
			}
		}
		else {
			plm_sh4_out_dc(d, clamp + value);
		}
	}
	else {
		if (n <= 3) {
			plm_fidct_sparse3(coefs, plm_fidct_mtx);
		}
		else
		{
			float ftmp[64] __attribute__((aligned(8)));
			plm_fidct_sh4_v2(coefs, ftmp, (uint32_t *)coefs, plm_fidct_mtx);
		}
		const uint8_t *cl = (const uint8_t *)((uintptr_t)clamp - PLM_FIDCT_MAGIC_BITS);
		if (self->macroblock_intra) {
			plm_sh4_out_put(d, (uint32_t *)coefs, cl);
		}
		else {
			plm_sh4_out_add(d, (uint32_t *)coefs, cl);
		}
	}
}

// YCbCr conversion following the BT.601 standard:
// https://infogalactic.com/info/YCbCr#ITU-R_BT.601_conversion

#define PLM_PUT_PIXEL(RI, GI, BI, Y_OFFSET, DEST_OFFSET) \
	y = ((frame->y.data[y_index + Y_OFFSET]-16) * 76309) >> 16; \
	dest[d_index + DEST_OFFSET + RI] = plm_clamp(y + r); \
	dest[d_index + DEST_OFFSET + GI] = plm_clamp(y - g); \
	dest[d_index + DEST_OFFSET + BI] = plm_clamp(y + b);

#define PLM_DEFINE_FRAME_CONVERT_FUNCTION(NAME, BYTES_PER_PIXEL, RI, GI, BI) \
	void NAME(plm_frame_t *frame, uint8_t *dest, int stride) { \
		int cols = frame->width >> 1; \
		int rows = frame->height >> 1; \
		int yw = frame->y.width; \
		int cw = frame->cb.width; \
		for (int row = 0; row < rows; row++) { \
			int c_index = row * cw; \
			int y_index = row * 2 * yw; \
			int d_index = row * 2 * stride; \
			for (int col = 0; col < cols; col++) { \
				int y; \
				int cr = frame->cr.data[c_index] - 128; \
				int cb = frame->cb.data[c_index] - 128; \
				int r = (cr * 104597) >> 16; \
				int g = (cb * 25674 + cr * 53278) >> 16; \
				int b = (cb * 132201) >> 16; \
				PLM_PUT_PIXEL(RI, GI, BI, 0,      0); \
				PLM_PUT_PIXEL(RI, GI, BI, 1,      BYTES_PER_PIXEL); \
				PLM_PUT_PIXEL(RI, GI, BI, yw,     stride); \
				PLM_PUT_PIXEL(RI, GI, BI, yw + 1, stride + BYTES_PER_PIXEL); \
				c_index += 1; \
				y_index += 2; \
				d_index += 2 * BYTES_PER_PIXEL; \
			} \
		} \
	}

PLM_DEFINE_FRAME_CONVERT_FUNCTION(plm_frame_to_rgb,  3, 0, 1, 2)
PLM_DEFINE_FRAME_CONVERT_FUNCTION(plm_frame_to_bgr,  3, 2, 1, 0)
PLM_DEFINE_FRAME_CONVERT_FUNCTION(plm_frame_to_rgba, 4, 0, 1, 2)
PLM_DEFINE_FRAME_CONVERT_FUNCTION(plm_frame_to_bgra, 4, 2, 1, 0)
PLM_DEFINE_FRAME_CONVERT_FUNCTION(plm_frame_to_argb, 4, 1, 2, 3)
PLM_DEFINE_FRAME_CONVERT_FUNCTION(plm_frame_to_abgr, 4, 3, 2, 1)


#undef PLM_PUT_PIXEL
#undef PLM_DEFINE_FRAME_CONVERT_FUNCTION



// -----------------------------------------------------------------------------
// plm_audio implementation

// Based on kjmp2 by Martin J. Fiedler
// http://keyj.emphy.de/kjmp2/

static const int PLM_AUDIO_FRAME_SYNC = 0x7ff;

static const int PLM_AUDIO_MPEG_2_5 = 0x0;
static const int PLM_AUDIO_MPEG_2 = 0x2;
static const int PLM_AUDIO_MPEG_1 = 0x3;

static const int PLM_AUDIO_LAYER_III = 0x1;
static const int PLM_AUDIO_LAYER_II = 0x2;
static const int PLM_AUDIO_LAYER_I = 0x3;

static const int PLM_AUDIO_MODE_STEREO = 0x0;
static const int PLM_AUDIO_MODE_JOINT_STEREO = 0x1;
static const int PLM_AUDIO_MODE_DUAL_CHANNEL = 0x2;
static const int PLM_AUDIO_MODE_MONO = 0x3;

 __attribute__((aligned(32))) static const unsigned short PLM_AUDIO_SAMPLE_RATE[] = {
	44100, 48000, 32000, 0, // MPEG-1
	22050, 24000, 16000, 0  // MPEG-2
};

 __attribute__((aligned(32))) static const short PLM_AUDIO_BIT_RATE[] = {
	32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, // MPEG-1
	 8, 16, 24, 32, 40, 48,  56,  64,  80,  96, 112, 128, 144, 160  // MPEG-2
};

 __attribute__((aligned(32))) static const int PLM_AUDIO_SCALEFACTOR_BASE[] = {
	0x02000000, 0x01965FEA, 0x01428A30
};

 __attribute__((aligned(32))) static const float PLM_AUDIO_SYNTHESIS_WINDOW[] = {
	     0.0,     -0.5,     -0.5,     -0.5,     -0.5,     -0.5,
	    -0.5,     -1.0,     -1.0,     -1.0,     -1.0,     -1.5,
	    -1.5,     -2.0,     -2.0,     -2.5,     -2.5,     -3.0,
	    -3.5,     -3.5,     -4.0,     -4.5,     -5.0,     -5.5,
	    -6.5,     -7.0,     -8.0,     -8.5,     -9.5,    -10.5,
	   -12.0,    -13.0,    -14.5,    -15.5,    -17.5,    -19.0,
	   -20.5,    -22.5,    -24.5,    -26.5,    -29.0,    -31.5,
	   -34.0,    -36.5,    -39.5,    -42.5,    -45.5,    -48.5,
	   -52.0,    -55.5,    -58.5,    -62.5,    -66.0,    -69.5,
	   -73.5,    -77.0,    -80.5,    -84.5,    -88.0,    -91.5,
	   -95.0,    -98.0,   -101.0,   -104.0,    106.5,    109.0,
	   111.0,    112.5,    113.5,    114.0,    114.0,    113.5,
	   112.0,    110.5,    107.5,    104.0,    100.0,     94.5,
	    88.5,     81.5,     73.0,     63.5,     53.0,     41.5,
	    28.5,     14.5,     -1.0,    -18.0,    -36.0,    -55.5,
	   -76.5,    -98.5,   -122.0,   -147.0,   -173.5,   -200.5,
	  -229.5,   -259.5,   -290.5,   -322.5,   -355.5,   -389.5,
	  -424.0,   -459.5,   -495.5,   -532.0,   -568.5,   -605.0,
	  -641.5,   -678.0,   -714.0,   -749.0,   -783.5,   -817.0,
	  -849.0,   -879.5,   -908.5,   -935.0,   -959.5,   -981.0,
	 -1000.5,  -1016.0,  -1028.5,  -1037.5,  -1042.5,  -1043.5,
	 -1040.0,  -1031.5,   1018.5,   1000.0,    976.0,    946.5,
	   911.0,    869.5,    822.0,    767.5,    707.0,    640.0,
	   565.5,    485.0,    397.0,    302.5,    201.0,     92.5,
	   -22.5,   -144.0,   -272.5,   -407.0,   -547.5,   -694.0,
	  -846.0,  -1003.0,  -1165.0,  -1331.5,  -1502.0,  -1675.5,
	 -1852.5,  -2031.5,  -2212.5,  -2394.0,  -2576.5,  -2758.5,
	 -2939.5,  -3118.5,  -3294.5,  -3467.5,  -3635.5,  -3798.5,
	 -3955.0,  -4104.5,  -4245.5,  -4377.5,  -4499.0,  -4609.5,
	 -4708.0,  -4792.5,  -4863.5,  -4919.0,  -4958.0,  -4979.5,
	 -4983.0,  -4967.5,  -4931.5,  -4875.0,  -4796.0,  -4694.5,
	 -4569.5,  -4420.0,  -4246.0,  -4046.0,  -3820.0,  -3567.0,
	  3287.0,   2979.5,   2644.0,   2280.5,   1888.0,   1467.5,
	  1018.5,    541.0,     35.0,   -499.0,  -1061.0,  -1650.0,
	 -2266.5,  -2909.0,  -3577.0,  -4270.0,  -4987.5,  -5727.5,
	 -6490.0,  -7274.0,  -8077.5,  -8899.5,  -9739.0, -10594.5,
	-11464.5, -12347.0, -13241.0, -14144.5, -15056.0, -15973.5,
	-16895.5, -17820.0, -18744.5, -19668.0, -20588.0, -21503.0,
	-22410.5, -23308.5, -24195.0, -25068.5, -25926.5, -26767.0,
	-27589.0, -28389.0, -29166.5, -29919.0, -30644.5, -31342.0,
	-32009.5, -32645.0, -33247.0, -33814.5, -34346.0, -34839.5,
	-35295.0, -35710.0, -36084.5, -36417.5, -36707.5, -36954.0,
	-37156.5, -37315.0, -37428.0, -37496.0,  37519.0,  37496.0,
	 37428.0,  37315.0,  37156.5,  36954.0,  36707.5,  36417.5,
	 36084.5,  35710.0,  35295.0,  34839.5,  34346.0,  33814.5,
	 33247.0,  32645.0,  32009.5,  31342.0,  30644.5,  29919.0,
	 29166.5,  28389.0,  27589.0,  26767.0,  25926.5,  25068.5,
	 24195.0,  23308.5,  22410.5,  21503.0,  20588.0,  19668.0,
	 18744.5,  17820.0,  16895.5,  15973.5,  15056.0,  14144.5,
	 13241.0,  12347.0,  11464.5,  10594.5,   9739.0,   8899.5,
	  8077.5,   7274.0,   6490.0,   5727.5,   4987.5,   4270.0,
	  3577.0,   2909.0,   2266.5,   1650.0,   1061.0,    499.0,
	   -35.0,   -541.0,  -1018.5,  -1467.5,  -1888.0,  -2280.5,
	 -2644.0,  -2979.5,   3287.0,   3567.0,   3820.0,   4046.0,
	  4246.0,   4420.0,   4569.5,   4694.5,   4796.0,   4875.0,
	  4931.5,   4967.5,   4983.0,   4979.5,   4958.0,   4919.0,
	  4863.5,   4792.5,   4708.0,   4609.5,   4499.0,   4377.5,
	  4245.5,   4104.5,   3955.0,   3798.5,   3635.5,   3467.5,
	  3294.5,   3118.5,   2939.5,   2758.5,   2576.5,   2394.0,
	  2212.5,   2031.5,   1852.5,   1675.5,   1502.0,   1331.5,
	  1165.0,   1003.0,    846.0,    694.0,    547.5,    407.0,
	   272.5,    144.0,     22.5,    -92.5,   -201.0,   -302.5,
	  -397.0,   -485.0,   -565.5,   -640.0,   -707.0,   -767.5,
	  -822.0,   -869.5,   -911.0,   -946.5,   -976.0,  -1000.0,
	  1018.5,   1031.5,   1040.0,   1043.5,   1042.5,   1037.5,
	  1028.5,   1016.0,   1000.5,    981.0,    959.5,    935.0,
	   908.5,    879.5,    849.0,    817.0,    783.5,    749.0,
	   714.0,    678.0,    641.5,    605.0,    568.5,    532.0,
	   495.5,    459.5,    424.0,    389.5,    355.5,    322.5,
	   290.5,    259.5,    229.5,    200.5,    173.5,    147.0,
	   122.0,     98.5,     76.5,     55.5,     36.0,     18.0,
	     1.0,    -14.5,    -28.5,    -41.5,    -53.0,    -63.5,
	   -73.0,    -81.5,    -88.5,    -94.5,   -100.0,   -104.0,
	  -107.5,   -110.5,   -112.0,   -113.5,   -114.0,   -114.0,
	  -113.5,   -112.5,   -111.0,   -109.0,    106.5,    104.0,
	   101.0,     98.0,     95.0,     91.5,     88.0,     84.5,
	    80.5,     77.0,     73.5,     69.5,     66.0,     62.5,
	    58.5,     55.5,     52.0,     48.5,     45.5,     42.5,
	    39.5,     36.5,     34.0,     31.5,     29.0,     26.5,
	    24.5,     22.5,     20.5,     19.0,     17.5,     15.5,
	    14.5,     13.0,     12.0,     10.5,      9.5,      8.5,
	     8.0,      7.0,      6.5,      5.5,      5.0,      4.5,
	     4.0,      3.5,      3.5,      3.0,      2.5,      2.5,
	     2.0,      2.0,      1.5,      1.5,      1.0,      1.0,
	     1.0,      1.0,      0.5,      0.5,      0.5,      0.5,
	     0.5,      0.5
};

// Quantizer lookup, step 1: bitrate classes
 __attribute__((aligned(32))) static const uint8_t PLM_AUDIO_QUANT_LUT_STEP_1[2][16] = {
	// 32, 48, 56, 64, 80, 96,112,128,160,192,224,256,320,384 <- bitrate
	{ 0,  0,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,  2,  2 }, // mono
	// 16, 24, 28, 32, 40, 48, 56, 64, 80, 96,112,128,160,192 <- bitrate / chan
	{ 0,  0,  0,  0,  0,  0,  1,  1,  1,  2,  2,  2,  2,  2 } // stereo
};

// Quantizer lookup, step 2: bitrate class, sample rate -> B2 table idx, sblimit
#define PLM_AUDIO_QUANT_TAB_A (27 | 64)   // Table 3-B.2a: high-rate, sblimit = 27
#define PLM_AUDIO_QUANT_TAB_B (30 | 64)   // Table 3-B.2b: high-rate, sblimit = 30
#define PLM_AUDIO_QUANT_TAB_C 8           // Table 3-B.2c:  low-rate, sblimit =  8
#define PLM_AUDIO_QUANT_TAB_D 12          // Table 3-B.2d:  low-rate, sblimit = 12

 __attribute__((aligned(32))) static const uint8_t QUANT_LUT_STEP_2[3][3] = {
	//44.1 kHz,              48 kHz,                32 kHz
	{ PLM_AUDIO_QUANT_TAB_C, PLM_AUDIO_QUANT_TAB_C, PLM_AUDIO_QUANT_TAB_D }, // 32 - 48 kbit/sec/ch
	{ PLM_AUDIO_QUANT_TAB_A, PLM_AUDIO_QUANT_TAB_A, PLM_AUDIO_QUANT_TAB_A }, // 56 - 80 kbit/sec/ch
	{ PLM_AUDIO_QUANT_TAB_B, PLM_AUDIO_QUANT_TAB_A, PLM_AUDIO_QUANT_TAB_B }  // 96+	 kbit/sec/ch
};

// Quantizer lookup, step 3: B2 table, subband -> nbal, row index
// (upper 4 bits: nbal, lower 4 bits: row index)
 __attribute__((aligned(32))) static const uint8_t PLM_AUDIO_QUANT_LUT_STEP_3[3][32] = {
	// Low-rate table (3-B.2c and 3-B.2d)
	{
		0x44,0x44,
		0x34,0x34,0x34,0x34,0x34,0x34,0x34,0x34,0x34,0x34
	},
	// High-rate table (3-B.2a and 3-B.2b)
	{
		0x43,0x43,0x43,
		0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,
		0x31,0x31,0x31,0x31,0x31,0x31,0x31,0x31,0x31,0x31,0x31,0x31,
		0x20,0x20,0x20,0x20,0x20,0x20,0x20
	},
	// MPEG-2 LSR table (B.2 in ISO 13818-3)
	{
		0x45,0x45,0x45,0x45,
		0x34,0x34,0x34,0x34,0x34,0x34,0x34,
		0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,
		0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24
	}
};

// Quantizer lookup, step 4: table row, allocation[] value -> quant table index
 __attribute__((aligned(32))) static const uint8_t PLM_AUDIO_QUANT_LUT_STEP_4[6][16] = {
	{ 0, 1, 2, 17 },
	{ 0, 1, 2,  3, 4, 5, 6, 17 },
	{ 0, 1, 2,  3, 4, 5, 6,  7,  8,  9, 10, 11, 12, 13, 14, 17 },
	{ 0, 1, 3,  5, 6, 7, 8,  9, 10, 11, 12, 13, 14, 15, 16, 17 },
	{ 0, 1, 2,  4, 5, 6, 7,  8,  9, 10, 11, 12, 13, 14, 15, 17 },
	{ 0, 1, 2,  3, 4, 5, 6,  7,  8,  9, 10, 11, 12, 13, 14, 15 }
};

typedef struct plm_quantizer_spec_t {
	unsigned short levels;
	unsigned char group;
	unsigned char bits;
	unsigned short recip;   // 65536/levels + 1 (for fast div by levels)
	unsigned short scale;   // 65536 / (levels + 1) (for postmultiply)
	unsigned short adj_half; // ((levels + 1) >> 1) - 1
	float fscale;           // 65536 / levels: the exact ISO 11172-3 step (2 / levels), float requantisation
} plm_quantizer_spec_t;

 __attribute__((aligned(32))) static const plm_quantizer_spec_t PLM_AUDIO_QUANT_TAB[] = {
	//  levels, group, bits, recip,  scale, adj_half
	{     3, 1,  5, 21846, 16384,     1, 21845.3333f },  //  1
	{     5, 1,  7, 13108, 10923,     2, 13107.2f },  //  2
	{     7, 0,  3,  9363,  8192,     3, 9362.28571f },  //  3
	{     9, 1, 10,  7282,  6554,     4, 7281.77778f },  //  4
	{    15, 0,  4,  4370,  4096,     7, 4369.06667f },  //  5
	{    31, 0,  5,  2114,  2048,    15, 2114.06452f },  //  6
	{    63, 0,  6,  1041,  1024,    31, 1040.25397f },  //  7
	{   127, 0,  7,   516,   512,    63, 516.031496f },  //  8
	{   255, 0,  8,   257,   256,   127, 257.003922f },  //  9
	{   511, 0,  9,   128,   128,   255, 128.250489f },  // 10
	{  1023, 0, 10,    64,    64,   511, 64.0625611f },  // 11
	{  2047, 0, 11,    32,    32,  1023, 32.0156326f },  // 12
	{  4095, 0, 12,    16,    16,  2047, 16.0039072f },  // 13
	{  8191, 0, 13,     8,     8,  4095, 8.00097668f },  // 14
	{ 16383, 0, 14,     4,     4,  8191, 4.00024416f },  // 15
	{ 32767, 0, 15,     2,     2, 16383, 2.00006104f },  // 16
	{ 65535, 0, 16,     1,     1, 32767, 1.00001526f }   // 17
};

#define PLM_AUDIO_D_SCALE (-32767.0f / 1073741824.0f)   /* -U * 32767 / 2^30 (upstream: / 1090519040, 1.6% low) */
typedef float plm_audio_sample_t;
struct plm_audio_t {
	plm_buffer_t *buffer;
	int bound;
	int mode;
	int v_pos[2];
	int samplerate_index;
	int bitrate_index;

	const plm_quantizer_spec_t *allocation[2][32];
	uint8_t scale_factor_info[2][32];
	int scale_factor[2][32][3];
	plm_audio_sample_t sample[2][32][3];
	// Synthesis window and state first, then the PCM output: 4 + 8 + 4.6 KB. With the output in the
	// middle (as before) the 17.5 KB block wrapped the 16 KB D-cache onto V's end.
	float D[1024] __attribute__((aligned(32)));
	float V[2][1024];
	plm_samples_t samples;

	double time;
	int samples_decoded;
	int version;
	int layer;
	int next_frame_data_size;
	int has_header;
	int destroy_buffer_when_done;

	// CRI ADX (ADPCM) instead of MPEG audio, as in Sofdec (.sfd) files: see plm_audio_adx_header()
	int adx;
	int adx_channels;
	int adx_samplerate;
	int adx_coef[2];
	int adx_hist[2][2];
	int adx_ended;
};

static int plm_audio_adx_header(plm_audio_t *self);
static void plm_audio_adx_decode_frame(plm_audio_t *self);
static inline int plm_audio_rate(plm_audio_t *self) {
	return self->adx ? self->adx_samplerate : PLM_AUDIO_SAMPLE_RATE[self->samplerate_index];
}
int plm_audio_find_frame_sync(plm_audio_t *self);
int plm_audio_decode_header(plm_audio_t *self);
void plm_audio_decode_frame(plm_audio_t *self);
const plm_quantizer_spec_t *plm_audio_read_allocation(plm_audio_t *self, int sb, int tab3);
void plm_audio_read_samples(plm_audio_t *self, int ch, int sb, int part);

// Precomputed scalefactor values for sf=0..62.
// Each entry = (PLM_AUDIO_SCALEFACTOR_BASE[sf%3] + ((1<<(sf/3))>>1)) >> (sf/3)
// sf=63 maps to 0 (handled by bounds check or extra entry).
 __attribute__((aligned(32))) static const int PLM_AUDIO_SCALEFACTOR_TABLE[64] = {
	// sf=0..2 (shift=0)
	0x02000000, 0x01965FEA, 0x01428A30,
	// sf=3..5 (shift=1)
	0x01000000, 0x00CB2FF5, 0x00A14518,
	// sf=6..8 (shift=2)
	0x00800000, 0x006597FB, 0x0050A28C,
	// sf=9..11 (shift=3)
	0x00400000, 0x0032CBFD, 0x00285146,
	// sf=12..14 (shift=4)
	0x00200000, 0x001965FF, 0x001428A3,
	// sf=15..17 (shift=5)
	0x00100000, 0x000CB2FF, 0x000A1452,
	// sf=18..20 (shift=6)
	0x00080000, 0x00065980, 0x00050A29,
	// sf=21..23 (shift=7)
	0x00040000, 0x00032CC0, 0x00028514,
	// sf=24..26 (shift=8)
	0x00020000, 0x00019660, 0x0001428A,
	// sf=27..29 (shift=9)
	0x00010000, 0x0000CB30, 0x0000A145,
	// sf=30..32 (shift=10)
	0x00008000, 0x00006598, 0x000050A3,
	// sf=33..35 (shift=11)
	0x00004000, 0x000032CC, 0x00002851,
	// sf=36..38 (shift=12)
	0x00002000, 0x00001966, 0x00001429,
	// sf=39..41 (shift=13)
	0x00001000, 0x00000CB3, 0x00000A14,
	// sf=42..44 (shift=14)
	0x00000800, 0x00000659, 0x0000050A,
	// sf=45..47 (shift=15)
	0x00000400, 0x0000032D, 0x00000285,
	// sf=48..50 (shift=16)
	0x00000200, 0x00000196, 0x00000143,
	// sf=51..53 (shift=17)
	0x00000100, 0x000000CB, 0x000000A1,
	// sf=54..56 (shift=18)
	0x00000080, 0x00000066, 0x00000051,
	// sf=57..59 (shift=19)
	0x00000040, 0x00000033, 0x00000028,
	// sf=60..62 (shift=20)
	0x00000020, 0x00000019, 0x00000014,
	// sf=63 (special case: returns 0)
	0x00000000
};

static inline int plm_audio_decode_scalefactor_value(int sf) {
	return PLM_AUDIO_SCALEFACTOR_TABLE[sf];
}

plm_audio_t *plm_audio_create_with_buffer(plm_buffer_t *buffer, int destroy_when_done) {
	plm_audio_t *self = (plm_audio_t *)PLM_MEMALIGN(32, sizeof(plm_audio_t));
	if(!self) {
		fprintf(stderr, "Out of memory for self. [plm_audio_create_with_buffer]\n");
		return NULL;
	}
	PLM_MEMZERO(self, sizeof(plm_audio_t));

	self->samples.count = PLM_AUDIO_SAMPLES_PER_FRAME;
	self->buffer = buffer;
	self->destroy_buffer_when_done = destroy_when_done;
	self->samplerate_index = 3; // Indicates 0

	// Build a window table in a layout that's faster for the Dreamcast.
	// We write the synthesis window into self->D in the exact order the
	// decoder will read it (grab every 32nd value), and we write each chunk
	// twice so later code can read straight through without wrap-around.
	float *d = self->D;
	float *s = (float *)PLM_AUDIO_SYNTHESIS_WINDOW;
	for (int i = 0; i < 32; i++)
	{
		for (int j = 0; j < 2; j++)
		{
			*d++ = s[0] * PLM_AUDIO_D_SCALE;
			*d++ = s[32] * PLM_AUDIO_D_SCALE;
			*d++ = s[64] * PLM_AUDIO_D_SCALE;
			*d++ = s[96] * PLM_AUDIO_D_SCALE;
			*d++ = s[128] * PLM_AUDIO_D_SCALE;
			*d++ = s[160] * PLM_AUDIO_D_SCALE;
			*d++ = s[192] * PLM_AUDIO_D_SCALE;
			*d++ = s[224] * PLM_AUDIO_D_SCALE;
			*d++ = s[256] * PLM_AUDIO_D_SCALE;
			*d++ = s[288] * PLM_AUDIO_D_SCALE;
			*d++ = s[320] * PLM_AUDIO_D_SCALE;
			*d++ = s[352] * PLM_AUDIO_D_SCALE;
			*d++ = s[384] * PLM_AUDIO_D_SCALE;
			*d++ = s[416] * PLM_AUDIO_D_SCALE;
			*d++ = s[448] * PLM_AUDIO_D_SCALE;
			*d++ = s[480] * PLM_AUDIO_D_SCALE;
		}
		s++;
	}

	// Attempt to decode first header
	self->next_frame_data_size = plm_audio_decode_header(self);

	return self;
}

void plm_audio_destroy(plm_audio_t *self) {
	if(!self)
		return;

	if (self->destroy_buffer_when_done) {
		plm_buffer_destroy(self->buffer);
	}
	PLM_FREE(self);
}

int plm_audio_has_header(plm_audio_t *self) {
	if (self->has_header) {
		return TRUE;
	}

	self->next_frame_data_size = plm_audio_decode_header(self);
	return self->has_header;
}

int plm_audio_get_samplerate(plm_audio_t *self) {
	return plm_audio_has_header(self)
		? plm_audio_rate(self)
		: 0;
}

double plm_audio_get_time(plm_audio_t *self) {
	return self->time;
}

void plm_audio_set_time(plm_audio_t *self, double time) {
	self->samples_decoded = time *
		(double)plm_audio_rate(self);
	self->time = time;
}

void plm_audio_rewind(plm_audio_t *self) {
	plm_buffer_rewind(self->buffer);
	self->time = 0;
	self->samples_decoded = 0;
	self->next_frame_data_size = 0;
	// Start the synthesis filter as a new decoder does: with the previous pass's history the first
	// frame after a rewind came out different (up to 3732 of 32767 at a loop point: a click).
	memset(self->V, 0, sizeof(self->V));
	self->v_pos[0] = self->v_pos[1] = 0;
	// An ADX stream starts with its header again
	if (self->adx) {
		self->adx = 0;
		self->adx_ended = 0;
		self->has_header = FALSE;
	}
}

int plm_audio_has_ended(plm_audio_t *self) {
	return plm_buffer_has_ended(self->buffer);
}

plm_samples_t *plm_audio_decode(plm_audio_t *self) {
	if (!self || !self->buffer) {
		return NULL;
	}

	// Do we have at least enough information to decode the frame header?
	if (!self->next_frame_data_size) {
		if (!plm_buffer_has(self->buffer, 48)) {
			return NULL;
		}
		self->next_frame_data_size = plm_audio_decode_header(self);
	}

	if (self->next_frame_data_size == 0) {
		return NULL;
	}
	// One call asks the source for more data once, which may be a packet smaller than a frame:
	// keep asking while it delivers.
	while (!plm_buffer_has(self->buffer, self->next_frame_data_size << 3)) {
		size_t had = self->buffer->length;
		if (plm_buffer_has(self->buffer, self->next_frame_data_size << 3)) {
			break;
		}
		if (self->buffer->length <= had) {
			return NULL;
		}
	}

	if (self->adx) {
		plm_audio_adx_decode_frame(self);
	}
	else {
		plm_audio_decode_frame(self);
	}
	self->next_frame_data_size = 0;

	self->samples.time = self->time;

	self->samples_decoded += PLM_AUDIO_SAMPLES_PER_FRAME;
	self->time = (double)self->samples_decoded /
		(double)plm_audio_rate(self);

	return &self->samples;
}

// -----------------------------------------------------------------------------
// CRI ADX audio, the ADPCM format of Sofdec (.sfd) files, where it takes the place of the MPEG
// audio stream. Written from the published description of the format; no CRI code was used.
//
// Header (big endian): 80 00, u16 offset of the data minus 4, u8 encoding (3), u8 block size (18),
// u8 bits per sample (4), u8 channels, u32 sample rate, u32 sample count, u16 high-pass cutoff.
// Data: blocks of 18 bytes, one per channel in turn: u16 scale, then 32 samples of 4 bits, high
// nibble first. A scale with bit 15 set ends the stream.
//   sample = nibble * (scale + 1) + ((c1 * previous + c2 * the one before) >> 12), clamped
// (the + 1 is needed: real files have blocks with a stored scale of 0 and samples that are not).
// c1 and c2 follow from the cutoff and the sample rate.
//
// A frame of PLM_AUDIO_SAMPLES_PER_FRAME (1152) samples is 36 blocks per channel.

#define PLM_ADX_BLOCK 18
#define PLM_ADX_BLOCKS_PER_FRAME (PLM_AUDIO_SAMPLES_PER_FRAME / 32)

static inline int plm_audio_adx_byte(plm_buffer_t *b, size_t byte_off) {
	return *plm_buffer_ptr_from_read(b, (b->bit_index >> 3) + byte_off);
}

// Is the stream ADX? Reads the header if it is (returns 1), or returns -1 if more data is needed.
static int plm_audio_adx_header(plm_audio_t *self) {
	plm_buffer_t *b = self->buffer;
	if (plm_audio_adx_byte(b, 0) != 0x80 || plm_audio_adx_byte(b, 1) != 0x00) {
		return 0;
	}

	size_t data = (size_t)((plm_audio_adx_byte(b, 2) << 8) | plm_audio_adx_byte(b, 3)) + 4;
	if (data < 20) {
		return 0;
	}
	if (!plm_buffer_has(b, data << 3)) {
		return -1;
	}

	int channels = plm_audio_adx_byte(b, 7);
	int rate = (plm_audio_adx_byte(b, 8) << 24) | (plm_audio_adx_byte(b, 9) << 16) |
		(plm_audio_adx_byte(b, 10) << 8) | plm_audio_adx_byte(b, 11);
	int cutoff = (plm_audio_adx_byte(b, 16) << 8) | plm_audio_adx_byte(b, 17);
	if (
		plm_audio_adx_byte(b, 4) != 3 || plm_audio_adx_byte(b, 5) != PLM_ADX_BLOCK ||
		plm_audio_adx_byte(b, 6) != 4 || channels < 1 || channels > 2 || rate < 1000 || rate > 96000
	) {
		return 0;
	}

	// c = (a - sqrt((a + b)(a - b))) / b with a = sqrt(2) - cos(2 pi cutoff / rate), b = sqrt(2) - 1;
	// c1 = 2c, c2 = -c^2, in 1/4096. cos by its series (the angle is small), sqrt by Newton's
	// method, so no maths library is needed.
	double x = 6.283185307179586 * (double)cutoff / (double)rate;
	double x2 = x * x;
	double cosx = 1.0 - x2 / 2.0 + x2 * x2 / 24.0 - x2 * x2 * x2 / 720.0 + x2 * x2 * x2 * x2 / 40320.0;
	double a = 1.4142135623730951 - cosx;
	double bb = 1.4142135623730951 - 1.0;
	double v = (a + bb) * (a - bb);
	double r = v > 0.0 ? 1.0 : 0.0;
	for (int i = 0; i < 40 && r > 0.0; i++) {
		r = 0.5 * (r + v / r);
	}
	double c = (a - r) / bb;
	self->adx_coef[0] = (int)(c * 2.0 * 4096.0 + 0.5);
	self->adx_coef[1] = -(int)(c * c * 4096.0 + 0.5);

	self->adx = 1;
	self->adx_ended = 0;
	self->adx_channels = channels;
	self->adx_samplerate = rate;
	self->adx_hist[0][0] = self->adx_hist[0][1] = 0;
	self->adx_hist[1][0] = self->adx_hist[1][1] = 0;
	self->has_header = TRUE;
	b->bit_index += data << 3;
	return 1;
}

// Clamp to 16 bits; the test is one compare and almost never true
#define PLM_ADX_CLAMP(s) \
	if (__builtin_expect((unsigned)((s) + 32768) > 65535u, 0)) { \
		(s) = (s) < 0 ? -32768 : 32767; \
	}

// The 16 values a nibble can stand for: nibble (as a signed 4-bit number) times the multiplier.
// Built with additions, which leaves the multiplier to the predictor.
static inline void plm_audio_adx_table(int *t, int scale) {
	int v = 0;
	for (int i = 0; i < 8; i++) {
		t[i] = v;
		v += scale;
	}
	v = -8 * scale;
	for (int i = 8; i < 16; i++) {
		t[i] = v;
		v += scale;
	}
}

// The stereo block in assembly. Every sample needs two multiplies (c1 * the sample before,
// c2 * the one before that), and on the SH-4 a product has to be fetched from MACL before the
// next multiply: about 5 cycles per multiply at best, which is what bounds the loop. So:
// - c2 * (older sample) for the NEXT sample is computed as soon as its operand exists and waits
//   in r13 (left) / r14 (right), which keeps it off the path from one sample to the next;
// - the two channels alternate, each one's other work placed in the other's multiply time;
// - a sample is clamped out of line (it almost never is).
// 17 cycles per sample in the SH7091 pipeline model; the compiled loop measured about 25.
//
// void plm_adx_stereo_sh4(const uint8_t *in, short *out, int *ctx)
//   in:  the left block's 16 sample bytes; the right block's are 18 bytes on
//   out: 32 stereo samples
//   ctx: [0..3] left newest, left older, right newest, right older (updated); [4] c1, [5] c2;
//        [6..21] the left block's 16 nibble values, [22..37] the right block's
#if defined(__sh__)
void plm_adx_stereo_sh4(const uint8_t *in, short *out, int *ctx);
__asm__(
"	.section .text.plm_adx,\"ax\"\n"
"	.align	5\n"
"	.global	_plm_adx_stereo_sh4\n"
"_plm_adx_stereo_sh4:\n"
"	mov.l	r8,@-r15\n"
"	mov.l	r9,@-r15\n"
"	mov.l	r10,@-r15\n"
"	mov.l	r11,@-r15\n"
"	mov.l	r12,@-r15\n"
"	mov.l	r13,@-r15\n"
"	mov.l	r14,@-r15\n"
"	mov	r5,r0\n"
"	add	#64,r0\n"
"	add	#64,r0\n"
"	mov.l	r0,@-r15\n"          /* @(4,r15) in the loop: where the output ends */
"	mov.l	@(16,r6),r8\n"       /* c1 */
"	mov.l	@(20,r6),r9\n"       /* c2 */
"	mov	r6,r10\n"
"	add	#24,r10\n"            /* left nibble values */
"	mov	r10,r11\n"
"	add	#64,r11\n"            /* right nibble values */
"	mov	#-12,r12\n"
"	mov.l	@r6,r1\n"            /* left newest */
"	mov.l	@(4,r6),r2\n"        /* left older */
"	mov.l	@(8,r6),r3\n"        /* right newest */
"	mov.l	@(12,r6),r7\n"       /* right older */
"	mov.l	r6,@-r15\n"          /* @r15: ctx */
"	mov	r4,r6\n"
"	add	#18,r6\n"             /* the right block's bytes */
"	mul.l	r9,r2\n"
"	sts	macl,r13\n"
"	mul.l	r9,r7\n"
"	sts	macl,r14\n"
"	.align	2\n"
".Ladx_loop:\n"
"	mul.l	r8,r1\n"
"	mov.b	@r4,r0\n"
"	shlr2	r0\n"
"	and	#60,r0\n"
"	sts	macl,r2\n"
"	mul.l	r9,r1\n"
"	add	r13,r2\n"
"	mov.l	@(r0,r10),r0\n"
"	shad	r12,r2\n"
"	add	r0,r2\n"
"	sts	macl,r13\n"
"	mul.l	r8,r3\n"
"	exts.w	r2,r0\n"
"	cmp/eq	r0,r2\n"
"	bf	.Ladx_c1l\n"
".Ladx_r1l:\n"
"	mov.b	@r6,r0\n"
"	shlr2	r0\n"
"	and	#60,r0\n"
"	sts	macl,r7\n"
"	mul.l	r9,r3\n"
"	add	r14,r7\n"
"	mov.l	@(r0,r11),r0\n"
"	shad	r12,r7\n"
"	add	r0,r7\n"
"	sts	macl,r14\n"
"	exts.w	r7,r0\n"
"	cmp/eq	r0,r7\n"
"	bf	.Ladx_c1r\n"
".Ladx_r1r:\n"
"	mov.w	r2,@r5\n"
"	mov	r7,r0\n"
"	mov.w	r0,@(2,r5)\n"
"	mul.l	r8,r2\n"
"	mov.b	@r4+,r0\n"
"	shll2	r0\n"
"	and	#60,r0\n"
"	sts	macl,r1\n"
"	mul.l	r9,r2\n"
"	add	r13,r1\n"
"	mov.l	@(r0,r10),r0\n"
"	shad	r12,r1\n"
"	add	r0,r1\n"
"	sts	macl,r13\n"
"	mul.l	r8,r7\n"
"	exts.w	r1,r0\n"
"	cmp/eq	r0,r1\n"
"	bf	.Ladx_c2l\n"
".Ladx_r2l:\n"
"	mov.b	@r6+,r0\n"
"	shll2	r0\n"
"	and	#60,r0\n"
"	sts	macl,r3\n"
"	mul.l	r9,r7\n"
"	add	r14,r3\n"
"	mov.l	@(r0,r11),r0\n"
"	shad	r12,r3\n"
"	add	r0,r3\n"
"	sts	macl,r14\n"
"	exts.w	r3,r0\n"
"	cmp/eq	r0,r3\n"
"	bf	.Ladx_c2r\n"
".Ladx_r2r:\n"
"	mov	r1,r0\n"
"	mov.w	r0,@(4,r5)\n"
"	mov	r3,r0\n"
"	mov.w	r0,@(6,r5)\n"
"	add	#8,r5\n"
"	mov.l	@(4,r15),r0\n"
"	cmp/eq	r0,r5\n"
"	bf	.Ladx_loop\n"
"	mov.l	@r15+,r6\n"
"	add	#4,r15\n"
"	mov.l	r1,@r6\n"
"	mov.l	r2,@(4,r6)\n"
"	mov.l	r3,@(8,r6)\n"
"	mov.l	r7,@(12,r6)\n"
"	mov.l	@r15+,r14\n"
"	mov.l	@r15+,r13\n"
"	mov.l	@r15+,r12\n"
"	mov.l	@r15+,r11\n"
"	mov.l	@r15+,r10\n"
"	mov.l	@r15+,r9\n"
"	rts\n"
"	mov.l	@r15+,r8\n"
".Ladx_c1l:\n"
"	cmp/pz	r2\n"
"	mov.w	.Ladx_max,r2\n"
"	bt	.Ladx_r1l\n"
"	bra	.Ladx_r1l\n"
"	not	r2,r2\n"
".Ladx_c1r:\n"
"	cmp/pz	r7\n"
"	mov.w	.Ladx_max,r7\n"
"	bt	.Ladx_r1r\n"
"	bra	.Ladx_r1r\n"
"	not	r7,r7\n"
".Ladx_c2l:\n"
"	cmp/pz	r1\n"
"	mov.w	.Ladx_max,r1\n"
"	bt	.Ladx_r2l\n"
"	bra	.Ladx_r2l\n"
"	not	r1,r1\n"
".Ladx_c2r:\n"
"	cmp/pz	r3\n"
"	mov.w	.Ladx_max,r3\n"
"	bt	.Ladx_r2r\n"
"	bra	.Ladx_r2r\n"
"	not	r3,r3\n"
"	.align	1\n"
".Ladx_max:\n"
"	.word	32767\n"
"	.text\n"
);
#endif

// One block of each channel (in: the left block's 18 bytes, then the right's) to 32 stereo samples.
// The two channels are worked on together: each sample needs the one before it, so one channel
// alone is a single chain of multiplies, and two chains overlap.
static __attribute__((unused)) void plm_audio_adx_block_stereo(const uint8_t *in, short *out, int *hist, int c1, int c2) {
	int tl[16], tr[16];
	plm_audio_adx_table(tl, (((in[0] << 8) | in[1]) & 0x1fff) + 1);
	plm_audio_adx_table(tr, (((in[18] << 8) | in[19]) & 0x1fff) + 1);

	int l1 = hist[0], l2 = hist[1], r1 = hist[2], r2 = hist[3];
	for (int i = 2; i < 18; i++) {
		unsigned bl = in[i], br = in[i + 18];
		int l = tl[bl >> 4] + ((c1 * l1 + c2 * l2) >> 12);
		int r = tr[br >> 4] + ((c1 * r1 + c2 * r2) >> 12);
		PLM_ADX_CLAMP(l);
		PLM_ADX_CLAMP(r);
		out[0] = (short)l;
		out[1] = (short)r;
		l2 = tl[bl & 15] + ((c1 * l + c2 * l1) >> 12);
		r2 = tr[br & 15] + ((c1 * r + c2 * r1) >> 12);
		PLM_ADX_CLAMP(l2);
		PLM_ADX_CLAMP(r2);
		out[2] = (short)l2;
		out[3] = (short)r2;
		out += 4;
		// l2/r2 hold the newest sample: swap the roles for the next pair
		l1 = l2; l2 = l;
		r1 = r2; r2 = r;
	}
	hist[0] = l1; hist[1] = l2; hist[2] = r1; hist[3] = r2;
}

// One block to 32 samples, written to every second place of out
static void plm_audio_adx_block_mono(const uint8_t *in, short *out, int *hist, int c1, int c2) {
	int t[16];
	plm_audio_adx_table(t, (((in[0] << 8) | in[1]) & 0x1fff) + 1);

	int s1 = hist[0], s2 = hist[1];
	for (int i = 2; i < 18; i++) {
		unsigned byte = in[i];
		int s = t[byte >> 4] + ((c1 * s1 + c2 * s2) >> 12);
		PLM_ADX_CLAMP(s);
		out[0] = (short)s;
		s2 = t[byte & 15] + ((c1 * s + c2 * s1) >> 12);
		PLM_ADX_CLAMP(s2);
		out[2] = (short)s2;
		out += 4;
		s1 = s2; s2 = s;
	}
	hist[0] = s1; hist[1] = s2;
}

static void plm_audio_adx_decode_frame(plm_audio_t *self) {
	plm_buffer_t *b = self->buffer;
	short *pcm = self->samples.pcm;
	int channels = self->adx_channels;
	int c1 = self->adx_coef[0], c2 = self->adx_coef[1];
	size_t step = (size_t)(PLM_ADX_BLOCK * channels);

	for (int blk = 0; blk < PLM_ADX_BLOCKS_PER_FRAME; blk++, pcm += 64) {
		// The block(s) in one piece: the stream buffer is a ring
		uint8_t wrapped[2 * PLM_ADX_BLOCK];
		const uint8_t *in = plm_buffer_ptr_from_read(b, b->bit_index >> 3);
		if (__builtin_expect(in + step > b->bytes + b->capacity, 0)) {
			for (size_t i = 0; i < step; i++) {
				wrapped[i] = (uint8_t)plm_audio_adx_byte(b, i);
			}
			in = wrapped;
		}

		// A scale with bit 15 set is the end marker: silence from here on
		if (self->adx_ended || ((in[0] | in[step - PLM_ADX_BLOCK]) & 0x80)) {
			self->adx_ended = 1;
			memset(pcm, 0, 64 * sizeof(short));
			continue;
		}

		if (channels == 2) {
#if defined(__sh__)
			int ctx[6 + 32];
			ctx[0] = self->adx_hist[0][0]; ctx[1] = self->adx_hist[0][1];
			ctx[2] = self->adx_hist[1][0]; ctx[3] = self->adx_hist[1][1];
			ctx[4] = c1; ctx[5] = c2;
			plm_audio_adx_table(ctx + 6, (((in[0] << 8) | in[1]) & 0x1fff) + 1);
			plm_audio_adx_table(ctx + 22, (((in[18] << 8) | in[19]) & 0x1fff) + 1);
			plm_adx_stereo_sh4(in + 2, pcm, ctx);
			self->adx_hist[0][0] = ctx[0]; self->adx_hist[0][1] = ctx[1];
			self->adx_hist[1][0] = ctx[2]; self->adx_hist[1][1] = ctx[3];
#else
			plm_audio_adx_block_stereo(in, pcm, &self->adx_hist[0][0], c1, c2);
#endif
		}
		else {
			plm_audio_adx_block_mono(in, pcm, &self->adx_hist[0][0], c1, c2);
		}
		b->bit_index += step << 3;
	}

	if (channels == 1) {
		pcm = self->samples.pcm;
		for (int i = 0; i < PLM_AUDIO_SAMPLES_PER_FRAME; i++) {
			pcm[i * 2 + 1] = pcm[i * 2];
		}
	}
}

int plm_audio_find_frame_sync(plm_audio_t *self) {
    plm_buffer_t *b = self->buffer;

    size_t start = (size_t)(b->bit_index >> 3);
    if (b->length < 2 || start > b->length - 2) {
        b->bit_index = (uint32_t)(b->length << 3);
        return FALSE;
    }

    size_t i = start;
    const uint8_t *p = plm_buffer_ptr_from_read(b, start);

    // Scan pairs up to (length-2, length-1)
    for (; i < b->length - 1; i++) {
        // p[1] is always safe because of the 4-byte mirror at end
        if (p[0] == 0xFF && (p[1] & 0xFE) == 0xFC) {
            b->bit_index = (uint32_t)(((i + 1) << 3) + 3);
            return TRUE;
        }

        // Advance one byte with wrap
        p++;
        if (p == b->bytes + b->capacity) {
            p = b->bytes; // wrap to start
        }
    }

    b->bit_index = (uint32_t)((i + 1) << 3);
    return FALSE;
}

int plm_audio_decode_header(plm_audio_t *self) {
	if (!plm_buffer_has(self->buffer, 48)) {
		return 0;
	}

	// CRI ADX in place of MPEG audio (Sofdec files). One "frame" is 36 blocks per channel.
	if (!self->has_header && !self->adx) {
		int adx = plm_audio_adx_header(self);
		if (adx < 0) {
			return 0;
		}
	}
	if (self->adx) {
		return self->adx_ended ? 0 : PLM_ADX_BLOCKS_PER_FRAME * PLM_ADX_BLOCK * self->adx_channels;
	}

	plm_buffer_skip_bytes(self->buffer, 0x00);
	int sync = plm_buffer_read(self->buffer, 11);


	// Attempt to resync if no syncword was found. This sucks balls. The MP2
	// stream contains a syncword just before every frame (11 bits set to 1).
	// However, this syncword is not guaranteed to not occur elsewhere in the
	// stream. So, if we have to resync, we also have to check if the header
	// (samplerate, bitrate) differs from the one we had before. This all
	// may still lead to garbage data being decoded :/

	if (sync != PLM_AUDIO_FRAME_SYNC && !plm_audio_find_frame_sync(self)) {
		return 0;
	}

	self->version = plm_buffer_read(self->buffer, 2);
	self->layer = plm_buffer_read(self->buffer, 2);
	int hasCRC = !plm_buffer_read(self->buffer, 1);

	if (
		self->version != PLM_AUDIO_MPEG_1 ||
		self->layer != PLM_AUDIO_LAYER_II
	) {
		return 0;
	}

	int bitrate_index = plm_buffer_read(self->buffer, 4) - 1;
	if (bitrate_index < 0 || bitrate_index > 13) {
		return 0;
	}

	int samplerate_index = plm_buffer_read(self->buffer, 2);
	if (samplerate_index == 3) {
		return 0;
	}

	int padding = plm_buffer_read(self->buffer, 1);
	plm_buffer_skip(self->buffer, 1); // f_private
	int mode = plm_buffer_read(self->buffer, 2);

	// If we already have a header, make sure the samplerate, bitrate and mode
	// are still the same, otherwise we might have missed sync.
	if (
		self->has_header && (
			self->bitrate_index != bitrate_index ||
			self->samplerate_index != samplerate_index ||
			self->mode != mode
		)
	) {
		return 0;
	}

	self->bitrate_index = bitrate_index;
	self->samplerate_index = samplerate_index;
	self->mode = mode;
	self->has_header = TRUE;

	// Parse the mode_extension, set up the stereo bound
	if (mode == PLM_AUDIO_MODE_JOINT_STEREO) {
		self->bound = (plm_buffer_read(self->buffer, 2) + 1) << 2;
	}
	else {
		plm_buffer_skip(self->buffer, 2);
		self->bound = (mode == PLM_AUDIO_MODE_MONO) ? 0 : 32;
	}

	// Discard the last 4 bits of the header and the CRC value, if present
	plm_buffer_skip(self->buffer, 4); // copyright(1), original(1), emphasis(2)
	if (hasCRC) {
		plm_buffer_skip(self->buffer, 16);
	}

	// Compute frame size, check if we have enough data to decode the whole
	// frame.
	int bitrate = PLM_AUDIO_BIT_RATE[self->bitrate_index];
	int samplerate = PLM_AUDIO_SAMPLE_RATE[self->samplerate_index];
	int frame_size = (144000 * bitrate / samplerate) + padding;
	return frame_size - (hasCRC ? 6 : 4);
}

// The audio decoder's per-frame code in one section, so its functions are contiguous and cannot
// evict each other from the 8 KB direct-mapped I-cache (the window routine landing on idct36's
// lines cost 27k cycles per audio frame).
#define PLM_AUDIO_HOT __attribute__((section(".text.plm_audio")))
// One synthesis step's windowing for one channel: 16 iterations of two outputs, each the sum of four
// FIPRs over the transposed window (d) and the strided V entries (v1, v2), summed in the order of
// the port's original C loop (shz_pl_inner_loop, by Falco Girgis): ((s2 + s1) + s3) + s4, scaled,
// clamped and stored to out[0], out[2], ...
// (stereo-interleaved). Runs in the primary bank (fr12-fr15 saved) with one partial sum parked in
// FPUL, instead of two bank switches and a stack push/pop per output. Software-pipelined per the
// SH7091 latencies (a load reaches FIPR at D=3, FIPR results at D=4): each FIPR issues while the next
// group loads, and an output's conversion and store happen during the next output's loads. Outputs
// are k = 0..31 (d + 32k floats, v1/v2 + k, out + 2k shorts); the last iteration preloads one group
// past the end (reads only).
// The MP2 synthesis matrixing (pl_mpeg's plm_audio_idct36: a fast 32-point DCT, Lee's algorithm)
// in generated assembly. Same operations as pl_mpeg's C (x + y, x - y, x * c, -x, in the C's order
// for each value; no multiply-add contraction): bit-identical to that C compiled with
// -ffp-contract=off (GDB simulator, 2000 random blocks). Generated from the C's dataflow: statement
// order with first-stage butterflies sunk to their first use and stores raised to just after each
// final value, Belady register allocation (16 FP registers, 3 spills), constants read as one stream
// in use order, then the order refined on the ornio.nilware.io SH7091 pipeline model. Against
// GCC's build of the C, which contracts some multiply-adds into fmac, output differs in the last bit
// on about 0.01% of samples.
void plm_audio_idct36_sh4(plm_audio_sample_t s[32][3], int ss, float *d, int dp);
__asm__(
	"\t.section .text.plm_audio,\"ax\"\n"
	"\t.align 5\n"
	"\t.global _plm_audio_idct36_sh4\n"
	"_plm_audio_idct36_sh4:\n\t"
	"mov.l r8,@-r15\n\t"
	"fmov.s fr12,@-r15\n\t"
	"fmov.s fr13,@-r15\n\t"
	"fmov.s fr14,@-r15\n\t"
	"fmov.s fr15,@-r15\n\t"
	"add #-12,r15\n\t"
	"shll2 r5\n\t"
	"add r4,r5\n\t"
	"shll2 r7\n\t"
	"add r6,r7\n\t"
	"mov r7,r2\n\t"
	"mov r2,r3\n\t"
	"add #127,r3\n\t"
	"add #1,r3\n\t"
	"mov r5,r6\n\t"
	"add #127,r6\n\t"
	"add #1,r6\n\t"
	"mov r6,r7\n\t"
	"add #127,r7\n\t"
	"add #1,r7\n\t"
	"mov.l .Lca_idct36,r1\n\t"
	"bra .Lgo_idct36\n\t"
	"mov #0,r8\n\t"
	"\t.align 2\n"
	".Lca_idct36: .long .Lc_idct36\n"
	".Lgo_idct36:\n\t"
	"fmov.s @r1+,fr5\n\t"
	"mov #116,r0\n\t"
	"fmov.s @(r0,r7),fr1\n\t"
	"mov #28,r0\n\t"
	"fmov.s @(r0,r6),fr14\n\t"
	"mov #24,r0\n\t"
	"fmov.s @(r0,r5),fr12\n\t"
	"mov #76,r0\n\t"
	"fmov.s @(r0,r6),fr9\n\t"
	"mov #0,r0\n\t"
	"fmov.s @(r0,r5),fr0\n\t"
	"mov #88,r0\n\t"
	"fmov.s @(r0,r6),fr15\n\t"
	"mov #40,r0\n\t"
	"fmov.s @(r0,r6),fr8\n\t"
	"mov #52,r0\n\t"
	"fmov.s @(r0,r6),fr2\n\t"
	"mov #12,r0\n\t"
	"fmov.s @(r0,r5),fr6\n\t"
	"mov #64,r0\n\t"
	"fmov.s @(r0,r6),fr3\n\t"
	"mov #104,r0\n\t"
	"fmov.s @(r0,r7),fr7\n\t"
	"fadd fr15,fr14\n\t"
	"mov #68,r0\n\t"
	"fmov.s @(r0,r7),fr15\n\t"
	"mov #92,r0\n\t"
	"fmov.s @(r0,r7),fr13\n\t"
	"fadd fr1,fr0\n\t"
	"fadd fr9,fr8\n\t"
	"mov #100,r0\n\t"
	"fmov.s @(r0,r6),fr9\n\t"
	"fadd fr3,fr2\n\t"
	"fadd fr13,fr12\n\t"
	"fmov.s @r1+,fr11\n\t"
	"fadd fr7,fr6\n\t"
	"fmov fr0,fr4\n\t"
	"fadd fr2,fr4\n\t"
	"mov #16,r0\n\t"
	"fmov.s @(r0,r6),fr7\n\t"
	"fsub fr2,fr0\n\t"
	"fmov fr6,fr10\n\t"
	"fadd fr8,fr10\n\t"
	"fmul fr5,fr0\n\t"
	"fmov fr12,fr1\n\t"
	"fadd fr14,fr1\n\t"
	"mov #36,r0\n\t"
	"fmov.s @(r0,r5),fr2\n\t"
	"mov #80,r0\n\t"
	"fmov.s @(r0,r7),fr5\n\t"
	"fsub fr8,fr6\n\t"
	"fmov.s @r1+,fr3\n\t"
	"fadd fr9,fr7\n\t"
	"fsub fr14,fr12\n\t"
	"mov #48,r0\n\t"
	"fmov.s @(r0,r5),fr13\n\t"
	"mov #4,r0\n\t"
	"fmov.s @(r0,r6),fr14\n\t"
	"fadd fr5,fr2\n\t"
	"fmul fr3,fr12\n\t"
	"mov #112,r0\n\t"
	"fmov.s @(r0,r6),fr3\n\t"
	"fadd fr15,fr13\n\t"
	"fmul fr11,fr6\n\t"
	"fmov fr2,fr8\n\t"
	"fadd fr7,fr8\n\t"
	"fmov.s @r1+,fr11\n\t"
	"fmov.s @r1+,fr9\n\t"
	"fsub fr7,fr2\n\t"
	"fadd fr3,fr14\n\t"
	"mov #124,r0\n\t"
	"fmov.s @(r0,r6),fr3\n\t"
	"fmul fr11,fr2\n\t"
	"mov #56,r0\n\t"
	"fmov.s @(r0,r7),fr11\n\t"
	"fmov fr13,fr5\n\t"
	"fadd fr14,fr5\n\t"
	"mov #120,r0\n\t"
	"fmov.s @(r0,r5),fr15\n\t"
	"mov #60,r0\n\t"
	"fmov.s @(r0,r5),fr7\n\t"
	"fadd fr3,fr15\n\t"
	"fadd fr11,fr7\n\t"
	"mov #72,r0\n\t"
	"fmov.s @(r0,r5),fr11\n\t"
	"mov #44,r0\n\t"
	"fmov.s @(r0,r7),fr3\n\t"
	"fsub fr14,fr13\n\t"
	"fmov fr7,fr14\n\t"
	"fadd fr15,fr14\n\t"
	"fsub fr15,fr7\n\t"
	"fmul fr9,fr13\n\t"
	"mov #108,r0\n\t"
	"fmov.s @(r0,r5),fr15\n\t"
	"fmov.s @r1+,fr9\n\t"
	"fadd fr3,fr11\n\t"
	"fmul fr9,fr7\n\t"
	"mov #8,r0\n\t"
	"fmov.s @(r0,r7),fr9\n\t"
	"mov #0,r0\n\t"
	"fmov.s fr2,@(r0,r15)\n\t"
	"fadd fr9,fr15\n\t"
	"fmov.s @r1+,fr9\n\t"
	"fmov fr11,fr3\n\t"
	"fadd fr15,fr3\n\t"
	"fsub fr15,fr11\n\t"
	"mov #84,r0\n\t"
	"fmov.s @(r0,r5),fr15\n\t"
	"mov #20,r0\n\t"
	"fmov.s @(r0,r7),fr2\n\t"
	"fmul fr9,fr11\n\t"
	"mov #32,r0\n\t"
	"fmov.s @(r0,r7),fr9\n\t"
	"fadd fr9,fr15\n\t"
	"mov #96,r0\n\t"
	"fmov.s @(r0,r5),fr9\n\t"
	"fadd fr2,fr9\n\t"
	"fmov fr15,fr2\n\t"
	"fadd fr9,fr2\n\t"
	"fsub fr9,fr15\n\t"
	"fmov.s @r1+,fr9\n\t"
	"fmul fr9,fr15\n\t"
	"fmov fr4,fr9\n\t"
	"fadd fr2,fr9\n\t"
	"fsub fr2,fr4\n\t"
	"fmov.s @r1+,fr2\n\t"
	"fmul fr2,fr4\n\t"
	"fmov fr10,fr2\n\t"
	"fadd fr3,fr2\n\t"
	"fsub fr3,fr10\n\t"
	"fmov.s @r1+,fr3\n\t"
	"fmul fr3,fr10\n\t"
	"fmov fr1,fr3\n\t"
	"fadd fr14,fr3\n\t"
	"fsub fr14,fr1\n\t"
	"fmov.s @r1+,fr14\n\t"
	"fmul fr14,fr1\n\t"
	"fmov fr8,fr14\n\t"
	"fadd fr5,fr14\n\t"
	"fsub fr5,fr8\n\t"
	"fmov.s @r1+,fr5\n\t"
	"fmul fr5,fr8\n\t"
	"fmov fr9,fr5\n\t"
	"fadd fr14,fr5\n\t"
	"fsub fr14,fr9\n\t"
	"fmov.s @r1+,fr14\n\t"
	"fmul fr14,fr9\n\t"
	"fmov fr2,fr14\n\t"
	"fadd fr3,fr14\n\t"
	"fsub fr3,fr2\n\t"
	"fmov.s @r1+,fr3\n\t"
	"fmul fr3,fr2\n\t"
	"fmov fr5,fr3\n\t"
	"fadd fr14,fr3\n\t"
	"fsub fr14,fr5\n\t"
	"fneg fr3\n\t"
	"mov #64,r0\n\t"
	"fmov.s fr3,@(r0,r3)\n\t"
	"fmov.s @r1+,fr3\n\t"
	"fmul fr3,fr5\n\t"
	"fmov fr9,fr3\n\t"
	"fadd fr2,fr3\n\t"
	"mov #0,r0\n\t"
	"fmov.s fr5,@(r0,r2)\n\t"
	"fsub fr2,fr9\n\t"
	"fmov fr5,fr14\n\t"
	"fneg fr14\n\t"
	"mov #0,r0\n\t"
	"fmov.s fr14,@(r0,r3)\n\t"
	"fmov.s @r1+,fr14\n\t"
	"fmul fr14,fr9\n\t"
	"fmov.s @r1+,fr14\n\t"
	"fmov fr4,fr2\n\t"
	"fadd fr8,fr2\n\t"
	"fsub fr8,fr4\n\t"
	"fmov fr9,fr5\n\t"
	"fneg fr5\n\t"
	"mov #96,r0\n\t"
	"fmov.s fr5,@(r0,r2)\n\t"
	"mov #32,r0\n\t"
	"fmov.s fr9,@(r0,r2)\n\t"
	"fadd fr9,fr3\n\t"
	"fmov.s @r1+,fr9\n\t"
	"fmov fr10,fr5\n\t"
	"fadd fr1,fr5\n\t"
	"fmov.s @r1+,fr8\n\t"
	"fmul fr14,fr4\n\t"
	"fneg fr3\n\t"
	"fsub fr1,fr10\n\t"
	"mov #96,r0\n\t"
	"fmov.s fr3,@(r0,r3)\n\t"
	"mov #32,r0\n\t"
	"fmov.s fr3,@(r0,r3)\n\t"
	"fmul fr9,fr10\n\t"
	"fmov fr2,fr3\n\t"
	"fadd fr5,fr3\n\t"
	"fsub fr5,fr2\n\t"
	"fmov fr4,fr14\n\t"
	"fadd fr10,fr14\n\t"
	"fmov.s @r1+,fr1\n\t"
	"fsub fr10,fr4\n\t"
	"fmov.s @r1+,fr10\n\t"
	"fmul fr8,fr2\n\t"
	"fmov fr0,fr8\n\t"
	"fadd fr15,fr8\n\t"
	"fmul fr1,fr4\n\t"
	"fmov fr6,fr1\n\t"
	"fadd fr11,fr1\n\t"
	"mov #48,r0\n\t"
	"fmov.s fr4,@(r0,r2)\n\t"
	"fsub fr15,fr0\n\t"
	"fadd fr4,fr14\n\t"
	"fmov fr4,fr9\n\t"
	"fneg fr9\n\t"
	"mov #80,r0\n\t"
	"fmov.s fr9,@(r0,r2)\n\t"
	"fmov.s @r1+,fr9\n\t"
	"fadd fr14,fr3\n\t"
	"fadd fr2,fr14\n\t"
	"fadd fr4,fr2\n\t"
	"fneg fr3\n\t"
	"mov #16,r0\n\t"
	"fmov.s fr2,@(r0,r2)\n\t"
	"mov #80,r0\n\t"
	"fmov.s fr3,@(r0,r3)\n\t"
	"mov #48,r0\n\t"
	"fmov.s fr3,@(r0,r3)\n\t"
	"fmov fr12,fr3\n\t"
	"fadd fr7,fr3\n\t"
	"fneg fr14\n\t"
	"fmov fr2,fr5\n\t"
	"fneg fr5\n\t"
	"mov #112,r0\n\t"
	"fmov.s fr5,@(r0,r2)\n\t"
	"mov #16,r0\n\t"
	"fmov.s fr14,@(r0,r3)\n\t"
	"fsub fr7,fr12\n\t"
	"fsub fr11,fr6\n\t"
	"fmov fr1,fr11\n\t"
	"fadd fr3,fr11\n\t"
	"mov #0,r0\n\t"
	"fmov.s @(r0,r15),fr4\n\t"
	"mov #112,r0\n\t"
	"fmov.s fr14,@(r0,r3)\n\t"
	"fmul fr9,fr6\n\t"
	"fmul fr10,fr0\n\t"
	"fmov fr4,fr5\n\t"
	"fadd fr13,fr5\n\t"
	"fmov.s @r1+,fr14\n\t"
	"fmov.s @r1+,fr2\n\t"
	"fsub fr13,fr4\n\t"
	"fmul fr14,fr12\n\t"
	"fmov fr8,fr15\n\t"
	"fadd fr5,fr15\n\t"
	"fmov.s @r1+,fr10\n\t"
	"fsub fr3,fr1\n\t"
	"fsub fr5,fr8\n\t"
	"fmov fr6,fr3\n\t"
	"fadd fr12,fr3\n\t"
	"fsub fr12,fr6\n\t"
	"fmov.s @r1+,fr9\n\t"
	"fmov.s @r1+,fr14\n\t"
	"fmul fr2,fr4\n\t"
	"fmov.s @r1+,fr2\n\t"
	"fmul fr10,fr8\n\t"
	"fmul fr9,fr1\n\t"
	"fmov fr0,fr5\n\t"
	"fadd fr4,fr5\n\t"
	"fmov fr15,fr7\n\t"
	"fadd fr11,fr7\n\t"
	"fsub fr4,fr0\n\t"
	"fmov.s @r1+,fr10\n\t"
	"fsub fr11,fr15\n\t"
	"fmov.s @r1+,fr9\n\t"
	"fmov fr8,fr13\n\t"
	"fadd fr1,fr13\n\t"
	"fmul fr10,fr0\n\t"
	"fmov fr5,fr11\n\t"
	"fadd fr3,fr11\n\t"
	"fsub fr3,fr5\n\t"
	"mov #0,r0\n\t"
	"fmov.s @(r0,r5),fr3\n\t"
	"fmul fr9,fr6\n\t"
	"fmul fr14,fr15\n\t"
	"fsub fr1,fr8\n\t"
	"fmov fr0,fr1\n\t"
	"fadd fr6,fr1\n\t"
	"fmov.s @r1+,fr14\n\t"
	"fmul fr2,fr8\n\t"
	"fmov.s @r1+,fr2\n\t"
	"fsub fr6,fr0\n\t"
	"fmov.s @r1+,fr6\n\t"
	"fmul fr14,fr5\n\t"
	"mov #116,r0\n\t"
	"fmov.s @(r0,r7),fr14\n\t"
	"fmul fr2,fr0\n\t"
	"mov #52,r0\n\t"
	"fmov.s @(r0,r6),fr2\n\t"
	"mov #56,r0\n\t"
	"fmov.s fr0,@(r0,r2)\n\t"
	"fsub fr14,fr3\n\t"
	"fadd fr0,fr1\n\t"
	"fmov fr0,fr4\n\t"
	"fneg fr4\n\t"
	"fadd fr8,fr13\n\t"
	"mov #72,r0\n\t"
	"fmov.s fr4,@(r0,r2)\n\t"
	"fadd fr1,fr11\n\t"
	"fadd fr5,fr1\n\t"
	"mov #64,r0\n\t"
	"fmov.s @(r0,r6),fr4\n\t"
	"fmul fr6,fr3\n\t"
	"fadd fr11,fr7\n\t"
	"fadd fr13,fr11\n\t"
	"mov #24,r0\n\t"
	"fmov.s @(r0,r5),fr14\n\t"
	"fsub fr4,fr2\n\t"
	"fneg fr7\n\t"
	"mov #92,r0\n\t"
	"fmov.s @(r0,r7),fr6\n\t"
	"mov #72,r0\n\t"
	"fmov.s fr7,@(r0,r3)\n\t"
	"mov #56,r0\n\t"
	"fmov.s fr7,@(r0,r3)\n\t"
	"fadd fr0,fr5\n\t"
	"fadd fr1,fr13\n\t"
	"fneg fr11\n\t"
	"fmov.s @r1+,fr7\n\t"
	"mov #88,r0\n\t"
	"fmov.s fr11,@(r0,r3)\n\t"
	"mov #40,r0\n\t"
	"fmov.s fr11,@(r0,r3)\n\t"
	"fmul fr7,fr2\n\t"
	"fneg fr13\n\t"
	"mov #24,r0\n\t"
	"fmov.s fr13,@(r0,r3)\n\t"
	"fadd fr15,fr1\n\t"
	"fsub fr6,fr14\n\t"
	"fmov fr3,fr11\n\t"
	"fadd fr2,fr11\n\t"
	"mov #104,r0\n\t"
	"fmov.s fr13,@(r0,r3)\n\t"
	"fmov.s @r1+,fr13\n\t"
	"fadd fr5,fr15\n\t"
	"fneg fr1\n\t"
	"mov #8,r0\n\t"
	"fmov.s fr1,@(r0,r3)\n\t"
	"fadd fr8,fr5\n\t"
	"fsub fr2,fr3\n\t"
	"fmov fr15,fr10\n\t"
	"fneg fr10\n\t"
	"mov #120,r0\n\t"
	"fmov.s fr10,@(r0,r2)\n\t"
	"mov #104,r0\n\t"
	"fmov.s @(r0,r7),fr10\n\t"
	"mov #28,r0\n\t"
	"fmov.s @(r0,r6),fr7\n\t"
	"mov #8,r0\n\t"
	"fmov.s fr15,@(r0,r2)\n\t"
	"mov #88,r0\n\t"
	"fmov.s @(r0,r6),fr2\n\t"
	"fadd fr0,fr8\n\t"
	"fmov.s @r1+,fr15\n\t"
	"mov #100,r0\n\t"
	"fmov.s @(r0,r6),fr6\n\t"
	"mov #24,r0\n\t"
	"fmov.s fr5,@(r0,r2)\n\t"
	"fmov fr5,fr12\n\t"
	"fneg fr12\n\t"
	"mov #76,r0\n\t"
	"fmov.s @(r0,r6),fr5\n\t"
	"mov #120,r0\n\t"
	"fmov.s fr1,@(r0,r3)\n\t"
	"mov #104,r0\n\t"
	"fmov.s fr12,@(r0,r2)\n\t"
	"mov #40,r0\n\t"
	"fmov.s @(r0,r6),fr12\n\t"
	"mov #12,r0\n\t"
	"fmov.s @(r0,r5),fr1\n\t"
	"fsub fr5,fr12\n\t"
	"fmov.s @r1+,fr0\n\t"
	"fmov fr8,fr9\n\t"
	"fneg fr9\n\t"
	"fsub fr10,fr1\n\t"
	"fmul fr0,fr12\n\t"
	"mov #40,r0\n\t"
	"fmov.s fr8,@(r0,r2)\n\t"
	"mov #88,r0\n\t"
	"fmov.s fr9,@(r0,r2)\n\t"
	"mov #36,r0\n\t"
	"fmov.s @(r0,r5),fr5\n\t"
	"fmul fr15,fr1\n\t"
	"fmov.s @r1+,fr8\n\t"
	"fsub fr2,fr7\n\t"
	"fmov.s @r1+,fr4\n\t"
	"fmul fr13,fr3\n\t"
	"fmov fr1,fr9\n\t"
	"fadd fr12,fr9\n\t"
	"fmov.s @r1+,fr13\n\t"
	"fmul fr4,fr14\n\t"
	"mov #80,r0\n\t"
	"fmov.s @(r0,r7),fr0\n\t"
	"fmul fr13,fr7\n\t"
	"fsub fr12,fr1\n\t"
	"fmov.s @r1+,fr15\n\t"
	"fsub fr0,fr5\n\t"
	"fmov fr14,fr10\n\t"
	"fadd fr7,fr10\n\t"
	"fmov.s @r1+,fr12\n\t"
	"fmul fr8,fr1\n\t"
	"mov #16,r0\n\t"
	"fmov.s @(r0,r6),fr8\n\t"
	"fsub fr7,fr14\n\t"
	"fmul fr12,fr5\n\t"
	"fmov.s @r1+,fr4\n\t"
	"fsub fr6,fr8\n\t"
	"mov #112,r0\n\t"
	"fmov.s @(r0,r6),fr6\n\t"
	"fmov.s @r1+,fr13\n\t"
	"mov #4,r0\n\t"
	"fmov.s @(r0,r6),fr12\n\t"
	"fmul fr4,fr8\n\t"
	"fmul fr15,fr14\n\t"
	"mov #68,r0\n\t"
	"fmov.s @(r0,r7),fr15\n\t"
	"mov #48,r0\n\t"
	"fmov.s @(r0,r5),fr7\n\t"
	"fsub fr6,fr12\n\t"
	"fmov.s @r1+,fr0\n\t"
	"fsub fr15,fr7\n\t"
	"fmov.s @r1+,fr4\n\t"
	"fmov fr5,fr2\n\t"
	"fadd fr8,fr2\n\t"
	"mov #60,r0\n\t"
	"fmov.s @(r0,r5),fr15\n\t"
	"fsub fr8,fr5\n\t"
	"fmul fr0,fr7\n\t"
	"mov #56,r0\n\t"
	"fmov.s @(r0,r7),fr0\n\t"
	"fmul fr4,fr12\n\t"
	"fsub fr0,fr15\n\t"
	"fmov fr7,fr8\n\t"
	"fadd fr12,fr8\n\t"
	"mov #120,r0\n\t"
	"fmov.s @(r0,r5),fr4\n\t"
	"fsub fr12,fr7\n\t"
	"fmul fr13,fr5\n\t"
	"fmov.s @r1+,fr13\n\t"
	"fmov.s @r1+,fr6\n\t"
	"mov #124,r0\n\t"
	"fmov.s @(r0,r6),fr12\n\t"
	"fmul fr13,fr7\n\t"
	"fmov.s @r1+,fr13\n\t"
	"fsub fr12,fr4\n\t"
	"mov #72,r0\n\t"
	"fmov.s @(r0,r5),fr12\n\t"
	"fmul fr6,fr15\n\t"
	"fmov.s @r1+,fr6\n\t"
	"fmul fr13,fr4\n\t"
	"mov #44,r0\n\t"
	"fmov.s @(r0,r7),fr13\n\t"
	"fmov fr15,fr0\n\t"
	"fadd fr4,fr0\n\t"
	"fsub fr4,fr15\n\t"
	"fmov.s @r1+,fr4\n\t"
	"fsub fr13,fr12\n\t"
	"mov #8,r0\n\t"
	"fmov.s @(r0,r7),fr13\n\t"
	"fmul fr6,fr15\n\t"
	"mov #108,r0\n\t"
	"fmov.s @(r0,r5),fr6\n\t"
	"fsub fr13,fr6\n\t"
	"fmul fr4,fr12\n\t"
	"fmov.s @r1+,fr4\n\t"
	"fmul fr4,fr6\n\t"
	"mov #4,r0\n\t"
	"fmov.s fr5,@(r0,r15)\n\t"
	"fmov.s @r1+,fr4\n\t"
	"fmov fr12,fr13\n\t"
	"fadd fr6,fr13\n\t"
	"mov #20,r0\n\t"
	"fmov.s @(r0,r7),fr5\n\t"
	"fsub fr6,fr12\n\t"
	"mov #84,r0\n\t"
	"fmov.s @(r0,r5),fr6\n\t"
	"fmul fr4,fr12\n\t"
	"mov #32,r0\n\t"
	"fmov.s @(r0,r7),fr4\n\t"
	"fsub fr4,fr6\n\t"
	"fmov.s @r1+,fr4\n\t"
	"fmul fr4,fr6\n\t"
	"mov #96,r0\n\t"
	"fmov.s @(r0,r5),fr4\n\t"
	"fsub fr5,fr4\n\t"
	"fmov.s @r1+,fr5\n\t"
	"fmul fr5,fr4\n\t"
	"fmov fr6,fr5\n\t"
	"fadd fr4,fr5\n\t"
	"fsub fr4,fr6\n\t"
	"fmov.s @r1+,fr4\n\t"
	"fmul fr4,fr6\n\t"
	"fmov fr11,fr4\n\t"
	"fadd fr5,fr4\n\t"
	"fsub fr5,fr11\n\t"
	"fmov.s @r1+,fr5\n\t"
	"fmul fr5,fr11\n\t"
	"fmov fr9,fr5\n\t"
	"fadd fr13,fr5\n\t"
	"fsub fr13,fr9\n\t"
	"fmov.s @r1+,fr13\n\t"
	"fmul fr13,fr9\n\t"
	"fmov fr10,fr13\n\t"
	"fadd fr0,fr13\n\t"
	"fsub fr0,fr10\n\t"
	"fmov.s @r1+,fr0\n\t"
	"fmul fr0,fr10\n\t"
	"fmov fr2,fr0\n\t"
	"fadd fr8,fr0\n\t"
	"fsub fr8,fr2\n\t"
	"fmov.s @r1+,fr8\n\t"
	"fmul fr8,fr2\n\t"
	"fmov fr4,fr8\n\t"
	"fadd fr0,fr8\n\t"
	"fsub fr0,fr4\n\t"
	"fmov.s @r1+,fr0\n\t"
	"fmul fr0,fr4\n\t"
	"fmov fr5,fr0\n\t"
	"fadd fr13,fr0\n\t"
	"fsub fr13,fr5\n\t"
	"fmov.s @r1+,fr13\n\t"
	"fmul fr13,fr5\n\t"
	"fmov fr8,fr13\n\t"
	"fadd fr0,fr13\n\t"
	"fsub fr0,fr8\n\t"
	"fmov.s @r1+,fr0\n\t"
	"fmul fr0,fr8\n\t"
	"fmov fr4,fr0\n\t"
	"fadd fr5,fr0\n\t"
	"fsub fr5,fr4\n\t"
	"fmov.s @r1+,fr5\n\t"
	"fmul fr5,fr4\n\t"
	"fmov fr11,fr5\n\t"
	"fadd fr2,fr5\n\t"
	"fsub fr2,fr11\n\t"
	"fmov.s @r1+,fr2\n\t"
	"fmul fr2,fr11\n\t"
	"fmov fr9,fr2\n\t"
	"fadd fr10,fr2\n\t"
	"fsub fr10,fr9\n\t"
	"fmov.s @r1+,fr10\n\t"
	"fmul fr10,fr9\n\t"
	"fmov fr5,fr10\n\t"
	"fadd fr2,fr10\n\t"
	"fsub fr2,fr5\n\t"
	"fmov.s @r1+,fr2\n\t"
	"fmul fr2,fr5\n\t"
	"fmov fr11,fr2\n\t"
	"fadd fr9,fr2\n\t"
	"fsub fr9,fr11\n\t"
	"fmov.s @r1+,fr9\n\t"
	"fmul fr9,fr11\n\t"
	"fmov fr3,fr9\n\t"
	"fadd fr6,fr9\n\t"
	"fsub fr6,fr3\n\t"
	"fmov.s @r1+,fr6\n\t"
	"fadd fr11,fr2\n\t"
	"fmul fr6,fr3\n\t"
	"fmov fr1,fr6\n\t"
	"fadd fr12,fr6\n\t"
	"mov #8,r0\n\t"
	"fmov.s fr11,@(r0,r15)\n\t"
	"fsub fr12,fr1\n\t"
	"fmov.s @r1+,fr12\n\t"
	"fmul fr12,fr1\n\t"
	"fmov fr14,fr12\n\t"
	"fadd fr15,fr12\n\t"
	"fsub fr15,fr14\n\t"
	"fmov.s @r1+,fr15\n\t"
	"fmul fr15,fr14\n\t"
	"mov #4,r0\n\t"
	"fmov.s @(r0,r15),fr15\n\t"
	"fadd fr2,fr10\n\t"
	"fadd fr5,fr2\n\t"
	"fadd fr11,fr5\n\t"
	"fmov fr15,fr11\n\t"
	"fadd fr7,fr11\n\t"
	"fsub fr7,fr15\n\t"
	"fmov.s @r1+,fr7\n\t"
	"fmul fr7,fr15\n\t"
	"fmov fr9,fr7\n\t"
	"fadd fr11,fr7\n\t"
	"fsub fr11,fr9\n\t"
	"fmov.s @r1+,fr11\n\t"
	"fmul fr11,fr9\n\t"
	"fmov fr6,fr11\n\t"
	"fadd fr12,fr11\n\t"
	"fsub fr12,fr6\n\t"
	"fmov.s @r1+,fr12\n\t"
	"fmul fr12,fr6\n\t"
	"fmov fr7,fr12\n\t"
	"fadd fr11,fr12\n\t"
	"fsub fr11,fr7\n\t"
	"fmov.s @r1+,fr11\n\t"
	"fmul fr11,fr7\n\t"
	"fmov fr9,fr11\n\t"
	"fadd fr6,fr11\n\t"
	"fsub fr6,fr9\n\t"
	"fmov.s @r1+,fr6\n\t"
	"fadd fr4,fr0\n\t"
	"fmul fr6,fr9\n\t"
	"fmov fr3,fr6\n\t"
	"fadd fr15,fr6\n\t"
	"fsub fr15,fr3\n\t"
	"fmov.s @r1+,fr15\n\t"
	"fmul fr15,fr3\n\t"
	"fmov fr1,fr15\n\t"
	"fadd fr14,fr15\n\t"
	"fsub fr14,fr1\n\t"
	"fmov.s @r1+,fr14\n\t"
	"fmul fr14,fr1\n\t"
	"fmov fr6,fr14\n\t"
	"fadd fr15,fr14\n\t"
	"fsub fr15,fr6\n\t"
	"fmov.s @r1+,fr15\n\t"
	"fmul fr15,fr6\n\t"
	"fmov fr3,fr15\n\t"
	"fadd fr1,fr15\n\t"
	"mov #64,r0\n\t"
	"mov.l r8,@(r0,r2)\n\t"
	"fsub fr1,fr3\n\t"
	"fmov.s @r1+,fr1\n\t"
	"fmul fr1,fr3\n\t"
	"mov #60,r0\n\t"
	"fmov.s fr3,@(r0,r2)\n\t"
	"fadd fr9,fr11\n\t"
	"fadd fr3,fr15\n\t"
	"fadd fr15,fr14\n\t"
	"fadd fr6,fr15\n\t"
	"fadd fr3,fr6\n\t"
	"fadd fr14,fr12\n\t"
	"fmov fr3,fr1\n\t"
	"fneg fr1\n\t"
	"fadd fr11,fr14\n\t"
	"fadd fr15,fr11\n\t"
	"mov #68,r0\n\t"
	"fmov.s fr1,@(r0,r2)\n\t"
	"fadd fr12,fr13\n\t"
	"fadd fr10,fr12\n\t"
	"fadd fr14,fr10\n\t"
	"fneg fr13\n\t"
	"mov #60,r0\n\t"
	"fmov.s fr13,@(r0,r3)\n\t"
	"fadd fr0,fr14\n\t"
	"fadd fr7,fr15\n\t"
	"fneg fr10\n\t"
	"fneg fr12\n\t"
	"mov #52,r0\n\t"
	"fmov.s fr12,@(r0,r3)\n\t"
	"fadd fr6,fr7\n\t"
	"fneg fr14\n\t"
	"fadd fr9,fr6\n\t"
	"fadd fr3,fr9\n\t"
	"mov #68,r0\n\t"
	"fmov.s fr13,@(r0,r3)\n\t"
	"mov #84,r0\n\t"
	"fmov.s fr10,@(r0,r3)\n\t"
	"mov #92,r0\n\t"
	"fmov.s fr14,@(r0,r3)\n\t"
	"mov #36,r0\n\t"
	"fmov.s fr14,@(r0,r3)\n\t"
	"fadd fr11,fr0\n\t"
	"fadd fr2,fr11\n\t"
	"mov #76,r0\n\t"
	"fmov.s fr12,@(r0,r3)\n\t"
	"fadd fr15,fr2\n\t"
	"fneg fr0\n\t"
	"mov #44,r0\n\t"
	"fmov.s fr10,@(r0,r3)\n\t"
	"fneg fr2\n\t"
	"mov #12,r0\n\t"
	"fmov.s fr2,@(r0,r3)\n\t"
	"fadd fr8,fr15\n\t"
	"fneg fr11\n\t"
	"mov #20,r0\n\t"
	"fmov.s fr11,@(r0,r3)\n\t"
	"fadd fr7,fr8\n\t"
	"fneg fr15\n\t"
	"mov #116,r0\n\t"
	"fmov.s fr2,@(r0,r3)\n\t"
	"fadd fr5,fr7\n\t"
	"fadd fr6,fr5\n\t"
	"fmov fr8,fr1\n\t"
	"fneg fr1\n\t"
	"fmov fr7,fr13\n\t"
	"fneg fr13\n\t"
	"mov #28,r0\n\t"
	"fmov.s fr0,@(r0,r3)\n\t"
	"mov #12,r0\n\t"
	"fmov.s fr7,@(r0,r2)\n\t"
	"fadd fr4,fr6\n\t"
	"fadd fr9,fr4\n\t"
	"fmov fr5,fr12\n\t"
	"fneg fr12\n\t"
	"mov #108,r0\n\t"
	"fmov.s fr12,@(r0,r2)\n\t"
	"mov #28,r0\n\t"
	"fmov.s fr6,@(r0,r2)\n\t"
	"fmov fr6,fr10\n\t"
	"fneg fr10\n\t"
	"mov #124,r0\n\t"
	"fmov.s fr1,@(r0,r2)\n\t"
	"mov #100,r0\n\t"
	"fmov.s fr0,@(r0,r3)\n\t"
	"mov #8,r0\n\t"
	"fmov.s @(r0,r15),fr0\n\t"
	"mov #4,r0\n\t"
	"fmov.s fr15,@(r0,r3)\n\t"
	"mov #20,r0\n\t"
	"fmov.s fr5,@(r0,r2)\n\t"
	"fadd fr0,fr9\n\t"
	"fadd fr3,fr0\n\t"
	"fmov fr4,fr14\n\t"
	"fneg fr14\n\t"
	"mov #92,r0\n\t"
	"fmov.s fr14,@(r0,r2)\n\t"
	"fmov fr0,fr2\n\t"
	"fneg fr2\n\t"
	"mov #108,r0\n\t"
	"fmov.s fr11,@(r0,r3)\n\t"
	"mov #76,r0\n\t"
	"fmov.s fr2,@(r0,r2)\n\t"
	"fmov fr9,fr11\n\t"
	"fneg fr11\n\t"
	"mov #36,r0\n\t"
	"fmov.s fr4,@(r0,r2)\n\t"
	"mov #52,r0\n\t"
	"fmov.s fr0,@(r0,r2)\n\t"
	"mov #100,r0\n\t"
	"fmov.s fr10,@(r0,r2)\n\t"
	"mov #116,r0\n\t"
	"fmov.s fr13,@(r0,r2)\n\t"
	"mov #4,r0\n\t"
	"fmov.s fr8,@(r0,r2)\n\t"
	"mov #124,r0\n\t"
	"fmov.s fr15,@(r0,r3)\n\t"
	"mov #84,r0\n\t"
	"fmov.s fr11,@(r0,r2)\n\t"
	"mov #44,r0\n\t"
	"fmov.s fr9,@(r0,r2)\n\t"
	"add #12,r15\n\t"
	"fmov.s @r15+,fr15\n\t"
	"fmov.s @r15+,fr14\n\t"
	"fmov.s @r15+,fr13\n\t"
	"fmov.s @r15+,fr12\n\t"
	"rts\n\t"
	"mov.l @r15+,r8\n\t"
	"\n\t.align 2\n"
	".Lc_idct36:\n"
	"\t.long 0x3f009e8d\n"
	"\t.long 0x3f05c278\n"
	"\t.long 0x3f11233f\n"
	"\t.long 0x3f25961d\n"
	"\t.long 0x3f49c480\n"
	"\t.long 0x3f87c449\n"
	"\t.long 0x3fdc7926\n"
	"\t.long 0x40a33c9c\n"
	"\t.long 0x3f0281f7\n"
	"\t.long 0x3f19f1bd\n"
	"\t.long 0x3f6664d7\n"
	"\t.long 0x402406cf\n"
	"\t.long 0x3f0a8bd4\n"
	"\t.long 0x3fa73d75\n"
	"\t.long 0x3f3504f3\n"
	"\t.long 0x3f3504f3\n"
	"\t.long 0x3f0a8bd4\n"
	"\t.long 0x3fa73d75\n"
	"\t.long 0x3f3504f3\n"
	"\t.long 0x3f3504f3\n"
	"\t.long 0x3f0281f7\n"
	"\t.long 0x3f19f1bd\n"
	"\t.long 0x3f6664d7\n"
	"\t.long 0x402406cf\n"
	"\t.long 0x3f0a8bd4\n"
	"\t.long 0x3fa73d75\n"
	"\t.long 0x3f3504f3\n"
	"\t.long 0x3f3504f3\n"
	"\t.long 0x3f0a8bd4\n"
	"\t.long 0x3fa73d75\n"
	"\t.long 0x3f3504f3\n"
	"\t.long 0x3f3504f3\n"
	"\t.long 0x3f002785\n"
	"\t.long 0x41230a46\n"
	"\t.long 0x3f009e8d\n"
	"\t.long 0x3f01668b\n"
	"\t.long 0x405a1642\n"
	"\t.long 0x3f05c278\n"
	"\t.long 0x3f03f45b\n"
	"\t.long 0x4003b2af\n"
	"\t.long 0x3f11233f\n"
	"\t.long 0x3f07f268\n"
	"\t.long 0x3fbdf91b\n"
	"\t.long 0x3f25961d\n"
	"\t.long 0x3f0d9838\n"
	"\t.long 0x3f95b035\n"
	"\t.long 0x3f49c480\n"
	"\t.long 0x3f153b3a\n"
	"\t.long 0x3f78fa3b\n"
	"\t.long 0x3f87c449\n"
	"\t.long 0x3f1f5c6e\n"
	"\t.long 0x3f56df9e\n"
	"\t.long 0x3fdc7926\n"
	"\t.long 0x3f2cc03d\n"
	"\t.long 0x3f3e99ee\n"
	"\t.long 0x40a33c9c\n"
	"\t.long 0x3f0281f7\n"
	"\t.long 0x3f19f1bd\n"
	"\t.long 0x3f6664d7\n"
	"\t.long 0x402406cf\n"
	"\t.long 0x3f0a8bd4\n"
	"\t.long 0x3fa73d75\n"
	"\t.long 0x3f3504f3\n"
	"\t.long 0x3f3504f3\n"
	"\t.long 0x3f0a8bd4\n"
	"\t.long 0x3fa73d75\n"
	"\t.long 0x3f3504f3\n"
	"\t.long 0x3f3504f3\n"
	"\t.long 0x3f0281f7\n"
	"\t.long 0x3f19f1bd\n"
	"\t.long 0x3f6664d7\n"
	"\t.long 0x402406cf\n"
	"\t.long 0x3f0a8bd4\n"
	"\t.long 0x3fa73d75\n"
	"\t.long 0x3f3504f3\n"
	"\t.long 0x3f3504f3\n"
	"\t.long 0x3f0a8bd4\n"
	"\t.long 0x3fa73d75\n"
	"\t.long 0x3f3504f3\n"
	"\t.long 0x3f3504f3\n"
	"\t.text\n"
);

void plm_audio_window32(const float *d, const float *v1, const float *v2, short *out);
__asm__(
	"\t.section .text.plm_audio,\"ax\"\n"
	"\t.align 5\n"
	"\t.global _plm_audio_window32\n"
	"_plm_audio_window32:\n\t"
	"mov.l r8,@-r15\n\t"
	"mov.l r9,@-r15\n\t"
	"fmov.s fr12,@-r15\n\t"
	"fmov.s fr13,@-r15\n\t"
	"fmov.s fr14,@-r15\n\t"
	"fmov.s fr15,@-r15\n\t"
	"mov #2,r1\n\t"
	"shll8 r1\n\t"
	"mov.l r10,@-r15\n\t"
	"mov #-1,r9\n\t"
	"shlr16 r9\n\t"
	"shlr r9\n\t"                                 /* 32767 */
	"mov r9,r10\n\t"
	"not r10,r10\n\t"                             /* -32768 */
	"mov #31,r8\n\t"
	"mov r5,r2\n\t"
	"mov r6,r3\n\t"
	"fmov.s @r4+,fr0\n\t"
	"fmov.s @r4+,fr1\n\t"
	"fmov.s @r4+,fr2\n\t"
	"fmov.s @r4+,fr3\n\t"
	"fmov.s @r2,fr4\n\t"
	"add r1,r2\n\t"
	"fmov.s @r3,fr5\n\t"
	"add r1,r3\n\t"
	"fmov.s @r2,fr6\n\t"
	"add r1,r2\n\t"
	"fmov.s @r3,fr7\n\t"
	"add r1,r3\n\t"
	/* output 0 */
	"fmov.s @r4+,fr8\n\t"
	"fmov.s @r4+,fr9\n\t"
	"fmov.s @r4+,fr10\n\t"
	"fmov.s @r4+,fr11\n\t"
	"fipr fv0,fv4\n\t"
	"fmov.s @r2,fr12\n\t"
	"add r1,r2\n\t"
	"fmov.s @r3,fr13\n\t"
	"add r1,r3\n\t"
	"fmov.s @r2,fr14\n\t"
	"add r1,r2\n\t"
	"fmov.s @r3,fr15\n\t"
	"add r1,r3\n\t"
	"flds fr7,fpul\n\t"
	"fmov.s @r4+,fr0\n\t"
	"fmov.s @r4+,fr1\n\t"
	"fmov.s @r4+,fr2\n\t"
	"fmov.s @r4+,fr3\n\t"
	"fipr fv8,fv12\n\t"
	"fmov.s @r2,fr4\n\t"
	"add r1,r2\n\t"
	"fmov.s @r3,fr5\n\t"
	"add r1,r3\n\t"
	"fmov.s @r2,fr6\n\t"
	"add r1,r2\n\t"
	"fmov.s @r3,fr7\n\t"
	"add r1,r3\n\t"
	"fsts fpul,fr11\n\t"
	"fadd fr11,fr15\n\t"
	"fmov.s @r4+,fr8\n\t"
	"fmov.s @r4+,fr9\n\t"
	"fmov.s @r4+,fr10\n\t"
	"flds fr15,fpul\n\t"
	"fmov.s @r4+,fr11\n\t"
	"fipr fv0,fv4\n\t"
	"fmov.s @r2,fr12\n\t"
	"add r1,r2\n\t"
	"fmov.s @r3,fr13\n\t"
	"add r1,r3\n\t"
	"fmov.s @r2,fr14\n\t"
	"fmov.s @r3,fr15\n\t"
	"fsts fpul,fr3\n\t"
	"fadd fr7,fr3\n\t"
	"add #64,r4\n\t"
	"fipr fv8,fv12\n\t"
	"flds fr3,fpul\n\t"
	"add #4,r5\n\t"
	"add #4,r6\n\t"
	"mov r5,r2\n\t"
	"mov r6,r3\n\t"
	"fmov.s @r4+,fr0\n\t"
	"fmov.s @r4+,fr1\n\t"
	"fmov.s @r4+,fr2\n\t"
	"fmov.s @r4+,fr3\n\t"
	"fmov.s @r2,fr4\n\t"
	"add r1,r2\n\t"
	"fmov.s @r3,fr5\n\t"
	"add r1,r3\n\t"
	"fmov.s @r2,fr6\n\t"
	"add r1,r2\n\t"
	"fmov.s @r3,fr7\n\t"
	"add r1,r3\n\t"
	"fsts fpul,fr8\n\t"
	"fadd fr15,fr8\n\t"
	"ftrc fr8,fpul\n\t"
	"1:\n\t"   /* one output per pass; order from a search on the ornio.nilware.io SH7091 model (49 -> 40 cycles, 50 -> 41 with the clamp branches); same operations and summation order */
	"fmov.s @r3,fr13\n\t"
	"fmov.s @r4+,fr8\n\t"
	"fmov.s @r4+,fr9\n\t"
	"fmov.s @r2,fr12\n\t"
	"fmov.s @r4+,fr10\n\t"
	"add r1,r3\n\t"
	"fmov.s @r3,fr15\n\t"
	"add r1,r3\n\t"
	"sts fpul,r0\n\t"
	"fmov.s @r4+,fr11\n\t"
	"fipr fv0,fv4\n\t"
	"add r1,r2\n\t"
	"fmov.s @r4+,fr0\n\t"
	"fmov.s @r2,fr14\n\t"
	"add r1,r2\n\t"
	"fmov.s @r4+,fr1\n\t"
	"fmov.s @r2,fr4\n\t"
	"cmp/gt r9,r0\n\t"
	"bt .Lw_hi_a\n\t"
	"cmp/gt r0,r10\n\t"
	"bt .Lw_lo_a\n"
	".Lw_ba:\n\t"
	"flds fr7,fpul\n\t"
	"fmov.s @r4+,fr2\n\t"
	"add r1,r2\n\t"
	"fmov.s @r3,fr5\n\t"
	"add r1,r3\n\t"
	"fmov.s @r2,fr6\n\t"
	"fipr fv8,fv12\n\t"
	"fsts fpul,fr11\n\t"
	"fmov.s @r4+,fr3\n\t"
	"add r1,r2\n\t"
	"fmov.s @r4+,fr8\n\t"
	"fmov.s @r3,fr7\n\t"
	"fadd fr11,fr15\n\t"
	"add r1,r3\n\t"
	"fmov.s @r2,fr12\n\t"
	"add #4,r6\n\t"
	"fmov.s @r4+,fr9\n\t"
	"fmov.s @r3,fr13\n\t"
	"fipr fv0,fv4\n\t"
	"fmov.s @r4+,fr10\n\t"
	"add r1,r2\n\t"
	"fmov.s @r4+,fr11\n\t"
	"add r1,r3\n\t"
	"add #4,r5\n\t"
	"flds fr15,fpul\n\t"
	"fsts fpul,fr3\n\t"
	"fmov.s @r3,fr15\n\t"
	"fadd fr7,fr3\n\t"
	"fmov.s @r2,fr14\n\t"
	"mov r5,r2\n\t"
	"mov r6,r3\n\t"
	"fmov.s @r3,fr5\n\t"
	"fmov.s @r2,fr4\n\t"
	"add r1,r3\n\t"
	"add #64,r4\n\t"
	"fmov.s @r3,fr7\n\t"
	"fipr fv8,fv12\n\t"
	"flds fr3,fpul\n\t"
	"fsts fpul,fr8\n\t"
	"fmov.s @r4+,fr0\n\t"
	"add r1,r2\n\t"
	"add r1,r3\n\t"
	"fmov.s @r2,fr6\n\t"
	"fadd fr15,fr8\n\t"
	"fmov.s @r4+,fr1\n\t"
	"add r1,r2\n\t"
	"fmov.s @r4+,fr2\n\t"
	"mov.w r0,@r7\n\t"
	"add #4,r7\n\t"
	"fmov.s @r4+,fr3\n\t"
	"ftrc fr8,fpul\n\t"
	"dt r8\n\t"
	"bf 1b\n\t"
	"sts fpul,r0\n\t"
	"cmp/gt r9,r0\n\t"
	"bt .Lw_hi_b\n\t"
	"cmp/gt r0,r10\n\t"
	"bt .Lw_lo_b\n"
	".Lw_bb:\n\t"
	"mov.w r0,@r7\n\t"
	"mov.l @r15+,r10\n\t"
	"fmov.s @r15+,fr15\n\t"
	"fmov.s @r15+,fr14\n\t"
	"fmov.s @r15+,fr13\n\t"
	"fmov.s @r15+,fr12\n\t"
	"mov.l @r15+,r9\n\t"
	"rts\n\t"
	"mov.l @r15+,r8\n"
	".Lw_hi_a:\n\t"                               /* clamping, out of line (rare) */
	"bra .Lw_ba\n\t"
	"mov r9,r0\n"
	".Lw_lo_a:\n\t"
	"bra .Lw_ba\n\t"
	"mov r10,r0\n"
	".Lw_hi_b:\n\t"
	"bra .Lw_bb\n\t"
	"mov r9,r0\n"
	".Lw_lo_b:\n\t"
	"bra .Lw_bb\n\t"
	"mov r10,r0\n"
	"\t.text\n"
);

PLM_AUDIO_HOT void plm_audio_decode_frame(plm_audio_t *self) {
	// Prepare the quantizer table lookups
	int tab3 = 0;
	int sblimit = 0;
	int mono = (self->mode == PLM_AUDIO_MODE_MONO);

	int tab1 = mono ? 0 : 1;
	int tab2 = PLM_AUDIO_QUANT_LUT_STEP_1[tab1][self->bitrate_index];
	tab3 = QUANT_LUT_STEP_2[tab2][self->samplerate_index];
	sblimit = tab3 & 63;
	tab3 >>= 6;

	if (self->bound > sblimit) {
		self->bound = sblimit;
	}

	// Read the allocation information
	for (int sb = 0; sb < self->bound; sb++) {
		self->allocation[0][sb] = plm_audio_read_allocation(self, sb, tab3);
		self->allocation[1][sb] = plm_audio_read_allocation(self, sb, tab3);
	}

	for (int sb = self->bound; sb < sblimit; sb++) {
		self->allocation[0][sb] =
			self->allocation[1][sb] =
			plm_audio_read_allocation(self, sb, tab3);
	}

	// Read scale factor selector information
	int channels = mono ? 1 : 2;
	for (int sb = 0; sb < sblimit; sb++) {
		for (int ch = 0; ch < channels; ch++) {
			if (self->allocation[ch][sb]) {
				self->scale_factor_info[ch][sb] = plm_buffer_read(self->buffer, 2);
			}
		}
		if (mono) {
			self->scale_factor_info[1][sb] = self->scale_factor_info[0][sb];
		}
	}

	// Read scale factors
	for (int sb = 0; sb < sblimit; sb++) {
		for (int ch = 0; ch < channels; ch++) {
			if (self->allocation[ch][sb]) {
				int *sf = self->scale_factor[ch][sb];
				switch (self->scale_factor_info[ch][sb]) {
					case 0:
						sf[0] = plm_audio_decode_scalefactor_value(plm_buffer_read(self->buffer, 6));
						sf[1] = plm_audio_decode_scalefactor_value(plm_buffer_read(self->buffer, 6));
						sf[2] = plm_audio_decode_scalefactor_value(plm_buffer_read(self->buffer, 6));
						break;
					case 1:
						sf[0] =
						sf[1] = plm_audio_decode_scalefactor_value(plm_buffer_read(self->buffer, 6));
						sf[2] = plm_audio_decode_scalefactor_value(plm_buffer_read(self->buffer, 6));
						break;
					case 2:
						sf[0] =
						sf[1] =
						sf[2] = plm_audio_decode_scalefactor_value(plm_buffer_read(self->buffer, 6));
						break;
					case 3:
						sf[0] = plm_audio_decode_scalefactor_value(plm_buffer_read(self->buffer, 6));
						sf[1] =
						sf[2] = plm_audio_decode_scalefactor_value(plm_buffer_read(self->buffer, 6));
						break;
				}
			}
		}
		if (mono) {
			self->scale_factor[1][sb][0] = self->scale_factor[0][sb][0];
			self->scale_factor[1][sb][1] = self->scale_factor[0][sb][1];
			self->scale_factor[1][sb][2] = self->scale_factor[0][sb][2];
		}
	}

	// Coefficient input and reconstruction
	short *out = self->samples.pcm;
	for (int part = 0; part < 3; part++) {
		for (int granule = 0; granule < 4; granule++) {

			// Read the samples
			for (int sb = 0; sb < self->bound; sb++) {
				plm_audio_read_samples(self, 0, sb, part);
				plm_audio_read_samples(self, 1, sb, part);
			}
			for (int sb = self->bound; sb < sblimit; sb++) {
				plm_audio_read_samples(self, 0, sb, part);
				self->sample[1][sb][0] = self->sample[0][sb][0];
				self->sample[1][sb][1] = self->sample[0][sb][1];
				self->sample[1][sb][2] = self->sample[0][sb][2];
			}
			for (int sb = sblimit; sb < 32; sb++) {
				self->sample[0][sb][0] = 0;
				self->sample[0][sb][1] = 0;
				self->sample[0][sb][2] = 0;
				self->sample[1][sb][0] = 0;
				self->sample[1][sb][1] = 0;
				self->sample[1][sb][2] = 0;
			}

			// Synthesis loop
			for (int p = 0; p < 3; p++) {
				for (int ch = 0; ch < 2; ch++) {
					// Shifting step
					self->v_pos[ch] = (self->v_pos[ch] - 64) & 1023;

					plm_audio_idct36_sh4(self->sample[ch], p, self->V[ch], self->v_pos[ch]);

					int d_index = (512 - (self->v_pos[ch] >> 1)) >> 5;
					int v_index = (self->v_pos[ch] & 127) >> 1;
					float *d = &self->D[d_index];
					float *v1 = &self->V[ch][v_index];
					float *v2 = &self->V[ch][96 - v_index];
					/* KOS stream splitter expects stereo pairs as L, R in memory. */
					short *out_ch = out + ch;
					plm_audio_window32(d, v1, v2, out_ch);
				}
				out += 64;

			} // End of synthesis sub-block loop

		} // Decoding of the granule finished
	}

	plm_buffer_align(self->buffer);
}

PLM_AUDIO_HOT const plm_quantizer_spec_t *plm_audio_read_allocation(plm_audio_t *self, int sb, int tab3) {
	int tab4 = PLM_AUDIO_QUANT_LUT_STEP_3[tab3][sb];
	int qtab = PLM_AUDIO_QUANT_LUT_STEP_4[tab4 & 15][plm_buffer_read(self->buffer, tab4 >> 4)];
	return qtab ? (&PLM_AUDIO_QUANT_TAB[qtab - 1]) : 0;
}

PLM_AUDIO_HOT void plm_audio_read_samples(plm_audio_t *self, int ch, int sb, int part) {
	const plm_quantizer_spec_t *q = self->allocation[ch][sb];
	float *sample = self->sample[ch][sb];

	if (!q) {
		// No bits allocated for this subband
		sample[0] = sample[1] = sample[2] = 0;
		return;
	}

	int s0, s1, s2;
	int adj = q->levels;
	int bits = q->bits;
	// One 32-bit window (25 or more valid bits) serves all three samples when 3 * bits <= 25, i.e.
	// every quantiser up to 8 bits; wider ones take a fresh window per sample. The bit position
	// is written back once.
	plm_buffer_t *buf = self->buffer;
	uint32_t bi = (uint32_t)buf->bit_index;
	uint32_t w = plm_buffer_load_u32be(plm_buffer_ptr_from_read(buf, bi >> 3)) << (bi & 7);
	int sh = 32 - bits;
	if (q->group) {
		// Grouped samples: reciprocal multiply instead of division
		int val = (int)(w >> sh);
		bi += (uint32_t)bits;
		unsigned int recip = q->recip;
		int quot = (unsigned int)((unsigned int)val * recip) >> 16;
		s0 = val - quot * adj;
		val = quot;
		quot = (unsigned int)((unsigned int)val * recip) >> 16;
		s1 = val - quot * adj;
		s2 = quot;
	}
	else if (bits * 3 <= 25) {
		s0 = (int)(w >> sh);
		s1 = (int)((w << bits) >> sh);
		s2 = (int)((w << (bits * 2)) >> sh);
		bi += (uint32_t)(bits * 3);
	}
	else {
		s0 = (int)(w >> sh);
		bi += (uint32_t)bits;
		w = plm_buffer_load_u32be(plm_buffer_ptr_from_read(buf, bi >> 3)) << (bi & 7);
		s1 = (int)(w >> sh);
		bi += (uint32_t)bits;
		w = plm_buffer_load_u32be(plm_buffer_ptr_from_read(buf, bi >> 3)) << (bi & 7);
		s2 = (int)(w >> sh);
		bi += (uint32_t)bits;
	}
	buf->bit_index = bi;

	// (adj_half - s) * (65536 / levels) * scalefactor / 2^24: the ISO 11172-3 requantisation
	// (2 / levels) * (s - (levels - 1) / 2), sign folded into the output scale. pl_mpeg (and the
	// integer path below) uses 65536 / (levels + 1), which leaves every subband levels / (levels + 1)
	// too quiet (-2.5 dB at 3 levels): about 20-30 dB SNR against a reference decode on music.
	float f = q->fscale * (float)self->scale_factor[ch][sb][part] * (1.0f / 16777216.0f);
	int ah = q->adj_half;
	sample[0] = (float)(ah - s0) * f;
	sample[1] = (float)(ah - s1) * f;
	sample[2] = (float)(ah - s2) * f;
}

#endif // PL_MPEG_IMPLEMENTATION
