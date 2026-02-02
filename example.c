#include <kos.h>

#include "mpeg.h"

int main(void) {
    pvr_init_defaults();
    snd_stream_init();

    mpeg_player_t *player;

    static const mpeg_player_options_t default_options = {
        .player_list_type   = PVR_LIST_TR_POLY,
        .player_filter_mode = PVR_FILTER_NONE,
        .player_volume      = 127,
        .player_loop        = true
    };

    player = mpeg_player_create_ex("/rd/ManiaHP.mpg", &default_options);
    if(player == NULL)
        return -1;

    //mpeg_player_set_loop(player, 1);

    // const mpeg_cancel_options_t skip_opts = {
    //     .pad_button_any = CONT_START,
    //     .kbd_keys_any = (const uint16_t[]){ KBD_KEY_ENTER, KBD_KEY_SPACE },
    //     .kbd_keys_any_count = 2
    // };
    // mpeg_play_ex(player, &skip_opts);

    //mpeg_play(player, CONT_START);

    mpeg_snd_stream_start(player);

    mpeg_decode_result_t decode_result = MPEG_DECODE_IDLE;
    while(1) {
        MAPLE_FOREACH_BEGIN(MAPLE_FUNC_CONTROLLER, cont_state_t, st)
            if(CONT_START && ((st->buttons & CONT_START) == CONT_START)) {
                /* Push cancel buttons */
                break;
            }
            if(st->buttons == CONT_RESET_BUTTONS) {
                /* ABXY + START (Software reset) */
                break;
            }
        MAPLE_FOREACH_END()

        decode_result = mpeg_decode_step(player);

        /* Render */
        pvr_scene_begin();
        pvr_list_begin(PVR_LIST_TR_POLY);

        //if(decode_result == MPEG_DECODE_FRAME) {
            mpeg_upload_frame(player);
            mpeg_draw_frame(player);
        //}

        pvr_list_finish();
        pvr_scene_finish();
    }

    mpeg_player_destroy(player);

    return 0;
}
