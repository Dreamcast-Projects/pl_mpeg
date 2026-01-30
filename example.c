#include <kos.h>
#include "mpeg.h"

int keyboard_skip(void)
{
  maple_device_t *kbd = maple_enum_dev(1, MAPLE_FUNC_KEYBOARD);
  if (!kbd)
    return 0;

  kbd_state_t *st = (kbd_state_t *)maple_dev_status(kbd);
  if (!st)
    return 0;

  return st->matrix[KBD_KEY_ENTER] || st->matrix[KBD_KEY_SPACE];
}

int main(void) {
    pvr_init_defaults();
    snd_stream_init();

    mpeg_player_t *player;

    player = mpeg_player_create("/rd/orgface.mpg");
    if(player == NULL)
        return -1;

    mpeg_player_set_loop(player, 1);

    mpeg_play(player, CONT_START, keyboard_skip);

    mpeg_player_destroy(player);

    return 0;
}
