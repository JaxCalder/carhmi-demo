#include "fuel.h"
#include <string.h>

void fuel_init(fuel_t* f, uint64_t now_ms)
{
    *f = (fuel_t){ .remaining_ms = FUEL_RUN_MS, .updated_ms = now_ms };
}

void fuel_update(fuel_t* f, uint64_t now_ms)
{
    uint64_t elapsed = now_ms >= f->updated_ms ? now_ms - f->updated_ms : 0;
    f->updated_ms = now_ms;
    f->warning_left_ms -= elapsed < f->warning_left_ms ? (uint32_t)elapsed : f->warning_left_ms;
    if (f->visible && f->stage == FUEL_PUMPING)
    {
        /* Clamp before multiplying, including after a long stalled frame. */
        if (elapsed >= FUEL_FILL_MS)
            f->remaining_ms = FUEL_RUN_MS;
        else
        {
            uint32_t added = (uint32_t)elapsed * (FUEL_RUN_MS / FUEL_FILL_MS);
            uint32_t space = FUEL_RUN_MS - f->remaining_ms;
            f->remaining_ms += added < space ? added : space;
        }
        if (f->remaining_ms == FUEL_RUN_MS)
            f->stage = FUEL_FULL;
    }
    else if (f->engine_on)
        f->remaining_ms -= elapsed < f->remaining_ms ? (uint32_t)elapsed : f->remaining_ms;
    if (f->remaining_ms == 0 && f->engine_on)
    {
        f->engine_on = false;
        f->notice = FUEL_NOTICE_EMPTY;
    }
    if (f->remaining_ms > 0 && f->notice == FUEL_NOTICE_EMPTY)
        f->notice = FUEL_NOTICE_NONE;
}

bool fuel_event(fuel_t* f, const char* event)
{
    if (strcmp(event, "engine") == 0)
    {
        if (f->engine_on)
        {
            f->engine_on = false;
            f->warning_left_ms = 0;
            f->notice = FUEL_NOTICE_NONE;
        }
        else if (f->remaining_ms == 0)
            f->notice = FUEL_NOTICE_EMPTY;
        else if (f->visible && (f->stage == FUEL_PUMPING || f->stage == FUEL_FULL))
            f->notice = FUEL_NOTICE_HOSE_CONNECTED;
        else
        {
            f->engine_on = true;
            f->notice = FUEL_NOTICE_NONE;
        }
    }
    else if (strcmp(event, "fuel.open") == 0)
    {
        f->visible = true;
        f->warning_left_ms = 0;
        f->stage = FUEL_CAPPED;
    }
    else if (strcmp(event, "fuel.close") == 0)
    {
        f->visible = false;
        f->warning_left_ms = 0;
        f->stage = FUEL_CAPPED;
        if (f->notice != FUEL_NOTICE_EMPTY) f->notice = FUEL_NOTICE_NONE;
    }
    else if (strcmp(event, "fuel.filler") == 0)
    {
        if (!f->visible) return true;
        if (f->engine_on && (f->stage == FUEL_CAPPED || f->stage == FUEL_OPEN))
        {
            f->warning_left_ms = FUEL_WARNING_MS;
            f->notice = FUEL_NOTICE_ENGINE_RUNNING;
            return true;
        }
        if (f->notice != FUEL_NOTICE_EMPTY) f->notice = FUEL_NOTICE_NONE;
        if (f->stage == FUEL_CAPPED) f->stage = FUEL_OPEN;
        else if (f->stage == FUEL_OPEN)
            f->stage = f->remaining_ms < FUEL_RUN_MS ? FUEL_PUMPING : FUEL_FULL;
        else if (f->stage == FUEL_DISCONNECTED) f->stage = FUEL_CAPPED;
        else f->stage = FUEL_DISCONNECTED;
    }
    else return false;
    return true;
}

int32_t fuel_level(const fuel_t* f)
{
    return (int32_t)((f->remaining_ms * 1000u) / FUEL_RUN_MS);
}

bool fuel_warning_visible(const fuel_t* f)
{
    return f->visible && f->notice == FUEL_NOTICE_ENGINE_RUNNING && f->warning_left_ms > 0 &&
        ((FUEL_WARNING_MS - f->warning_left_ms) / FUEL_WARNING_HALF_MS) % 2u == 0;
}
