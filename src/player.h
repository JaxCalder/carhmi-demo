#ifndef CARHMI_PLAYER_H
#define CARHMI_PLAYER_H

#include "dmod.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * The MP3 player of carhmi: the files of a directory, played one after the
 * other by a thread of its own - decoded by dmmp3, played through libdmaudio
 * (the device's mixer paces it).
 */

typedef struct player_track
{
    char*       path;
    char*       title;
    char*       artist;
    uint32_t    duration_ms;
} player_track_t;

typedef struct player* player_t;

/** @brief What the player is at: a snapshot */
typedef struct
{
    int32_t     track;          /**< -1: none */
    bool        playing;
    uint32_t    position_ms;
    uint32_t    duration_ms;
    uint32_t    changes;        /**< Grows when the track, or playing, changed */
} player_state_t;

/** @brief The MP3 files of `dir` (sorted by name), a thread to play them. NULL: out of memory */
player_t player_new(const char* dir);
void player_free(player_t p);

uint32_t player_count(player_t p);
const player_track_t* player_track(player_t p, uint32_t index);
void player_load_track_info(player_t p, uint32_t index);

void player_play(player_t p, uint32_t index);      /* From its start */
void player_toggle(player_t p);
void player_next(player_t p, int32_t step);         /* +1 / -1, around */
void player_state(player_t p, player_state_t* state);

#endif /* CARHMI_PLAYER_H */
