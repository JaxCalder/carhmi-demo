#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "local_time.h"
#include "volume.h"
#include "covers.h"
#include "player.h"
#include "playstate.h"
#include "sfx.h"
#include "fuel.h"
#include "sfx_synth.h"
#include "dmosi.h"
#include <string.h>

/* "YYYY-MM-DD HH:MM:SS" of the local time of a UTC calendar */
static const char* local_of(int32_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    static char text[32];
    dmutc_calendar_t c;
    memset(&c, 0, sizeof(c));
    c.year = year;
    c.month = month;
    c.day = day;
    c.hour = hour;
    c.minute = minute;
    c.second = second;
    dmutc_time_t t;
    dmutc_calendar_t l;
    if (dmutc_from_calendar(&c, &t) != 0 || local_time(&t, &l) != 0)
        return "error";
    Dmod_SnPrintf(text, sizeof(text), "%04d-%02u-%02u %02u:%02u:%02u", (int)l.year, (unsigned)l.month,
                  (unsigned)l.day, (unsigned)l.hour, (unsigned)l.minute, (unsigned)l.second);
    return text;
}

DMOD_TEST_STEP(local_time_is_polish_summer_and_winter_time)
{
    DMOD_TEST_EXPECT(strcmp(local_of(2026, 10, 17, 12, 0, 0), "2026-10-17 14:00:00") == 0);   /* CEST */
    DMOD_TEST_EXPECT(strcmp(local_of(2026, 1, 15, 12, 0, 0), "2026-01-15 13:00:00") == 0);    /* CET */
}

DMOD_TEST_STEP(local_time_changes_on_the_last_sundays_at_one_utc)
{
    /* 2026: March 29th and October 25th */
    DMOD_TEST_EXPECT(strcmp(local_of(2026, 3, 29, 0, 59, 59), "2026-03-29 01:59:59") == 0);
    DMOD_TEST_EXPECT(strcmp(local_of(2026, 3, 29, 1, 0, 0), "2026-03-29 03:00:00") == 0);
    DMOD_TEST_EXPECT(strcmp(local_of(2026, 10, 25, 0, 59, 59), "2026-10-25 02:59:59") == 0);
    DMOD_TEST_EXPECT(strcmp(local_of(2026, 10, 25, 1, 0, 0), "2026-10-25 02:00:00") == 0);
    /* 2027: March 28th, October 31st (a Sunday itself) */
    DMOD_TEST_EXPECT(strcmp(local_of(2027, 3, 28, 1, 0, 0), "2027-03-28 03:00:00") == 0);
    DMOD_TEST_EXPECT(strcmp(local_of(2027, 10, 31, 0, 30, 0), "2027-10-31 02:30:00") == 0);
    DMOD_TEST_EXPECT(strcmp(local_of(2027, 10, 31, 1, 30, 0), "2027-10-31 02:30:00") == 0);
}

DMOD_TEST_STEP(local_time_goes_into_the_next_year)
{
    DMOD_TEST_EXPECT(strcmp(local_of(2026, 12, 31, 23, 30, 0), "2027-01-01 00:30:00") == 0);
}

/* A directory the test may write to */
#define VOLUME_DIR "/tmp"

DMOD_TEST_STEP(volume_starts_at_the_default_and_stays_in_range)
{
    (void)Dmod_FileRemove(VOLUME_DIR "/.volume");
    volume_t v = volume_new(VOLUME_DIR);
    DMOD_TEST_EXPECT(v != NULL);
    if (v == NULL)
        return;
    DMOD_TEST_EXPECT(volume_level(v) == VOLUME_DEFAULT);
    DMOD_TEST_EXPECT(!volume_muted(v));

    volume_set(v, 30);
    DMOD_TEST_EXPECT(volume_level(v) == 30);
    volume_set(v, 250);
    DMOD_TEST_EXPECT(volume_level(v) == 100);
    volume_set(v, -5);
    DMOD_TEST_EXPECT(volume_level(v) == 0);
    volume_free(v);
}

DMOD_TEST_STEP(volume_mute_is_turned_off_by_a_new_level)
{
    volume_t v = volume_new(NULL);
    DMOD_TEST_EXPECT(v != NULL);
    if (v == NULL)
        return;
    volume_toggle_mute(v);
    DMOD_TEST_EXPECT(volume_muted(v));
    DMOD_TEST_EXPECT(volume_level(v) == VOLUME_DEFAULT);        /* (the level is kept) */
    volume_toggle_mute(v);
    DMOD_TEST_EXPECT(!volume_muted(v));
    volume_toggle_mute(v);
    volume_set(v, 40);
    DMOD_TEST_EXPECT(!volume_muted(v));
    volume_free(v);
}

DMOD_TEST_STEP(volume_is_kept_between_starts)
{
    (void)Dmod_FileRemove(VOLUME_DIR "/.volume");
    volume_t v = volume_new(VOLUME_DIR);
    DMOD_TEST_EXPECT(v != NULL);
    if (v == NULL)
        return;
    volume_set(v, 35);
    volume_free(v);                                             /* (writes what was not written) */

    v = volume_new(VOLUME_DIR);
    DMOD_TEST_EXPECT(v != NULL);
    if (v == NULL)
        return;
    DMOD_TEST_EXPECT(volume_level(v) == 35);
    DMOD_TEST_EXPECT(!volume_muted(v));
    volume_free(v);
    (void)Dmod_FileRemove(VOLUME_DIR "/.volume");
}

