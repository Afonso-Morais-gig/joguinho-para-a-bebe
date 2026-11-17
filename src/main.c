#include <SDL.h>  
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define TAU 6.28318530717958647692f

/* Logical width is fixed; logical height adapts to the phone's aspect ratio
   so the game fills the whole screen with no black bars. */
#define SCREEN_WIDTH 480
#define DEFAULT_HEIGHT 800

/* Set to 1 if tapping empty space should also cost a life. */
#define MISS_TAP_COSTS_LIFE 0

/* Maximum simultaneous targets when score scales past 100 */
#define MAX_TARGETS 6

static int g_H = DEFAULT_HEIGHT;   /* logical height */
static float g_time = 0.0f;        /* seconds since start, drives animations */

typedef enum {
    STATE_MAIN_MENU,
    STATE_GAMEPLAY,
    STATE_PAUSE_MENU,
    STATE_GAME_OVER
} GameState;

typedef struct {
    bool active;
    float x, y;
    float radius;
    float maxTime;
    float timeLeft;
    float hue;
    float age;
} Target;

typedef struct {
    GameState state;
    int score;
    int best;
    int lives;
    bool newBest;
    float stateTime;
    Target targets[MAX_TARGETS];
} Game;

/* ---------- effects state ---------- */

typedef struct { float x, y, vx, vy, life, maxLife, size; SDL_Color c; bool isHeart; } Particle;
typedef struct { float x, y, age, hue; bool active; } Ripple;
typedef struct { float x, y, r, speed, phase, hue; bool isHeart; } Bokeh;

#define MAX_PARTICLES 200
#define MAX_RIPPLES 12
#define NUM_BOKEH 20

static Particle g_particles[MAX_PARTICLES];
static Ripple g_ripples[MAX_RIPPLES];
static Bokeh g_bokeh[NUM_BOKEH];
static float g_missFlash = 0.0f;
static float g_scorePop = 0.0f;

/* Dreamy Pink / Magenta / Berry / Rose-Gold Background Palettes: {top rgb, bottom rgb}. */
#define NUM_PAL 5
static const float PAL[NUM_PAL][2][3] = {
    {{ 95,  18,  75}, { 28,   6,  32}},   /* Cotton Candy Dusk */
    {{135,  22,  85}, { 38,   8,  40}},   /* Hot Magenta Night */
    {{110,  30, 105}, { 24,   8,  45}},   /* Berry Velvet      */
    {{150,  45,  90}, { 45,  10,  38}},   /* Rose Gold Glow    */
    {{ 80,  15,  95}, { 20,   5,  36}}    /* Neon Orchid       */
};
static float g_bg[2][3] = {{95, 18, 75}, {28, 6, 32}};

static float frand(void) { return (float)rand() / (float)RAND_MAX; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

/* ---------- sound: everything is synthesized in code (no audio files) ---------- */
#define AUDIO_RATE 44100
#define MAX_VOICES 8
#define MUSIC_BPM 140
#define STEP_SAMPLES ((AUDIO_RATE * 60) / (MUSIC_BPM * 4))   /* one 16th note */

typedef struct { bool active; int wave; float phase, f0, f1, t, dur, vol; } Voice;
static Voice g_voices[MAX_VOICES];
static SDL_AudioDeviceID g_audio = 0;
static bool g_musicOn = true;
static unsigned int g_sample = 0;

/* Original cheerful 2-bar loop (C major, bouncy). Lead notes in Hz, 0 = rest. */
static const float LEAD[32] = {
    659,659,0,659, 0,523,659,0,  784,0,0,0, 392,0,0,0,
    523,0,392,0,   330,0,440,494, 466,440,392,0, 330,0,294,0
};
static const float BASS[4] = { 131.0f, 98.0f, 110.0f, 87.0f };  /* one chord per 8 steps */

static float MusicSample(void) {
    static float leadPhase = 0.0f, bassPhase = 0.0f;
    const unsigned int step = (g_sample / STEP_SAMPLES) % 32u;
    const float posStep = (float)(g_sample % STEP_SAMPLES) / (float)STEP_SAMPLES;
    const unsigned int beatLen = STEP_SAMPLES * 4u;
    const float posBeat = (float)(g_sample % beatLen) / (float)beatLen;
    const float bassF = BASS[(g_sample / (STEP_SAMPLES * 8u)) % 4u];
    float out = 0.0f;

    float lf = LEAD[step];
    if (lf > 0.0f) {
        leadPhase += lf / AUDIO_RATE;
        if (leadPhase >= 1.0f) leadPhase -= 1.0f;
        float sq = (leadPhase < 0.5f) ? 1.0f : -1.0f;          /* square lead */
        float env = fmaxf(0.0f, 1.0f - posStep * 2.2f);        /* bouncy staccato */
        out += sq * env * 0.10f;
    }

    bassPhase += bassF / AUDIO_RATE;
    if (bassPhase >= 1.0f) bassPhase -= 1.0f;
    float tri = 4.0f * fabsf(bassPhase - 0.5f) - 1.0f;         /* triangle bass */
    float benv = fmaxf(0.0f, 1.0f - posBeat * 1.5f);
    out += tri * benv * 0.14f;

    g_sample++;
    return out;
}

static float VoiceSample(Voice* v) {
    float p = v->t / v->dur;
    if (p >= 1.0f) { v->active = false; return 0.0f; }
    float f = v->f0 * powf(v->f1 / v->f0, p);                  /* exponential pitch sweep */
    v->phase += f / AUDIO_RATE;
    if (v->phase >= 1.0f) v->phase -= 1.0f;
    float osc = (v->wave == 0) ? sinf(v->phase * TAU) : ((v->phase < 0.5f) ? 1.0f : -1.0f);
    float env = (1.0f - p) * (1.0f - p);
    v->t += 1.0f / AUDIO_RATE;
    return osc * env * v->vol;
}

static void AudioCallback(void* userdata, Uint8* stream, int len) {
    (void)userdata;
    float* out = (float*)stream;
    int n = len / (int)sizeof(float);
    for (int i = 0; i < n; i++) {
        float s = MusicSample();
        if (!g_musicOn) s = 0.0f;
        for (int v = 0; v < MAX_VOICES; v++)
            if (g_voices[v].active) s += VoiceSample(&g_voices[v]);
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        out[i] = s;
    }
}

static void OpenAudio(void) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        SDL_Log("Audio init failed: %s", SDL_GetError());
        return;
    }
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = AUDIO_RATE;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = AudioCallback;
    g_audio = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (g_audio) SDL_PauseAudioDevice(g_audio, 0);
    else SDL_Log("Audio unavailable: %s", SDL_GetError());
}

