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

/* Optional PPM previews from the actual RGB565 renderer (second argument). */
static void save_frame(const char* prefix, const char* page)
{
    char path[256], header[48];
    Dmod_SnPrintf(path, sizeof(path), "%s-%s.ppm", prefix, page);
    void* file = Dmod_FileOpen(path, "wb");
    uint8_t* row = Dmod_Malloc(surface.width * 3u);
    if (file == NULL || row == NULL)
    {
        if (file != NULL) Dmod_FileClose(file);
        Dmod_Free(row); failures++; return;
    }
    int n = Dmod_SnPrintf(header, sizeof(header), "P6\n%u %u\n255\n", surface.width, surface.height);
    Dmod_FileWrite(header, 1, (size_t)n, file);
    for (uint16_t y = 0; y < surface.height; y++)
    {
        const uint16_t* pixels = (const uint16_t*)((const uint8_t*)surface.pixels + y * surface.stride);
        for (uint16_t x = 0; x < surface.width; x++)
        {
            uint16_t c = pixels[x];
            row[x * 3] = ((c >> 11) & 31) * 255 / 31;
            row[x * 3 + 1] = ((c >> 5) & 63) * 255 / 63;
            row[x * 3 + 2] = (c & 31) * 255 / 31;
        }
        if (Dmod_FileWrite(row, 3, surface.width, file) != surface.width)
            failures++;
    }
    Dmod_FileClose(file); Dmod_Free(row);
}

int main(int argc, char** argv)
{
    int status = 0;
    const char* path = argc > 1 ? argv[1] : "/mnt/MEDIA/views/carhmi_view/carhmi.dmv";
    view = libdmview_open(path, &status);
    if (view == NULL)
    {
        Dmod_Printf("FAIL open: %d\n", status);
        return 1;
    }
    uint16_t width = 0, height = 0;
    libdmview_get_size(view, &width, &height);
    bool compact = width == 320 && height == 240;
    if (!compact && !(width == 480 && height == 272))
    {
        Dmod_Printf("FAIL unsupported view size: %ux%u\n", width, height);
        libdmview_close(view); return 1;
    }
    surface.width = width; surface.height = height; surface.stride = width * 2u;
    surface.format = DMDRVI_GFX_PIXEL_FORMAT_RGB565;
    surface.pixels = Dmod_Malloc(surface.stride * surface.height);
    if (surface.pixels == NULL)
    {
        Dmod_Printf("FAIL framebuffer allocation\n");
        libdmview_close(view); return 1;
    }
    libdmview_render(view, &surface, NULL);
    if (argc > 2) save_frame(argv[2], "nav");
    expect("nav_map_near_x", compact ? -160 : -110);
    input(1, 140, 140, 0, 0);
    input(1, 170, 160, 0, 0);
    expect("nav_map_near_x", compact ? -130 : -80);
    expect("nav_map_near_y", compact ? -344 : -312);
    expect("navg_car_x", compact ? 156 : 206);
    expect("navg_car_y", compact ? 148 : 180);
    expect("navRunning", 0);
    input(0, 0, 0, 0, 0);
    /* Two contacts move closer: native PINCH chooses the wide projection. */
    input(2, 130, 140, 230, 140);
    input(2, 130, 140, 190, 140);
    expect("nav_map_near_alpha", 0);
    expect("nav_map_wide_alpha", 255);
    input(0, 0, 0, 0, 0);
    input(2, 130, 140, 190, 140);
    input(2, 130, 140, 250, 140);
    expect("nav_map_near_alpha", 255);
    expect("nav_map_wide_alpha", 0);
    input(0, 0, 0, 0, 0);
    /* Captured drag may leave the viewport; its content stays bounded. */
    input(1, 150, 140, 0, 0);
    input(1, width - 1, height - 1, 0, 0);
    expect("nav_map_near_x", 0);
    input(0, 0, 0, 0, 0);
    click(width - 25, 115); /* minus */
    expect("nav_map_near_alpha", 0);
    click(width - 25, 75);  /* plus */
    expect("nav_map_near_alpha", 255);
    click(width - 25, 155); /* recenter */
    expect("navg_car_x", compact ? 126 : 176);
    expect("navg_car_y", compact ? 128 : 160);
    /* Finishing the recenter animation returns to the replay's current point. */
    now_ms += 6000;
    libdmview_update(view, now_ms);
    libdmview_render(view, &surface, NULL);
    expect("nav_map_near_x", compact ? -160 : -110);
    expect("nav_map_near_y", compact ? -364 : -332);
    /* Every page must remain reachable on the compact sidebar too. */
    static const char pages[][8] = { "home", "nav", "media", "climate", "sounds" };
    for (int i = 0; i < 5; i++)
    {
        click(20, compact ? 51 + i * 40 : 54 + i * 48);
        now_ms += 1000;
        libdmview_update(view, now_ms);
        libdmview_render(view, &surface, NULL);
        char alpha[48];
        Dmod_SnPrintf(alpha, sizeof(alpha), "view_%s_alpha", pages[i]);
        expect(alpha, 255);
        if (argc > 2) save_frame(argv[2], pages[i]);
    }
    libdmview_close(view); Dmod_Free(surface.pixels);
    Dmod_Printf("navigation_input %ux%u: %s (%d failures)\n", width, height, failures ? "FAIL" : "PASS", failures);
    return failures;
}