DMOD_TEST_STEP(volume_without_a_device_is_not_applied)
{
    volume_t v = volume_new(NULL);
    DMOD_TEST_EXPECT(v != NULL);
    if (v == NULL)
        return;
    DMOD_TEST_EXPECT(!volume_poll(v, 1000));                    /* (no audio device is served here) */
    volume_free(v);
}

/*
 * The covers of tests/fixtures/music (made with ffmpeg): a.mp3 with a 120 x 80
 * JPEG (ID3v2.3), b.mp3 without a picture, c.mp3 with a 48 x 48 PNG (ID3v2.4)
 */
#define COVERS_DIR      "/tmp/carhmi-covers"
#define COVERS_SIZES    "-40x40-k0-0-40x40-r5.dmvi -100x100-b10.dmvi"
#define COVERS_WAIT_MS  20000u

/* The key of a track's cover once it is made; 0: not in time */
static uint32_t wait_cover(covers_t c, uint32_t index)
{
    for (uint32_t waited = 0; waited < COVERS_WAIT_MS; waited += 20u)
    {
        uint32_t key = covers_key(c, index);
        if (key != 0)
            return key;
        dmosi_thread_sleep(20);
    }
    return 0;
}

/* The size of a .dmvi of the cover `key`; false: no such file */
static bool cover_size(uint32_t key, const char* tail, uint32_t* w, uint32_t* h)
{
    char path[96];
    uint8_t header[16];
    Dmod_SnPrintf(path, sizeof(path), "%s/%08x%s", COVERS_DIR, (unsigned)key, tail);
    void* f = Dmod_FileOpen(path, "rb");
    bool ok = f != NULL && Dmod_FileRead(header, 1, sizeof(header), f) == sizeof(header) && memcmp(header, "DMVI", 4) == 0;
    if (f != NULL)
        Dmod_FileClose(f);
    *w = ok ? (uint32_t)(header[12] | (header[13] << 8)) : 0u;
    *h = ok ? (uint32_t)(header[14] | (header[15] << 8)) : 0u;
    return ok;
}

static void remove_covers(uint32_t key)
{
    char path[96];
    Dmod_SnPrintf(path, sizeof(path), "%s/%08x-40x40-k0-0-40x40-r5.dmvi", COVERS_DIR, (unsigned)key);
    (void)Dmod_FileRemove(path);
    Dmod_SnPrintf(path, sizeof(path), "%s/%08x-100x100-b10.dmvi", COVERS_DIR, (unsigned)key);
    (void)Dmod_FileRemove(path);
}

DMOD_TEST_STEP(covers_are_made_at_the_sizes_of_the_view)
{
    player_t p = player_new(CARHMI_FIXTURES "/music");
    DMOD_TEST_EXPECT(p != NULL && player_count(p) == 3);
    if (p == NULL)
        return;
    covers_t c = covers_new(p, COVERS_DIR, COVERS_SIZES);
    DMOD_TEST_EXPECT(c != NULL);
    if (c == NULL)
    {
        player_free(p);
        return;
    }
    covers_want(c, 1, false);
    covers_want(c, 2, false);
    covers_want(c, 0, true);                                    /* Before the others */
    uint32_t jpeg = wait_cover(c, 0), png = wait_cover(c, 2);
    DMOD_TEST_EXPECT(jpeg != 0 && png != 0 && jpeg != png);
    dmosi_thread_sleep(200);
    DMOD_TEST_EXPECT(covers_key(c, 1) == 0);                    /* No picture */
    DMOD_TEST_EXPECT(covers_key(c, 7) == 0);                    /* No track */

    /* Each size: the middle of the 120 x 80 picture covering it, resized to it, cut, blurred */
    uint32_t w = 0, h = 0;
    DMOD_TEST_EXPECT(cover_size(jpeg, "-40x40-k0-0-40x40-r5.dmvi", &w, &h) && w == 40 && h == 40);
    DMOD_TEST_EXPECT(cover_size(jpeg, "-100x100-b10.dmvi", &w, &h) && w == 100 && h == 100);   /* (blurred: scaled up) */
    DMOD_TEST_EXPECT(cover_size(png, "-40x40-k0-0-40x40-r5.dmvi", &w, &h) && w == 40 && h == 40);
    char name[64];
    DMOD_TEST_EXPECT(covers_name_size(c) == strlen(COVERS_DIR) + 10U);
    covers_name(c, jpeg, name, sizeof(name));
    char expected[64];
    Dmod_SnPrintf(expected, sizeof(expected), "%s/%08x", COVERS_DIR, (unsigned)jpeg);
    DMOD_TEST_EXPECT(strcmp(name, expected) == 0);
    covers_free(c);

    /* Made once: the next start finds them */
    c = covers_new(p, COVERS_DIR, COVERS_SIZES);
    DMOD_TEST_EXPECT(c != NULL);
    if (c != NULL)
    {
        covers_want(c, 0, true);
        DMOD_TEST_EXPECT(wait_cover(c, 0) == jpeg);
        covers_free(c);
    }
    remove_covers(jpeg);
    remove_covers(png);
    player_free(p);
}

DMOD_TEST_STEP(covers_need_sizes_they_know)
{
    player_t p = player_new(CARHMI_FIXTURES "/music");
    DMOD_TEST_EXPECT(p != NULL);
    DMOD_TEST_EXPECT(covers_new(p, COVERS_DIR, "") == NULL);
    DMOD_TEST_EXPECT(covers_new(p, COVERS_DIR, "-40y40.dmvi -b4") == NULL);
    covers_t c = covers_new(p, COVERS_DIR, "-40x40.dmvi nonsense -40x40.dmvi");     /* The one it knows, once */
    DMOD_TEST_EXPECT(c != NULL);
    covers_free(c);
    covers_free(NULL);
    player_free(p);
}

