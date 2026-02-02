#include <kos.h>
#include "mpeg.h"

int main(void) {
    pvr_init_defaults();
    snd_stream_init();

    mpeg_player_t *player;

    player = mpeg_player_create("/rd/orgface.mpg");
    if(player == NULL)
        return -1;

    mpeg_player_set_loop(player, 1);

    const mpeg_cancel_options_t skip_opts = {
        .pad_button_any = CONT_START,
        .kbd_keys_any = (const uint16_t[]){ KBD_KEY_ENTER, KBD_KEY_SPACE },
        .kbd_keys_any_count = 2
    };
    mpeg_play_ex(player, &skip_opts);

    //mpeg_play(player, CONT_START);

    mpeg_player_destroy(player);

    return 0;
}
