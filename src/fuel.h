#ifndef CARHMI_FUEL_H
#define CARHMI_FUEL_H

#include <stdbool.h>
#include <stdint.h>

#define FUEL_RUN_MS (15u * 60u * 1000u)
#define FUEL_FILL_MS 20000u
#define FUEL_WARNING_HALF_MS 300u
#define FUEL_WARNING_FLASHES 4u
#define FUEL_WARNING_MS (FUEL_WARNING_HALF_MS * 2u * FUEL_WARNING_FLASHES)

typedef enum {
    FUEL_CAPPED = 0,
    FUEL_OPEN,
    FUEL_PUMPING,
    FUEL_FULL,
    FUEL_DISCONNECTED
} fuel_stage_t;

typedef enum {
    FUEL_NOTICE_NONE = 0,
    FUEL_NOTICE_EMPTY,
    FUEL_NOTICE_ENGINE_RUNNING,
    FUEL_NOTICE_HOSE_CONNECTED
} fuel_notice_t;

typedef struct {
    uint32_t remaining_ms;
    uint32_t warning_left_ms;
    uint64_t updated_ms;
    bool visible;
    bool engine_on;
    fuel_notice_t notice;
    fuel_stage_t stage;
} fuel_t;

/* Full at startup; consumes only elapsed time with the engine switched on. */
void fuel_init(fuel_t* f, uint64_t now_ms);
void fuel_update(fuel_t* f, uint64_t now_ms);
/* The engine event toggles the engine, unless empty or a hose is connected.
 * Opening/closing the screen disconnects the hose. A filler click opens the
 * cap, the next inserts the hose; another click removes the hose, then closes the cap. */
bool fuel_event(fuel_t* f, const char* event);
int32_t fuel_level(const fuel_t* f); /* 0..1000 */
bool fuel_warning_visible(const fuel_t* f);

#endif
