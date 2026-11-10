#ifdef WAPI_SDL2

#ifdef __MINGW32__
#define FOR_WINDOWS 1
#else
#define FOR_WINDOWS 0
#endif

#if FOR_WINDOWS
#define GLEW_STATIC
#include <GL/glew.h>
#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES 1
#include <SDL2/SDL_opengl.h>
#else
#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES 1

#ifdef OSX_BUILD
#include <SDL2/SDL_opengl.h>
#else
#include <SDL2/SDL_opengles2.h>
#endif

#endif // End of OS-Specific GL defines

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "gfx_window_manager_api.h"
#include "gfx_screen_config.h"
#include "../pc_main.h"
#include "../configfile.h"
#include "../cliopts.h"

#include "src/pc/controller/controller_keyboard.h"

#include "src/saturn/imgui/saturn_imgui.h"
#ifdef __ANDROID__
#include "src/pc/controller/controller_touch.h"
#endif
#include "src/saturn/saturn.h"
#include "src/pc/configfile.h"

// TODO: figure out if this shit even works
#ifdef VERSION_EU
# define FRAMERATE 25
#else
# define FRAMERATE 30
#endif

static SDL_Window *wnd;
static SDL_GLContext ctx = NULL;
static int inverted_scancode_table[512];
SDL_Scancode bind_to_sdl_scancode[512] = { 0 };

static kb_callback_t kb_key_down = NULL;
static kb_callback_t kb_key_up = NULL;
static void (*kb_all_keys_up)(void) = NULL;

// whether to use timer for frame control
static bool use_timer = true;
// time between consequtive game frames, in perf counter ticks
static double frame_time = 0.0; // set in init()
// GetPerformanceFrequency
static double perf_freq = 0.0;

const SDL_Scancode windows_scancode_table[] = {
  /*  0                        1                            2                         3                            4                     5                            6                            7  */
  /*  8                        9                            A                         B                            C                     D                            E                            F  */
  SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_ESCAPE,         SDL_SCANCODE_1,           SDL_SCANCODE_2,              SDL_SCANCODE_3,       SDL_SCANCODE_4,              SDL_SCANCODE_5,              SDL_SCANCODE_6,          /* 0 */
  SDL_SCANCODE_7,              SDL_SCANCODE_8,              SDL_SCANCODE_9,           SDL_SCANCODE_0,              SDL_SCANCODE_MINUS,   SDL_SCANCODE_EQUALS,         SDL_SCANCODE_BACKSPACE,      SDL_SCANCODE_TAB,        /* 0 */

  SDL_SCANCODE_Q,              SDL_SCANCODE_W,              SDL_SCANCODE_E,           SDL_SCANCODE_R,              SDL_SCANCODE_T,       SDL_SCANCODE_Y,              SDL_SCANCODE_U,              SDL_SCANCODE_I,          /* 1 */
  SDL_SCANCODE_O,              SDL_SCANCODE_P,              SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_RIGHTBRACKET,   SDL_SCANCODE_RETURN,  SDL_SCANCODE_LCTRL,          SDL_SCANCODE_A,              SDL_SCANCODE_S,          /* 1 */

  SDL_SCANCODE_D,              SDL_SCANCODE_F,              SDL_SCANCODE_G,           SDL_SCANCODE_H,              SDL_SCANCODE_J,       SDL_SCANCODE_K,              SDL_SCANCODE_L,              SDL_SCANCODE_SEMICOLON,  /* 2 */
  SDL_SCANCODE_APOSTROPHE,     SDL_SCANCODE_GRAVE,          SDL_SCANCODE_LSHIFT,      SDL_SCANCODE_BACKSLASH,      SDL_SCANCODE_Z,       SDL_SCANCODE_X,              SDL_SCANCODE_C,              SDL_SCANCODE_V,          /* 2 */

  SDL_SCANCODE_B,              SDL_SCANCODE_N,              SDL_SCANCODE_M,           SDL_SCANCODE_COMMA,          SDL_SCANCODE_PERIOD,  SDL_SCANCODE_SLASH,          SDL_SCANCODE_RSHIFT,         SDL_SCANCODE_PRINTSCREEN,/* 3 */
  SDL_SCANCODE_LALT,           SDL_SCANCODE_SPACE,          SDL_SCANCODE_CAPSLOCK,    SDL_SCANCODE_F1,             SDL_SCANCODE_F2,      SDL_SCANCODE_F3,             SDL_SCANCODE_F4,             SDL_SCANCODE_F5,         /* 3 */

  SDL_SCANCODE_F6,             SDL_SCANCODE_F7,             SDL_SCANCODE_F8,          SDL_SCANCODE_F9,             SDL_SCANCODE_F10,     SDL_SCANCODE_NUMLOCKCLEAR,   SDL_SCANCODE_SCROLLLOCK,     SDL_SCANCODE_HOME,       /* 4 */
  SDL_SCANCODE_UP,             SDL_SCANCODE_PAGEUP,         SDL_SCANCODE_KP_MINUS,    SDL_SCANCODE_LEFT,           SDL_SCANCODE_KP_5,    SDL_SCANCODE_RIGHT,          SDL_SCANCODE_KP_PLUS,        SDL_SCANCODE_END,        /* 4 */

  SDL_SCANCODE_DOWN,           SDL_SCANCODE_PAGEDOWN,       SDL_SCANCODE_INSERT,      SDL_SCANCODE_DELETE,         SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_NONUSBACKSLASH, SDL_SCANCODE_F11,        /* 5 */
  SDL_SCANCODE_F12,            SDL_SCANCODE_PAUSE,          SDL_SCANCODE_UNKNOWN,     SDL_SCANCODE_LGUI,           SDL_SCANCODE_RGUI,    SDL_SCANCODE_APPLICATION,    SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_UNKNOWN,    /* 5 */

  SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_UNKNOWN,     SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_F13,     SDL_SCANCODE_F14,            SDL_SCANCODE_F15,            SDL_SCANCODE_F16,        /* 6 */
  SDL_SCANCODE_F17,            SDL_SCANCODE_F18,            SDL_SCANCODE_F19,         SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_UNKNOWN,    /* 6 */

  SDL_SCANCODE_INTERNATIONAL2, SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_UNKNOWN,     SDL_SCANCODE_INTERNATIONAL1, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_UNKNOWN,    /* 7 */
  SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_INTERNATIONAL4, SDL_SCANCODE_UNKNOWN,     SDL_SCANCODE_INTERNATIONAL5, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_INTERNATIONAL3, SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_UNKNOWN     /* 7 */
};

