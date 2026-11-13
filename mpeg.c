#include <kos.h>
#include "mpeg.h"
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

/* Most frames the queue can hold (mpeg_player_options_t.frame_queue is limited to this) */
#define MPEG_QUEUE_MAX 6

/* Read-ahead (mpeg_player_options_t.read_ahead): a thread reads the file into a ring, and the
   decoder's stream buffer is filled from the ring. KOS disc reads sleep until the drive has
   delivered, so the play loop keeps running during a read, also a slow one. */
#define MPEG_RA_CHUNK (32 * 1024)

typedef struct mpeg_reader {
    MPEG_FILE_TYPE fh;
    uint8_t *data;
    size_t cap;
    size_t rpos, wpos, fill;      /* guarded by lock */
    bool eof, quit, seek_pending; /* guarded by lock */
    size_t seek_pos;              /* guarded by lock */
    size_t delivered;             /* file offset of the next byte the decoder gets */
    unsigned gen;                 /* counts rewinds: a read that straddles one is thrown away */
    mutex_t lock;
    kthread_t *thread;
} mpeg_reader_t;

struct mpeg_player_t {
    plm_t *decoder;
    mpeg_reader_t *reader;
    plm_frame_t *frame;
    uint64_t start_time;
    snd_stream_hnd_t snd_hnd;
    pvr_list_type_t list_type;
    size_t texture_width;
    size_t texture_height;
    size_t row_width;       /* texels per texture row in memory: texture_width, or the video's
                               width with a stride texture */

    uint8_t *snd_buf;
    uint8_t *pcm_ring;      /* decoded audio not yet given to the stream (see PCM_RING_BYTES) */
    int pcm_ring_read;      /* byte offset of the oldest decoded byte */
    int pcm_ring_used;      /* bytes decoded and not yet given to the stream */
    bool pcm_ring_eof;      /* the audio has ended: stop decoding ahead until the next reset */
    int sample_rate;
    int snd_volume;
    bool snd_started;

    pvr_poly_hdr_t hdr;
    pvr_vertex_t vert[4];

    pvr_ptr_t texture;
    int width;
    int height;
    bool loop;
    bool dma_busy;

    /* Frame queue (mpeg_player_options_t.frame_queue; see mpeg_play_queued). Each queued frame is
       already converted into a texture of its own; q_tex[0] is `texture`. */
    int q_depth;                              /* frames kept decoded ahead; 0: the plain play loop */
    int q_slots;                              /* textures in the ring: q_depth + 1 */
    int q_head;                               /* slot of the next frame to show */
    int q_count;                              /* uploaded frames not shown yet */
    pvr_ptr_t q_tex[MPEG_QUEUE_MAX + 1];
    pvr_poly_hdr_t q_hdr[MPEG_QUEUE_MAX + 1];
    double q_time[MPEG_QUEUE_MAX + 1];
    plm_frame_t q_pend[2];                    /* decoded frames waiting for a texture (copies) */
    bool q_pend_reused[2];                    /* ... whose buffer the next decode writes (B-pictures) */
    int q_npend;
    bool q_hold_decode;                       /* the DMA in flight must finish before the next decode */
    bool q_playing;                           /* the clock and the sound are running */
    bool in_video_decode;                     /* the sound callback must not decode audio now */
};

/* Size of the sound buffer for both the SH4 side and the AICA side */
#define SOUND_BUFFER (64 * 1024)
#define AUDIO_CHANNELS 2

/* Decoded audio kept ahead of the stream. The stream asks for half its buffer at a time (16384
   samples, 0.37 s at 44.1 kHz); decoding that much MP2 inside snd_stream_poll blocked the play loop
   for 27-31 ms, enough to make the next picture late in heavy passages. The player now decodes MP2
   frames into this ring while it would otherwise wait: one per play-loop pass (mostly the passes
   that wait for the next frame's time) and as many as fit while the frame upload DMA runs. The
   stream callback then only hands over data, decoding whatever is missing. Two requests long, so in
   steady state each request starts at offset 0 or SOUND_BUFFER and is passed to KOS in place. */
#define PCM_RING_BYTES (2 * SOUND_BUFFER)
#define PCM_FRAME_BYTES (PLM_AUDIO_SAMPLES_PER_FRAME * AUDIO_CHANNELS * (int)sizeof(short))

static int setup_graphics(mpeg_player_t *player, const mpeg_player_options_t *options);
static int setup_audio(mpeg_player_t *player);
static int pcm_ring_decode_one(mpeg_player_t *player);
static int pcm_ring_decode_ahead(mpeg_player_t *player);
static void mpeg_upload_frame_wait_decoding(mpeg_player_t *player);
static mpeg_play_result_t mpeg_play_queued(mpeg_player_t *player, const mpeg_cancel_options_t *cancel_options);

static void *mpeg_reader_thread(void *param) {
    mpeg_reader_t *r = (mpeg_reader_t *)param;

    while(true) {
        mutex_lock(&r->lock);
        bool quit = r->quit;
        bool seek = r->seek_pending;
        size_t wpos = r->wpos;
        size_t seek_pos = r->seek_pos;
        size_t want = r->cap - wpos < MPEG_RA_CHUNK ? r->cap - wpos : MPEG_RA_CHUNK;
        bool wait = r->eof || r->cap - r->fill < want;
        unsigned gen = r->gen;
        r->seek_pending = false;
        mutex_unlock(&r->lock);

        if(quit)
            break;

        if(seek) {
            MPEG_FILE_SEEK(r->fh, seek_pos, SEEK_SET);
            continue;
        }

        if(wait) {
            thd_sleep(2);
            continue;
        }

        int n = MPEG_FILE_READ(r->fh, r->data + wpos, want);

        mutex_lock(&r->lock);
        if(gen == r->gen) {
            if(n <= 0) {
                r->eof = true;
            }
            else {
                r->wpos = (wpos + (size_t)n) % r->cap;
                r->fill += (size_t)n;
            }
        }
        mutex_unlock(&r->lock);
    }

    return NULL;
}

/* The decoder's stream buffer wants more data. Like a file read, this fills all the room there is
   and only stops short at the end of the file: pl_mpeg takes a buffer that is still short after
   one call for "nothing more for now" and goes on with what it has, a picture or an audio frame
   with its end missing. */
