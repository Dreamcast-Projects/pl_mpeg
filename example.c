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

    mpeg_play(player, CONT_START);

    mpeg_player_destroy(player);

    return 0;
}