/* Where the music was: kept in /tmp/.playing */
#define PLAYSTATE_DIR   "/tmp"

static bool kept(const char* file, uint32_t position_ms, bool playing)
{
    playstate_t s = playstate_new(PLAYSTATE_DIR);
    const char* f = NULL;
    uint32_t ms = 0;
    bool on = false;
    bool ok = s != NULL && playstate_saved(s, &f, &ms, &on) && strcmp(f, file) == 0 && ms == position_ms && on == playing;
    if (!ok)
        Dmod_Printf("    kept: %s at %u ms, %d\n", (f != NULL) ? f : "-", (unsigned)ms, (int)on);
    playstate_free(s);
    return ok;
}

DMOD_TEST_STEP(playstate_keeps_the_track_and_where_it_was)
{
    (void)Dmod_FileRemove(PLAYSTATE_DIR "/.playing");
    playstate_t s = playstate_new(PLAYSTATE_DIR);
    const char* f = NULL;
    uint32_t ms = 0;
    bool on = false;
    DMOD_TEST_EXPECT(s != NULL && !playstate_saved(s, &f, &ms, &on));          /* Nothing kept yet */
    playstate_update(s, NULL, 0, false, 500);                                   /* (no track: nothing) */
    playstate_update(s, "Żółć - a song.mp3", 12345, true, 1000);                /* Another track: at once */
    DMOD_TEST_EXPECT(kept("Żółć - a song.mp3", 12000, true));                   /* (seconds) */
    playstate_update(s, "Żółć - a song.mp3", 20000, true, 1000 + PLAYSTATE_SAVE_MS - 1u);
    DMOD_TEST_EXPECT(kept("Żółć - a song.mp3", 12000, true));                   /* Not yet */
    playstate_update(s, "Żółć - a song.mp3", 21000, true, 1000 + PLAYSTATE_SAVE_MS);
    DMOD_TEST_EXPECT(kept("Żółć - a song.mp3", 21000, true));                   /* Every PLAYSTATE_SAVE_MS */
    playstate_update(s, "Żółć - a song.mp3", 22000, false, 1000 + PLAYSTATE_SAVE_MS + 10u);
    DMOD_TEST_EXPECT(kept("Żółć - a song.mp3", 22000, false));                  /* Paused: at once */
    playstate_update(s, "Żółć - a song.mp3", 23000, false, 1000 + PLAYSTATE_SAVE_MS + 20u);
    playstate_free(s);                                                          /* What was not written */
    DMOD_TEST_EXPECT(kept("Żółć - a song.mp3", 23000, false));
    (void)Dmod_FileRemove(PLAYSTATE_DIR "/.playing");
    playstate_free(NULL);
}

DMOD_TEST_STEP(player_finds_a_track_by_its_file)
{
    player_t p = player_new(CARHMI_FIXTURES "/music");
    DMOD_TEST_EXPECT(p != NULL);
    if (p == NULL)
        return;
    DMOD_TEST_EXPECT(player_find(p, "c.mp3") == 2);
    DMOD_TEST_EXPECT(player_find(p, "a.mp3") == 0);
    DMOD_TEST_EXPECT(player_find(p, "gone.mp3") == -1);
    DMOD_TEST_EXPECT(player_find(p, NULL) == -1);
    DMOD_TEST_EXPECT(player_file(p, 1) != NULL && strcmp(player_file(p, 1), "b.mp3") == 0);
    DMOD_TEST_EXPECT(player_file(p, 3) == NULL);
    player_free(p);
}

/* ---- The sounds of the car ---- */

#define RATE    SFX_SYNTH_RATE_HZ

/* Renders `seconds` of sound into `pcm`, which holds `frames` samples, and returns the peak */
static int32_t render(sfx_synth_t s, int16_t* pcm, uint32_t frames)
{
    sfx_synth_render(s, pcm, frames);
    int32_t peak = 0;
    for (uint32_t i = 0; i < frames; i++)
    {
        int32_t a = pcm[i] < 0 ? -pcm[i] : pcm[i];
        if (a > peak)
            peak = a;
    }
    return peak;
}

/* Of 1 second: the root of the mean square, 0 ... 1 of the full scale (in thousandths) */
static int32_t rms_milli(const int16_t* pcm, uint32_t frames)
{
    uint64_t sum = 0;
    for (uint32_t i = 0; i < frames; i++)
        sum += (uint64_t)((int32_t)pcm[i] * pcm[i]);
    double mean = (double)sum / (double)frames;
    double x = mean > 0 ? mean : 0;
    double r = x;                               /* (a square root without libm: Newton) */
    for (int i = 0; i < 40 && r > 0; i++)
        r = 0.5 * (r + x / r);
    return (int32_t)(r * 1000.0 / 32768.0);
}

/* Frequency by the zero crossings of a stretch, Hz */
static uint32_t crossings_hz(const int16_t* pcm, uint32_t frames)
{
    uint32_t n = 0;
    for (uint32_t i = 1; i < frames; i++)
        n += ((pcm[i] >= 0) != (pcm[i - 1] >= 0)) ? 1u : 0u;
    return (uint32_t)((uint64_t)n * RATE / (2u * frames));
}

