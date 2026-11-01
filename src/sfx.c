#include "sfx.h"
#include "sfx_synth.h"
#include "dmosi.h"
#include "libdmaudio.h"
#include <errno.h>
#include <string.h>

#define SFX_MAGIC       0x58465346u     /* "FSFX" */

#define CHUNK_FRAMES    441u            /* 20 ms */
#define BUFFER_MS       250u            /* Of the stream: what the player's decoding may take from the CPU */
#define WRITE_MS        200u
#define THREAD_STACK    4096u
#define THREAD_PRIORITY 2               /* As the player's: below the audio service (3), above the views (1) */
#define IDLE_MS         100u
#define DRAIN_MS        (BUFFER_MS + 500u)

struct sfx
{
    uint32_t            magic;
    dmosi_mutex_t       lock;
    dmosi_semaphore_t   wake;
    dmosi_thread_t      thread;
    /* Under `lock` */
    bool                want_engine;
    bool                want_siren;
    bool                applied_engine; /* Said by the thread: the wish it has acted on ... */
    bool                starting;       /* ... and that the engine is cranking, catching, settling */
    bool                quit;
    bool                done;           /* The thread is out */
};

static bool valid(sfx_t s)
{
    return s != NULL && s->magic == SFX_MAGIC;
}

/* What the thread has to do now: the wishes, taken */
typedef struct
{
    bool engine;
    bool siren;
    bool quit;
} wishes_t;

static void take_wishes(sfx_t s, wishes_t* w)
{
    dmosi_mutex_lock(s->lock);
    w->engine = s->want_engine;
    w->siren = s->want_siren;
    w->quit = s->quit;
    dmosi_mutex_unlock(s->lock);
}

static void publish(sfx_t s, sfx_synth_t synth, bool engine)
{
    bool starting = sfx_synth_engine_mode(synth) == SFX_ENGINE_START;
    dmosi_mutex_lock(s->lock);
    s->applied_engine = engine;
    s->starting = starting;
    dmosi_mutex_unlock(s->lock);
}

static libdmaudio_stream_t open_stream(void)
{
    libdmaudio_format_t format = { SFX_SYNTH_RATE_HZ, 1, DMDRVI_AUDIO_FORMAT_S16_LE };
    libdmaudio_stream_options_t options = { BUFFER_MS, 100 };
    int status = 0;
    libdmaudio_stream_t stream = libdmaudio_open(NULL, &format, &options, &status);
    if (stream == NULL)
    {
        DMOD_LOG_ERROR("carhmi: no audio device for the sounds of the car (%d)\n", status);
    }
    return stream;
}

/* Writes the chunk whole; false: the stream is gone, or the thread is asked to stop */
static bool write_chunk(sfx_t s, libdmaudio_stream_t stream, const int16_t* pcm, uint32_t frames)
{
    while (frames > 0)
    {
        int32_t w = libdmaudio_write(stream, pcm, frames, (int32_t)WRITE_MS);
        if (w == -ETIMEDOUT)
        {
            wishes_t wishes;
            take_wishes(s, &wishes);
            if (wishes.quit)
                return false;
            continue;
        }
        if (w < 0)
        {
            DMOD_LOG_ERROR("carhmi: the audio device is gone (%d)\n", (int)w);
            return false;
        }
        pcm += w;
        frames -= (uint32_t)w;
    }
    return true;
}

