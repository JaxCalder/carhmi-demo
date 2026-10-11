#include "player.h"
#include "dmosi.h"
#include "dmmp3.h"
#include "libdmaudio.h"
#include <errno.h>
#include <string.h>

/*
 * The player's thread decodes the current file and writes it into a stream
 * of libdmaudio; the write waits while the stream's buffer is full, so the
 * device's clock paces the decoding. The rest of carhmi only asks it to play,
 * pause, go on (under `lock`) and reads where it is.
 */

#define MAX_TRACKS      200u
#define CHUNK_FRAMES    1152u           /* An MP3 frame */
#define BUFFER_MS       500u            /* Of the stream: the card's and the drawing's delays */
#define THREAD_STACK    4096u           /* (the decoder's memory is on the heap) */
#define THREAD_PRIORITY 2               /* Below the audio service (3), above the views (1) */
#define IDLE_MS         100u
#define WRITE_MS        200u

struct player
{
    player_track_t*     tracks;
    uint32_t            count;
    dmosi_mutex_t       lock;
    dmosi_semaphore_t   wake;
    dmosi_thread_t      thread;
    bool                quit;
    bool                done;           /* The thread is out */

    /* Asked of the thread (under lock) */
    int32_t             current;        /* -1: none */
    int32_t             request;        /* A track to start, -1: none */
    bool                playing;
    uint32_t            changes;

    /* The thread's, read under lock */
    uint32_t            position_ms;
    uint32_t            duration_ms;
};

/* ---- The files ---- */

static bool is_mp3(const char* name)
{
    size_t n = strlen(name);
    if (n < 5 || name[0] == '.')
        return false;
    const char* e = name + n - 4;
    return e[0] == '.' && (e[1] | 0x20) == 'm' && (e[2] | 0x20) == 'p' && e[3] == '3';
}

static void copy_text(char* to, const char* from, size_t n)
{
    size_t k = strlen(from);
    if (k >= PLAYER_TEXT)
    {
        k = PLAYER_TEXT - 1U;
        while (k > 0 && ((uint8_t)from[k] & 0xC0u) == 0x80u)
            k--;                                    /* Not in a character */
    }
    if (k > n)
        k = n;
    memcpy(to, from, k);
    to[k] = '\0';
}

static int by_name(const player_track_t* a, const player_track_t* b)
{
    return strcmp(a->path, b->path);
}

static uint32_t scan(player_t p, const char* dir)
{
    void* d = Dmod_OpenDir(dir);
    if (d == NULL)
        return 0;
    const char* name;
    while ((name = Dmod_ReadDir(d)) != NULL && p->count < MAX_TRACKS)
    {
        if (!is_mp3(name))
            continue;
        player_track_t* t = &p->tracks[p->count];
        memset(t, 0, sizeof(*t));
        Dmod_SnPrintf(t->path, sizeof(t->path), "%s/%s", dir, name);
        dmmp3_info_t info;
        if (dmmp3_read_info(t->path, &info) != 0)
            continue;                               /* Not an MP3 file after all */
        if (info.title[0] != '\0')
            copy_text(t->title, info.title, sizeof(t->title) - 1U);
        else
            copy_text(t->title, name, strlen(name) - 4U);   /* Its name, without .mp3 */
        copy_text(t->artist, info.artist, sizeof(t->artist) - 1U);
        t->duration_ms = info.duration_ms;
        p->count++;
    }
    Dmod_CloseDir(d);

    /* By name (insertion: a card's directory, not a million files) */
    for (uint32_t i = 1; i < p->count; i++)
    {
        player_track_t t = p->tracks[i];
        uint32_t k = i;
        for (; k > 0 && by_name(&p->tracks[k - 1U], &t) > 0; k--)
            p->tracks[k] = p->tracks[k - 1U];
        p->tracks[k] = t;
    }
    return p->count;
}

/* ---- The thread ---- */