DMOD_TEST_STEP(sfx_sine_is_within_a_tenth_of_a_percent)
{
    DMOD_TEST_EXPECT(sfx_synth_sin(0.0f) < 0.002f && sfx_synth_sin(0.0f) > -0.002f);
    DMOD_TEST_EXPECT(sfx_synth_sin(0.25f) > 0.998f);
    DMOD_TEST_EXPECT(sfx_synth_sin(0.75f) < -0.998f);
    DMOD_TEST_EXPECT(sfx_synth_sin(-0.25f) < -0.998f);          /* (a negative phase) */
    DMOD_TEST_EXPECT(sfx_synth_sin(3.25f) > 0.998f);            /* (turns past the first) */
    float s = sfx_synth_sin(0.125f);
    DMOD_TEST_EXPECT(s > 0.7061f && s < 0.7081f);
}

DMOD_TEST_STEP(sfx_engine_cranks_catches_idles_and_dies_down)
{
    sfx_synth_t s = sfx_synth_create();
    int16_t* pcm = Dmod_Malloc(2u * RATE * sizeof(int16_t));    /* (2 s: the longest render) */
    DMOD_TEST_EXPECT(s != NULL && pcm != NULL);
    if (s == NULL || pcm == NULL)
        return;
    DMOD_TEST_EXPECT(!sfx_synth_active(s) && render(s, pcm, RATE) == 0);       /* (silent until started) */
    sfx_synth_engine(s, true);
    DMOD_TEST_EXPECT(sfx_synth_engine_mode(s) == SFX_ENGINE_START);
    DMOD_TEST_EXPECT(render(s, pcm, RATE) < 30000);                             /* 0 - 1 s: cranking */
    DMOD_TEST_EXPECT(rms_milli(pcm, RATE) > 50);
    DMOD_TEST_EXPECT(sfx_synth_engine_mode(s) == SFX_ENGINE_START);
    for (int i = 0; i < 3; i++)
        DMOD_TEST_EXPECT(render(s, pcm, RATE) < 30000);                         /* ... 4 s: the start is over (3.3 s) */
    DMOD_TEST_EXPECT(sfx_synth_engine_mode(s) == SFX_ENGINE_RUN);
    DMOD_TEST_EXPECT(rms_milli(pcm, RATE) > 50);                                /* The idle is heard */
    sfx_synth_engine(s, false);
    DMOD_TEST_EXPECT(sfx_synth_engine_mode(s) == SFX_ENGINE_STOP && sfx_synth_active(s));
    (void)render(s, pcm, RATE + RATE / 10u);                                    /* 1.1 s: it died down */
    DMOD_TEST_EXPECT(sfx_synth_engine_mode(s) == SFX_ENGINE_OFF && !sfx_synth_active(s));
    DMOD_TEST_EXPECT(render(s, pcm, RATE) == 0);
    Dmod_Free(pcm);
    sfx_synth_destroy(s);
}

DMOD_TEST_STEP(sfx_engine_starts_again_after_it_died_down)
{
    sfx_synth_t s = sfx_synth_create();
    int16_t* pcm = Dmod_Malloc(2u * RATE * sizeof(int16_t));    /* (2 s: the longest render) */
    DMOD_TEST_EXPECT(s != NULL && pcm != NULL);
    if (s == NULL || pcm == NULL)
        return;
    sfx_synth_engine(s, true);
    sfx_synth_engine(s, true);                                                  /* (twice: nothing happens) */
    (void)render(s, pcm, RATE / 2u);
    sfx_synth_engine(s, false);                                                 /* (still cranking) */
    DMOD_TEST_EXPECT(sfx_synth_engine_mode(s) == SFX_ENGINE_STOP);
    (void)render(s, pcm, RATE + RATE / 10u);
    DMOD_TEST_EXPECT(sfx_synth_engine_mode(s) == SFX_ENGINE_OFF);
    sfx_synth_engine(s, true);
    DMOD_TEST_EXPECT(sfx_synth_engine_mode(s) == SFX_ENGINE_START);
    Dmod_Free(pcm);
    sfx_synth_destroy(s);
}

DMOD_TEST_STEP(sfx_siren_wails_between_640_and_1440_hz)
{
    sfx_synth_t s = sfx_synth_create();
    int16_t* pcm = Dmod_Malloc(2u * RATE * sizeof(int16_t));    /* (2 s: the longest render) */
    DMOD_TEST_EXPECT(s != NULL && pcm != NULL);
    if (s == NULL || pcm == NULL)
        return;
    sfx_synth_siren(s, true);
    (void)render(s, pcm, RATE / 10u);                                           /* (the fade-in) */
    DMOD_TEST_EXPECT(render(s, pcm, RATE / 5u) < 30000);
    uint32_t low = crossings_hz(pcm, RATE / 5u);
    DMOD_TEST_EXPECT(low > 600 && low < 780);
    (void)render(s, pcm, RATE + RATE / 3u);                                     /* ... 1.7 s: the top of the wail */
    (void)render(s, pcm, RATE / 5u);
    uint32_t high = crossings_hz(pcm, RATE / 5u);
    DMOD_TEST_EXPECT(high > 1250 && high < 1500);
    DMOD_TEST_EXPECT(rms_milli(pcm, RATE / 5u) > 100);
    sfx_synth_siren(s, false);
    DMOD_TEST_EXPECT(sfx_synth_active(s));                                      /* (fading) */
    (void)render(s, pcm, RATE / 10u);
    DMOD_TEST_EXPECT(!sfx_synth_active(s) && render(s, pcm, RATE / 10u) == 0);
    Dmod_Free(pcm);
    sfx_synth_destroy(s);
}

/* Renders `seconds` in pieces of 50 ms; the highest rpm seen, and how many times it fell by `drop` in 0.25 s */
typedef struct
{
    float       max_rpm;
    uint32_t    drops;
} rpm_trace_t;