const SDL_Scancode scancode_rmapping_extended[][2] = {
    {SDL_SCANCODE_KP_ENTER, SDL_SCANCODE_RETURN},
    {SDL_SCANCODE_RALT, SDL_SCANCODE_LALT},
    {SDL_SCANCODE_RCTRL, SDL_SCANCODE_LCTRL},
    {SDL_SCANCODE_KP_DIVIDE, SDL_SCANCODE_SLASH},
    //{SDL_SCANCODE_KP_PLUS, SDL_SCANCODE_CAPSLOCK}
};

const SDL_Scancode scancode_rmapping_nonextended[][2] = {
    {SDL_SCANCODE_KP_7, SDL_SCANCODE_HOME},
    {SDL_SCANCODE_KP_8, SDL_SCANCODE_UP},
    {SDL_SCANCODE_KP_9, SDL_SCANCODE_PAGEUP},
    {SDL_SCANCODE_KP_4, SDL_SCANCODE_LEFT},
    {SDL_SCANCODE_KP_6, SDL_SCANCODE_RIGHT},
    {SDL_SCANCODE_KP_1, SDL_SCANCODE_END},
    {SDL_SCANCODE_KP_2, SDL_SCANCODE_DOWN},
    {SDL_SCANCODE_KP_3, SDL_SCANCODE_PAGEDOWN},
    {SDL_SCANCODE_KP_0, SDL_SCANCODE_INSERT},
    {SDL_SCANCODE_KP_PERIOD, SDL_SCANCODE_DELETE},
    {SDL_SCANCODE_KP_MULTIPLY, SDL_SCANCODE_PRINTSCREEN}
};

#define IS_FULLSCREEN() ((SDL_GetWindowFlags(wnd) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)

static inline void sys_sleep(const uint64_t us) {
    // TODO: not everything has usleep()
    usleep(us);
}

