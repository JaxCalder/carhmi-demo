#ifndef CARHMI_SFX_H
#define CARHMI_SFX_H

#include "dmod.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * The sounds of the car (sfx_synth.c) played beside the music: a thread of its own makes them and writes them into
 * a stream of libdmaudio, which the mixer of the device adds to the player's. The stream is open only while
 * something plays. The rest of carhmi only says what is wanted and asks what is heard.
 */

typedef struct sfx* sfx_t;

/** @brief The sounds, silent; NULL when out of memory (carhmi goes on without them) */
sfx_t sfx_new(void);

/** @brief Ask the thread to stop (it does not wait: sfx_stop() on everything, then sfx_free()) */
void sfx_stop(sfx_t s);

/** @brief Stop and free; safe on NULL */
void sfx_free(sfx_t s);

/** @brief The engine: true starts it (the starter, the catch, the idle), false lets it die down */
void sfx_engine_set(sfx_t s, bool on);

/** @brief The siren: true wails until false */
void sfx_siren_set(sfx_t s, bool on);

/** @brief What is wanted: the engine started (and not yet stopped) */
bool sfx_engine_wanted(sfx_t s);
bool sfx_siren_wanted(sfx_t s);

/** @brief The engine is past its start: it idles, or it is off (false: it is cranking, catching, settling) */
bool sfx_engine_started(sfx_t s);

#endif /* CARHMI_SFX_H */