static void trace_rpm(sfx_synth_t s, int16_t* pcm, float seconds, float drop, rpm_trace_t* t)
{
    const uint32_t piece = RATE / 20u;
    float history[5] = { 0, 0, 0, 0, 0 };
    bool falling = false;
    memset(t, 0, sizeof(*t));
    for (uint32_t done = 0; done < (uint32_t)(seconds * (float)RATE); done += piece)
    {
        sfx_synth_render(s, pcm, piece);
        float rpm = sfx_synth_rpm(s);
        if (rpm > t->max_rpm)
            t->max_rpm = rpm;
        bool now = history[0] - rpm > drop;
        if (now && !falling)
            t->drops++;
        falling = now;
        memmove(history, history + 1, sizeof(history) - sizeof(history[0]));
        history[4] = rpm;
        history[0] = history[1];
    }
}

/* An engine at the idle, past its start */
static sfx_synth_t idling_engine(int16_t* pcm)
{
    sfx_synth_t s = sfx_synth_create();
    if (s == NULL)
        return NULL;
    sfx_synth_engine(s, true);
    sfx_synth_render(s, pcm, RATE);
    sfx_synth_render(s, pcm, RATE);
    sfx_synth_render(s, pcm, RATE);
    sfx_synth_render(s, pcm, RATE / 2u);                        /* 3.5 s: it is past the start (3.3 s) */
    return s;
}

DMOD_TEST_STEP(sfx_maneuver_is_only_made_by_an_idling_engine)
{
    int16_t* pcm = Dmod_Malloc(2u * RATE * sizeof(int16_t));
    sfx_synth_t s = sfx_synth_create();
    DMOD_TEST_EXPECT(s != NULL && pcm != NULL);
    if (s == NULL || pcm == NULL)
        return;
    DMOD_TEST_EXPECT(!sfx_synth_maneuver(s, SFX_MANEUVER_BLIP));       /* (off) */
    sfx_synth_engine(s, true);
    DMOD_TEST_EXPECT(!sfx_synth_maneuver(s, SFX_MANEUVER_BLIP));       /* (still starting) */
    sfx_synth_destroy(s);
    s = idling_engine(pcm);
    DMOD_TEST_EXPECT(s != NULL && sfx_synth_engine_mode(s) == SFX_ENGINE_RUN);
    DMOD_TEST_EXPECT(!sfx_synth_maneuver(s, SFX_MANEUVER_NONE));
    DMOD_TEST_EXPECT(sfx_synth_maneuver(s, SFX_MANEUVER_BLIP));
    DMOD_TEST_EXPECT(!sfx_synth_maneuver(s, SFX_MANEUVER_PULL));       /* (one at a time) */
    DMOD_TEST_EXPECT(sfx_synth_maneuver_now(s) == SFX_MANEUVER_BLIP);
    DMOD_TEST_EXPECT(!sfx_synth_maneuver(NULL, SFX_MANEUVER_BLIP));
    Dmod_Free(pcm);
    sfx_synth_destroy(s);
}

DMOD_TEST_STEP(sfx_blip_revs_up_and_falls_back_to_the_idle)
{
    int16_t* pcm = Dmod_Malloc(2u * RATE * sizeof(int16_t));
    sfx_synth_t s = (pcm != NULL) ? idling_engine(pcm) : NULL;
    DMOD_TEST_EXPECT(s != NULL);
    if (s == NULL)
        return;
    rpm_trace_t t;
    float idle = sfx_synth_rpm(s);
    DMOD_TEST_EXPECT(idle > 850.0f && idle < 1000.0f);
    DMOD_TEST_EXPECT(sfx_synth_maneuver(s, SFX_MANEUVER_BLIP));
    trace_rpm(s, pcm, 2.5f, 1000.0f, &t);
    DMOD_TEST_EXPECT(t.max_rpm > 3000.0f);
    DMOD_TEST_EXPECT(sfx_synth_rpm(s) < 1100.0f);
    DMOD_TEST_EXPECT(sfx_synth_maneuver_now(s) == SFX_MANEUVER_NONE && sfx_synth_maneuvers_made(s) == 1);
    Dmod_Free(pcm);
    sfx_synth_destroy(s);
}

DMOD_TEST_STEP(sfx_pull_shifts_up_through_three_gears)
{
    int16_t* pcm = Dmod_Malloc(2u * RATE * sizeof(int16_t));
    sfx_synth_t s = (pcm != NULL) ? idling_engine(pcm) : NULL;
    DMOD_TEST_EXPECT(s != NULL);
    if (s == NULL)
        return;
    rpm_trace_t t;
    DMOD_TEST_EXPECT(sfx_synth_maneuver(s, SFX_MANEUVER_PULL));
    trace_rpm(s, pcm, 5.0f, 800.0f, &t);
    DMOD_TEST_EXPECT(t.max_rpm > 6000.0f && t.max_rpm < 6600.0f);
    DMOD_TEST_EXPECT(t.drops >= 3);                                     /* (a shift each: the revs fall at once) */
    trace_rpm(s, pcm, 1.5f, 800.0f, &t);
    DMOD_TEST_EXPECT(sfx_synth_rpm(s) < 1100.0f && sfx_synth_maneuvers_made(s) == 1);
    Dmod_Free(pcm);
    sfx_synth_destroy(s);
}