static int test_vsync(void) {
    // Even if SDL_GL_SetSwapInterval succeeds, it doesn't mean that VSync actually works.
    // A 60 Hz monitor should have a swap interval of 16.67 milliseconds.
    // Try to detect the length of a vsync by swapping buffers some times.
    // Since the graphics card may enqueue a fixed number of frames,
    // first send in four dummy frames to hopefully fill the queue.
    // This method will fail if the refresh rate is changed, which, in
    // combination with that we can't control the queue size (i.e. lag)
    // is a reason this generic SDL2 backend should only be used as last resort.

    for (int i = 0; i < 8; ++i)
        SDL_GL_SwapWindow(wnd);

    Uint32 start = SDL_GetTicks();
    SDL_GL_SwapWindow(wnd);
    SDL_GL_SwapWindow(wnd);
    SDL_GL_SwapWindow(wnd);
    SDL_GL_SwapWindow(wnd);
    Uint32 end = SDL_GetTicks();

    const float average = 4.0 * 1000.0 / (end - start);

    if (average > 27.0f && average < 33.0f) return 1;
    if (average > 57.0f && average < 63.0f) return 2;
    if (average > 86.0f && average < 94.0f) return 3;
    if (average > 115.0f && average < 125.0f) return 4;
    if (average > 234.0f && average < 246.0f) return 8;

    return 0;
}

static inline void gfx_sdl_set_vsync(const bool enabled) {
    if (!limit_fps) frame_time = perf_freq / (6 * FRAMERATE);
    else frame_time = perf_freq / (2 * FRAMERATE);

    if (enabled && limit_fps) {
        // try to detect refresh rate
        SDL_GL_SetSwapInterval(1);
        int vblanks = test_vsync();
        if (vblanks & 1)
            vblanks = 0; // not divisible by 60, fuck that
        else
            vblanks /= 2;
        if (vblanks) {
            printf("determined swap interval: %d\n", vblanks);
            SDL_GL_SetSwapInterval(vblanks);
            use_timer = false;
            return;
        } else {
            printf("could not determine swap interval, falling back to timer sync\n");
        }
    }

    use_timer = true;
    SDL_GL_SetSwapInterval(0);
}

static void gfx_sdl_set_fullscreen(void) {
    if (configWindow.reset)
        configWindow.fullscreen = false;
    if (configWindow.fullscreen == IS_FULLSCREEN())
        return;
    if (configWindow.fullscreen) {
        SDL_SetWindowFullscreen(wnd, SDL_WINDOW_FULLSCREEN_DESKTOP);
        SDL_ShowCursor(SDL_DISABLE);
    } else {
        SDL_SetWindowFullscreen(wnd, 0);
        SDL_ShowCursor(SDL_ENABLE);
        configWindow.exiting_fullscreen = true;
    }
}