static void player_thread(void* arg)
{
    player_t p = arg;
    int16_t* pcm = Dmod_Malloc(CHUNK_FRAMES * 2u * sizeof(int16_t));
    dmmp3_t mp3 = NULL;
    libdmaudio_stream_t stream = NULL;
    libdmaudio_format_t format = { 0, 0, DMDRVI_AUDIO_FORMAT_S16_LE };
    bool paused = false;

    while (pcm != NULL)
    {
        while (dmosi_semaphore_wait(p->wake, 1, 0) == 0)
            ;                                       /* (what woke it: seen below) */
        dmosi_mutex_lock(p->lock);
        bool quit = p->quit;
        int32_t request = p->request;
        bool playing = p->playing;
        p->request = -1;
        dmosi_mutex_unlock(p->lock);
        if (quit)
            break;

        if (request >= 0)
        {
            /* Another track: what the stream holds of the previous one is dropped */
            dmmp3_close(mp3);
            int status = 0;
            mp3 = dmmp3_open(p->tracks[request].path, &status);
            dmmp3_info_t info;
            memset(&info, 0, sizeof(info));
            if (mp3 != NULL)
                (void)dmmp3_get_info(mp3, &info);
            else
                DMOD_LOG_ERROR("carhmi: cannot play %s (%d)\n", p->tracks[request].path, status);
            libdmaudio_close(stream);
            stream = NULL;
            if (mp3 != NULL)
            {
                format.sample_rate_hz = info.sample_rate_hz;
                format.channels = info.channels;
                libdmaudio_stream_options_t o = { BUFFER_MS, 100 };
                stream = libdmaudio_open(NULL, &format, &o, &status);
                if (stream == NULL)
                    DMOD_LOG_ERROR("carhmi: no audio device to play on (%d)\n", status);
                paused = false;
            }
            dmosi_mutex_lock(p->lock);
            p->position_ms = 0;
            p->duration_ms = info.duration_ms;
            dmosi_mutex_unlock(p->lock);
        }

        if (stream != NULL && playing == paused)
        {
            /* Paused: what the stream holds waits in it */
            paused = !playing;
            (void)libdmaudio_set_paused(stream, paused);
        }
        if (mp3 == NULL || stream == NULL || !playing)
        {
            (void)dmosi_semaphore_wait(p->wake, 1, (int32_t)IDLE_MS);
            continue;
        }

        int32_t n = dmmp3_read(mp3, pcm, CHUNK_FRAMES);
        if (n <= 0)
        {
            /* Its end: what is buffered plays out, then the next one */
            (void)libdmaudio_drain(stream, 2000);
            dmmp3_close(mp3);
            mp3 = NULL;
            dmosi_mutex_lock(p->lock);
            if (p->request < 0 && p->count > 0)
            {
                p->current = (p->current + 1) % (int32_t)p->count;
                p->request = p->current;
                p->changes++;
            }
            dmosi_mutex_unlock(p->lock);
            continue;
        }
        const int16_t* at = pcm;
        while (n > 0)
        {
            int32_t w = libdmaudio_write(stream, at, (uint32_t)n, (int32_t)WRITE_MS);
            if (w == -ETIMEDOUT)
            {
                /* The stream is full and stays so (paused, or another track asked for): look again */
                dmosi_mutex_lock(p->lock);
                bool again = p->request < 0 && p->playing && !p->quit;
                dmosi_mutex_unlock(p->lock);
                if (!again)
                    break;
                continue;
            }
            if (w < 0)
            {
                DMOD_LOG_ERROR("carhmi: the audio device is gone (%d)\n", (int)w);
                libdmaudio_close(stream);
                stream = NULL;
                break;
            }
            at += w * format.channels;
            n -= w;
        }
        /* Where it is: what was decoded, less what the stream holds */
        libdmaudio_stream_status_t st;
        uint32_t held = 0;
        if (stream != NULL && libdmaudio_get_status(stream, &st) == 0 && format.sample_rate_hz != 0)
            held = (uint32_t)((uint64_t)st.buffered_frames * 1000u / format.sample_rate_hz);
        uint32_t told = dmmp3_tell(mp3);
        dmosi_mutex_lock(p->lock);
        p->position_ms = (told > held) ? told - held : 0;
        dmosi_mutex_unlock(p->lock);
    }

    dmmp3_close(mp3);
    libdmaudio_close(stream);
    if (pcm != NULL)
        Dmod_Free(pcm);
    dmosi_mutex_lock(p->lock);
    p->done = true;
    dmosi_mutex_unlock(p->lock);
}