DMOD_TEST_STEP(sfx_idling_engine_is_revved_by_itself)
{
    int16_t* pcm = Dmod_Malloc(2u * RATE * sizeof(int16_t));
    sfx_synth_t s = (pcm != NULL) ? idling_engine(pcm) : NULL;
    DMOD_TEST_EXPECT(s != NULL);
    if (s == NULL)
        return;
    rpm_trace_t t;
    trace_rpm(s, pcm, 30.0f, 1000.0f, &t);                              /* (the first comes after 4 - 7 s, then 7 - 16) */
    DMOD_TEST_EXPECT(sfx_synth_maneuvers_made(s) >= 2);
    DMOD_TEST_EXPECT(t.max_rpm > 3000.0f);
    DMOD_TEST_EXPECT(render(s, pcm, RATE / 10u) < 30000);               /* (never louder than the limit) */
    sfx_synth_engine(s, false);
    DMOD_TEST_EXPECT(sfx_synth_maneuver_now(s) == SFX_MANEUVER_NONE);   /* (switched off: no maneuver goes on) */
    Dmod_Free(pcm);
    sfx_synth_destroy(s);
}

DMOD_TEST_STEP(sfx_synth_ignores_null)
{
    int16_t pcm[4] = { 1, 2, 3, 4 };
    sfx_synth_engine(NULL, true);
    sfx_synth_siren(NULL, true);
    sfx_synth_render(NULL, pcm, 4);
    DMOD_TEST_EXPECT(pcm[0] == 0 && pcm[3] == 0 && !sfx_synth_active(NULL));
    DMOD_TEST_EXPECT(sfx_synth_engine_mode(NULL) == SFX_ENGINE_OFF);
    sfx_synth_destroy(NULL);
}

DMOD_TEST_STEP(sfx_says_the_start_is_not_over_until_the_thread_has_made_it)
{
    sfx_t s = sfx_new();
    DMOD_TEST_EXPECT(s != NULL);
    if (s == NULL)
        return;
    DMOD_TEST_EXPECT(sfx_engine_started(s) && !sfx_engine_wanted(s));          /* (nothing asked: nothing to wait for) */
    sfx_engine_set(s, true);
    DMOD_TEST_EXPECT(sfx_engine_wanted(s));
    DMOD_TEST_EXPECT(!sfx_engine_started(s));                                  /* (no audio device here: it stays so) */
    sfx_engine_set(s, false);
    DMOD_TEST_EXPECT(!sfx_engine_wanted(s) && sfx_engine_started(s));
    sfx_siren_set(s, true);
    DMOD_TEST_EXPECT(sfx_siren_wanted(s));
    sfx_siren_set(s, false);
    DMOD_TEST_EXPECT(!sfx_siren_wanted(s));
    sfx_free(s);
    sfx_free(NULL);
    DMOD_TEST_EXPECT(sfx_engine_started(NULL) && !sfx_engine_wanted(NULL));
}

DMOD_TEST_STEP(fuel_last_fifteen_minutes_and_clamps_at_empty)
{
    fuel_t f;
    fuel_init(&f, 5000);
    fuel_event(&f, "engine");
    fuel_update(&f, 5000 + FUEL_RUN_MS / 2);
    DMOD_TEST_EXPECT(fuel_level(&f) == 500);
    fuel_update(&f, 5000 + FUEL_RUN_MS - 1);
    DMOD_TEST_EXPECT(f.remaining_ms == 1);
    fuel_update(&f, 5000 + FUEL_RUN_MS);
    DMOD_TEST_EXPECT(fuel_level(&f) == 0);
    fuel_update(&f, UINT64_MAX);
    DMOD_TEST_EXPECT(f.remaining_ms == 0);
}

DMOD_TEST_STEP(fuel_requires_open_screen_cap_and_hose_then_stops_at_full)
{
    fuel_t f;
    fuel_init(&f, 0);
    fuel_event(&f, "engine");
    fuel_update(&f, FUEL_RUN_MS);
    fuel_event(&f, "fuel.filler");
    DMOD_TEST_EXPECT(!f.visible && f.stage == FUEL_CAPPED);
    fuel_event(&f, "fuel.open");
    fuel_event(&f, "fuel.filler");
    fuel_update(&f, FUEL_RUN_MS + 1000);
    DMOD_TEST_EXPECT(f.stage == FUEL_OPEN && fuel_level(&f) == 0);
    fuel_event(&f, "fuel.filler");
    fuel_update(&f, FUEL_RUN_MS + 1000 + FUEL_FILL_MS / 2);
    DMOD_TEST_EXPECT(f.stage == FUEL_PUMPING && fuel_level(&f) == 500);
    fuel_update(&f, FUEL_RUN_MS + 1000 + FUEL_FILL_MS);
    DMOD_TEST_EXPECT(f.stage == FUEL_FULL && fuel_level(&f) == 1000);
    fuel_update(&f, FUEL_RUN_MS * 2);
    DMOD_TEST_EXPECT(f.stage == FUEL_FULL && fuel_level(&f) == 1000);
    fuel_event(&f, "fuel.close");
    fuel_update(&f, FUEL_RUN_MS * 2 + 900);
    DMOD_TEST_EXPECT(fuel_level(&f) == 1000);
    fuel_event(&f, "engine");
    fuel_update(&f, FUEL_RUN_MS * 2 + 1800);
    DMOD_TEST_EXPECT(fuel_level(&f) == 999);
}