static void mpeg_reader_load(plm_buffer_t *buf, void *user) {
    mpeg_reader_t *r = (mpeg_reader_t *)user;

    if(buf->discard_read_bytes)
        plm_buffer_discard_read_bytes(buf);

    size_t space = buf->capacity - buf->length;

    while(space) {
        mutex_lock(&r->lock);
        size_t fill = r->fill;
        size_t rpos = r->rpos;
        bool ended = r->eof && !r->seek_pending;
        mutex_unlock(&r->lock);

        if(fill) {
            size_t n = fill < space ? fill : space;
            if(n > r->cap - rpos)
                n = r->cap - rpos;

            plm_buffer_write(buf, r->data + rpos, n);
            space -= n;

            mutex_lock(&r->lock);
            r->rpos = (rpos + n) % r->cap;
            r->fill -= n;
            r->delivered += n;
            mutex_unlock(&r->lock);
            continue;
        }

        if(ended) {
            plm_buffer_signal_end(buf);
            return;
        }

        thd_sleep(1);   /* the reader has fallen behind: wait for it */
    }
}

/* Continue reading at another file offset (the decoder's stream buffer is emptied by the caller) */
static void mpeg_reader_seek(plm_buffer_t *buf, size_t offset, void *user) {
    mpeg_reader_t *r = (mpeg_reader_t *)user;
    (void)buf;

    mutex_lock(&r->lock);
    r->gen++;
    r->rpos = r->wpos = r->fill = 0;
    r->eof = false;
    r->seek_pending = true;
    r->seek_pos = offset;
    r->delivered = offset;
    mutex_unlock(&r->lock);
}

static size_t mpeg_reader_tell(plm_buffer_t *buf, void *user) {
    mpeg_reader_t *r = (mpeg_reader_t *)user;
    (void)buf;
    return r->delivered;
}

static void mpeg_reader_destroy(mpeg_reader_t *r) {
    if(!r)
        return;

    if(r->thread) {
        mutex_lock(&r->lock);
        r->quit = true;
        mutex_unlock(&r->lock);
        thd_join(r->thread, NULL);
    }

    mutex_destroy(&r->lock);
    if(r->fh != MPEG_FILE_INVALID_HANDLE)
        MPEG_FILE_CLOSE(r->fh);
    if(r->data)
        MPEG_FREE(r->data);
    MPEG_FREE(r);
}

/* A decoder that reads `filename` through a read-ahead thread; NULL (and *out NULL) on failure */
static plm_t *mpeg_reader_open(const char *filename, uint32_t bytes, mpeg_reader_t **out) {
    *out = NULL;

    mpeg_reader_t *r = (mpeg_reader_t *)MPEG_MALLOC(sizeof(mpeg_reader_t));
    if(!r)
        return NULL;
    MPEG_MEMZERO(r, sizeof(mpeg_reader_t));
    mutex_init(&r->lock, MUTEX_TYPE_NORMAL);

    r->cap = (bytes + MPEG_RA_CHUNK - 1) / MPEG_RA_CHUNK * MPEG_RA_CHUNK;
    if(r->cap < 2 * MPEG_RA_CHUNK)
        r->cap = 2 * MPEG_RA_CHUNK;

    r->fh = MPEG_FILE_OPEN(filename);
    r->data = r->fh != MPEG_FILE_INVALID_HANDLE ? (uint8_t *)MPEG_MEMALIGN(32, r->cap) : NULL;
    plm_buffer_t *buf = r->data ? plm_buffer_create_with_capacity(PLM_BUFFER_DEFAULT_SIZE) : NULL;
    if(!buf) {
        mpeg_reader_destroy(r);
        return NULL;
    }
    buf->discard_read_bytes = TRUE;
    plm_buffer_set_load_callback(buf, mpeg_reader_load, r);
    buf->seek_callback = mpeg_reader_seek;
    buf->tell_callback = mpeg_reader_tell;

    r->thread = thd_create(false, mpeg_reader_thread, r);
    if(!r->thread) {
        plm_buffer_destroy(buf);
        mpeg_reader_destroy(r);
        return NULL;
    }

    plm_t *plm = plm_create_with_buffer(buf, TRUE);
    if(!plm) {
        mpeg_reader_destroy(r);
        return NULL;
    }

    *out = r;
    return plm;
}

static uint32_t next_power_of_two(uint32_t n) {
    if(n == 0)
        return 1;

    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n++;

    return n;
}

static inline void sound_stream_reset(mpeg_player_t *player) {
    if(!player)
        return;

    if(player->snd_hnd != SND_STREAM_INVALID && player->snd_started) {
        snd_stream_stop(player->snd_hnd);
        player->snd_started = false;
    }

    player->pcm_ring_read = 0;
    player->pcm_ring_used = 0;
    player->pcm_ring_eof = false;
}

#ifndef MPEG_SKIP_TO_INTRA_LATE
#define MPEG_SKIP_TO_INTRA_LATE 0.15   /* seconds behind before jumping to the next I-picture */
#endif
/* Drop B-pictures while the video is behind: if the frame just shown was due more than one frame
   period ago, the next B-pictures are skipped (the clock still advances) until it is on time again.
   Without this a stream that decodes slower than real time in heavy scenes drifts behind the audio
   (measured: up to 1 s at 640x480, 2 Mbit/s). */
static void mpeg_update_frame_dropping(mpeg_player_t *player, double playback_time) {
    double period = 1.0 / plm_get_framerate(player->decoder);
    double late = playback_time - player->frame->time;
    plm_set_video_skip_b_frames(player->decoder, late > period);
    /* Far behind (a stream without B-pictures cannot catch up otherwise): jump to the next
       I-picture; the picture holds until then, the audio stays in time. */
    if (late > MPEG_SKIP_TO_INTRA_LATE)
        plm_set_video_skip_to_intra(player->decoder);
}