static void PlaySfx(int wave, float f0, float f1, float dur, float vol) {
    if (!g_audio) return;
    SDL_LockAudioDevice(g_audio);
    int slot = 0;
    for (int i = 0; i < MAX_VOICES; i++)
        if (!g_voices[i].active) { slot = i; break; }
    Voice* v = &g_voices[slot];
    v->active = true; v->wave = wave; v->phase = 0.0f;
    v->f0 = f0; v->f1 = f1; v->t = 0.0f; v->dur = dur; v->vol = vol;
    SDL_UnlockAudioDevice(g_audio);
}

/* Bubbly "pop" for a hit: quick falling sine with a little random pitch. */
static void PlayPop(void) {
    float f = 900.0f + frand() * 500.0f;
    PlaySfx(0, f, f * 0.25f, 0.11f, 0.45f);
}

/* Low square "bwomp" for a missed target. */
static void PlayMiss(void) {
    PlaySfx(1, 220.0f, 110.0f, 0.25f, 0.14f);
}

/* Random hue strictly in the pink / magenta / rose / coral spectrum (300..355 deg) */
static float randomPinkHue(void) {
    return 305.0f + frand() * 50.0f;
}

/* ---------- color helpers ---------- */

static SDL_Color Hsv(float h, float s, float v, Uint8 a) {
    h = fmodf(h, 360.0f);
    if (h < 0) h += 360.0f;
    float c = v * s;
    float hp = h / 60.0f;
    float x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
    float r = 0, g = 0, b = 0;
    switch ((int)hp) {
        case 0: r = c; g = x; break;
        case 1: r = x; g = c; break;
        case 2: g = c; b = x; break;
        case 3: g = x; b = c; break;
        case 4: r = x; b = c; break;
        default: r = c; b = x; break;
    }
    float m = v - c;
    SDL_Color col = { (Uint8)((r + m) * 255), (Uint8)((g + m) * 255), (Uint8)((b + m) * 255), a };
    return col;
}

static SDL_Color Rgba(Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    SDL_Color c = { r, g, b, a };
    return c;
}

/* ---------- primitive drawing (anti-aliased-looking via geometry) ---------- */

static void FillCircle(SDL_Renderer* r, float cx, float cy, float rad, SDL_Color c) {
    if (rad <= 0.5f) return;
    int n = (int)(rad * 0.5f) + 14;
    if (n > 56) n = 56;
    SDL_Vertex v[60];
    int idx[56 * 3];
    SDL_FPoint zero = { 0, 0 };
    v[0].position.x = cx; v[0].position.y = cy; v[0].color = c; v[0].tex_coord = zero;
    for (int i = 0; i <= n; i++) {
        float a = (float)i / (float)n * TAU;
        v[i + 1].position.x = cx + cosf(a) * rad;
        v[i + 1].position.y = cy + sinf(a) * rad;
        v[i + 1].color = c;
        v[i + 1].tex_coord = zero;
    }
    for (int i = 0; i < n; i++) {
        idx[i * 3] = 0; idx[i * 3 + 1] = i + 1; idx[i * 3 + 2] = i + 2;
    }
    SDL_RenderGeometry(r, NULL, v, n + 2, idx, n * 3);
}

/* Thick arc from angle a0 to a1 (radians, clockwise on screen). */
static void FillArc(SDL_Renderer* r, float cx, float cy, float r0, float r1, float a0, float a1, SDL_Color c) {
    float span = a1 - a0;
    if (span <= 0.001f) return;
    int n = (int)(span / TAU * 72.0f) + 1;
    if (n > 72) n = 72;
    SDL_Vertex v[2 * 74];
    int idx[72 * 6];
    SDL_FPoint zero = { 0, 0 };
    for (int i = 0; i <= n; i++) {
        float a = a0 + span * (float)i / (float)n;
        float ca = cosf(a), sa = sinf(a);
        v[i * 2].position.x = cx + ca * r1; v[i * 2].position.y = cy + sa * r1;
        v[i * 2 + 1].position.x = cx + ca * r0; v[i * 2 + 1].position.y = cy + sa * r0;
        v[i * 2].color = c; v[i * 2 + 1].color = c;
        v[i * 2].tex_coord = zero; v[i * 2 + 1].tex_coord = zero;
    }
    for (int i = 0; i < n; i++) {
        int o = i * 2;
        int *q = &idx[i * 6];
        q[0] = o; q[1] = o + 1; q[2] = o + 2;
        q[3] = o + 2; q[4] = o + 1; q[5] = o + 3;
    }
    SDL_RenderGeometry(r, NULL, v, 2 * (n + 1), idx, n * 6);
}

static void FillTri(SDL_Renderer* r, float x1, float y1, float x2, float y2, float x3, float y3, SDL_Color c) {
    SDL_Vertex v[3];
    SDL_FPoint zero = { 0, 0 };
    v[0].position.x = x1; v[0].position.y = y1;
    v[1].position.x = x2; v[1].position.y = y2;
    v[2].position.x = x3; v[2].position.y = y3;
    for (int i = 0; i < 3; i++) { v[i].color = c; v[i].tex_coord = zero; }
    SDL_RenderGeometry(r, NULL, v, 3, NULL, 0);
}

