/**
    MPEG1 Decode Library for Dreamcast - Version 0.8 (2023/09/19)
    Originally ported by Tashi (aka Twada)
    Further edits done by Ian Robinson and Andy Barajas

    Overview:
    This library facilitates the playback of MPEG1 videos on the Sega Dreamcast console.
    It supports monaural audio and allows specifying a cancel button during playback.

    Key Features:
    - Video Playback: MPEG1 video playback.
    - Audio Support: Mono audio playback. Stereo videos will play only the left channel.
    - Cancel Button: Allows specifying a controller button combination to cancel playback.
    - Recommended Resolutions:
      - 4:3 Aspect Ratio: 320x240 pixels, Mono audio at 80kbits.
      - 16:9 Aspect Ratio: 368x208 pixels, Mono audio at 80kbits.

    To create compatible MPEG1 videos, use the following ffmpeg command:

    ffmpeg -i input.mp4 -vf "scale=320:240" -b:v 742k -minrate 742k -maxrate 742k -bufsize 742k -ac 1 -ar 32000 -c:a mp2 -b:a 64k -f mpeg output.mpg
 */

#ifndef _MPEG_H_INCLUDED_
#define _MPEG_H_INCLUDED_

#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

typedef struct mpeg_cancel_options_t {
    /* Pad */
    uint32_t pad_button_any;    /* Any of these triggers cancel */
    uint32_t pad_button_combo;  /* All of these must be held to cancel */

    /* Keyboard */
    const uint16_t *kbd_keys_any;   /* Array of keys - cancel if any pressed*/
    size_t kbd_keys_any_count;

    const uint16_t *kbd_keys_combo; /* Array of keys - Cancel only if all of these keys are pressed */
    size_t kbd_keys_combo_count;
} mpeg_cancel_options_t;

typedef struct mpeg_player_t mpeg_player_t;

/** \brief   Create an MPEG player instance.
    \ingroup mpeg_playback

    This function initializes an MPEG player for video and audio playback.
    It allocates memory for the mpeg_player_t structure, initializes the MPEG
    decoder with the given filename, and sets up the graphics and audio systems
    for playback.

    \param  filename        The filename of the MPEG file to be played. Must not be NULL.
    \return                 A pointer to an initialized mpeg_player_t structure,
                            or NULL if initialization fails at any stage.
*/
mpeg_player_t *mpeg_player_create(const char *filename);

/** \brief   Create an MPEG player instance from memory.
    \ingroup mpeg_playback

    This function initializes an MPEG player for video and audio playback
    using MPEG data stored in memory. It allocates memory for the mpeg_player_t
    structure, initializes the MPEG decoder with the provided memory buffer,
    and sets up the graphics and audio systems for playback.

    \param  memory          The pointer to the MPEG data in memory. Must not be NULL.
    \param  length          The size of the MPEG data in bytes.
    \return                 A pointer to an initialized mpeg_player_t structure,
                            or NULL if initialization fails at any stage.
*/
mpeg_player_t *mpeg_player_create_memory(unsigned char *memory, const size_t length);

/**
    \brief   Retrieves the loop status of the MPEG player.
    \ingroup mpeg_playback

    This function checks whether the MPEG player is set to loop playback.

    \param   player  The MPEG player instance.
    \return          An integer representing the loop status (non-zero for loop).
 */
int mpeg_player_get_loop(mpeg_player_t *player);

/**
    \brief   Sets the loop status of the MPEG player.
    \ingroup mpeg_playback

    This function configures the MPEG player to either loop or not loop playback
    based on the provided loop parameter.

    \param   player  The MPEG player instance to configure.
    \param   loop    An integer indicating the desired loop status (non-zero for loop).
 */
void mpeg_player_set_loop(mpeg_player_t *player, int loop);

/** \brief   Destroy an MPEG player instance.
    \ingroup mpeg_playback

    This function releases all resources associated with an MPEG player. It
    frees the memory allocated for the mpeg_player_t structure and any internal
    components such as the MPEG decoder, texture, and sound buffer. If a valid
    sound handle exists, it is also destroyed.

    \param  player          The pointer to the mpeg_player_t structure to be destroyed.
                            If NULL, the function does nothing.
*/
void mpeg_player_destroy(mpeg_player_t *player);

/** \brief   Play an MPEG video using an MPEG player.
    \ingroup mpeg_playback

    This function starts the playback of an MPEG video using the specified
    MPEG player instance. It continuously decodes video frames and handles
    audio streaming while checking for cancellation inputs via controller buttons.

    \param  player          The MPEG player instance used for playback. Must be initialized.
    \param  cancel_buttons  A bit mask of controller buttons that can cancel the playback.
    \return                 An integer indicating the reason for playback termination.
                            Returns -1 if player or decoder is NULL. Returns a non-negative
                            value representing cancellation status otherwise.
*/
int mpeg_play(mpeg_player_t *player, uint32_t cancel_buttons);

/** \brief   Play an MPEG video with extended input cancel options.
    \ingroup mpeg_playback

    This function starts playback of an MPEG video using the specified MPEG player
    instance. It continuously decodes video frames, renders them, and streams audio
    while checking for input-based cancellation. Unlike the simpler mpeg_play() variant,
    this function allows for more granular cancellation input through controller button
    masks (any or combo) and keyboard key matching (any or combo).

    Use this function if you need fine-grained control over what input combinations
    cancel video playback (e.g., keyboard escape key, button combos, or reset patterns).

    \param  player          The MPEG player instance used for playback. Must be initialized.
    \param  cancel_options  A pointer to a mpeg_cancel_options_t struct describing
                            which controller and/or keyboard inputs should cancel playback.
                            May be NULL to disable cancel checks.
    \return                 An integer indicating the reason for playback termination.
                            Returns -1 if the player or decoder is NULL.
                            Returns 1 if cancelled via controller or keyboard.
                            Returns 2 if cancelled via reset combo (ABXY+START).
                            Returns 0 if playback finished normally.
*/
int mpeg_play_ex(mpeg_player_t *player, const mpeg_cancel_options_t *cancel_options);

#ifdef __cplusplus
}
#endif

#endif