static int mpeg_check_cancel(const mpeg_cancel_options_t *opt) {
    if(!opt) return 0;

    /*   Controller Cancel   */
    MAPLE_FOREACH_BEGIN(MAPLE_FUNC_CONTROLLER, cont_state_t, st)
        if(opt->pad_button_any && (st->buttons & opt->pad_button_any))
            return 1;

        if(opt->pad_button_combo &&
            (st->buttons & opt->pad_button_combo) == opt->pad_button_combo)
            return 1;

        /* Always cancel on reset combo */
        if(st->buttons == CONT_RESET_BUTTONS)
            return 2;
    MAPLE_FOREACH_END()

    /*   Keyboard Cancel   */
    MAPLE_FOREACH_BEGIN(MAPLE_FUNC_KEYBOARD, kbd_state_t, kbd_st)
        for(size_t i = 0; i < opt->kbd_keys_any_count; ++i) {
            if(kbd_st->key_states[opt->kbd_keys_any[i]].is_down)
                return 1;
        }

        int all_pressed = 1;
        for(size_t i = 0; i < opt->kbd_keys_combo_count; ++i) {
            if(!kbd_st->key_states[opt->kbd_keys_combo[i]].is_down) {
                all_pressed = 0;
                break;
            }
        }

        if(opt->kbd_keys_combo_count && all_pressed)
            return 1;
    MAPLE_FOREACH_END()

    return 0;
}

/** Default MPEG player options used when NULL is passed to *_ex() functions. */
static const mpeg_player_options_t MPEG_PLAYER_OPTIONS_DEFAULT = MPEG_PLAYER_OPTIONS_INITIALIZER;

static bool mpeg_player_init(mpeg_player_t *player, const mpeg_player_options_t *opts) {
    /* Never let plm loop internally — it rewinds without resetting our
       start_time, which causes playback_time >> frame->time and the video
       plays at max speed.  We store the flag ourselves and rewind manually
       so that start_time and audio are properly re-initialized. */
    player->loop = opts->loop;
    plm_set_loop(player->decoder, 0);

    player->snd_buf = (uint8_t *)MPEG_MEMALIGN(32, SOUND_BUFFER);
    if(!player->snd_buf) {
        fprintf(stderr, "Out of memory for player->snd_buf\n");
        mpeg_player_destroy(player);
        return false;
    }

    player->pcm_ring = (uint8_t *)MPEG_MEMALIGN(32, PCM_RING_BYTES);
    if(!player->pcm_ring) {
        fprintf(stderr, "Out of memory for player->pcm_ring\n");
        mpeg_player_destroy(player);
        return false;
    }

    /* A Sofdec file's first system header names the audio stream only: look at the packets */
    if(plm_get_num_video_streams(player->decoder) == 0)
        plm_probe(player->decoder, 512 * 1024);

    player->list_type = opts->list_type;
    if(setup_graphics(player, opts) < 0) {
        fprintf(stderr, "Setting up graphics failed\n");
        mpeg_player_destroy(player);
        return false;
    }

    player->snd_volume = opts->volume;
    if(setup_audio(player) < 0) {
        fprintf(stderr, "Setting up audio failed\n");
        mpeg_player_destroy(player);
        return false;
    }

    return true;
}

mpeg_player_t *mpeg_player_create_ex(const char *filename, const mpeg_player_options_t *options) {
    mpeg_player_t *player = NULL;
    const mpeg_player_options_t *opts = options ? options : &MPEG_PLAYER_OPTIONS_DEFAULT;

    if(!filename) {
        fprintf(stderr, "filename is NULL\n");
        return NULL;
    }

    player = (mpeg_player_t *)MPEG_MALLOC(sizeof(mpeg_player_t));
    if(!player) {
        fprintf(stderr, "Out of memory for player\n");
        return NULL;
    }

    MPEG_MEMZERO(player, sizeof(mpeg_player_t));
    player->snd_hnd = SND_STREAM_INVALID;

    if(opts->read_ahead)
        player->decoder = mpeg_reader_open(filename, opts->read_ahead, &player->reader);
    else
        player->decoder = plm_create_with_filename(filename);
    if(!player->decoder) {
        fprintf(stderr, "Out of memory for player->decoder\n");
        mpeg_player_destroy(player);
        return NULL;
    }

    if(!mpeg_player_init(player, opts))
        return NULL;

    return player;
}

mpeg_player_t *mpeg_player_create_memory_ex(unsigned char *memory, const size_t length, const mpeg_player_options_t *options) {
    mpeg_player_t *player = NULL;
    const mpeg_player_options_t *opts = options ? options : &MPEG_PLAYER_OPTIONS_DEFAULT;

    if(!memory) {
        fprintf(stderr, "memory is NULL\n");
        return NULL;
    }

    player = (mpeg_player_t *)MPEG_MALLOC(sizeof(mpeg_player_t));
    if(!player) {
        fprintf(stderr, "Out of memory for player\n");
        return NULL;
    }

    MPEG_MEMZERO(player, sizeof(mpeg_player_t));
    player->snd_hnd = SND_STREAM_INVALID;

    player->decoder = plm_create_with_memory(memory, length, 1);
    if(!player->decoder) {
        fprintf(stderr, "Out of memory for player->decoder\n");
        mpeg_player_destroy(player);
        return NULL;
    }

    if(!mpeg_player_init(player, opts))
        return NULL;

    return player;
}

mpeg_player_t *mpeg_player_create(const char *filename) {
    return mpeg_player_create_ex(filename, &MPEG_PLAYER_OPTIONS_DEFAULT);
}

mpeg_player_t *mpeg_player_create_memory(uint8_t *memory, const size_t length) {
    return mpeg_player_create_memory_ex(memory, length, &MPEG_PLAYER_OPTIONS_DEFAULT);
}

int mpeg_player_get_loop(mpeg_player_t *player) {
    if(!player)
        return -1;

    return player->loop;
}

void mpeg_player_set_loop(mpeg_player_t *player, int loop) {
    if(!player)
        return;

    player->loop = loop;
}

void mpeg_player_set_volume(mpeg_player_t *player, uint8_t volume) {
    if(!player)
        return;

    player->snd_volume = volume;
    snd_stream_volume(player->snd_hnd, player->snd_volume);
}

const pvr_poly_hdr_t *mpeg_player_get_texture_hdr(mpeg_player_t *player) {
    if(!player)
        return NULL;

    return &player->hdr;
}

void mpeg_player_get_uv_scale(mpeg_player_t *player, float *u_scale, float *v_scale) {
    if(!player)
        return;

    if(u_scale)
        *u_scale = (float)player->width / (float)player->texture_width;
    if(v_scale)
        *v_scale = (float)player->height / (float)player->texture_height;
}

void mpeg_player_get_dimensions(mpeg_player_t *player, int *width, int *height) {
    if(!player)
        return;

    if(width)
        *width = player->width;
    if(height)
        *height = player->height;
}