static void FillRectC(SDL_Renderer* r, float x, float y, float w, float h, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_FRect rc = { x, y, w, h };
    SDL_RenderFillRectF(r, &rc);
}

static void FillRoundRect(SDL_Renderer* r, float x, float y, float w, float h, float rad, SDL_Color c) {
    if (rad > w * 0.5f) rad = w * 0.5f;
    if (rad > h * 0.5f) rad = h * 0.5f;
    FillRectC(r, x + rad, y, w - 2 * rad, h, c);
    FillRectC(r, x, y + rad, w, h - 2 * rad, c);
    FillCircle(r, x + rad, y + rad, rad, c);
    FillCircle(r, x + w - rad, y + rad, rad, c);
    FillCircle(r, x + rad, y + h - rad, rad, c);
    FillCircle(r, x + w - rad, y + h - rad, rad, c);
}

static void DrawHeart(SDL_Renderer* r, float cx, float cy, float s, SDL_Color c) {
    FillCircle(r, cx - 0.5f * s, cy - 0.2f * s, 0.58f * s, c);
    FillCircle(r, cx + 0.5f * s, cy - 0.2f * s, 0.58f * s, c);
    FillTri(r, cx - 1.04f * s, cy - 0.05f * s, cx + 1.04f * s, cy - 0.05f * s, cx, cy + 1.15f * s, c);
}

static void DrawBackground(SDL_Renderer* r) {
    SDL_Color top = Rgba((Uint8)g_bg[0][0], (Uint8)g_bg[0][1], (Uint8)g_bg[0][2], 255);
    SDL_Color bot = Rgba((Uint8)g_bg[1][0], (Uint8)g_bg[1][1], (Uint8)g_bg[1][2], 255);
    SDL_Vertex v[4];
    SDL_FPoint zero = { 0, 0 };
    v[0].position.x = 0;            v[0].position.y = 0;           v[0].color = top;
    v[1].position.x = SCREEN_WIDTH; v[1].position.y = 0;           v[1].color = top;
    v[2].position.x = 0;            v[2].position.y = (float)g_H;  v[2].color = bot;
    v[3].position.x = SCREEN_WIDTH; v[3].position.y = (float)g_H;  v[3].color = bot;
    for (int i = 0; i < 4; i++) v[i].tex_coord = zero;
    int idx[6] = { 0, 1, 2, 2, 1, 3 };
    SDL_RenderGeometry(r, NULL, v, 4, idx, 6);

    /* Subtle top radial pink spotlight */
    FillCircle(r, SCREEN_WIDTH * 0.5f, 80.0f, 260.0f, Rgba(255, 110, 185, 18));

    /* Floating bokeh circles & soft hearts */
    for (int i = 0; i < NUM_BOKEH; i++) {
        Bokeh* b = &g_bokeh[i];
        float pulse = 0.78f + 0.22f * sinf(g_time * 1.1f + b->phase);
        SDL_Color bc = Hsv(b->hue, 0.55f, 1.0f, 28);
        if (b->isHeart) {
            DrawHeart(r, b->x, b->y, b->r * 0.45f * pulse, bc);
        } else {
            FillCircle(r, b->x, b->y, b->r * pulse, bc);
        }
    }
}

/* ---------- pixel font (5x7) ---------- */

typedef struct { char ch; const char* rows[7]; } Glyph;

