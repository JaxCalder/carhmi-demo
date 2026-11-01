#include "sfx_synth.h"
#include "dmod.h"
#include <string.h>

/*
 * The engine: a V8, four firings a turn (rpm / 15 Hz), and not even ones - the cylinders do not sound alike and each
 * firing comes a little early or late, which is the lump of the idle. A firing is a pulse that rises in a few
 * per cent of its period (2.5 %) and dies away (the phase of the firing is the clock, so the pitch follows the revs, which
 * a bank of fixed notes would not). The pulses go through the exhaust: the low note (a low-pass whose cut-off
 * opens with the throttle) and three resonances, some 180, 520 and 1400 Hz, where a small speaker has its say.
 * The roar of the intake is noise, the throttle's and the revs'; the whole is limited softly, which is the grit.
 *
 * The start (SFX_SYNTH_START_S): 0 - 1.4 s the starter cranks it, the compression strokes heavy; 1.4 - 1.9 s it
 * catches with a bark - up to 2700 rpm, misfires fewer and fewer; 1.9 - 3.3 s it falls to the idle, 900 rpm.
 *
 * At the idle it is revved now and then (sfx_maneuver_t): a blip, a double blip, a pull through three gears. A
 * maneuver is a short list of steps - the rpm to go to and how fast, the throttle, an ignition cut for the shift
 * (a bang), the pops of the exhaust when the throttle is shut. The next one comes after 7 - 16 seconds.
 *
 * The siren: a wail, 640 - 1440 Hz and back in 3.4 s, the note a square-ish wave (the 3rd and 5th harmonics).
 */

#define SYNTH_MAGIC         0x58465353u         /* "SSFX" */

#define TWO_PI              6.28318530718f
#define DT                  (1.0f / (float)SFX_SYNTH_RATE_HZ)

#define IDLE_RPM            900.0f
#define CRANK_RPM           170.0f
#define BARK_RPM            2700.0f
#define CRANK_S             1.4f
#define CATCH_S             0.5f
#define SETTLE_S            (SFX_SYNTH_START_S - CRANK_S - CATCH_S)

#define FIRINGS_PER_TURN    4.0f
#define CYCLE               8u                  /* Firings that make the pattern of the cylinders */
#define CUT_S               0.09f               /* The ignition cut of a shift */
#define POPS_PER_S          16.0f

#define NEXT_MIN_S          7.0f
#define NEXT_SPAN_S         9.0f
#define FIRST_MIN_S         4.0f
#define FIRST_SPAN_S        3.0f

#define SIREN_PERIOD_S      3.4f
#define SIREN_LOW_HZ        640.0f
#define SIREN_SPAN_HZ       800.0f
#define FADE_S              0.05f

/* One step of a maneuver: from `at` seconds on the engine goes to `target` rpm, at most `up` / `down` rpm a second */
typedef struct
{
    float   at;
    float   target;
    float   up;
    float   down;
    float   throttle;
    uint8_t cut;            /* An ignition cut, for the shift */
    uint8_t pops;           /* The exhaust pops while the revs fall */
} step_t;

typedef struct
{
    float   b0;             /* A band-pass, b1 = 0, b2 = -b0 */
    float   a1;
    float   a2;
    float   z1;
    float   z2;
} bandpass_t;

struct sfx_synth
{
    uint32_t            magic;
    /* The engine */
    sfx_engine_mode_t   mode;
    float               t;              /* Seconds in the mode */
    float               rpm;
    float               target;         /* Where the rpm goes at the idle */
    float               up;             /* ... how fast, rpm a second */
    float               down;
    float               throttle;       /* 0 ... 1 */
    float               throttle_goal;
    float               rpm_stop;       /* The rpm when it started to die down */
    float               fire_phase;     /* Of the firing (one turn: one firing) */
    float               fire_scale;     /* A firing's own length: it comes early or late */
    float               pulse_amp;      /* Of the firing that is on */
    uint32_t            fire_index;
    float               cut_t;          /* Seconds of an ignition cut that are left */
    float               pop_env;
    float               pop_lp;
    float               pops;           /* 1: the exhaust pops */
    float               noise_lp;
    float               body_lp;
    float               starter_phase;
    float               starter_lp;
    float               dc;
    float               wander;         /* Slow drift of the idle's rpm */
    bandpass_t          formant[3];
    /* The maneuvers */
    sfx_maneuver_t      maneuver;
    float               maneuver_t;
    uint32_t            maneuver_step;
    float               next_s;         /* Until the next one comes by itself */
    uint32_t            maneuvers;      /* Made */
    /* The siren */
    bool                siren_on;
    float               siren_t;
    float               siren_phase;
    float               siren_level;
    uint32_t            rng;
};