void mpeg_player_reset(mpeg_player_t *player) {
    if(player) mpeg_upload_frame_wait(player);
    if(!player)
        return;

    sound_stream_reset(player);
    player->start_time = 0;
    player->frame = NULL;

    if(player->decoder)
        plm_rewind(player->decoder);
}

void mpeg_player_destroy(mpeg_player_t *player) {
    if(player) mpeg_upload_frame_wait(player);
    if(!player)
        return;

    for(int i = 1; i < player->q_slots; i++) {
        if(player->q_tex[i])
            MPEG_PVR_FREE(player->q_tex[i]);
        player->q_tex[i] = NULL;
    }

    if(player->texture) {
        MPEG_PVR_FREE(player->texture);
        player->texture = NULL;
    }

    if(player->snd_hnd != SND_STREAM_INVALID) {
        snd_stream_destroy(player->snd_hnd);
        player->snd_hnd = SND_STREAM_INVALID;
    }

    if(player->snd_buf) {
        MPEG_FREE(player->snd_buf);
        player->snd_buf = NULL;
    }

    if(player->pcm_ring) {
        MPEG_FREE(player->pcm_ring);
        player->pcm_ring = NULL;
    }

    if(player->decoder) {
        plm_destroy(player->decoder);
        player->decoder = NULL;
    }

    mpeg_reader_destroy(player->reader);
    player->reader = NULL;

    MPEG_FREE(player);
}

mpeg_play_result_t mpeg_play_ex(mpeg_player_t *player, const mpeg_cancel_options_t *cancel_options) {
    mpeg_play_result_t result = MPEG_PLAY_NORMAL;

    if(!player || !player->decoder)
        return MPEG_PLAY_ERROR;

    if(player->q_depth > 0)
        return mpeg_play_queued(player, cancel_options);

    /* First frame, then the sound, then the clock: the audio starts playing when
       snd_stream_start returns, so the video clock starts there too. */
    sound_stream_reset(player);
    player->frame = plm_decode_video(player->decoder);
    if(!player->frame) {
        /* Reset some stuff */
        sound_stream_reset(player);
        return MPEG_PLAY_ERROR;
    }
    snd_stream_start(player->snd_hnd, player->sample_rate, AUDIO_CHANNELS - 1);
    player->snd_started = true;
    player->start_time = timer_ns_gettime64();

    /* Time from deciding to show a frame to its scene being submitted (running average). With the
       next decode overlapping the upload this is about one decode, so frames are started that much
       early to appear on time. */
    double lead = 0.0;

    while(true) {
        /* Get elapsed playback time */
        double playback_time = (timer_ns_gettime64() - player->start_time) * 1e-9f;

        /* Check cancel matching */
        int cancel = mpeg_check_cancel(cancel_options);
        if(cancel == 1 || cancel == 2) {
            result = (cancel == 1) ? MPEG_PLAY_CANCEL_INPUT : MPEG_PLAY_CANCEL_RESET;
            goto finish;
        }

        /* Poll audio regardless, and decode one MP2 frame ahead (see PCM_RING_BYTES) */
        snd_stream_poll(player->snd_hnd);
        pcm_ring_decode_ahead(player);

        if(playback_time + lead >= player->frame->time) {
            const uint32_t *shown_buf = player->frame->display;
            double shown_time = player->frame->time;
            plm_frame_t *next = NULL;
            bool decoded = false;

            /* Render the current frame. The upload runs as DMA. */
            pvr_wait_ready();
            pvr_scene_begin();
            int dma = mpeg_upload_frame_start(player);

            /* An I- or P-picture is decoded into another buffer than the one being sent, so the
               next frame can be decoded during the transfer (B-pictures reuse it: wait first). */
            if(dma && player->decoder->video_decoder &&
               player->decoder->video_decoder->frame_current.display != shown_buf) {
                mpeg_update_frame_dropping(player, playback_time + lead);
                next = plm_decode_video(player->decoder);
                decoded = true;
            }
            snd_stream_poll(player->snd_hnd);
            mpeg_upload_frame_wait_decoding(player);

            pvr_list_begin(player->list_type);

            mpeg_draw_frame(player);

            pvr_list_finish();
            pvr_scene_finish();

            double submit_time = (timer_ns_gettime64() - player->start_time) * 1e-9f;
            lead = 0.75 * lead + 0.25 * (submit_time - playback_time);
            (void)shown_time;

            /* Otherwise decode the NEXT frame now, dropping pictures if we are behind */
            if(!decoded) {
                mpeg_update_frame_dropping(player, playback_time + lead);
                next = plm_decode_video(player->decoder);
            }
            player->frame = next;
            if(!player->frame) {
                /* Are we looping? */
                if(!player->loop) {
                    result = MPEG_PLAY_NORMAL;
                    goto finish;
                }

                /* We are looping. Reset and restart (first frame, sound, clock) */
                mpeg_player_reset(player);
                player->frame = plm_decode_video(player->decoder);
                if(!player->frame) {
                    result = MPEG_PLAY_ERROR;
                    goto finish;
                }
                snd_stream_start(player->snd_hnd, player->sample_rate, AUDIO_CHANNELS - 1);
                player->snd_started = true;

                player->start_time = timer_ns_gettime64();
                lead = 0.0;
            }
        }
    }

finish:
    /* Reset some stuff */
    sound_stream_reset(player);
    player->start_time = 0;

    return result;
}

mpeg_play_result_t mpeg_play(mpeg_player_t *player, uint32_t cancel_buttons) {
    mpeg_cancel_options_t opts = {
        .pad_button_any = cancel_buttons
    };

    return mpeg_play_ex(player, &opts);
}