static const Glyph FONT[] = {
    {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
    {'C', {".####", "#....", "#....", "#....", "#....", "#....", ".####"}},
    {'D', {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
    {'G', {".####", "#....", "#....", "#.###", "#...#", "#...#", ".###."}},
    {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'I', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"}},
    {'J', {"..###", "...#.", "...#.", "...#.", "#..#.", "#..#.", ".##.."}},
    {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
    {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'N', {"#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
    {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
    {'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
    {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
    {'!', {"..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."}},
    {'+', {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}},
    {'-', {".....", ".....", ".....", "#####", ".....", ".....", "....."}},
    {'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
    {'1', {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
    {'3', {"#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."}},
    {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
    {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
    {'6', {".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."}},
    {'7', {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
    {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
    {'9', {".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."}},
};

static const Glyph* FindGlyph(char ch) {
    for (size_t i = 0; i < sizeof FONT / sizeof FONT[0]; i++)
        if (FONT[i].ch == ch) return &FONT[i];
    return NULL;
}

static float TextWidth(const char* s, float scale) {
    size_t n = strlen(s);
    return n ? ((float)n * 6.0f - 1.0f) * scale : 0.0f;
}

static void DrawGlyph(SDL_Renderer* r, float x, float y, float scale, char ch, SDL_Color c) {
    const Glyph* g = FindGlyph(ch);
    if (!g) return;
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    for (int row = 0; row < 7; row++)
        for (int col = 0; col < 5; col++)
            if (g->rows[row][col] == '#') {
                SDL_FRect rc = { x + col * scale, y + row * scale, scale, scale };
                SDL_RenderFillRectF(r, &rc);
            }
}

/* cx = center x of text (or left x when centered == false). */
static void DrawText(SDL_Renderer* r, float cx, float y, float scale, const char* s, SDL_Color c, bool centered) {
    float x = centered ? cx - TextWidth(s, scale) / 2.0f : cx;
    float sh = scale * 0.45f;
    if (sh < 1.0f) sh = 1.0f;
    for (size_t i = 0; s[i]; i++)
        DrawGlyph(r, x + (float)i * 6.0f * scale + sh, y + sh, scale, s[i], Rgba(45, 0, 30, 120));
    for (size_t i = 0; s[i]; i++)
        DrawGlyph(r, x + (float)i * 6.0f * scale, y, scale, s[i], c);
}

/* Shimmering pink / blush / magenta wave text */
static void DrawTextPinkWave(SDL_Renderer* r, float cx, float y, float scale, const char* s, float bob) {
    float x = cx - TextWidth(s, scale) / 2.0f;
    float sh = scale * 0.5f;
    for (size_t i = 0; s[i]; i++) {
        float yy = y + sinf(g_time * 3.5f + (float)i * 0.65f) * bob;
        float xx = x + (float)i * 6.0f * scale;
        /* Soft magenta outer glow behind glyph */
        DrawGlyph(r, xx + sh, yy + sh, scale, s[i], Rgba(80, 0, 50, 150));
        /* Oscillate hue smoothly between 310 (magenta-pink) and 350 (warm rose) */
        float hue = 330.0f + 22.0f * sinf(g_time * 2.5f + (float)i * 0.5f);
        float sat = 0.45f + 0.35f * (0.5f + 0.5f * cosf(g_time * 3.0f + (float)i * 0.8f));
        DrawGlyph(r, xx, yy, scale, s[i], Hsv(hue, sat, 1.0f, 255));
    }
}

/* ---------- effects ---------- */

static void SpawnBurst(float x, float y, float hue, int count, bool miss) {
    for (int k = 0; k < count; k++) {
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (g_particles[i].life > 0.0f) continue;
            Particle* p = &g_particles[i];
            float a = frand() * TAU;
            float sp = 110.0f + frand() * 290.0f;
            p->x = x; p->y = y;
            p->vx = cosf(a) * sp; p->vy = sinf(a) * sp - 70.0f;
            p->maxLife = 0.45f + frand() * 0.45f;
            p->life = p->maxLife;
            p->size = 4.5f + frand() * 6.5f;
            p->isHeart = (!miss && (k % 3 == 0));
            p->c = miss ? Rgba(255, 50, 90, 255)
                        : Hsv(hue + (frand() - 0.5f) * 30.0f, 0.55f + frand() * 0.4f, 1.0f, 255);
            break;
        }
    }
}

static void SpawnRipple(float x, float y, float hue) {
    for (int i = 0; i < MAX_RIPPLES; i++) {
        if (g_ripples[i].active) continue;
        g_ripples[i].x = x; g_ripples[i].y = y;
        g_ripples[i].age = 0.0f; g_ripples[i].hue = hue; g_ripples[i].active = true;
        return;
    }
}

static void UpdateEffects(float dt, int paletteIndex) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle* p = &g_particles[i];
        if (p->life <= 0.0f) continue;
        p->life -= dt;
        p->vy += 620.0f * dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
    }
    for (int i = 0; i < MAX_RIPPLES; i++) {
        if (!g_ripples[i].active) continue;
        g_ripples[i].age += dt;
        if (g_ripples[i].age > 0.48f) g_ripples[i].active = false;
    }
    for (int i = 0; i < NUM_BOKEH; i++) {
        Bokeh* b = &g_bokeh[i];
        b->y -= b->speed * dt;
        if (b->y < -b->r) {
            b->y = (float)g_H + b->r;
            b->x = frand() * SCREEN_WIDTH;
            b->hue = randomPinkHue();
        }
    }
    if (g_missFlash > 0.0f) g_missFlash -= dt;
    if (g_scorePop > 0.0f) g_scorePop -= dt * 4.0f;

    int pi = paletteIndex % NUM_PAL;
    float k = 1.0f - expf(-2.5f * dt);
    for (int c = 0; c < 2; c++)
        for (int j = 0; j < 3; j++)
            g_bg[c][j] = lerpf(g_bg[c][j], PAL[pi][c][j], k);
}

static void DrawEffects(SDL_Renderer* r) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle* p = &g_particles[i];
        if (p->life <= 0.0f) continue;
        float t = p->life / p->maxLife;
        SDL_Color c = p->c;
        c.a = (Uint8)(t * 255.0f);
        float sz = p->size * (0.4f + 0.6f * t);
        if (p->isHeart)
            DrawHeart(r, p->x, p->y, sz * 0.9f, c);
        else
            FillCircle(r, p->x, p->y, sz, c);
    }
    for (int i = 0; i < MAX_RIPPLES; i++) {
        Ripple* rp = &g_ripples[i];
        if (!rp->active) continue;
        float t = rp->age / 0.48f;
        float rad = 42.0f + t * 125.0f;
        float th = 8.0f * (1.0f - t) + 1.0f;
        FillArc(r, rp->x, rp->y, rad - th, rad + th, 0.0f, TAU,
                Rgba(255, 160, 215, (Uint8)((1.0f - t) * 220.0f)));
    }
    if (g_missFlash > 0.0f) {
        float a = g_missFlash / 0.35f;
        if (a > 1.0f) a = 1.0f;
        FillRectC(r, 0, 0, SCREEN_WIDTH, (float)g_H, Rgba(255, 30, 80, (Uint8)(a * 115.0f)));
    }
}

/* ---------- target rendering & multi-target logic ---------- */

static float EaseOutBack(float t) {
    float c1 = 1.70158f, c3 = c1 + 1.0f;
    float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

/* Calculates how many simultaneous targets should be active at the current score.
   - Score 0..99: 1 target
   - Score 100..149: 2 targets
   - Score 150..199: 3 targets
   - Increases by 1 every 50 points after 100, up to MAX_TARGETS (6). */
static int GetDesiredTargetCount(int score) {
    if (score < 100) return 1;
    int count = 2 + (score - 100) / 50;
    if (count > MAX_TARGETS) count = MAX_TARGETS;
    return count;
}

static void DrawTarget(SDL_Renderer* r, const Target* t) {
    if (!t->active) return;

    float k = t->age / 0.16f;
    if (k > 1.0f) k = 1.0f;
    float s = EaseOutBack(k);
    float rad = t->radius * s;
    float x = t->x, y = t->y;

    /* Countdown track + glowing pink/rose progress ring */
    float progress = t->timeLeft / t->maxTime;
    if (progress < 0.0f) progress = 0.0f;
    float r0 = t->radius + 9.0f, r1 = t->radius + 17.0f;

    /* Soft outer halo around countdown track */
    FillArc(r, x, y, r0 - 2.0f, r1 + 3.0f, 0.0f, TAU, Rgba(255, 120, 190, 28));
    /* Background track */
    FillArc(r, x, y, r0, r1, 0.0f, TAU, Rgba(255, 210, 235, 45));

    /* Active countdown arc: shifts from bright pastel pink (325) to urgent neon coral-red (355) */
    float ringHue = 355.0f - 32.0f * progress;
    float ringSat = 0.55f + 0.40f * (1.0f - progress);
    FillArc(r, x, y, r0, r1, -(float)M_PI / 2.0f, -(float)M_PI / 2.0f + progress * TAU,
            Hsv(ringHue, ringSat, 1.0f, 255));

    /* Soft pink outer glow + drop shadow */
    FillCircle(r, x, y, rad * 1.48f, Hsv(t->hue, 0.75f, 1.0f, 48));
    FillCircle(r, x + 3.0f, y + 6.0f, rad, Rgba(25, 0, 20, 85));

    /* Concentric pink bullseye rings */
    FillCircle(r, x, y, rad, Hsv(t->hue, 0.82f, 1.0f, 255));                  /* Outer hot pink */
    FillCircle(r, x, y, rad * 0.78f, Rgba(255, 240, 248, 255));               /* Blush white    */
    FillCircle(r, x, y, rad * 0.58f, Hsv(t->hue + 12.0f, 0.88f, 0.96f, 255)); /* Deep rose      */
    FillCircle(r, x, y, rad * 0.36f, Rgba(255, 245, 250, 255));               /* Blush white    */
    FillCircle(r, x, y, rad * 0.19f, Rgba(255, 45, 135, 255));                /* Bullseye core  */

    /* Glossy top-left highlight */
    FillCircle(r, x - rad * 0.32f, y - rad * 0.32f, rad * 0.14f, Rgba(255, 255, 255, 110));
}

/* Spawns a single target at slot `idx`, avoiding overlap with other active targets. */
static void SpawnSingleTarget(Game* game, int idx) {
    Target* t = &game->targets[idx];
    int desired = GetDesiredTargetCount(game->score);

    /* Slightly compact radius when 4+ targets are on screen so they fit comfortably */
    t->radius = (desired >= 4) ? 39.0f : 44.0f;

    float minY = 165.0f, maxY = (float)g_H - 105.0f;
    float minX = 68.0f,  maxX = SCREEN_WIDTH - 68.0f;

    float bestX = SCREEN_WIDTH * 0.5f;
    float bestY = (minY + maxY) * 0.5f;

    for (int attempt = 0; attempt < 25; attempt++) {
        float cx = minX + frand() * (maxX - minX);
        float cy = minY + frand() * (maxY - minY);
        bool overlaps = false;
        float minDist = (t->radius + 22.0f) * 2.0f;

        for (int j = 0; j < MAX_TARGETS; j++) {
            if (j == idx || !game->targets[j].active) continue;
            float dx = cx - game->targets[j].x;
            float dy = cy - game->targets[j].y;
            if (dx * dx + dy * dy < minDist * minDist) {
                overlaps = true;
                break;
            }
        }
        bestX = cx;
        bestY = cy;
        if (!overlaps) break;
    }

    t->x = bestX;
    t->y = bestY;
    t->hue = randomPinkHue();
    t->age = 0.0f;
    t->active = true;

    /* Time limit tightens with score, with a slight fairness buffer when multiple targets are active */
    float baseTime = fmaxf(0.85f, 2.2f - (game->score * 0.012f));
    float multiBonus = (desired > 1) ? (float)(desired - 1) * 0.22f : 0.0f;
    t->maxTime = baseTime + multiBonus;
    t->timeLeft = t->maxTime;
}

static void EnsureTargetCount(Game* game) {
    int desired = GetDesiredTargetCount(game->score);
    int activeCount = 0;
    for (int i = 0; i < MAX_TARGETS; i++) {
        if (game->targets[i].active) activeCount++;
    }
    for (int i = 0; i < MAX_TARGETS && activeCount < desired; i++) {
        if (!game->targets[i].active) {
            SpawnSingleTarget(game, i);
            /* Stagger initial timers slightly when spawning extra targets so they don't expire on the exact same frame */
            if (activeCount > 0) {
                game->targets[i].timeLeft += 0.18f * (float)activeCount;
                game->targets[i].maxTime = game->targets[i].timeLeft;
            }
            activeCount++;
        }
    }
}

static void SetState(Game* game, GameState s) {
    game->state = s;
    game->stateTime = 0.0f;
}

static void ResetGame(Game* game) {
    game->score = 0;
    game->lives = 3;
    game->newBest = false;
    for (int i = 0; i < MAX_TARGETS; i++) {
        game->targets[i].active = false;
    }
    EnsureTargetCount(game);
}

static void LoseLifeAt(Game* game, int idx) {
    Target* t = &game->targets[idx];
    SpawnBurst(t->x, t->y, 350.0f, 24, true);
    PlayMiss();
    g_missFlash = 0.35f;
    game->lives--;
    if (game->lives <= 0) {
        if (game->score > game->best) {
            game->best = game->score;
            game->newBest = game->score > 0;
        }
        SetState(game, STATE_GAME_OVER);
    } else {
        SpawnSingleTarget(game, idx);
        EnsureTargetCount(game);
    }
}

static void HitTargetAt(Game* game, int idx) {
    Target* t = &game->targets[idx];
    SpawnBurst(t->x, t->y, t->hue, 22, false);
    PlayPop();
    SpawnRipple(t->x, t->y, t->hue);
    g_scorePop = 1.0f;
    game->score++;
    SpawnSingleTarget(game, idx);
    EnsureTargetCount(game);
}

/* ---------- UI screens ---------- */

/* Little bouncy bear mascot, drawn with shapes (original art). */
static void DrawBear(SDL_Renderer* r, float cx, float cy, float s) {
    cy += sinf(g_time * 3.0f) * 4.0f * s;
    SDL_Color fur = Rgba(214, 160, 116, 255), inner = Rgba(255, 170, 200, 255);
    SDL_Color muz = Rgba(246, 220, 190, 255), ink = Rgba(40, 10, 30, 255);
    FillCircle(r, cx - 62.0f * s, cy - 58.0f * s, 26.0f * s, fur);
    FillCircle(r, cx + 62.0f * s, cy - 58.0f * s, 26.0f * s, fur);
    FillCircle(r, cx - 62.0f * s, cy - 58.0f * s, 13.0f * s, inner);
    FillCircle(r, cx + 62.0f * s, cy - 58.0f * s, 13.0f * s, inner);
    FillCircle(r, cx, cy, 78.0f * s, fur);
    FillCircle(r, cx, cy + 22.0f * s, 36.0f * s, muz);
    FillCircle(r, cx - 30.0f * s, cy - 6.0f * s, 9.0f * s, ink);
    FillCircle(r, cx + 30.0f * s, cy - 6.0f * s, 9.0f * s, ink);
    FillCircle(r, cx - 27.0f * s, cy - 9.0f * s, 3.0f * s, Rgba(255, 255, 255, 255));
    FillCircle(r, cx + 33.0f * s, cy - 9.0f * s, 3.0f * s, Rgba(255, 255, 255, 255));
    FillCircle(r, cx, cy + 14.0f * s, 9.0f * s, ink);                     /* nose */
    FillArc(r, cx, cy + 6.0f * s, 20.0f * s, 24.0f * s, 0.4f, 2.7f, ink); /* smile */
    FillCircle(r, cx - 48.0f * s, cy + 16.0f * s, 11.0f * s, Rgba(255, 120, 170, 150));
    FillCircle(r, cx + 48.0f * s, cy + 16.0f * s, 11.0f * s, Rgba(255, 120, 170, 150));
}

static void DrawMenu(SDL_Renderer* r, const Game* game) {
    float cx = SCREEN_WIDTH / 2.0f;
    float cy = (float)g_H * 0.54f;

    /* Decorative top card backdrop for title */
    FillRoundRect(r, 44.0f, (float)g_H * 0.08f - 16.0f, SCREEN_WIDTH - 88.0f, 215.0f, 32.0f, Rgba(255, 105, 180, 28));
    FillRoundRect(r, 48.0f, (float)g_H * 0.08f - 12.0f, SCREEN_WIDTH - 96.0f, 207.0f, 28.0f, Rgba(40, 8, 36, 150));

    /* Game Title: "ACERTA" on top, "O 6 7" below */
    DrawTextPinkWave(r, cx, (float)g_H * 0.09f, 10.0f, "ACERTA", 6.0f);
    DrawTextPinkWave(r, cx, (float)g_H * 0.09f + 95.0f, 12.0f, "O 6 7", 7.0f);

    /* Mascot in the bottom-right corner, out of the way of the Play button */
    DrawBear(r, SCREEN_WIDTH - 52.0f, (float)g_H - 150.0f, 0.55f);

    /* Expanding pink rings from the play button */
    for (int k = 0; k < 3; k++) {
        float t = fmodf(g_time * 0.55f + (float)k / 3.0f, 1.0f);
        float rad = 95.0f + t * 95.0f;
        FillArc(r, cx, cy, rad - 3.0f, rad + 3.0f, 0.0f, TAU,
                Rgba(255, 110, 195, (Uint8)((1.0f - t) * 145.0f)));
    }

    /* Pulsing glossy pink Play button */
    float p = 1.0f + 0.06f * sinf(g_time * 4.0f);
    FillCircle(r, cx, cy, 126.0f * p, Rgba(255, 80, 175, 45));
    FillCircle(r, cx + 3.0f, cy + 7.0f, 92.0f * p, Rgba(30, 0, 25, 90));
    FillCircle(r, cx, cy, 92.0f * p, Rgba(255, 55, 150, 255));
    FillCircle(r, cx, cy, 78.0f * p, Rgba(255, 130, 200, 255));
    FillCircle(r, cx - 22.0f * p, cy - 24.0f * p, 18.0f * p, Rgba(255, 220, 240, 95));
    FillTri(r, cx - 20.0f * p, cy - 38.0f * p, cx - 20.0f * p, cy + 38.0f * p, cx + 42.0f * p, cy, Rgba(255, 250, 253, 255));

    float blink = 0.55f + 0.45f * sinf(g_time * 5.0f);
    DrawText(r, cx, cy + 158.0f, 4.0f, "TAP TO PLAY", Rgba(255, 225, 242, (Uint8)(blink * 255.0f)), true);

    if (game->best > 0) {
        char buf[32];
        snprintf(buf, sizeof buf, "BEST %d", game->best);
        FillRoundRect(r, cx - 110.0f, (float)g_H - 122.0f, 220.0f, 52.0f, 26.0f, Rgba(255, 105, 180, 45));
        DrawText(r, cx, (float)g_H - 112.0f, 4.5f, buf, Rgba(255, 210, 235, 255), true);
    }
}

static void DrawHud(SDL_Renderer* r, const Game* game) {
    /* Frosted pink top bar */
    FillRoundRect(r, 16.0f, 18.0f, SCREEN_WIDTH - 32.0f, 76.0f, 26.0f, Rgba(255, 120, 195, 38));
    FillRoundRect(r, 20.0f, 22.0f, SCREEN_WIDTH - 40.0f, 68.0f, 22.0f, Rgba(35, 8, 32, 165));

    /* Lives as glowing pink hearts on the left */
    for (int i = 0; i < 3; i++) {
        float hx = 52.0f + (float)i * 38.0f, hy = 56.0f;
        if (i < game->lives) {
            DrawHeart(r, hx, hy, 15.5f, Rgba(255, 150, 210, 90));
            DrawHeart(r, hx, hy, 13.0f, Rgba(255, 65, 145, 255));
        } else {
            DrawHeart(r, hx, hy, 11.5f, Rgba(255, 190, 220, 40));
        }
    }

    /* Center Score with pop animation */
    char buf[16];
    snprintf(buf, sizeof buf, "%d", game->score);
    float scale = 6.5f + (g_scorePop > 0.0f ? g_scorePop * 2.5f : 0.0f);
    DrawText(r, SCREEN_WIDTH * 0.56f, 34.0f, floorf(scale), buf, Rgba(255, 240, 250, 255), true);

    /* Multi-target wave badge once score reaches 100 */
    int targetCount = GetDesiredTargetCount(game->score);
    if (targetCount > 1) {
        char mbuf[16];
        snprintf(mbuf, sizeof mbuf, "X%d", targetCount);
        FillRoundRect(r, SCREEN_WIDTH * 0.72f, 38.0f, 54.0f, 36.0f, 14.0f, Rgba(255, 60, 155, 200));
        DrawText(r, SCREEN_WIDTH * 0.72f + 27.0f, 46.0f, 3.0f, mbuf, Rgba(255, 255, 255, 255), true);
    }

    /* Top-right Pause Button (420, 34, 44x44) */
    float px = SCREEN_WIDTH - 68.0f, py = 34.0f;
    FillRoundRect(r, px, py, 44.0f, 44.0f, 14.0f, Rgba(255, 110, 185, 85));
    FillRectC(r, px + 14.0f, py + 12.0f, 5.0f, 20.0f, Rgba(255, 240, 250, 255));
    FillRectC(r, px + 25.0f, py + 12.0f, 5.0f, 20.0f, Rgba(255, 240, 250, 255));
}

static void DrawPauseMenu(SDL_Renderer* r) {
    FillRectC(r, 0, 0, SCREEN_WIDTH, (float)g_H, Rgba(20, 4, 22, 165));

    float pw = 360.0f, ph = 320.0f;
    float px = (SCREEN_WIDTH - pw) / 2.0f, py = ((float)g_H - ph) / 2.0f;
    float cx = SCREEN_WIDTH / 2.0f;

    FillRoundRect(r, px - 5.0f, py - 5.0f, pw + 10.0f, ph + 10.0f, 32.0f, Rgba(255, 95, 180, 255));
    FillRoundRect(r, px, py, pw, ph, 28.0f, Rgba(42, 10, 38, 250));

    DrawTextPinkWave(r, cx, py + 38.0f, 6.5f, "PAUSED", 3.0f);

    /* Resume Button */
    FillRoundRect(r, cx - 125.0f, py + 125.0f, 250.0f, 64.0f, 24.0f, Rgba(255, 65, 155, 255));
    DrawText(r, cx, py + 145.0f, 4.0f, "RESUME", Rgba(255, 255, 255, 255), true);

    /* Main Menu Button */
    FillRoundRect(r, cx - 125.0f, py + 210.0f, 250.0f, 64.0f, 24.0f, Rgba(140, 35, 95, 255));
    DrawText(r, cx, py + 230.0f, 4.0f, "MAIN MENU", Rgba(255, 220, 240, 255), true);
}

static void DrawGameOver(SDL_Renderer* r, const Game* game) {
    FillRectC(r, 0, 0, SCREEN_WIDTH, (float)g_H, Rgba(22, 4, 24, 165));

    float pw = 380.0f, ph = 410.0f;
    float px = (SCREEN_WIDTH - pw) / 2.0f, py = ((float)g_H - ph) / 2.0f;
    float cx = SCREEN_WIDTH / 2.0f;

    FillRoundRect(r, px - 5.0f, py - 5.0f, pw + 10.0f, ph + 10.0f, 32.0f,
                  Hsv(325.0f + 20.0f * sinf(g_time * 3.0f), 0.7f, 1.0f, 255));
    FillRoundRect(r, px, py, pw, ph, 28.0f, Rgba(40, 10, 38, 255));

    DrawText(r, cx, py + 36.0f, 6.0f, "GAME OVER", Rgba(255, 105, 180, 255), true);
    DrawText(r, cx, py + 114.0f, 4.0f, "SCORE", Rgba(255, 185, 225, 255), true);

    char buf[32];
    snprintf(buf, sizeof buf, "%d", game->score);
    DrawText(r, cx, py + 158.0f, 10.0f, buf, Rgba(255, 245, 252, 255), true);

    if (game->newBest) {
        DrawTextPinkWave(r, cx, py + 258.0f, 5.0f, "NEW BEST!", 3.5f);
    } else {
        snprintf(buf, sizeof buf, "BEST %d", game->best);
        DrawText(r, cx, py + 258.0f, 5.0f, buf, Rgba(255, 195, 230, 255), true);
    }

    if (game->stateTime > 0.6f) {
        float blink = 0.55f + 0.45f * sinf(g_time * 5.0f);
        DrawText(r, cx, py + 338.0f, 3.0f, "TAP TO CONTINUE", Rgba(255, 230, 245, (Uint8)(blink * 255.0f)), true);
    }
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    srand((unsigned int)time(NULL));

    /* Use only touch->mouse events so a tap isn't counted twice. */
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        SDL_Log("SDL Initialization failed: %s", SDL_GetError());
        return 1;
    }

#ifdef __ANDROID__
    Uint32 winFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_ALLOW_HIGHDPI;
#else
    Uint32 winFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
#endif
    SDL_Window* window = SDL_CreateWindow(
        "Acerta o 6 7",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH, DEFAULT_HEIGHT, winFlags
    );
    if (!window) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        SDL_Log("Renderer creation failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    OpenAudio();

    /* Match the logical height to the screen shape so there are no bars. */
    int ow = 0, oh = 0;
    SDL_GetRendererOutputSize(renderer, &ow, &oh);
    if (ow > 0 && oh > 0) {
        g_H = (int)((float)SCREEN_WIDTH * (float)oh / (float)ow);
        if (g_H < 640) g_H = 640;
        if (g_H > 1100) g_H = 1100;
    }
    SDL_RenderSetLogicalSize(renderer, SCREEN_WIDTH, g_H);

    for (int i = 0; i < NUM_BOKEH; i++) {
        g_bokeh[i].x = frand() * SCREEN_WIDTH;
        g_bokeh[i].y = frand() * (float)g_H;
        g_bokeh[i].r = 26.0f + frand() * 58.0f;
        g_bokeh[i].speed = 8.0f + frand() * 20.0f;
        g_bokeh[i].phase = frand() * TAU;
        g_bokeh[i].hue = randomPinkHue();
        g_bokeh[i].isHeart = (i % 3 == 0);
    }

    Game game = { .state = STATE_MAIN_MENU, .score = 0, .best = 0, .lives = 3 };
    ResetGame(&game);
    SetState(&game, STATE_MAIN_MENU);

    bool running = true;
    SDL_Event event;
    Uint32 lastTime = SDL_GetTicks();

    while (running) {
        Uint32 currentTime = SDL_GetTicks();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;
        if (deltaTime > 0.05f) deltaTime = 0.05f; /* avoid jumps after pause */
        g_time += deltaTime;

        /* --- 1. EVENTS --- */
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
            case SDL_APP_TERMINATING:
                running = false;
                break;

            case SDL_APP_DIDENTERFOREGROUND:
                lastTime = SDL_GetTicks();
                break;

            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_AC_BACK || event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_p) {
                    if (game.state == STATE_MAIN_MENU) running = false;
                    else if (game.state == STATE_GAMEPLAY) SetState(&game, STATE_PAUSE_MENU);
                    else if (game.state == STATE_PAUSE_MENU) SetState(&game, STATE_GAMEPLAY);
                    else SetState(&game, STATE_MAIN_MENU);
                }
                break;

            case SDL_MOUSEBUTTONDOWN: {
                float tx = (float)event.button.x;
                float ty = (float)event.button.y;

                if (game.state == STATE_MAIN_MENU) {
                    ResetGame(&game);
                    SetState(&game, STATE_GAMEPLAY);
                } else if (game.state == STATE_GAMEPLAY) {
                    /* Check top-right Pause Button tap */
                    if (tx >= SCREEN_WIDTH - 74.0f && tx <= SCREEN_WIDTH - 16.0f && ty >= 24.0f && ty <= 86.0f) {
                        SetState(&game, STATE_PAUSE_MENU);
                        break;
                    }

                    /* Check if tap hit any active target */
                    int hitIndex = -1;
                    for (int i = 0; i < MAX_TARGETS; i++) {
                        if (!game.targets[i].active) continue;
                        float dx = tx - game.targets[i].x;
                        float dy = ty - game.targets[i].y;
                        if (dx * dx + dy * dy <= game.targets[i].radius * game.targets[i].radius) {
                            hitIndex = i;
                            break;
                        }
                    }

                    if (hitIndex >= 0) {
                        HitTargetAt(&game, hitIndex);
                    } else if (MISS_TAP_COSTS_LIFE) {
                        LoseLifeAt(&game, 0);
                    }
                } else if (game.state == STATE_PAUSE_MENU) {
                    float py = ((float)g_H - 320.0f) / 2.0f;
                    float cx = SCREEN_WIDTH / 2.0f;
                    /* Resume button hit box */
                    if (tx >= cx - 125.0f && tx <= cx + 125.0f && ty >= py + 125.0f && ty <= py + 189.0f) {
                        SetState(&game, STATE_GAMEPLAY);
                    }
                    /* Main Menu button hit box */
                    else if (tx >= cx - 125.0f && tx <= cx + 125.0f && ty >= py + 210.0f && ty <= py + 274.0f) {
                        SetState(&game, STATE_MAIN_MENU);
                    }
                } else if (game.state == STATE_GAME_OVER) {
                    /* short lockout so a frantic last tap doesn't skip the screen */
                    if (game.stateTime > 0.6f) SetState(&game, STATE_MAIN_MENU);
                }
                break;
            }
            default:
                break;
            }
        }

        /* --- 2. UPDATE --- */
        game.stateTime += deltaTime;
        if (game.state == STATE_GAMEPLAY) {
            EnsureTargetCount(&game);
            for (int i = 0; i < MAX_TARGETS; i++) {
                if (!game.targets[i].active) continue;
                game.targets[i].age += deltaTime;
                game.targets[i].timeLeft -= deltaTime;
                if (game.targets[i].timeLeft <= 0.0f) {
                    LoseLifeAt(&game, i);
                    if (game.state != STATE_GAMEPLAY) break;
                }
            }
        }
        int palette = (game.state == STATE_GAMEPLAY || game.state == STATE_PAUSE_MENU || game.state == STATE_GAME_OVER)
                    ? game.score / 8
                    : (int)(g_time / 5.0f);
        UpdateEffects(deltaTime, palette);

        /* --- 3. RENDER --- */
        SDL_SetRenderDrawColor(renderer, 35, 8, 32, 255);
        SDL_RenderClear(renderer);
        DrawBackground(renderer);

        if (game.state == STATE_MAIN_MENU) {
            DrawMenu(renderer, &game);
        } else {
            DrawHud(renderer, &game);
            if (game.state == STATE_GAMEPLAY || game.state == STATE_PAUSE_MENU) {
                for (int i = 0; i < MAX_TARGETS; i++) {
                    DrawTarget(renderer, &game.targets[i]);
                }
            }
        }
        DrawEffects(renderer);
        if (game.state == STATE_PAUSE_MENU) DrawPauseMenu(renderer);
        if (game.state == STATE_GAME_OVER) DrawGameOver(renderer, &game);

        SDL_RenderPresent(renderer);
    }

    if (g_audio) SDL_CloseAudioDevice(g_audio);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