/* ---- The player ---- */

player_t player_new(const char* dir)
{
    player_t p = Dmod_Malloc(sizeof(*p));
    if (p == NULL)
        return NULL;
    memset(p, 0, sizeof(*p));
    p->tracks = Dmod_Malloc(MAX_TRACKS * sizeof(player_track_t));
    p->lock = dmosi_mutex_create(false);
    p->wake = dmosi_semaphore_create(0, 8);
    p->current = -1;
    p->request = -1;
    if (p->tracks == NULL || p->lock == NULL || p->wake == NULL)
    {
        player_free(p);
        return NULL;
    }
    (void)scan(p, dir);
    p->thread = dmosi_thread_create(player_thread, p, THREAD_PRIORITY, THREAD_STACK, "carhmi_player",
                                    dmosi_process_current());
    if (p->thread == NULL)
    {
        player_free(p);
        return NULL;
    }
    return p;
}

void player_free(player_t p)
{
    if (p == NULL)
        return;
    if (p->thread != NULL)
    {
        dmosi_mutex_lock(p->lock);
        p->quit = true;
        dmosi_mutex_unlock(p->lock);
        (void)dmosi_semaphore_post(p->wake, 1);
        (void)dmosi_thread_join(p->thread);
        dmosi_thread_destroy(p->thread);
    }
    if (p->wake != NULL)
        dmosi_semaphore_destroy(p->wake);
    if (p->lock != NULL)
        dmosi_mutex_destroy(p->lock);
    if (p->tracks != NULL)
        Dmod_Free(p->tracks);
    Dmod_Free(p);
}

uint32_t player_count(player_t p)
{
    return p->count;
}

const player_track_t* player_track(player_t p, uint32_t index)
{
    return (index < p->count) ? &p->tracks[index] : NULL;
}

/* The thread takes what it was asked at once (it takes the posts at the top of its loop) */
static void wake(player_t p)
{
    (void)dmosi_semaphore_post(p->wake, 1);
}

void player_play(player_t p, uint32_t index)
{
    if (index >= p->count)
        return;
    dmosi_mutex_lock(p->lock);
    p->current = (int32_t)index;
    p->request = (int32_t)index;
    p->playing = true;
    p->changes++;
    dmosi_mutex_unlock(p->lock);
    wake(p);
}

void player_toggle(player_t p)
{
    dmosi_mutex_lock(p->lock);
    bool start = p->current < 0 && p->count > 0;
    p->playing = !p->playing || start;
    if (start)
    {
        p->current = 0;
        p->request = 0;
    }
    p->changes++;
    dmosi_mutex_unlock(p->lock);
    wake(p);
}

void player_next(player_t p, int32_t step)
{
    if (p->count == 0)
        return;
    dmosi_mutex_lock(p->lock);
    int32_t n = (int32_t)p->count;
    int32_t i = (p->current < 0) ? 0 : (((p->current + step) % n) + n) % n;
    dmosi_mutex_unlock(p->lock);
    player_play(p, (uint32_t)i);
}

void player_state(player_t p, player_state_t* state)
{
    dmosi_mutex_lock(p->lock);
    state->track = p->current;
    state->playing = p->playing;
    state->position_ms = p->position_ms;
    state->duration_ms = p->duration_ms;
    state->changes = p->changes;
    dmosi_mutex_unlock(p->lock);
}