mpeg_decode_result_t mpeg_decode_step(mpeg_player_t *player) {
    if(!player || !player->decoder)
        return MPEG_DECODE_ERROR;

    if(player->start_time == 0) {
        /* Prime the first frame, then start the sound and the clock together */
        sound_stream_reset(player);
        player->frame = plm_decode_video(player->decoder);
        if(!player->frame)
            return MPEG_DECODE_EOF;

        snd_stream_start(player->snd_hnd, player->sample_rate, AUDIO_CHANNELS - 1);
        player->snd_started = true;
        player->start_time = timer_ns_gettime64();

        /* Poll first thing as well since we have a video frame ready */
        snd_stream_poll(player->snd_hnd);
        return MPEG_DECODE_FRAME;
    }

    /* Get elapsed playback time */
    double playback_time = (timer_ns_gettime64() - player->start_time) * 1e-9f;

    /* Poll audio regardless */
    snd_stream_poll(player->snd_hnd);

    /* Check if it's time to decode the next frame */
    if(playback_time >= player->frame->time) {
        mpeg_upload_frame_wait_decoding(player);   /* the next frame may reuse the buffer being sent */
        mpeg_update_frame_dropping(player, playback_time);
        player->frame = plm_decode_video(player->decoder);
        if(player->frame)
            return MPEG_DECODE_FRAME;

        /* Are we looping? */
        if(!player->loop) {
            sound_stream_reset(player);
            player->start_time = 0;
            return MPEG_DECODE_EOF;
        }

        /* We are Looping. Reset and restart (first frame, sound, clock) */
        mpeg_player_reset(player);
        player->frame = plm_decode_video(player->decoder);
        if(!player->frame) {
            sound_stream_reset(player);
            return MPEG_DECODE_EOF;
        }
        snd_stream_start(player->snd_hnd, player->sample_rate, AUDIO_CHANNELS - 1);
        player->snd_started = true;

        player->start_time = timer_ns_gettime64();
        return MPEG_DECODE_FRAME;
    }

    return MPEG_DECODE_IDLE;
}

int mpeg_upload_frame_start(mpeg_player_t *player) {
    if(!player || !player->frame)
        return 0;

    /* HACK: Fix Flycast */
    PVR_SET(PVR_YUV_CFG, (((player->texture_height >> 4) - 1) << 8) |
                      ((player->row_width >> 4) - 1));

    /* Video size in macroblocks (16x16) */
    const int video_blocks_w = player->frame->y.width  >> 4;
    const int video_blocks_h = player->frame->y.height >> 4;

    /*
     * PVR YUV converter stride (in macroblocks).
     * This MUST match the width configured in PVR_YUV_CFG.
     *
     * Example:
     *   player->texture_width = 512 px
     *   → stride = 512 / 16 = 32 macroblocks
     */
    const int pvr_blocks_per_row = player->row_width >> 4;
    const int stride = (int)player->frame->display_stride_mbs;

    /* The decoder's rows already have the converter's stride: one DMA of the whole frame,
       padding blocks included (they are never written; the converter puts them outside the
       visible part of the texture). Each macroblock is 384 bytes. */
    if(stride == pvr_blocks_per_row && !player->dma_busy) {
        size_t bytes = (size_t)video_blocks_h * (size_t)pvr_blocks_per_row * 384;
        if(pvr_dma_yuv_conv(player->frame->display, bytes, false, NULL, NULL) == 0) {
            player->dma_busy = true;
            return 1;
        }
    }

    /* Store queues, padding each row to the converter's stride */
    const int pad_blocks_x = pvr_blocks_per_row - video_blocks_w;

    /*
     * Each macroblock is 384 bytes = 96 uint32_t
     * sq_fast_cpy works in 32-byte chunks → 384 / 32 = 12 iterations
     */
    const int mb_sq_iters = 384 / 32;

    uint32_t *src = player->frame->display;
    uint32_t *d = SQ_MASK_DEST((void *)PVR_TA_YUV_CONV);
    sq_lock((void *)PVR_TA_YUV_CONV);

    for(int y = 0; y < video_blocks_h; y++) {
        /* Upload whole row of real macroblocks */
        sq_fast_cpy(d, src, video_blocks_w * mb_sq_iters);
        src += 96 * stride;

        /* Pad row to PVR stride */
        for(int i = 0; i < pad_blocks_x * mb_sq_iters; i++)
            sq_flush(d);
    }

    sq_unlock();
    return 0;
}

void mpeg_upload_frame_wait(mpeg_player_t *player) {
    if(!player || !player->dma_busy)
        return;
    while(!pvr_dma_ready())
        thd_pass();
    player->dma_busy = false;
}

/* The same wait, decoding audio into the PCM ring instead of spinning (one MP2 frame takes about
   1.5 ms, so the wait can end up to that much after the DMA) */
static void mpeg_upload_frame_wait_decoding(mpeg_player_t *player) {
    if(!player || !player->dma_busy)
        return;
    while(!pvr_dma_ready()) {
        if(!pcm_ring_decode_ahead(player))
            thd_pass();
    }
    player->dma_busy = false;
}

void mpeg_upload_frame(mpeg_player_t *player) {
    mpeg_upload_frame_start(player);
    mpeg_upload_frame_wait(player);
}

void mpeg_draw_frame(mpeg_player_t *player) {
    if(!player || !player->frame)
        return;

    pvr_prim(&player->hdr, sizeof(pvr_poly_hdr_t));

    pvr_prim(&player->vert[0], sizeof(pvr_vertex_t));
    pvr_prim(&player->vert[1], sizeof(pvr_vertex_t));
    pvr_prim(&player->vert[2], sizeof(pvr_vertex_t));
    pvr_prim(&player->vert[3], sizeof(pvr_vertex_t));
}

