#ifndef _MPEG_H_INCLUDED_
#define _MPEG_H_INCLUDED_

#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

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

#ifdef __cplusplus
}
#endif

#endif
