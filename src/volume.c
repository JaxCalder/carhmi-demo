#include "volume.h"
#include "libdmaudio.h"
#include <string.h>

#define FILE_NAME   ".volume"
#define OUTPUT_FILE ".output"
#define NO_OUTPUT   (-1)

struct volume
{
    char*               path;           /* The file, NULL: nothing is kept */
    uint8_t             level;
    bool                muted;
    bool                applied;        /* The device has level and muted */
    bool                dirty;          /* Level is not in the file */
    Dmod_Timestamp_t    changed_at;
    char*               output_path;    /* The file of the output, NULL: not kept */
    int32_t             output;         /* What the device plays through, NO_OUTPUT: not known yet */
    int32_t             wanted;         /* The output to set, NO_OUTPUT: none */
};

/* The level the file says, else VOLUME_DEFAULT */
static uint8_t read_level(const char* path)
{
    uint32_t level = VOLUME_DEFAULT;
    void* f = Dmod_FileOpen(path, "rb");
    if (f != NULL)
    {
        char text[8];
        size_t n = Dmod_FileRead(text, 1, sizeof(text), f);
        Dmod_FileClose(f);
        uint32_t value = 0;
        size_t digits = 0;
        while (digits < n && text[digits] >= '0' && text[digits] <= '9')
            value = value * 10u + (uint32_t)(text[digits++] - '0');
        if (digits > 0 && value <= 100u)
            level = value;
    }
    return (uint8_t)level;
}

static void write_level(const char* path, uint8_t level)
{
    void* f = Dmod_FileOpen(path, "wb");
    if (f == NULL)
        return;                         /* (a read-only card: the volume is not kept) */
    char text[8];
    int n = Dmod_SnPrintf(text, sizeof(text), "%u\n", (unsigned)level);
    if (n > 0)
        (void)Dmod_FileWrite(text, 1, (size_t)n, f);
    Dmod_FileClose(f);
}

/* The output the file says, else NO_OUTPUT */
static int32_t read_output(const char* path)
{
    void* f = Dmod_FileAvailable(path) ? Dmod_FileOpen(path, "rb") : NULL;   /* (none: no error logged) */
    if (f == NULL)
        return NO_OUTPUT;
    char text[16] = { 0 };
    (void)Dmod_FileRead(text, 1, sizeof(text) - 1u, f);
    Dmod_FileClose(f);
    if (strncmp(text, "speaker", 7) == 0)
        return DMDRVI_AUDIO_OUTPUT_SPEAKER;
    if (strncmp(text, "headphone", 9) == 0)
        return DMDRVI_AUDIO_OUTPUT_HEADPHONE;
    return NO_OUTPUT;
}

static void write_output(const char* path, int32_t output)
{
    void* f = Dmod_FileOpen(path, "wb");
    if (f == NULL)
        return;
    const char* text = (output == DMDRVI_AUDIO_OUTPUT_SPEAKER) ? "speaker\n" : "headphone\n";
    (void)Dmod_FileWrite(text, 1, strlen(text), f);
    Dmod_FileClose(f);
}

/* "<dir>/<name>", NULL: out of memory */
static char* path_in(const char* dir, const char* name)
{
    size_t size = strlen(dir) + 1u + strlen(name) + 1u;
    char* path = Dmod_Malloc(size);
    if (path != NULL)
        Dmod_SnPrintf(path, size, "%s/%s", dir, name);
    return path;
}

volume_t volume_new(const char* dir)
{
    volume_t v = Dmod_Malloc(sizeof(*v));
    if (v == NULL)
        return NULL;
    memset(v, 0, sizeof(*v));
    v->level = VOLUME_DEFAULT;
    v->output = NO_OUTPUT;
    v->wanted = NO_OUTPUT;
    if (dir != NULL)
    {
        v->path = path_in(dir, FILE_NAME);
        if (v->path != NULL)
            v->level = read_level(v->path);
        v->output_path = path_in(dir, OUTPUT_FILE);
        if (v->output_path != NULL)
            v->wanted = read_output(v->output_path);
    }
    return v;
}

void volume_free(volume_t v)
{
    if (v == NULL)
        return;
    if (v->dirty && v->path != NULL)
        write_level(v->path, v->level);
    Dmod_Free(v->path);
    Dmod_Free(v->output_path);
    Dmod_Free(v);
}

uint8_t volume_level(volume_t v)
{
    return v->level;
}

bool volume_muted(volume_t v)
{
    return v->muted;
}

void volume_set(volume_t v, int32_t level)
{
    uint8_t clamped = (level < 0) ? 0 : (level > 100) ? 100 : (uint8_t)level;
    if (clamped == v->level && !v->muted)
        return;
    v->level = clamped;
    v->muted = false;
    v->applied = false;
    v->dirty = true;
    v->changed_at = Dmod_GetUptime();
}

void volume_toggle_mute(volume_t v)
{
    v->muted = !v->muted;
    v->applied = false;
}

int32_t volume_output(volume_t v)
{
    return v->output;
}

void volume_toggle_output(volume_t v)
{
    if (v->output == NO_OUTPUT)
        return;
    v->wanted = (v->output == DMDRVI_AUDIO_OUTPUT_HEADPHONE) ? DMDRVI_AUDIO_OUTPUT_SPEAKER
                                                             : DMDRVI_AUDIO_OUTPUT_HEADPHONE;
    v->output = v->wanted;              /* (shown at once; the device says it again below) */
    if (v->output_path != NULL)
        write_output(v->output_path, v->wanted);
}

bool volume_poll(volume_t v, Dmod_Timestamp_t now)
{
    /* The service switches it a moment later - or goes back, when the codec refuses: then the device says so */
    if (v->wanted != NO_OUTPUT && libdmaudio_device_set_output(NULL, (dmdrvi_audio_output_t)v->wanted) == 0)
        v->wanted = NO_OUTPUT;
    libdmaudio_device_info_t info;
    if (v->wanted == NO_OUTPUT && libdmaudio_device_info(NULL, &info) == 0)
        v->output = (int32_t)info.output;
    if (!v->applied)
    {
        /* The device (NULL: the first one) may not be served yet */
        v->applied = libdmaudio_device_set_volume(NULL, v->level) == 0 &&
                     libdmaudio_device_set_mute(NULL, v->muted) == 0;
    }
    if (v->dirty && v->path != NULL && now - v->changed_at >= VOLUME_SAVE_MS)
    {
        write_level(v->path, v->level);
        v->dirty = false;
    }
    return v->applied;
}