static int setup_graphics(mpeg_player_t *player, const mpeg_player_options_t *opts) {
    float screen_x = 0.0f;
    float screen_y = 0.0f;
    float screen_width = (float)vid_mode->width;
    float screen_height = (float)vid_mode->height;
    player->width = plm_get_width(player->decoder);
    player->height = plm_get_height(player->decoder);

    /* Check if the w/h ratio matches the screen */
    float video_ratio = (float)player->width / (float)player->height;
    float screen_ratio = screen_width / screen_height;

    /* If the video ratio is not one that will fit the screen nicely when stretched */
    if(fabsf(video_ratio - screen_ratio) > 0.0001f) {
        if(video_ratio > screen_ratio) {
            /* Video is wider than screen, adjust height */
            screen_height = screen_width / video_ratio;
            screen_y = ((float)vid_mode->height - screen_height) / 2.0f;
        } else {
            /* Video is taller than screen, adjust width */
            screen_width = screen_height * video_ratio;
            screen_x = ((float)vid_mode->width - screen_width) / 2.0f;
        }
    }

    player->texture_width = next_power_of_two(player->width);
    player->texture_height = next_power_of_two(player->height);

    /* Stride texture: rows as long as the video is wide. The PVR takes row lengths that are a
       multiple of 32 up to 992; the YUV converter is told the same width, and the decoder lays
       its rows out without padding so a frame still goes out in one DMA. */
    int txr_flags = PVR_TXRFMT_YUV422 | PVR_TXRFMT_NONTWIDDLED;
    player->row_width = player->texture_width;
    if(opts->stride_texture && player->decoder->video_decoder &&
       (size_t)player->width != player->texture_width &&
       (player->width & 31) == 0 && player->width <= 992) {
        player->row_width = player->width;
        txr_flags |= PVR_TXRFMT_X32_STRIDE;
        pvr_txr_set_stride(player->row_width);
        plm_video_set_display_stride_exact(player->decoder->video_decoder, 1);
    }

    player->texture = MPEG_PVR_MALLOC(player->row_width * player->texture_height * 2);
    if(!player->texture) {
        fprintf(stderr, "Failed to allocate PVR memory!\n");
        return -1;
    }

    /* Set SQ to YUV converter. */
    PVR_SET(PVR_YUV_ADDR, (((uint32_t)player->texture) & 0xffffff));
    /* Divide texture width and texture height by 16 and subtract 1.
       The actual values to set are 1, 3, 7, 15, 31, 63. */
    PVR_SET(PVR_YUV_CFG, (((player->texture_height >> 4) - 1) << 8) |
                          ((player->row_width >> 4) - 1));
    PVR_GET(PVR_YUV_CFG);

    /* Clear texture to black. In YUV422 that is Y = 0 with both chroma samples at 128; all-zero
       texels are green, and the bilinear filter blended them into the edges of the picture. */
    sq_set16(player->texture, 0x0080, player->row_width * player->texture_height * 2);

    pvr_poly_cxt_t cxt;
    pvr_poly_cxt_txr(&cxt, player->list_type,
                     txr_flags,
                     player->texture_width, player->texture_height,
                     player->texture,
                     opts->filter_mode);
    /* Do not wrap at the top and left edges (the opposite edge of the texture is not picture) */
    cxt.txr.uv_clamp = PVR_UVCLAMP_UV;
    pvr_poly_compile(&player->hdr, &cxt);

    /* Frame queue: one more texture per queued frame, plus the one on screen. Stops early, or
       stays off, when video memory runs short. */
    player->q_tex[0] = player->texture;
    player->q_hdr[0] = player->hdr;
    player->q_slots = 1;
    player->q_depth = 0;
    {
        size_t tex_bytes = player->row_width * player->texture_height * 2;
        int want = opts->frame_queue > MPEG_QUEUE_MAX ? MPEG_QUEUE_MAX : opts->frame_queue;
        while(player->q_slots < want + 1) {
            pvr_ptr_t t = MPEG_PVR_MALLOC(tex_bytes);
            if(!t)
                break;
            sq_set16(t, 0x0080, tex_bytes);
            pvr_poly_cxt_txr(&cxt, player->list_type,
                             txr_flags,
                             player->texture_width, player->texture_height,
                             t, opts->filter_mode);
            cxt.txr.uv_clamp = PVR_UVCLAMP_UV;
            pvr_poly_compile(&player->q_hdr[player->q_slots], &cxt);
            player->q_tex[player->q_slots++] = t;
        }
        player->q_depth = player->q_slots - 1;
    }

    /* Stop half a texel short of the right and bottom edges, so the filter does not reach the
       texels beyond the picture (padding macroblocks on the right, unused rows below). */
    float u = ((float)player->width - 0.5f) / player->texture_width;
    float v = ((float)player->height - 0.5f) / player->texture_height;
    float left   = screen_x;
    float top    = screen_y;
    float right  = screen_x + screen_width;
    float bottom = screen_y + screen_height;
    int color = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);

    player->vert[0].x = left;
    player->vert[0].y = top;
    player->vert[0].z = 1.0f;
    player->vert[0].u = 0.0f;
    player->vert[0].v = 0.0f;
    player->vert[0].argb = color;
    player->vert[0].oargb = 0;
    player->vert[0].flags = PVR_CMD_VERTEX;

    player->vert[1].x = right;
    player->vert[1].y = top;
    player->vert[1].z = 1.0f;
    player->vert[1].u = u;
    player->vert[1].v = 0.0f;
    player->vert[1].argb = color;
    player->vert[1].oargb = 0;
    player->vert[1].flags = PVR_CMD_VERTEX;

    player->vert[2].x = left;
    player->vert[2].y = bottom;
    player->vert[2].z = 1.0f;
    player->vert[2].u = 0.0f;
    player->vert[2].v = v;
    player->vert[2].argb = color;
    player->vert[2].oargb = 0;
    player->vert[2].flags = PVR_CMD_VERTEX;

    player->vert[3].x = right;
    player->vert[3].y = bottom;
    player->vert[3].z = 1.0f;
    player->vert[3].u = u;
    player->vert[3].v = v;
    player->vert[3].argb = color;
    player->vert[3].oargb = 0;
    player->vert[3].flags = PVR_CMD_VERTEX_EOL;

    return 0;
}

/* Decode one MP2 frame into the PCM ring if it has room. Returns 0 when the ring is full or the audio
   has ended. */
static int pcm_ring_decode_one(mpeg_player_t *player) {
    if(!player->pcm_ring || player->pcm_ring_used + PCM_FRAME_BYTES > PCM_RING_BYTES)
        return 0;

    plm_samples_t *smp = plm_decode_audio(player->decoder);
    if(!smp) {
        player->pcm_ring_eof = true;
        return 0;
    }

    int w = player->pcm_ring_read + player->pcm_ring_used;
    if(w >= PCM_RING_BYTES)
        w -= PCM_RING_BYTES;
    int first = PCM_RING_BYTES - w;
    if(first > PCM_FRAME_BYTES)
        first = PCM_FRAME_BYTES;
    memcpy(player->pcm_ring + w, smp->pcm, first);
    if(first < PCM_FRAME_BYTES)
        memcpy(player->pcm_ring, (const uint8_t *)smp->pcm + first, PCM_FRAME_BYTES - first);
    player->pcm_ring_used += PCM_FRAME_BYTES;
    return 1;
}

/* Decode one MP2 frame ahead while the stream is playing, unless the audio has ended */
static int pcm_ring_decode_ahead(mpeg_player_t *player) {
    if(!player->snd_started || player->pcm_ring_eof)
        return 0;
    return pcm_ring_decode_one(player);
}

