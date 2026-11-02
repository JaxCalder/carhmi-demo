#include "dmod.h"
#include "libdmview.h"
#include <string.h>

/* Run through the real renderer/input interpreter on host or board. This is
 * an independent off-screen view: synthetic contacts do not touch the UI. */
static libdmview_t view;
static libdmview_surface_t surface;
static uint32_t now_ms;
static int failures;
static int previous_contacts;

static void expect(const char* name, int32_t expected)
{
    int32_t actual = 0;
    int rc = libdmview_get_int(view, name, &actual);
    if (rc != 0 || actual != expected)
    {
        Dmod_Printf("FAIL %s: rc=%d got=%ld wanted=%ld\n", name, rc, (long)actual, (long)expected);
        failures++;
    }
}

static void input(int count, int x0, int y0, int x1, int y1)
{
    dmdrvi_input_state_t state;
    memset(&state, 0, sizeof(state));
    state.contact_count = (uint8_t)count;
    state.contacts[0].id = 0;
    state.contacts[0].x = (uint16_t)x0;
    state.contacts[0].y = (uint16_t)y0;
    state.contacts[1].id = 1;
    state.contacts[1].x = (uint16_t)x1;
    state.contacts[1].y = (uint16_t)y1;
    state.contacts[0].event = previous_contacts > 0 ? DMDRVI_INPUT_CONTACT_MOVE : DMDRVI_INPUT_CONTACT_DOWN;
    state.contacts[1].event = previous_contacts > 1 ? DMDRVI_INPUT_CONTACT_MOVE : DMDRVI_INPUT_CONTACT_DOWN;
    previous_contacts = count;
    now_ms += 20;
    int rc = libdmview_input(view, &state, now_ms);
    if (rc < 0) { Dmod_Printf("FAIL input: %d\n", rc); failures++; }
    rc = libdmview_render(view, &surface, NULL);
    if (rc < 0) { Dmod_Printf("FAIL render: %d\n", rc); failures++; }
}

static void click(int x, int y)
{
    input(1, x, y, 0, 0);
    input(0, 0, 0, 0, 0);
}

int main(int argc, char** argv)
{
    int status = 0;
    const char* path = argc > 1 ? argv[1] : "/mnt/MEDIA/views/carhmi_view/carhmi.dmv";
    surface.width = 480; surface.height = 272; surface.stride = 960;
    surface.format = DMDRVI_GFX_PIXEL_FORMAT_RGB565;
    surface.pixels = Dmod_Malloc(surface.stride * surface.height);
    view = libdmview_open(path, &status);
    if (view == NULL || surface.pixels == NULL)
    {
        Dmod_Printf("FAIL open: %d\n", status);
        libdmview_close(view); Dmod_Free(surface.pixels); return 1;
    }
    libdmview_render(view, &surface, NULL);
    expect("nav_map_near_x", -110);
    input(1, 150, 140, 0, 0);
    input(1, 180, 160, 0, 0);
    expect("nav_map_near_x", -80);
    expect("nav_map_near_y", -312);
    expect("navg_car_x", 206);
    expect("navg_car_y", 180);
    expect("navRunning", 0);
    input(0, 0, 0, 0, 0);
    /* Two contacts move closer: native PINCH chooses the wide projection. */
    input(2, 180, 150, 280, 150);
    input(2, 180, 150, 240, 150);
    expect("nav_map_near_alpha", 0);
    expect("nav_map_wide_alpha", 255);
    input(0, 0, 0, 0, 0);
    input(2, 180, 150, 240, 150);
    input(2, 180, 150, 300, 150);
    expect("nav_map_near_alpha", 255);
    expect("nav_map_wide_alpha", 0);
    input(0, 0, 0, 0, 0);
    /* Captured drag may leave the viewport; its content stays bounded. */
    input(1, 150, 140, 0, 0);
    input(1, 479, 271, 0, 0);
    expect("nav_map_near_x", 0);
    input(0, 0, 0, 0, 0);
    click(455, 115); /* minus */
    expect("nav_map_near_alpha", 0);
    click(455, 75);  /* plus */
    expect("nav_map_near_alpha", 255);
    click(455, 155); /* recenter */
    expect("navg_car_x", 176);
    expect("navg_car_y", 160);
    /* Finishing the recenter animation returns to the replay's current point. */
    libdmview_update(view, now_ms + 6000);
    libdmview_render(view, &surface, NULL);
    expect("nav_map_near_x", -110);
    expect("nav_map_near_y", -332);
    libdmview_close(view); Dmod_Free(surface.pixels);
    Dmod_Printf("navigation_input: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures;
}