DMOD_TEST_STEP(fuel_can_interrupt_and_resume_without_resetting_the_level)
{
    fuel_t f;
    fuel_init(&f, 100);
    fuel_event(&f, "engine");
    fuel_update(&f, 100 + FUEL_RUN_MS);
    fuel_event(&f, "fuel.open");
    fuel_event(&f, "fuel.filler");
    fuel_event(&f, "fuel.filler");
    fuel_update(&f, 100 + FUEL_RUN_MS + 10000);
    fuel_event(&f, "fuel.close");
    fuel_update(&f, 100 + FUEL_RUN_MS + 10900);
    DMOD_TEST_EXPECT(f.stage == FUEL_CAPPED && fuel_level(&f) == 500);
    fuel_event(&f, "fuel.open");
    DMOD_TEST_EXPECT(f.stage == FUEL_CAPPED && fuel_level(&f) == 500);
    fuel_event(&f, "fuel.filler"); fuel_event(&f, "fuel.filler");
    fuel_event(&f, "fuel.filler");
    DMOD_TEST_EXPECT(f.stage == FUEL_DISCONNECTED);
    fuel_update(&f, 100 + FUEL_RUN_MS + 11800);
    DMOD_TEST_EXPECT(fuel_level(&f) == 500);
    fuel_event(&f, "fuel.filler"); /* Close the cap before reopening and reconnecting. */
    DMOD_TEST_EXPECT(f.stage == FUEL_CAPPED);
    fuel_event(&f, "fuel.filler"); fuel_event(&f, "fuel.filler");
    fuel_update(&f, UINT64_MAX);
    DMOD_TEST_EXPECT(f.stage == FUEL_FULL && fuel_level(&f) == 1000);
    DMOD_TEST_EXPECT(!fuel_event(&f, "next"));
}

DMOD_TEST_STEP(sfx_pump_works_alone_and_fades_to_silence)
{
    sfx_synth_t s = sfx_synth_create();
    int16_t* pcm = Dmod_Malloc(RATE * sizeof(int16_t));
    DMOD_TEST_EXPECT(s != NULL && pcm != NULL);
    if (s == NULL || pcm == NULL) { sfx_synth_destroy(s); Dmod_Free(pcm); return; }
    sfx_synth_pump(s, true);
    DMOD_TEST_EXPECT(sfx_synth_active(s));
    DMOD_TEST_EXPECT(render(s, pcm, RATE) > 1000);
    DMOD_TEST_EXPECT(sfx_synth_engine_mode(s) == SFX_ENGINE_OFF);
    sfx_synth_pump(s, false);
    sfx_synth_render(s, pcm, RATE);
    DMOD_TEST_EXPECT(!sfx_synth_active(s) && render(s, pcm, RATE) == 0);
    sfx_synth_pump(s, true);
    DMOD_TEST_EXPECT(render(s, pcm, RATE) > 1000);
    sfx_synth_pump(NULL, true);
    sfx_pump_set(NULL, true);
    sfx_synth_destroy(s); Dmod_Free(pcm);
}

DMOD_TEST_STEP(fuel_empty_stops_engine_and_prevents_restart_until_refuelled)
{
    fuel_t f;
    fuel_init(&f, 0);
    fuel_event(&f, "engine");
    DMOD_TEST_EXPECT(f.engine_on);
    fuel_update(&f, FUEL_RUN_MS);
    DMOD_TEST_EXPECT(!f.engine_on && f.notice == FUEL_NOTICE_EMPTY);
    fuel_event(&f, "engine");
    DMOD_TEST_EXPECT(!f.engine_on && f.notice == FUEL_NOTICE_EMPTY);
    fuel_event(&f, "fuel.open");
    fuel_event(&f, "fuel.filler"); fuel_event(&f, "fuel.filler");
    fuel_update(&f, FUEL_RUN_MS + 1000);
    DMOD_TEST_EXPECT(fuel_level(&f) == 50 && f.notice == FUEL_NOTICE_NONE);
    fuel_event(&f, "engine");
    DMOD_TEST_EXPECT(!f.engine_on && f.notice == FUEL_NOTICE_HOSE_CONNECTED);
    fuel_event(&f, "fuel.close");
    fuel_event(&f, "engine");
    DMOD_TEST_EXPECT(f.engine_on && f.notice == FUEL_NOTICE_NONE);
}

DMOD_TEST_STEP(fuel_cannot_fill_with_engine_on_or_start_with_hose_connected)
{
    fuel_t f;
    fuel_init(&f, 0);
    fuel_event(&f, "engine");
    fuel_update(&f, FUEL_RUN_MS / 2);
    fuel_event(&f, "fuel.open");
    fuel_event(&f, "fuel.filler"); fuel_event(&f, "fuel.filler");
    DMOD_TEST_EXPECT(f.stage == FUEL_CAPPED && f.notice == FUEL_NOTICE_ENGINE_RUNNING);
    fuel_update(&f, FUEL_RUN_MS / 2 + 900);
    DMOD_TEST_EXPECT(fuel_level(&f) == 499 && f.engine_on);
    fuel_event(&f, "engine");
    DMOD_TEST_EXPECT(!f.engine_on && f.notice == FUEL_NOTICE_NONE);
    fuel_event(&f, "fuel.filler"); fuel_event(&f, "fuel.filler");
    DMOD_TEST_EXPECT(f.stage == FUEL_PUMPING);
    fuel_event(&f, "engine");
    DMOD_TEST_EXPECT(!f.engine_on && f.stage == FUEL_PUMPING);
    fuel_update(&f, FUEL_RUN_MS / 2 + 900 + FUEL_FILL_MS);
    DMOD_TEST_EXPECT(f.stage == FUEL_FULL);
    fuel_event(&f, "engine");
    DMOD_TEST_EXPECT(!f.engine_on && f.notice == FUEL_NOTICE_HOSE_CONNECTED);
    fuel_event(&f, "fuel.filler");
    fuel_event(&f, "engine");
    DMOD_TEST_EXPECT(f.engine_on && f.stage == FUEL_DISCONNECTED);
    fuel_event(&f, "fuel.filler");
    DMOD_TEST_EXPECT(f.stage == FUEL_CAPPED); /* Closing remains possible with the hose removed. */
    fuel_event(&f, "fuel.filler");
    DMOD_TEST_EXPECT(f.stage == FUEL_CAPPED && f.notice == FUEL_NOTICE_ENGINE_RUNNING);
}