/* What the mode asks of the engine at this moment */
typedef struct
{
    float   level;          /* Of the whole engine */
    float   starter;        /* Of the starter */
    float   misfire;        /* The chance that a firing does not happen */
    float   strength;       /* Of a firing */
} engine_demand_t;

static inline float absf(float x)
{
    return x < 0.0f ? -x : x;
}

static inline bool valid(sfx_synth_t s)
{
    return s != NULL && s->magic == SYNTH_MAGIC;
}

float sfx_synth_sin(float turns)
{
    float t = turns - (float)(int32_t)turns;
    if (t < 0.0f)
        t += 1.0f;
    float x = (t > 0.5f ? t - 1.0f : t) * TWO_PI;       /* -pi ... pi */
    float y = 1.27323954f * x - 0.405284735f * x * absf(x);
    return 0.225f * (y * absf(y) - y) + y;              /* (the parabola, corrected: within 0.1 %) */
}

/* -1 ... 1 */
static float noise(sfx_synth_t s)
{
    uint32_t x = s->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s->rng = x;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

static float noise01(sfx_synth_t s)
{
    return noise(s) * 0.5f + 0.5f;
}

/* tanh(x), as a fraction (the Pade approximant), clamped where it is already 1 */
static float limit(float x)
{
    if (x > 3.0f)
        x = 3.0f;
    else if (x < -3.0f)
        x = -3.0f;
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

/* A band-pass at `hz` with the quality `q` (the RBJ cookbook's, the constant skirt gain one) */
static void bandpass_set(bandpass_t* f, float hz, float q)
{
    float turns = hz * DT;
    float alpha = sfx_synth_sin(turns) / (2.0f * q);
    float cosw = sfx_synth_sin(turns + 0.25f);
    float a0 = 1.0f + alpha;
    f->b0 = alpha / a0;
    f->a1 = -2.0f * cosw / a0;
    f->a2 = (1.0f - alpha) / a0;
    f->z1 = 0.0f;
    f->z2 = 0.0f;
}

static float bandpass_run(bandpass_t* f, float x)
{
    float y = f->b0 * x + f->z1;
    f->z1 = f->z2 - f->a1 * y;
    f->z2 = -f->b0 * x - f->a2 * y;
    return y;
}

sfx_synth_t sfx_synth_create(void)
{
    sfx_synth_t s = Dmod_Malloc(sizeof(*s));
    if (s == NULL)
        return NULL;
    memset(s, 0, sizeof(*s));
    s->magic = SYNTH_MAGIC;
    s->rng = 0x2545F491u;
    s->fire_scale = 1.0f;
    bandpass_set(&s->formant[0], 180.0f, 2.0f);
    bandpass_set(&s->formant[1], 520.0f, 3.0f);
    bandpass_set(&s->formant[2], 1400.0f, 4.0f);
    return s;
}

void sfx_synth_destroy(sfx_synth_t s)
{
    if (!valid(s))
        return;
    s->magic = 0;
    Dmod_Free(s);
}

void sfx_synth_engine(sfx_synth_t s, bool on)
{
    if (!valid(s))
        return;
    if (on && (s->mode == SFX_ENGINE_OFF || s->mode == SFX_ENGINE_STOP))
    {
        s->mode = SFX_ENGINE_START;
        s->t = 0.0f;
        s->rpm = CRANK_RPM;
        s->fire_phase = 0.9f;                   /* (the first stroke comes at once) */
        s->pulse_amp = 0.0f;
        s->wander = 0.0f;
        s->cut_t = 0.0f;
        s->maneuver = SFX_MANEUVER_NONE;
    }
    else if (!on && (s->mode == SFX_ENGINE_START || s->mode == SFX_ENGINE_RUN))
    {
        s->mode = SFX_ENGINE_STOP;
        s->rpm_stop = s->rpm;
        s->t = 0.0f;
        s->maneuver = SFX_MANEUVER_NONE;
    }
}

void sfx_synth_siren(sfx_synth_t s, bool on)
{
    if (!valid(s))
        return;
    if (on && !s->siren_on && s->siren_level <= 0.0f)
    {
        s->siren_t = 0.0f;
        s->siren_phase = 0.0f;
    }
    s->siren_on = on;
}

sfx_engine_mode_t sfx_synth_engine_mode(sfx_synth_t s)
{
    return valid(s) ? s->mode : SFX_ENGINE_OFF;
}

bool sfx_synth_active(sfx_synth_t s)
{
    return valid(s) && (s->mode != SFX_ENGINE_OFF || s->siren_on || s->siren_level > 0.0f);
}

float sfx_synth_rpm(sfx_synth_t s)
{
    return valid(s) ? s->rpm : 0.0f;
}

uint32_t sfx_synth_maneuvers_made(sfx_synth_t s)
{
    return valid(s) ? s->maneuvers : 0u;
}

sfx_maneuver_t sfx_synth_maneuver_now(sfx_synth_t s)
{
    return valid(s) ? s->maneuver : SFX_MANEUVER_NONE;
}

/*
 * The maneuvers. A blip: the throttle opens (up to 3800 rpm in 0.3 s), shuts, the revs fall and the exhaust pops.
 * A double one. A pull: from the idle to 6300 rpm in the first gear, the ignition is cut, the revs fall to 4200 in
 * the second (and so on to the third), and at the end the throttle is lifted off and the exhaust crackles. The last
 * step of each is the return to the idle; the maneuver ends at `end` seconds.
 */
static const step_t BLIP[] =
{
    { 0.00f, 3800.0f, 9000.0f, 5000.0f, 1.0f, 0, 0 },
    { 0.30f, IDLE_RPM, 9000.0f, 3200.0f, 0.0f, 0, 1 },
};
static const step_t DOUBLE[] =
{
    { 0.00f, 3100.0f, 9000.0f, 6000.0f, 1.0f, 0, 0 },
    { 0.22f, 1300.0f, 9000.0f, 5500.0f, 0.1f, 0, 1 },
    { 0.44f, 4300.0f, 9000.0f, 5000.0f, 1.0f, 0, 0 },
    { 0.74f, IDLE_RPM, 9000.0f, 3000.0f, 0.0f, 0, 1 },
};
static const step_t PULL[] =
{
    { 0.00f, 6300.0f, 4200.0f, 4000.0f, 1.0f, 0, 0 },
    { 1.40f, 4300.0f, 4200.0f, 22000.0f, 0.0f, 1, 0 },
    { 1.55f, 6300.0f, 3300.0f, 22000.0f, 1.0f, 0, 0 },
    { 2.30f, 4700.0f, 3000.0f, 22000.0f, 0.0f, 1, 0 },
    { 2.45f, 6300.0f, 2700.0f, 22000.0f, 1.0f, 0, 0 },
    { 3.25f, 5000.0f, 2500.0f, 22000.0f, 0.0f, 1, 0 },
    { 3.40f, 6000.0f, 2300.0f, 22000.0f, 1.0f, 0, 0 },
    { 3.90f, IDLE_RPM, 2500.0f, 3800.0f, 0.0f, 0, 1 },
};

static const step_t* steps_of(sfx_maneuver_t m, uint32_t* count, float* end)
{
    switch (m)
    {
    case SFX_MANEUVER_BLIP:
        *count = sizeof(BLIP) / sizeof(BLIP[0]);
        *end = 1.9f;
        return BLIP;
    case SFX_MANEUVER_DOUBLE:
        *count = sizeof(DOUBLE) / sizeof(DOUBLE[0]);
        *end = 2.4f;
        return DOUBLE;
    case SFX_MANEUVER_PULL:
        *count = sizeof(PULL) / sizeof(PULL[0]);
        *end = 5.4f;
        return PULL;
    default:
        *count = 0;
        *end = 0.0f;
        return NULL;
    }
}

bool sfx_synth_maneuver(sfx_synth_t s, sfx_maneuver_t maneuver)
{
    if (!valid(s) || s->mode != SFX_ENGINE_RUN || s->maneuver != SFX_MANEUVER_NONE || maneuver == SFX_MANEUVER_NONE)
        return false;
    s->maneuver = maneuver;
    s->maneuver_t = 0.0f;
    s->maneuver_step = 0;
    return true;
}

/* The idle's rpm (a little lump of its own) and the next maneuver, when it is time */
static void idle_run(sfx_synth_t s)
{
    s->wander += noise(s) * 0.5f - s->wander * 0.0008f;
    s->next_s -= DT;
    if (s->next_s <= 0.0f && s->maneuver == SFX_MANEUVER_NONE)
    {
        float pick = noise01(s);
        (void)sfx_synth_maneuver(s, pick < 0.45f ? SFX_MANEUVER_BLIP : (pick < 0.75f ? SFX_MANEUVER_DOUBLE : SFX_MANEUVER_PULL));
        s->next_s = NEXT_MIN_S + NEXT_SPAN_S * noise01(s);
    }
}

/* The steps of the maneuver that is on: those whose time has come are taken; false when it is over */
static void maneuver_run(sfx_synth_t s)
{
    uint32_t count;
    float end;
    const step_t* steps = steps_of(s->maneuver, &count, &end);
    if (steps == NULL)
        return;
    s->maneuver_t += DT;
    while (s->maneuver_step < count && s->maneuver_t >= steps[s->maneuver_step].at)
    {
        const step_t* st = &steps[s->maneuver_step++];
        s->target = st->target;
        s->up = st->up;
        s->down = st->down;
        s->throttle_goal = st->throttle;
        s->pops = st->pops ? 1.0f : 0.0f;
        if (st->cut)
        {
            s->cut_t = CUT_S;
            s->pop_env = 1.2f;                  /* (the bang of the cut) */
        }
    }
    if (s->maneuver_t >= end)
    {
        s->maneuver = SFX_MANEUVER_NONE;
        s->maneuvers++;
        s->pops = 0.0f;
        s->next_s = NEXT_MIN_S + NEXT_SPAN_S * noise01(s);      /* (a pause after each, whoever started it) */
    }
}

/* The start: cranking, the bark, the fall to the idle - the rpm and what the demand is at s->t */
static void start_profile(sfx_synth_t s, engine_demand_t* d)
{
    if (s->t < CRANK_S)
    {
        s->rpm = CRANK_RPM + 25.0f * sfx_synth_sin(2.1f * s->t);
        s->throttle = 0.1f;
        d->starter = 1.0f;
        d->strength = 0.9f;
    }
    else if (s->t < CRANK_S + CATCH_S)
    {
        float u = (s->t - CRANK_S) / CATCH_S;
        s->rpm = CRANK_RPM + (BARK_RPM - CRANK_RPM) * (1.0f - (1.0f - u) * (1.0f - u));
        s->throttle = 0.3f + 0.7f * u;
        d->starter = u < 0.5f ? 1.0f - u * 2.0f : 0.0f;
        d->misfire = 0.4f * (1.0f - u);
    }
    else
    {
        float r = 1.0f - (s->t - CRANK_S - CATCH_S) / SETTLE_S;
        s->rpm = IDLE_RPM + (BARK_RPM - IDLE_RPM) * r * r * r;
        s->throttle = 0.15f + 0.85f * r;
    }
    if (s->t < FADE_S)
        d->level = s->t / FADE_S;
}

/* The revs follow the target, as fast as the step allows; the throttle follows its goal */
static void follow_target(sfx_synth_t s)
{
    float gap = s->target - s->rpm;
    float step = (gap > 0.0f ? s->up : s->down) * DT;
    s->rpm += absf(gap) < step ? gap : (gap > 0.0f ? step : -step);
    float dt = s->throttle_goal - s->throttle;
    float slew = 8.0f * DT;
    s->throttle += absf(dt) < slew ? dt : (dt > 0.0f ? slew : -slew);
}

static void run_profile(sfx_synth_t s, engine_demand_t* d)
{
    (void)d;
    idle_run(s);
    if (s->maneuver == SFX_MANEUVER_NONE)
    {
        s->target = IDLE_RPM + 14.0f * sfx_synth_sin(0.27f * s->t) + s->wander;
        s->up = 1500.0f;
        s->down = 1500.0f;
        s->throttle_goal = 0.15f;
        s->pops = 0.0f;
    }
    else
        maneuver_run(s);
    follow_target(s);
}

/* Moves the mode on and says what the engine is asked for; false: it is silent */
static bool engine_demand(sfx_synth_t s, engine_demand_t* d)
{
    d->level = 1.0f;
    d->starter = 0.0f;
    d->misfire = 0.0f;
    d->strength = 0.8f;
    s->t += DT;
    if (s->mode == SFX_ENGINE_START && s->t >= SFX_SYNTH_START_S)
    {
        s->mode = SFX_ENGINE_RUN;
        s->t = 0.0f;
        s->target = IDLE_RPM;
        s->throttle = 0.15f;
        s->throttle_goal = 0.15f;
        s->next_s = FIRST_MIN_S + FIRST_SPAN_S * noise01(s);
    }
    switch (s->mode)
    {
    case SFX_ENGINE_START:
        start_profile(s, d);
        return true;
    case SFX_ENGINE_RUN:
        run_profile(s, d);
        return true;
    case SFX_ENGINE_STOP:
    {
        float x = s->t / SFX_SYNTH_STOP_S;
        if (x >= 1.0f)
        {
            s->mode = SFX_ENGINE_OFF;
            s->rpm = 0.0f;
            return false;
        }
        s->rpm = s->rpm_stop * (1.0f - x) * (1.0f - x);
        s->throttle = 0.0f;
        d->level = 1.0f - x;
        return true;
    }
    default:
        return false;
    }
}

/* The cylinders do not sound alike: the weight of the firing of each of the eight */
static float cylinder_weight(uint32_t index)
{
    static const float WEIGHT[CYCLE] = { 1.0f, 0.78f, 0.95f, 0.62f, 1.0f, 0.74f, 0.9f, 0.68f };
    return WEIGHT[index % CYCLE];
}

/* The firing: the phase runs, at its end a pulse of the next cylinder starts (not at a misfire, not in a cut) */
static void engine_fire(sfx_synth_t s, const engine_demand_t* d)
{
    s->fire_phase += s->rpm * (FIRINGS_PER_TURN / 60.0f) * DT * s->fire_scale;
    if (s->cut_t > 0.0f)
        s->cut_t -= DT;
    if (s->fire_phase < 1.0f)
        return;
    s->fire_phase -= 1.0f;
    s->fire_scale = 1.0f + 0.07f * noise(s);            /* (this firing comes early or late) */
    float strength = d->strength * (0.45f + 0.55f * s->throttle);
    bool silent = noise01(s) < d->misfire || s->cut_t > 0.0f;
    s->pulse_amp = silent ? 0.0f : strength * cylinder_weight(s->fire_index);
    s->fire_index++;
    if (s->cut_t <= 0.0f && s->pulse_amp > 0.0f && s->throttle < 0.2f && s->rpm > 1500.0f && s->pops > 0.0f &&
        noise01(s) < 0.03f)
        s->pop_env = 0.6f + 0.5f * noise01(s);          /* (a pop on the back of a firing) */
}

/* The pulse of a firing at the phase p: a quick rise and a long fall */
static float pulse_shape(float p)
{
    float rise = p < 0.025f ? p * 40.0f : 1.0f;
    float fall = 1.0f - p;
    fall *= fall;
    return rise * fall * fall;
}

/* The exhaust: the low note, the three resonances, the roar; what the pops add */
static float exhaust(sfx_synth_t s, float src)
{
    float open = 0.07f + 0.45f * s->throttle;           /* The low-pass opens with the throttle */
    s->body_lp += open * (src - s->body_lp);
    float low = s->body_lp;
    float f1 = bandpass_run(&s->formant[0], src);
    float f2 = bandpass_run(&s->formant[1], src);
    float f3 = bandpass_run(&s->formant[2], src) * (0.5f + 0.5f * s->throttle);
    s->noise_lp += 0.35f * (noise(s) - s->noise_lp);
    float roar = s->noise_lp * (s->rpm * (1.0f / 6000.0f)) * (0.1f + 0.9f * s->throttle);
    float out = 0.35f * low + 0.65f * f1 + 2.2f * f2 + 1.7f * f3 + 0.8f * roar;
    if (s->pop_env > 0.001f)
    {
        s->pop_lp += 0.5f * (noise(s) - s->pop_lp);
        out += s->pop_env * (noise(s) + 0.6f * s->pop_lp) * 0.9f;
        s->pop_env -= s->pop_env * DT / 0.006f;
    }
    return out;
}

/* The starter's whine: a low-passed sawtooth that follows the strokes of the compression */
static float starter_voice(sfx_synth_t s, float amount)
{
    s->starter_phase += (230.0f + 20.0f * sfx_synth_sin(2.1f * s->t)) * DT;
    if (s->starter_phase >= 1.0f)
        s->starter_phase -= 1.0f;
    s->starter_lp += 0.3f * ((2.0f * s->starter_phase - 1.0f) - s->starter_lp);
    return s->starter_lp * 0.16f * amount;
}

/* One sample of the engine, -1 ... 1 or so */
static float engine_sample(sfx_synth_t s)
{
    engine_demand_t d;
    if (!engine_demand(s, &d))
        return 0.0f;
    engine_fire(s, &d);
    float src = s->pulse_amp * (pulse_shape(s->fire_phase) - 0.17f);
    float e = exhaust(s, src);
    if (s->pops > 0.0f && s->pop_env <= 0.001f && s->rpm > 1500.0f && s->throttle < 0.2f &&
        noise01(s) < POPS_PER_S * DT * 0.25f)
        s->pop_env = 0.5f + 0.6f * noise01(s);          /* (and now and then one of its own) */
    if (d.starter > 0.0f)
        e += starter_voice(s, d.starter);
    s->dc += 0.002f * (e - s->dc);                      /* (what the pulses leave of a constant) */
    return limit((e - s->dc) * 2.1f) * d.level;
}

/* One sample of the siren, -1 ... 1 or so */
static float siren_sample(sfx_synth_t s)
{
    float target = s->siren_on ? 1.0f : 0.0f;
    if (s->siren_level < target)
    {
        s->siren_level += DT / FADE_S;
        if (s->siren_level > target)
            s->siren_level = target;
    }
    else if (s->siren_level > target)
    {
        s->siren_level -= DT / FADE_S;
        if (s->siren_level < target)
            s->siren_level = target;
    }
    if (s->siren_level <= 0.0f)
        return 0.0f;

    s->siren_t += DT;
    if (s->siren_t >= SIREN_PERIOD_S)
        s->siren_t -= SIREN_PERIOD_S;
    float u = s->siren_t * (1.0f / SIREN_PERIOD_S);
    float tri = u < 0.5f ? 2.0f * u : 2.0f - 2.0f * u;
    float smooth = tri * tri * (3.0f - 2.0f * tri);
    s->siren_phase += (SIREN_LOW_HZ + SIREN_SPAN_HZ * smooth) * DT;
    if (s->siren_phase >= 1.0f)
        s->siren_phase -= 1.0f;
    float p = s->siren_phase;
    float wave = sfx_synth_sin(p) + 0.30f * sfx_synth_sin(3.0f * p) + 0.15f * sfx_synth_sin(5.0f * p);
    return wave * (1.0f / 1.45f) * s->siren_level;
}

void sfx_synth_render(sfx_synth_t s, int16_t* out, uint32_t frames)
{
    if (!valid(s))
    {
        memset(out, 0, frames * sizeof(*out));
        return;
    }
    for (uint32_t i = 0; i < frames; i++)
    {
        float v = limit(engine_sample(s) * 0.9f + siren_sample(s) * 0.5f);
        out[i] = (int16_t)(v * 30000.0f);
    }
}