static void gfx_sdl_reset_dimension_and_pos(void) {
    if (configWindow.exiting_fullscreen)
        configWindow.exiting_fullscreen = false;

    if (configWindow.reset) {
        configWindow.x = WAPI_WIN_CENTERPOS;
        configWindow.y = WAPI_WIN_CENTERPOS;
        configWindow.w = DESIRED_SCREEN_WIDTH;
        configWindow.h = DESIRED_SCREEN_HEIGHT;
        configWindow.reset = false;
    } else if (!configWindow.settings_changed) {
        return;
    }

    int xpos = (configWindow.x == WAPI_WIN_CENTERPOS) ? SDL_WINDOWPOS_CENTERED : configWindow.x;
    int ypos = (configWindow.y == WAPI_WIN_CENTERPOS) ? SDL_WINDOWPOS_CENTERED : configWindow.y;

    SDL_SetWindowSize(wnd, configWindow.w, configWindow.h);
    SDL_SetWindowPosition(wnd, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    // in case vsync changed
    gfx_sdl_set_vsync(configWindow.vsync);
}

static void gfx_sdl_init(const char *window_title) {
    SDL_Init(SDL_INIT_VIDEO);

    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    #ifdef USE_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);  // These attributes allow for hardware acceleration on RPis.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    #endif
#ifdef __ANDROID__
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#else
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
#endif

    //SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    //SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    int xpos = SDL_WINDOWPOS_CENTERED;
    int ypos = SDL_WINDOWPOS_CENTERED;

    wnd = SDL_CreateWindow(
        window_title,
        xpos, ypos, configWindow.w, configWindow.h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    ctx = SDL_GL_CreateContext(wnd);

    saturn_imgui_init_backend(wnd, ctx);

    gfx_sdl_set_vsync(configWindow.vsync);

    gfx_sdl_set_fullscreen();

    perf_freq = SDL_GetPerformanceFrequency();
    frame_time = perf_freq / (2 * FRAMERATE);

    for (size_t i = 0; i < sizeof(windows_scancode_table) / sizeof(SDL_Scancode); i++) {
        inverted_scancode_table[windows_scancode_table[i]] = i;
    }

    for (size_t i = 0; i < sizeof(scancode_rmapping_extended) / sizeof(scancode_rmapping_extended[0]); i++) {
        inverted_scancode_table[scancode_rmapping_extended[i][0]] = inverted_scancode_table[scancode_rmapping_extended[i][1]] + 0x100;
    }

    for (size_t i = 0; i < sizeof(scancode_rmapping_nonextended) / sizeof(scancode_rmapping_nonextended[0]); i++) {
        inverted_scancode_table[scancode_rmapping_nonextended[i][0]] = inverted_scancode_table[scancode_rmapping_nonextended[i][1]];
        inverted_scancode_table[scancode_rmapping_nonextended[i][1]] += 0x100;
    }

    for (size_t i = 0; i < 512; i++) {
        if (inverted_scancode_table[i] >= 512) { continue; }
        bind_to_sdl_scancode[inverted_scancode_table[i]] = i;
    }
}

static void gfx_sdl_main_loop(void (*run_one_game_iter)(void)) {
    run_one_game_iter();
}

static void gfx_sdl_get_dimensions(uint32_t *width, uint32_t *height) {
    int w, h;
    SDL_GetWindowSize(wnd, &w, &h);
    if (width) *width = w;
    if (height) *height = h;
}

static int translate_scancode(int scancode) {
    if (scancode < 512) {
        return inverted_scancode_table[scancode];
    } else {
        return 0;
    }
}

static void gfx_sdl_onkeydown(int scancode) {
    if (kb_key_down)
        kb_key_down(translate_scancode(scancode));

    const Uint8 *state = SDL_GetKeyboardState(NULL);

    if (state[SDL_SCANCODE_LALT] && state[SDL_SCANCODE_RETURN]) {
        configWindow.fullscreen = !configWindow.fullscreen;
        configWindow.settings_changed = true;
    }
}

static void gfx_sdl_onkeyup(int scancode) {
    if (kb_key_up)
        kb_key_up(translate_scancode(scancode));
}

static void gfx_sdl_handle_events(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {

        saturn_imgui_handle_events(&event);
#ifdef __ANDROID__
        touch_handle_event(&event);
#endif

        switch (event.type) {
#ifndef TARGET_WEB
            // Scancodes are broken in Emscripten SDL2: https://bugzilla.libsdl.org/show_bug.cgi?id=3259
            case SDL_KEYDOWN:
                gfx_sdl_onkeydown(event.key.keysym.scancode);
                break;
            case SDL_KEYUP:
                gfx_sdl_onkeyup(event.key.keysym.scancode);
                break;
#endif
            case SDL_WINDOWEVENT: // TODO: Check if this makes sense to be included in the Web build
                if (!IS_FULLSCREEN()) {
                    switch (event.window.event) {
                        case SDL_WINDOWEVENT_MOVED:
                            if (!configWindow.exiting_fullscreen) {
                                if (event.window.data1 >= 0) configWindow.x = event.window.data1;
                                if (event.window.data2 >= 0) configWindow.y = event.window.data2;
                            }
                            break;
                        case SDL_WINDOWEVENT_SIZE_CHANGED:
                            configWindow.w = event.window.data1;
                            configWindow.h = event.window.data2;
                            break;
                    }
                }
                break;
            case SDL_QUIT:
                game_exit();
                break;
#ifdef __ANDROID__
            // Android never sends SDL_QUIT when the app is swiped away, so save settings when going to the background.
            case SDL_APP_WILLENTERBACKGROUND:
            case SDL_APP_DIDENTERBACKGROUND:
            case SDL_APP_TERMINATING:
                configfile_save(configfile_name());
                break;
#endif
        }
    }

#ifdef __ANDROID__
    // Also save the settings every 10 seconds, in case the app gets killed without any warning.
    {
        static Uint32 last_config_save = 0;
        Uint32 now_ticks = SDL_GetTicks();
        if (last_config_save == 0) last_config_save = now_ticks;
        if (now_ticks - last_config_save >= 10000) {
            configfile_save(configfile_name());
            last_config_save = now_ticks;
        }
    }
#endif

    if (configWindow.settings_changed) {
        gfx_sdl_set_fullscreen();
        gfx_sdl_reset_dimension_and_pos();
        configWindow.settings_changed = false;
    }

    if (configWindow.fps_changed) {
        gfx_sdl_set_vsync(configWindow.vsync);
        configWindow.fps_changed = false;
    }
}

static void gfx_sdl_set_keyboard_callbacks(kb_callback_t on_key_down, kb_callback_t on_key_up, void (*on_all_keys_up)(void)) {
    kb_key_down = on_key_down;
    kb_key_up = on_key_up;
    kb_all_keys_up = on_all_keys_up;
}

static bool gfx_sdl_start_frame(void) {
    return true;
}

static inline void sync_framerate_with_timer(void) {
    static double last_time;
    static double last_sec;
    static int frames_since_last_sec;
    const double now = SDL_GetPerformanceCounter();
    frames_since_last_sec += 1;
    if (last_time) {
        const double elapsed = last_sec ? (now - last_sec) : (now - last_time);
        if ((elapsed < frame_time && !last_sec) || (elapsed < frames_since_last_sec * frame_time && last_sec)) {
            const double delay = last_sec ? frames_since_last_sec * frame_time - elapsed : frame_time - elapsed;
            sys_sleep(delay / perf_freq * 1000000.0);
            last_time = now + delay;
        } else {
            last_time = now;
        }
        if ((int64_t)(now / perf_freq) > (int64_t)(last_sec / perf_freq)) {
            last_sec = last_time;
            frames_since_last_sec = 0;
        }
    } else {
        last_time = now;
    }
}

#ifdef __ANDROID__
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

// Screenshots. The touch UI sets android_screenshot_request on a frame that has no panels on screen; the frame is
// then saved as screenshots/screenshot_<date>_<time>.png (inside Saturn's data folder) right before it is shown.
int android_screenshot_request = 0;
int android_screenshot_result = 0;      // 1 = saved, -1 = failed (read by the touch UI to show a message)
char android_screenshot_name[96] = { 0 };

static uint32_t shot_crc_table[256];
static int shot_crc_ready = 0;

static void shot_crc_init(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        shot_crc_table[n] = c;
    }
    shot_crc_ready = 1;
}

static uint32_t shot_crc_update(uint32_t crc, const unsigned char *buf, size_t len) {
    for (size_t i = 0; i < len; i++) crc = shot_crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

static void shot_put32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v;
}

static void shot_chunk_begin(FILE *f, uint32_t len, const char *type, uint32_t *crc) {
    unsigned char b[4];
    shot_put32(b, len);
    fwrite(b, 1, 4, f);
    fwrite(type, 1, 4, f);
    *crc = shot_crc_update(0xFFFFFFFFu, (const unsigned char *)type, 4);
}

static void shot_chunk_data(FILE *f, const void *data, size_t len, uint32_t *crc) {
    fwrite(data, 1, len, f);
    *crc = shot_crc_update(*crc, (const unsigned char *)data, len);
}

static void shot_chunk_end(FILE *f, uint32_t crc) {
    unsigned char b[4];
    shot_put32(b, ~crc);
    fwrite(b, 1, 4, f);
}

// Writes an RGB PNG from bottom-up RGBA pixels (what glReadPixels returns). The image data is stored without
// compression (no zlib needed), so the files are large but always valid.
static int android_write_png(const char *path, const unsigned char *rgba, int w, int h) {
    if (!shot_crc_ready) shot_crc_init();
    size_t rowlen = 1 + (size_t)w * 3;
    size_t rawlen = rowlen * (size_t)h;
    unsigned char *raw = (unsigned char *)malloc(rawlen);
    if (!raw) return 0;
    for (int y = 0; y < h; y++) {
        const unsigned char *src = rgba + (size_t)(h - 1 - y) * (size_t)w * 4;
        unsigned char *dst = raw + (size_t)y * rowlen;
        *dst++ = 0;   // PNG filter type 0 (none)
        for (int x = 0; x < w; x++) {
            dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
            dst += 3; src += 4;
        }
    }
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < rawlen; i++) {
        a += raw[i]; if (a >= 65521u) a -= 65521u;
        b += a;      if (b >= 65521u) b -= 65521u;
    }

    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); return 0; }

    static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    fwrite(sig, 1, 8, f);

    unsigned char ihdr[13];
    shot_put32(ihdr, (uint32_t)w);
    shot_put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;   // 8 bit, RGB
    uint32_t crc;
    shot_chunk_begin(f, 13, "IHDR", &crc);
    shot_chunk_data(f, ihdr, 13, &crc);
    shot_chunk_end(f, crc);

    size_t nblocks = (rawlen + 65534) / 65535;
    uint32_t idat_len = (uint32_t)(2 + nblocks * 5 + rawlen + 4);
    shot_chunk_begin(f, idat_len, "IDAT", &crc);
    static const unsigned char zhdr[2] = { 0x78, 0x01 };
    shot_chunk_data(f, zhdr, 2, &crc);
    size_t pos = 0;
    while (pos < rawlen) {
        size_t n = rawlen - pos;
        if (n > 65535) n = 65535;
        unsigned char bh[5];
        bh[0] = (pos + n >= rawlen) ? 1 : 0;
        bh[1] = (unsigned char)(n & 0xFF);        bh[2] = (unsigned char)(n >> 8);
        bh[3] = (unsigned char)(~n & 0xFF);       bh[4] = (unsigned char)((~n >> 8) & 0xFF);
        shot_chunk_data(f, bh, 5, &crc);
        shot_chunk_data(f, raw + pos, n, &crc);
        pos += n;
    }
    unsigned char ad[4];
    shot_put32(ad, (b << 16) | a);
    shot_chunk_data(f, ad, 4, &crc);
    shot_chunk_end(f, crc);

    shot_chunk_begin(f, 0, "IEND", &crc);
    shot_chunk_end(f, crc);

    int ok = !ferror(f);
    if (fclose(f) != 0) ok = 0;
    free(raw);
    return ok;
}