DMOD_TEST_STEP(fuel_empty_engine_becomes_silent_and_cannot_crank)
{
    fuel_t f;
    fuel_init(&f, 0);
    fuel_event(&f, "engine");
    sfx_synth_t s = sfx_synth_create();
    int16_t* pcm = Dmod_Malloc(RATE * sizeof(int16_t));
    DMOD_TEST_EXPECT(s != NULL && pcm != NULL);
    if (s == NULL || pcm == NULL) { sfx_synth_destroy(s); Dmod_Free(pcm); return; }
    sfx_synth_engine(s, f.engine_on);
    for (int i = 0; i < 4; i++) sfx_synth_render(s, pcm, RATE);
    DMOD_TEST_EXPECT(sfx_synth_engine_mode(s) == SFX_ENGINE_RUN);
    fuel_update(&f, FUEL_RUN_MS);
    sfx_synth_engine(s, f.engine_on);
    for (int i = 0; i < 2; i++) sfx_synth_render(s, pcm, RATE);
    DMOD_TEST_EXPECT(render(s, pcm, RATE) == 0 && !sfx_synth_active(s));
    fuel_event(&f, "engine");
    sfx_synth_engine(s, f.engine_on);
    DMOD_TEST_EXPECT(f.notice == FUEL_NOTICE_EMPTY);
    DMOD_TEST_EXPECT(render(s, pcm, RATE) == 0 && sfx_synth_engine_mode(s) == SFX_ENGINE_OFF);
    sfx_synth_destroy(s); Dmod_Free(pcm);
}

DMOD_TEST_STEP(fuel_counts_only_engine_time_across_long_stops)
{
    fuel_t f;
    fuel_init(&f, 0);
    uint64_t now = FUEL_RUN_MS;
    fuel_update(&f, now); /* Parked from startup. */
    DMOD_TEST_EXPECT(f.remaining_ms == FUEL_RUN_MS && !f.engine_on);
    fuel_event(&f, "engine");
    fuel_update(&f, now += FUEL_RUN_MS / 2);
    DMOD_TEST_EXPECT(fuel_level(&f) == 500 && f.engine_on);
    fuel_event(&f, "engine");
    fuel_update(&f, now += FUEL_RUN_MS * 3u);
    DMOD_TEST_EXPECT(fuel_level(&f) == 500 && !f.engine_on);
    fuel_event(&f, "fuel.open"); fuel_event(&f, "fuel.filler");
    fuel_update(&f, now += FUEL_RUN_MS);
    DMOD_TEST_EXPECT(fuel_level(&f) == 500 && f.stage == FUEL_OPEN);
    fuel_event(&f, "fuel.close");
    fuel_event(&f, "engine");
    fuel_update(&f, now); /* Restart must not charge the time spent parked. */
    DMOD_TEST_EXPECT(fuel_level(&f) == 500);
    fuel_update(&f, now += FUEL_RUN_MS / 2 - 1);
    DMOD_TEST_EXPECT(f.remaining_ms == 1 && f.engine_on);
    fuel_update(&f, now + 1);
    DMOD_TEST_EXPECT(f.remaining_ms == 0 && !f.engine_on);
}

DMOD_TEST_STEP(fuel_full_tank_disconnects_hose_then_closes_cap)
{
    fuel_t f;
    fuel_init(&f, 0);
    fuel_event(&f, "fuel.open");
    fuel_event(&f, "fuel.filler"); fuel_event(&f, "fuel.filler");
    DMOD_TEST_EXPECT(f.stage == FUEL_FULL);
    fuel_event(&f, "fuel.filler");
    DMOD_TEST_EXPECT(f.stage == FUEL_DISCONNECTED);
    fuel_event(&f, "fuel.filler");
    DMOD_TEST_EXPECT(f.stage == FUEL_CAPPED && fuel_level(&f) == 1000);
}

DMOD_TEST_STEP(fuel_engine_warning_flashes_four_times_then_disappears)
{
    fuel_t f;
    fuel_init(&f, 0);
    fuel_event(&f, "engine"); fuel_event(&f, "fuel.open");
    DMOD_TEST_EXPECT(!fuel_warning_visible(&f));
    fuel_event(&f, "fuel.filler");
    DMOD_TEST_EXPECT(fuel_warning_visible(&f));
    for (uint32_t half = 1; half <= FUEL_WARNING_FLASHES * 2u; half++)
    {
        fuel_update(&f, half * FUEL_WARNING_HALF_MS);
        bool visible = half < FUEL_WARNING_FLASHES * 2u && half % 2u == 0;
        DMOD_TEST_EXPECT(fuel_warning_visible(&f) == visible);
        DMOD_TEST_EXPECT(f.stage == FUEL_CAPPED);
    }
    fuel_event(&f, "fuel.filler");
    DMOD_TEST_EXPECT(fuel_warning_visible(&f)); /* Another attempt restarts it. */
    fuel_event(&f, "engine");
    DMOD_TEST_EXPECT(!fuel_warning_visible(&f));
    fuel_event(&f, "engine"); fuel_event(&f, "fuel.filler");
    DMOD_TEST_EXPECT(fuel_warning_visible(&f));
    fuel_event(&f, "fuel.close");
    DMOD_TEST_EXPECT(!fuel_warning_visible(&f));
}
