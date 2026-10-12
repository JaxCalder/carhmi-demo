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

#define CHUNK_FRAMES    1152u           /* An MP3 frame */
#define BUFFER_MS       500u            /* Of the stream: the card's and the drawing's delays */
#define THREAD_STACK    4096u           /* (the decoder's memory is on the heap) */
#define THREAD_PRIORITY 2               /* Below the audio service (3), above the views (1) */
#define IDLE_MS         100u
#define WRITE_MS        200u

struct player
{
    player_track_t*     tracks_head;
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

/* ---- Track memory management ---- */

static void track_free(player_track_t* t)
{
    if (t == NULL)
        return;
    if (t->path != NULL)
        Dmod_Free(t->path);
    if (t->title != NULL)
        Dmod_Free(t->title);
    if (t->artist != NULL)
        Dmod_Free(t->artist);
    Dmod_Free(t);
}

static player_track_t* track_alloc(void)
{
    player_track_t* t = Dmod_Malloc(sizeof(*t));
    if (t != NULL)
    {
        t->path = NULL;
        t->title = NULL;
        t->artist = NULL;
        t->duration_ms = 0;
        t->next = NULL;
    }
    return t;
}

/* ---- The files ---- */

static bool is_mp3(const char* name)
{
    size_t n = strlen(name);
    if (n < 5 || name[0] == '.')
        return false;
    const char* e = name + n - 4;
    return e[0] == '.' && (e[1] | 0x20) == 'm' && (e[2] | 0x20) == 'p' && e[3] == '3';
}

static int by_name(const char* a_path, const char* b_path)
{
    return strcmp(a_path, b_path);
}

static void track_insert_sorted(player_t p, player_track_t* t)
{
    player_track_t* prev = NULL;
    player_track_t* curr = p->tracks_head;
    while (curr != NULL && by_name(curr->path, t->path) < 0)
    {
        prev = curr;
        curr = curr->next;
    }
    t->next = curr;
    if (prev == NULL)
        p->tracks_head = t;
    else
        prev->next = t;
    p->count++;
}

static uint32_t scan(player_t p, const char* dir)
{
    void* d = Dmod_OpenDir(dir);
    if (d == NULL)
        return 0;
    const char* name;
    while ((name = Dmod_ReadDir(d)) != NULL)
    {
        if (!is_mp3(name))
            continue;
        size_t dir_len = strlen(dir);
        size_t name_len = strlen(name);
        size_t path_len = dir_len + 1 + name_len + 1;
        char* path = Dmod_Malloc(path_len);
        if (path == NULL)
            continue;
        Dmod_SnPrintf(path, path_len, "%s/%s", dir, name);
        dmmp3_info_t info;
        if (dmmp3_read_info(path, &info) != 0)
        {
            Dmod_Free(path);
            continue;
        }
        player_track_t* t = track_alloc();
        if (t == NULL)
        {
            Dmod_Free(path);
            continue;
        }
        t->path = path;
        if (info.title[0] != '\0')
            t->title = Dmod_StrDup(info.title);
        else
        {
            size_t base_len = name_len > 4 ? name_len - 4 : name_len;
            char* title = Dmod_Malloc(base_len + 1);
            if (title != NULL)
            {
                memcpy(title, name, base_len);
                title[base_len] = '\0';
            }
            t->title = title;
        }
        t->artist = (info.artist[0] != '\0') ? Dmod_StrDup(info.artist) : NULL;
        t->duration_ms = info.duration_ms;
        track_insert_sorted(p, t);
    }
    Dmod_CloseDir(d);
    return p->count;
}

static const player_track_t* track_at(player_t p, uint32_t index)
{
    player_track_t* t = p->tracks_head;
    for (uint32_t i = 0; i < index && t != NULL; i++)
        t = t->next;
    return t;
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
    const player_track_t* current_track = NULL;

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
            current_track = track_at(p, (uint32_t)request);
            dmmp3_close(mp3);
            int status = 0;
            mp3 = (current_track != NULL) ? dmmp3_open(current_track->path, &status) : NULL;
            dmmp3_info_t info;
            memset(&info, 0, sizeof(info));
            if (mp3 != NULL)
                (void)dmmp3_get_info(mp3, &info);
            else if (current_track != NULL)
                DMOD_LOG_ERROR("carhmi: cannot play %s (%d)\n", current_track->path, status);
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
            p->duration_ms = (mp3 != NULL) ? info.duration_ms : 0;
            dmosi_mutex_unlock(p->lock);
        }

        if (stream != NULL && playing == paused)
        {
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
    p->tracks_head = NULL;
    p->lock = dmosi_mutex_create(false);
    p->wake = dmosi_semaphore_create(0, 8);
    p->current = -1;
    p->request = -1;
    if (p->lock == NULL || p->wake == NULL)
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
    while (p->tracks_head != NULL)
    {
        player_track_t* next = p->tracks_head->next;
        track_free(p->tracks_head);
        p->tracks_head = next;
    }
    Dmod_Free(p);
}

uint32_t player_count(player_t p)
{
    return p->count;
}

const player_track_t* player_track(player_t p, uint32_t index)
{
    return track_at(p, index);
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