static void sfx_thread(void* arg)
{
    sfx_t s = arg;
    sfx_synth_t synth = sfx_synth_create();
    int16_t* pcm = Dmod_Malloc(CHUNK_FRAMES * sizeof(int16_t));
    libdmaudio_stream_t stream = NULL;
    bool engine = false;
    bool siren = false;

    while (synth != NULL && pcm != NULL)
    {
        wishes_t w;
        take_wishes(s, &w);
        if (w.quit)
            break;
        if (w.engine != engine)
            sfx_synth_engine(synth, engine = w.engine);
        if (w.siren != siren)
            sfx_synth_siren(synth, siren = w.siren);
        publish(s, synth, engine);

        if (!sfx_synth_active(synth))
        {
            if (stream != NULL)
            {
                (void)libdmaudio_drain(stream, (int32_t)DRAIN_MS);      /* (the end of the fade plays out) */
                libdmaudio_close(stream);
                stream = NULL;
            }
            (void)dmosi_semaphore_wait(s->wake, 1, (int32_t)IDLE_MS);
            continue;
        }
        if (stream == NULL && (stream = open_stream()) == NULL)
        {
            (void)dmosi_semaphore_wait(s->wake, 1, (int32_t)(IDLE_MS * 10u));
            continue;
        }
        sfx_synth_render(synth, pcm, CHUNK_FRAMES);
        if (!write_chunk(s, stream, pcm, CHUNK_FRAMES))
        {
            libdmaudio_close(stream);
            stream = NULL;
        }
    }

    libdmaudio_close(stream);
    if (pcm != NULL)
        Dmod_Free(pcm);
    sfx_synth_destroy(synth);
    dmosi_mutex_lock(s->lock);
    s->done = true;
    dmosi_mutex_unlock(s->lock);
}

sfx_t sfx_new(void)
{
    sfx_t s = Dmod_Malloc(sizeof(*s));
    if (s == NULL)
        return NULL;
    memset(s, 0, sizeof(*s));
    s->magic = SFX_MAGIC;
    s->lock = dmosi_mutex_create(false);
    s->wake = dmosi_semaphore_create(0, 1);
    if (s->lock != NULL && s->wake != NULL)
        s->thread = dmosi_thread_create(sfx_thread, s, THREAD_PRIORITY, THREAD_STACK, "carhmi_sfx",
                                        dmosi_process_current());
    if (s->thread == NULL)
    {
        sfx_free(s);
        return NULL;
    }
    return s;
}

void sfx_stop(sfx_t s)
{
    if (!valid(s))
        return;
    dmosi_mutex_lock(s->lock);
    s->quit = true;
    dmosi_mutex_unlock(s->lock);
    (void)dmosi_semaphore_post(s->wake, 1);
}

void sfx_free(sfx_t s)
{
    if (!valid(s))
        return;
    sfx_stop(s);
    if (s->thread != NULL)
    {
        (void)dmosi_thread_join(s->thread);
        dmosi_thread_destroy(s->thread);
    }
    if (s->wake != NULL)
        dmosi_semaphore_destroy(s->wake);
    if (s->lock != NULL)
        dmosi_mutex_destroy(s->lock);
    s->magic = 0;
    Dmod_Free(s);
}

static void set_wish(sfx_t s, bool* wish, bool on)
{
    dmosi_mutex_lock(s->lock);
    *wish = on;
    dmosi_mutex_unlock(s->lock);
    (void)dmosi_semaphore_post(s->wake, 1);
}

void sfx_engine_set(sfx_t s, bool on)
{
    if (valid(s))
        set_wish(s, &s->want_engine, on);
}

void sfx_siren_set(sfx_t s, bool on)
{
    if (valid(s))
        set_wish(s, &s->want_siren, on);
}

static bool read_flag(sfx_t s, const bool* flag, bool when_invalid)
{
    if (!valid(s))
        return when_invalid;
    dmosi_mutex_lock(s->lock);
    bool value = *flag;
    dmosi_mutex_unlock(s->lock);
    return value;
}

bool sfx_engine_wanted(sfx_t s)
{
    return valid(s) && read_flag(s, &s->want_engine, false);
}

bool sfx_siren_wanted(sfx_t s)
{
    return valid(s) && read_flag(s, &s->want_siren, false);
}

bool sfx_engine_started(sfx_t s)
{
    if (!valid(s))
        return true;
    dmosi_mutex_lock(s->lock);
    /* Wanted and not yet acted on, or acted on and still starting: not yet. Not wanted: nothing to wait for */
    bool started = !s->want_engine || (s->applied_engine && !s->starting);
    dmosi_mutex_unlock(s->lock);
    return started;
}