static void *sound_callback(snd_stream_hnd_t hnd, int request_size, int *size_out) {
    mpeg_player_t *player = (mpeg_player_t *)snd_stream_get_userdata(hnd);
    uint8_t *dest = player->snd_buf;
    int out = 0;

    /* Whatever the play loop has not decoded ahead is decoded now */
    while(player->pcm_ring_used < request_size && !player->in_video_decode && pcm_ring_decode_one(player))
        ;

    /* The usual case: a whole request in one piece of the ring, handed over in place (KOS copies it
       into its own channel buffers before returning to the player) */
    if(player->pcm_ring_used >= request_size && player->pcm_ring_read + request_size <= PCM_RING_BYTES) {
        uint8_t *src = player->pcm_ring + player->pcm_ring_read;
        player->pcm_ring_used -= request_size;
        player->pcm_ring_read += request_size;
        if(player->pcm_ring_read == PCM_RING_BYTES)
            player->pcm_ring_read = 0;
        *size_out = request_size;
        return src;
    }

    while(out < request_size && player->pcm_ring_used > 0) {
        int chunk = PCM_RING_BYTES - player->pcm_ring_read;
        if(chunk > player->pcm_ring_used)
            chunk = player->pcm_ring_used;
        if(chunk > request_size - out)
            chunk = request_size - out;

        /* A plain copy: KOS reads snd_buf through the cache (snd_pcm16_split), so a store-queue copy,
           which bypasses the cache, let it read lines left over from the previous request. */
        memcpy(dest + out, player->pcm_ring + player->pcm_ring_read, chunk);
        out += chunk;
        player->pcm_ring_used -= chunk;
        player->pcm_ring_read += chunk;
        if(player->pcm_ring_read == PCM_RING_BYTES)
            player->pcm_ring_read = 0;
    }

    if(out < request_size)
        MPEG_MEMZERO(dest + out, request_size - out);

    *size_out = request_size;

    return player->snd_buf;
}

static int setup_audio(mpeg_player_t *player) {
    player->pcm_ring_read = 0;
    player->pcm_ring_used = 0;
    player->pcm_ring_eof = false;
    player->sample_rate = plm_get_samplerate(player->decoder);

    player->snd_hnd = snd_stream_alloc(sound_callback, SOUND_BUFFER);
    if(player->snd_hnd == SND_STREAM_INVALID)
        return -1;

    snd_stream_volume(player->snd_hnd, player->snd_volume);
    snd_stream_set_userdata(player->snd_hnd, player);

    return 0;
}


/* ---------------------------------------------------------------------------------------------
   Play loop with a queue of decoded frames (mpeg_player_options_t.frame_queue > 0).

   The plain loop decodes each picture just before it is due, so a picture that takes longer than
   a frame period (every I-picture of a large video) is shown late, and so are the ones after it
   until the decoder has caught up. Here the decoder runs ahead: every decoded frame is converted
   straight into the next texture of a ring, and a frame is shown when its time comes, also in the
   middle of a long decode (the decoder calls mpeg_queue_slice_cb after every slice).

   - The YUV converter only returns to its start, and only takes a new destination texture, when
     the tile accelerator is initialised. The plain loop gets that from the scene around every
     upload; here mpeg_queue_try_upload initialises it before each one.
   - For that reason uploads and scenes exclude each other in both directions (KOS initialises
     the tile accelerator itself at the vblank after a scene has been submitted): a frame is not
     shown while a DMA runs, and a DMA is not started while a submitted scene waits for its vblank.
   - The decoder reuses the buffer of a returned frame two pictures later, so one decoded frame may
     wait for its upload while the next picture is decoded (it is then uploaded from the slice
     callback, once the vblank has passed); with two waiting the decoder stops. A B-picture is
     decoded into the buffer the next decode writes and is uploaded completely first.
   - The ring has one texture more than the queue holds: the frame on screen keeps its texture
     until another frame has been shown.
   - Audio is decoded between pictures only (video and audio share the demuxer). */

#define MPEG_PVR_TA_INIT    0x0144       /* PVR register: tile accelerator initialise */
#define MPEG_PVR_TA_INIT_GO 0x80000000

#define MPEG_QUEUE_LEAD (1.0 / 60.0)   /* a scene is on screen about one field after it is submitted */
#define MPEG_QUEUE_DROP_LATE 0.1       /* seconds overdue before a queued frame is left out */

static inline double mpeg_queue_now(mpeg_player_t *player) {
    return (timer_ns_gettime64() - player->start_time) * 1e-9;
}

/* Show the frame at the head of the queue if it is due. Returns 1 if a frame was shown. */
static int mpeg_queue_present(mpeg_player_t *player) {
    if(!player->q_playing || player->q_count == 0)
        return 0;

    double now = mpeg_queue_now(player) + MPEG_QUEUE_LEAD;

    /* A little behind (a slow disc read, say): the waiting frames are shown one per field until
       the picture has caught up. Far behind: frames are left out instead. */
    while(player->q_count > 1 && now - player->q_time[player->q_head] > MPEG_QUEUE_DROP_LATE) {
        player->q_head = (player->q_head + 1) % player->q_slots;
        player->q_count--;
    }

    if(now < player->q_time[player->q_head])
        return 0;

    /* The conversion DMA and the scene's vertices are not sent at the same time */
    if(player->dma_busy) {
        if(!pvr_dma_ready())
            return 0;
        player->dma_busy = false;
    }

    if(pvr_check_ready() < 0)
        return 0;

    pvr_scene_begin();
    pvr_list_begin(player->list_type);
    pvr_prim(&player->q_hdr[player->q_head], sizeof(pvr_poly_hdr_t));
    pvr_prim(&player->vert[0], sizeof(pvr_vertex_t));
    pvr_prim(&player->vert[1], sizeof(pvr_vertex_t));
    pvr_prim(&player->vert[2], sizeof(pvr_vertex_t));
    pvr_prim(&player->vert[3], sizeof(pvr_vertex_t));
    pvr_list_finish();
    pvr_scene_finish();

    player->q_head = (player->q_head + 1) % player->q_slots;
    player->q_count--;
    return 1;
}

/* Start converting the oldest waiting frame into the next free texture, if that is possible now.
   Returns 1 if an upload was started. */