static void android_take_screenshot(void) {
    android_screenshot_result = -1;
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(wnd, &w, &h);
    if (w <= 0 || h <= 0) return;
    unsigned char *px = (unsigned char *)malloc((size_t)w * (size_t)h * 4);
    if (!px) return;

    GLint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);

    mkdir("screenshots", 0777);
    char stamp[40];
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    if (lt) strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", lt);
    else snprintf(stamp, sizeof(stamp), "%ld", (long)t);
    snprintf(android_screenshot_name, sizeof(android_screenshot_name), "screenshots/screenshot_%s.png", stamp);

    int ok = android_write_png(android_screenshot_name, px, w, h);
    free(px);
    android_screenshot_result = ok ? 1 : -1;
    fprintf(stdout, "[screenshot] %s %dx%d %s\n", android_screenshot_name, w, h, ok ? "saved" : "FAILED");
    fflush(stdout);
}
#endif

static void gfx_sdl_swap_buffers_begin(void) {
    if (use_timer) sync_framerate_with_timer();
#ifdef __ANDROID__
    if (android_screenshot_request) {
        android_screenshot_request = 0;
        android_take_screenshot();
    }
#endif
    SDL_GL_SwapWindow(wnd);
}

static void gfx_sdl_swap_buffers_end(void) {
}

static double gfx_sdl_get_time(void) {
    return 0.0;
}


static void gfx_sdl_shutdown(void) {
    if (SDL_WasInit(0)) {
        if (ctx) { SDL_GL_DeleteContext(ctx); ctx = NULL; }
        if (wnd) { SDL_DestroyWindow(wnd); wnd = NULL; }
        SDL_Quit();
    }
}

struct GfxWindowManagerAPI gfx_sdl = {
    gfx_sdl_init,
    gfx_sdl_set_keyboard_callbacks,
    gfx_sdl_main_loop,
    gfx_sdl_get_dimensions,
    gfx_sdl_handle_events,
    gfx_sdl_start_frame,
    gfx_sdl_swap_buffers_begin,
    gfx_sdl_swap_buffers_end,
    gfx_sdl_get_time,
    gfx_sdl_shutdown
};

#endif // BACKEND_WM
