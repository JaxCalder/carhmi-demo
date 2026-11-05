#ifndef CARHMI_SFX_SYNTH_H
#define CARHMI_SFX_SYNTH_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The sounds of the car, made of nothing but arithmetic - no samples, no libm: a V8 sports engine (the starter
 * cranking it, the engine catching with a bark and settling to the idle, running - now and then it is revved, or
 * pulled through the gears -, shutting down), a police siren (a wail), and a fuel pump (motor and flowing fuel).
 * Mono, SFX_SYNTH_RATE_HZ, one sample at a time; the voices are mixed and limited here, so the stream gets what
 * the speaker can play. Nothing but its own context is touched, so it runs anywhere.
 */

#define SFX_SYNTH_RATE_HZ   22050u

/** Where the engine is. The start takes SFX_SYNTH_START_S: cranking, catching, settling */
typedef enum
{
    SFX_ENGINE_OFF = 0,     /**< Silent */
    SFX_ENGINE_START,       /**< The starter cranks it, it catches and settles to the idle */
    SFX_ENGINE_RUN,         /**< The idle */
    SFX_ENGINE_STOP,        /**< It dies down */
} sfx_engine_mode_t;

#define SFX_SYNTH_START_S   3.3f
#define SFX_SYNTH_STOP_S    1.0f

typedef struct sfx_synth* sfx_synth_t;

/** @brief A synthesizer with all voices silent; NULL when out of memory */
sfx_synth_t sfx_synth_create(void);
void sfx_synth_destroy(sfx_synth_t s);

/** The engine: on starts it (when it is off), off lets it die down (when it is on) */
void sfx_synth_engine(sfx_synth_t s, bool on);
void sfx_synth_siren(sfx_synth_t s, bool on);
void sfx_synth_pump(sfx_synth_t s, bool on);

sfx_engine_mode_t sfx_synth_engine_mode(sfx_synth_t s);

/** Something to play (a voice is on, or still fading) */
bool sfx_synth_active(sfx_synth_t s);

/** Fills `frames` samples (mono, signed 16 bit) with the mix of the voices */
void sfx_synth_render(sfx_synth_t s, int16_t* out, uint32_t frames);

/** What the driver does with the engine while it idles; they come by themselves every few seconds */
typedef enum
{
    SFX_MANEUVER_NONE = 0,
    SFX_MANEUVER_BLIP,      /**< The throttle is blipped: up to 3800 rpm and back, the exhaust pops */
    SFX_MANEUVER_DOUBLE,    /**< Two blips in a row */
    SFX_MANEUVER_PULL,      /**< A pull through three gears: the revs climb to 6300, the ignition is cut for the shift, ... */
} sfx_maneuver_t;

/** Starts a maneuver (when the engine idles; false otherwise). The idle does it by itself too, see SFX_MANEUVER_* */
bool sfx_synth_maneuver(sfx_synth_t s, sfx_maneuver_t maneuver);
sfx_maneuver_t sfx_synth_maneuver_now(sfx_synth_t s);

/** The revolutions per minute of the engine, and how many maneuvers it has made (for the tests) */
float sfx_synth_rpm(sfx_synth_t s);
uint32_t sfx_synth_maneuvers_made(sfx_synth_t s);

/** sin(2 pi x) with x in turns, within 0.1 % (for the tests) */
float sfx_synth_sin(float turns);

#endif /* CARHMI_SFX_SYNTH_H */
