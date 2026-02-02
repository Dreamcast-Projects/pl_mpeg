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

#include <dc/pvr/pvr_header.h>

/* You can also define PLM_MALLOC, PLM_REALLOC and PLM_FREE to provide your own
   memory management functions. */

#ifndef MPEG_MALLOC
	#define MPEG_MALLOC(sz)      malloc(sz)
	#define MPEG_FREE(p)         free(p)
	#define MPEG_REALLOC(p, sz)  realloc((p), (sz))
    #define MPEG_MEMALIGN(a, sz) memalign((a), (sz))
#endif

#ifndef MPEG_PVR_MALLOC
	#define MPEG_PVR_MALLOC(sz)  pvr_mem_malloc(sz)
	#define MPEG_PVR_FREE(p)     pvr_mem_free(p)
#endif

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

typedef struct mpeg_player_options_t {
    pvr_list_type_t   player_list_type;
    pvr_filter_mode_t player_filter_mode;

    uint8_t player_volume;
    bool    player_loop;
} mpeg_player_options_t;

/** \brief   Create an MPEG player instance with custom options.
    \ingroup mpeg_playback

    This function initializes an MPEG player for video and audio playback using
    the specified filename and a set of player options. It behaves like
    `mpeg_player_create()` but allows customization of playback parameters such
    as volume, looping behavior, and PVR rendering options.

    If the \p options parameter is NULL, default player settings are used.

    \param  filename        The filename of the MPEG file to be played.
                            Must not be NULL.
    \param  options         Optional pointer to a mpeg_player_options_t structure
                            specifying playback and rendering options.
                            May be NULL to use defaults.
    \return                 A pointer to an initialized mpeg_player_t structure,
                            or NULL if initialization fails at any stage.
*/
mpeg_player_t *mpeg_player_create_ex(const char *filename, const mpeg_player_options_t *options);

/** \brief   Create an MPEG player instance from memory with custom options.
    \ingroup mpeg_playback

    This function initializes an MPEG player for video and audio playback using
    MPEG data stored entirely in memory and a set of player options. It behaves
    like `mpeg_player_create_memory()` but allows customization of playback
    parameters such as volume, looping behavior, and PVR rendering options.

    If the \p options parameter is NULL, default player settings are used.

    \param  memory          Pointer to the MPEG data in memory.
                            Must not be NULL.
    \param  length          Size of the MPEG data in bytes.
    \param  options         Optional pointer to a mpeg_player_options_t structure
                            specifying playback and rendering options.
                            May be NULL to use defaults.
    \return                 A pointer to an initialized mpeg_player_t structure,
                            or NULL if initialization fails at any stage.
*/
mpeg_player_t *mpeg_player_create_memory_ex(unsigned char *memory, const size_t length, const mpeg_player_options_t *options);

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

/** \brief   Input cancellation options for MPEG playback.
    \ingroup mpeg_playback

    This structure defines user input combinations that can cancel MPEG video playback
    when passed to `mpeg_play_ex()`.

    It supports cancel detection via:
    - Controller buttons (any or combo)
    - Keyboard keys (any or combo)

    Each group is optional — set unused fields to 0 or NULL. If both controller and
    keyboard cancel checks are defined, either can trigger cancellation.

    Use this to implement fine-grained control over when playback should exit,
    such as when the Escape key is pressed, or a button combo like L+R+START.

    Example usage:
    \code
    const uint16_t keys[] = { KBD_KEY_ESCAPE };
    mpeg_cancel_options_t opts = {
        .pad_button_any = CONT_START,
        .kbd_keys_any = keys,
        .kbd_keys_any_count = 1
    };
    mpeg_play_ex(player, &opts);
    \endcode
*/
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

/** \brief   Decode the next video frame step (non-blocking).
    \ingroup mpeg_playback

    This function performs a single decoding step for the MPEG player. It checks
    whether it's time to decode a new video frame based on synchronization with
    the audio playback time. If decoding is required, it attempts to decode the
    next frame from the video stream and updates the internal timing.

    This function is useful for use in a game loop or custom playback control logic.

    \param  player      The MPEG player instance. Must be initialized.
    \return             0 if decoding succeeded or wasn't needed.
                        Returns -1 if the player or decoder is NULL.
                        Returns 0 if decoding reached the end of stream and not looping.
 */
int mpeg_decode_step(mpeg_player_t *player);

/** \brief   Upload the most recently decoded video frame to PVR YUV converter memory.
    \ingroup mpeg_playback

    This function transfers the latest decoded frame from the MPEG decoder's internal
    buffer into the PVR YUV converter memory using DMA-friendly store queues.

    The frame must have already been decoded using `mpeg_decode_step()` or
    through the playback loop.

    \param  player      The MPEG player instance. Must be initialized and must
                        have a valid frame decoded.
 */
void mpeg_upload_frame(mpeg_player_t *player);

/** \brief   Render the most recently uploaded frame to the screen.
    \ingroup mpeg_playback

    This function draws the currently uploaded MPEG frame using the Dreamcast's PVR
    rendering system. It assumes that `mpeg_upload_frame()` has already been called
    for the current frame and that a PVR scene is active.

    The function submits a single textured quad using the PVR YUV texture and
    compiled polygon header.

    \param  player      The MPEG player instance. Must be initialized.
 */
void mpeg_draw_frame(mpeg_player_t *player);

/** \brief   Start the audio stream for MPEG playback.
    \ingroup mpeg_playback

    Starts the Dreamcast AICA sound streaming system for MPEG playback. This is
    typically used when integrating MPEG decoding into custom loops, and may be
    automatically called by `mpeg_play()` or `mpeg_play_ex()`.

    \param  player      The MPEG player instance. Must be initialized.
 */
void mpeg_snd_stream_start(mpeg_player_t *player);

/** \brief   Stop the audio stream for MPEG playback.
    \ingroup mpeg_playback

    Stops the audio stream started with `mpeg_snd_stream_start()`. This function is
    typically used in cleanup or when pausing playback externally.

    \param  player      The MPEG player instance. Must be initialized.
 */
void mpeg_snd_stream_stop(mpeg_player_t *player);

#ifdef __cplusplus
}
#endif

#endif