static int mpeg_queue_try_upload(mpeg_player_t *player) {
    if(player->q_npend == 0 || player->q_count >= player->q_depth)
        return 0;

    if(player->dma_busy) {
        if(!pvr_dma_ready())
            return 0;
        player->dma_busy = false;
    }

    /* A submitted scene is waiting for its vblank */
    if(pvr_check_ready() < 0)
        return 0;

    int slot = (player->q_head + player->q_count) % player->q_slots;

    /* The converter is set up for the whole texture height and gets fewer rows, and it only goes
       back to its start (and to a new destination) when the tile accelerator is initialised:
       writing its address or configuration alone does not do it. No scene is being collected
       here (checked above), so the initialisation does not lose anything. */
    PVR_SET(PVR_YUV_ADDR, (((uint32_t)player->q_tex[slot]) & 0xffffff));
    PVR_SET(PVR_YUV_CFG, (((player->texture_height >> 4) - 1) << 8) |
                          ((player->row_width >> 4) - 1));
    PVR_SET(MPEG_PVR_TA_INIT, MPEG_PVR_TA_INIT_GO);
    (void)PVR_GET(MPEG_PVR_TA_INIT);
    player->frame = &player->q_pend[0];
    mpeg_upload_frame_start(player);
    player->frame = NULL;

    player->q_time[slot] = player->q_pend[0].time;
    player->q_count++;
    player->q_hold_decode = player->q_pend_reused[0];

    player->q_pend[0] = player->q_pend[1];
    player->q_pend_reused[0] = player->q_pend_reused[1];
    player->q_npend--;
    return 1;
}

/* May the next picture be decoded now? */
static bool mpeg_queue_can_decode(mpeg_player_t *player) {
    if(player->q_npend >= 2)
        return false;

    if(player->dma_busy && pvr_dma_ready())
        player->dma_busy = false;

    /* The frame being sent, or the one waiting, sits in the buffer the decoder writes next */
    if(player->q_hold_decode && player->dma_busy)
        return false;
    if(player->q_npend == 1 && (player->q_pend_reused[0] || player->dma_busy))
        return false;

    return true;
}

/* Called by the decoder after every slice of a picture */
static void mpeg_queue_slice_cb(plm_video_t *video, void *user) {
    mpeg_player_t *player = (mpeg_player_t *)user;
    (void)video;

    if(!player->q_playing)
        return;

    snd_stream_poll(player->snd_hnd);
    mpeg_queue_present(player);
    mpeg_queue_try_upload(player);
}

/* Decode audio until the ring holds a whole stream request. The sound stream is polled during a
   video decode as well, where no audio can be decoded (see in_video_decode), so the data for the
   next request has to be there before a picture is started. */
static void mpeg_queue_fill_audio(mpeg_player_t *player) {
    while(!player->pcm_ring_eof && player->pcm_ring_used < SOUND_BUFFER && pcm_ring_decode_one(player))
        ;
}

static mpeg_play_result_t mpeg_play_queued(mpeg_player_t *player, const mpeg_cancel_options_t *cancel_options) {
    mpeg_play_result_t result = MPEG_PLAY_NORMAL;
    plm_video_t *video = player->decoder->video_decoder;
    plm_frame_t *decoded;
    bool ended;
    double newest;             /* time of the last frame the decoder returned */

    if(video)
        plm_video_set_slice_callback(video, mpeg_queue_slice_cb, player);

restart:
    sound_stream_reset(player);
    mpeg_upload_frame_wait(player);
    player->q_head = 0;
    player->q_count = 0;
    player->q_npend = 0;
    player->q_hold_decode = false;
    player->q_playing = false;
    ended = false;
    newest = 0.0;

    while(true) {
        int cancel = mpeg_check_cancel(cancel_options);
        if(cancel == 1 || cancel == 2) {
            result = (cancel == 1) ? MPEG_PLAY_CANCEL_INPUT : MPEG_PLAY_CANCEL_RESET;
            goto finish;
        }

        if(player->q_playing) {
            snd_stream_poll(player->snd_hnd);
            mpeg_queue_present(player);
        }

        mpeg_queue_try_upload(player);

        if(!ended && mpeg_queue_can_decode(player)) {
            /* Behind with nothing queued: drop pictures as the plain loop does */
            if(player->q_playing) {
                double period = 1.0 / plm_get_framerate(player->decoder);
                double late = mpeg_queue_now(player) - newest;
                plm_set_video_skip_b_frames(player->decoder, late > period);
                if(late > MPEG_SKIP_TO_INTRA_LATE)
                    plm_set_video_skip_to_intra(player->decoder);
            }

            mpeg_queue_fill_audio(player);
            player->in_video_decode = true;
            decoded = plm_decode_video(player->decoder);
            player->in_video_decode = false;

            if(!decoded) {
                ended = true;
                if(!player->q_playing && player->q_count == 0 && player->q_npend == 0) {
                    result = MPEG_PLAY_ERROR;      /* not one frame */
                    goto finish;
                }
                continue;
            }

            if(!video) {
                video = player->decoder->video_decoder;
                plm_video_set_slice_callback(video, mpeg_queue_slice_cb, player);
            }

            /* The decoder's frame structures are reused: keep a copy */
            newest = decoded->time;
            player->q_pend[player->q_npend] = *decoded;
            player->q_pend_reused[player->q_npend] = decoded->display == video->frame_current.display;
            player->q_npend++;
            continue;
        }

        /* Start the sound and the clock once the queue is full (or the video is that short) */
        if(!player->q_playing) {
            if(player->q_count < player->q_depth && player->q_npend > 0)
                continue;                          /* an upload is still on its way */

            mpeg_queue_fill_audio(player);
            snd_stream_start(player->snd_hnd, player->sample_rate, AUDIO_CHANNELS - 1);
            player->snd_started = true;
            player->start_time = timer_ns_gettime64();
            player->q_playing = true;
            continue;
        }

        /* The video has ended and everything decoded has been shown */
        if(ended && player->q_npend == 0 && player->q_count == 0) {
            if(!player->loop)
                goto finish;

            mpeg_player_reset(player);
            goto restart;
        }

        /* Waiting for a frame's time, a vblank or a texture: use the time for audio */
        if(!pcm_ring_decode_ahead(player))
            thd_pass();
    }

finish:
    mpeg_upload_frame_wait(player);
    if(video)
        plm_video_set_slice_callback(video, NULL, NULL);
    PVR_SET(PVR_YUV_ADDR, (((uint32_t)player->texture) & 0xffffff));
    player->q_playing = false;
    player->q_count = 0;
    player->q_npend = 0;
    player->frame = NULL;
    sound_stream_reset(player);
    player->start_time = 0;

    return result;
}
