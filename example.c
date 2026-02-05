/**
 * \file example.c
 * \brief MPEG Playback Example for Dreamcast
 *
 * This example demonstrates three ways to use the MPEG library:
 * - Simple legacy usage
 * - Recommended extended usage with cancel options
 * - Manual control (decode + render loop)
 *
 * Only one example should be called from `main()`. Comment out others.
 *
 * ---
 * ## 🔧 Custom Memory and File Handling
 * You can override allocators or file ops by defining these macros **before** including mpeg.h:
 *
 * ```c
 * #define MPEG_MALLOC(sz)           my_malloc(sz)
 * #define MPEG_FREE(p)              my_free(p)
 * #define MPEG_REALLOC(p, sz)       my_realloc(p, sz)
 * #define MPEG_MEMALIGN(a, sz)      my_memalign(a, sz)
 * #define MPEG_MEMZERO(p, sz)       my_memzero(p, sz)
 *
 * #define MPEG_PVR_MALLOC(sz)       my_pvr_malloc(sz)
 * #define MPEG_PVR_FREE(p)          my_pvr_free(p)
 *
 * #define MPEG_FILE_TYPE            my_file_t
 * #define MPEG_FILE_INVALID_HANDLE  MY_INVALID_HANDLE
 * #define MPEG_FILE_OPEN(fn)        my_file_open(fn)
 * #define MPEG_FILE_CLOSE(fh)       my_file_close(fh)
 * #define MPEG_FILE_SEEK(...)       my_file_seek(...)
 * #define MPEG_FILE_READ(...)       my_file_read(...)
 * #define MPEG_FILE_TELL(fh)        my_file_tell(fh)
 * ```
 */

#include <kos.h>
#include "mpeg.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

/* --- 1. Simple API (Legacy) --- */
static void play_simple(void) {
    mpeg_player_t *player = mpeg_player_create("/rd/sample.mpg");
    if(!player)
        return;

    mpeg_player_set_loop(player, 1);
    mpeg_play(player, CONT_START);
    mpeg_player_destroy(player);
}

/* --- 2. Extended API (Recommended) --- */
static void play_extended(void) {
    mpeg_player_options_t options = MPEG_PLAYER_OPTIONS_INITIALIZER;
    options.loop = true;
    mpeg_player_t *player = mpeg_player_create_ex("/rd/sample.mpg", &options);
    if(!player)
        return;

    const mpeg_cancel_options_t cancel_opts = {
        .pad_button_any = CONT_START,
        .pad_button_combo = CONT_A | CONT_B,
        .kbd_keys_any = (const uint16_t[]){ KBD_KEY_ENTER },
        .kbd_keys_any_count = 1
    };
    mpeg_play_ex(player, &cancel_opts);
    mpeg_player_destroy(player);
}

/* --- 3. Manual Frame Control (Advanced) --- */
static void play_manual(void) {
    mpeg_player_options_t options = MPEG_PLAYER_OPTIONS_INITIALIZER;
    options.list_type = PVR_LIST_TR_POLY;
    options.loop = true;

    mpeg_player_t *player = mpeg_player_create_ex("/rd/sample.mpg", &options);
    if(!player)
        return;

    bool done = false;

    /* Game Loop */
    while(!done) {
        /* Handle input (cancel) */
        MAPLE_FOREACH_BEGIN(MAPLE_FUNC_CONTROLLER, cont_state_t, st)
            if((st->buttons & CONT_START) == CONT_START) {
                done = true;
            }
        MAPLE_FOREACH_END()

        if(done)
            break;

        mpeg_decode_result_t result = mpeg_decode_step(player);
        if(result == MPEG_DECODE_EOF)
            break;

        pvr_scene_begin();
        if(result == MPEG_DECODE_FRAME)
            mpeg_upload_frame(player);

        pvr_list_begin(PVR_LIST_TR_POLY);

        mpeg_draw_frame(player);

        pvr_list_finish();
        pvr_scene_finish();
    }

    mpeg_player_destroy(player);
}

#pragma GCC diagnostic pop

int main(void) {
    pvr_init_defaults();
    snd_stream_init();

    /* --- Select one of the following to test --- */

    play_simple();
    //play_extended();
    //play_manual();

    return 0;
}
