/*
 * wasm/sdl12.h — minimal SDL 1.2 compatibility header for the wasm port.
 *
 * Declares exactly the SDL 1.2 API subset this project uses (grep for
 * SDL_ in src/). Values match real SDL 1.2 (keymap.c's scancode tables
 * depend on the SDLK_* values). Implementations live in wasm/shim.c.
 *
 *  - Video: a 2D <canvas> showing the 320x200 ST framebuffer.
 *  - Events: DOM handlers push into a queue drained by SDL_PollEvent.
 *  - Audio: not implemented yet — init fails gracefully (see shim.c).
 */
#ifndef WASM_SDL12_H
#define WASM_SDL12_H

#include <stddef.h>

/* SDL_types.h */
typedef unsigned char  Uint8;
typedef signed char    Sint8;
typedef unsigned short Uint16;
typedef signed short   Sint16;
typedef unsigned int   Uint32;
typedef signed int     Sint32;

/* The game only ever holds a pointer to this, but the shim needs a
 * complete type to hand out a dummy surface. */
struct SDL_Surface {
	int dummy_flags;
};
typedef struct SDL_Surface SDL_Surface;

#define SDLCALL

/* ------------------------------------------------------------------ */
/* SDL_video.h */

#define SDL_INIT_TIMER  0x00000008
#define SDL_INIT_AUDIO  0x00000010
#define SDL_INIT_VIDEO  0x00000020

#define SDL_FULLSCREEN  0x00000001
#define SDL_OPENGL      0x00000002
#define SDL_ANYFORMAT   0x10000000

#define SDL_GRAB_OFF    0
#define SDL_GRAB_ON     1

#define SDL_GL_DOUBLEBUFFER 5

struct SDL_PixelFormat {
	int BitsPerPixel;
};

struct SDL_VideoInfo {
	struct SDL_PixelFormat *vfmt;
};
typedef struct SDL_VideoInfo SDL_VideoInfo;

SDL_Surface *SDL_SetVideoMode (int width, int height, int bits, unsigned int flags);
struct SDL_VideoInfo *SDL_GetVideoInfo (void);
void SDL_WM_SetCaption (const char *title, const char *icon);
void SDL_GL_SwapBuffers (void);
void SDL_GL_SetAttribute (int attr, int value);
int SDL_ShowCursor (int toggle);
void SDL_WarpMouse (int x, int y);
void SDL_WM_GrabInput (unsigned int state);
const char *SDL_GetError (void);
int SDL_Init (Uint32 flags);
int SDL_InitSubSystem (Uint32 flags);
int SDL_WasInit (Uint32 flags);
void SDL_Quit (void);

/* ------------------------------------------------------------------ */
/* SDL_events.h */

#define SDL_ENABLE   1
#define SDL_DISABLE  0

#define SDL_QUIT             256
#define SDL_KEYDOWN          258
#define SDL_KEYUP            259
#define SDL_MOUSEMOTION      260
#define SDL_MOUSEBUTTONDOWN  261
#define SDL_MOUSEBUTTONUP    262

int SDL_EventState (int type, int state);

/* ------------------------------------------------------------------ */
/* SDL_keysym.h */

/* ASCII range */
#define SDLK_BACKSPACE  0x08
#define SDLK_TAB        0x09
#define SDLK_CLEAR      0x0B
#define SDLK_RETURN     0x0D
#define SDLK_PAUSE      0x13
#define SDLK_ESCAPE     0x1B
#define SDLK_SPACE      0x20
#define SDLK_EXCLAIM    0x21
#define SDLK_QUOTEDBL   0x22
#define SDLK_HASH       0x23
#define SDLK_DOLLAR     0x24
#define SDLK_PERCENT    0x25
#define SDLK_AMPERSAND  0x26
#define SDLK_QUOTE      0x27
#define SDLK_LEFTPAREN  0x28
#define SDLK_RIGHTPAREN 0x29
#define SDLK_ASTERISK   0x2A
#define SDLK_PLUS       0x2B
#define SDLK_COMMA      0x2C
#define SDLK_MINUS      0x2D
#define SDLK_PERIOD     0x2E
#define SDLK_SLASH      0x2F
#define SDLK_0          0x30
#define SDLK_1          0x31
#define SDLK_2          0x32
#define SDLK_3          0x33
#define SDLK_4          0x34
#define SDLK_5          0x35
#define SDLK_6          0x36
#define SDLK_7          0x37
#define SDLK_8          0x38
#define SDLK_9          0x39
#define SDLK_COLON      0x3A
#define SDLK_SEMICOLON  0x3B
#define SDLK_LESS       0x3C
#define SDLK_EQUALS     0x3D
#define SDLK_GREATER    0x3E
#define SDLK_QUESTION   0x3F
#define SDLK_AT         0x40
#define SDLK_A          0x41
#define SDLK_B          0x42
#define SDLK_C          0x43
#define SDLK_D          0x44
#define SDLK_E          0x45
#define SDLK_F          0x46
#define SDLK_G          0x47
#define SDLK_H          0x48
#define SDLK_I          0x49
#define SDLK_J          0x4A
#define SDLK_K          0x4B
#define SDLK_L          0x4C
#define SDLK_M          0x4D
#define SDLK_N          0x4E
#define SDLK_O          0x4F
#define SDLK_P          0x50
#define SDLK_Q          0x51
#define SDLK_R          0x52
#define SDLK_S          0x53
#define SDLK_T          0x54
#define SDLK_U          0x55
#define SDLK_V          0x56
#define SDLK_W          0x57
#define SDLK_X          0x58
#define SDLK_Y          0x59
#define SDLK_Z          0x5A
#define SDLK_LEFTBRACKET  0x5B
#define SDLK_BACKSLASH    0x5C
#define SDLK_RIGHTBRACKET 0x5D
#define SDLK_CARET        0x5E
#define SDLK_UNDERSCORE   0x5F
#define SDLK_BACKQUOTE    0x60
#define SDLK_a            0x61
#define SDLK_b            0x62
#define SDLK_c            0x63
#define SDLK_d            0x64
#define SDLK_e            0x65
#define SDLK_f            0x66
#define SDLK_g            0x67
#define SDLK_h            0x68
#define SDLK_i            0x69
#define SDLK_j            0x6A
#define SDLK_k            0x6B
#define SDLK_l            0x6C
#define SDLK_m            0x6D
#define SDLK_n            0x6E
#define SDLK_o            0x6F
#define SDLK_p            0x70
#define SDLK_q            0x71
#define SDLK_r            0x72
#define SDLK_s            0x73
#define SDLK_t            0x74
#define SDLK_u            0x75
#define SDLK_v            0x76
#define SDLK_w            0x77
#define SDLK_x            0x78
#define SDLK_y            0x79
#define SDLK_z            0x7A
#define SDLK_LEFTBRACE    0x7B
#define SDLK_PIPE         0x7C
#define SDLK_RIGHTBRACE   0x7D
#define SDLK_DELETE       0x7F

/* Special keys (SDL 1.2 values, 0x100 range) */
#define SDLK_KP0          0x100
#define SDLK_KP1          0x101
#define SDLK_KP2          0x102
#define SDLK_KP3          0x103
#define SDLK_KP4          0x104
#define SDLK_KP5          0x105
#define SDLK_KP6          0x106
#define SDLK_KP7          0x107
#define SDLK_KP8          0x108
#define SDLK_KP9          0x109
#define SDLK_KP_PERIOD    0x10A
#define SDLK_KP_DIVIDE    0x10B
#define SDLK_KP_MULTIPLY  0x10C
#define SDLK_KP_MINUS     0x10D
#define SDLK_KP_PLUS      0x10E
#define SDLK_KP_ENTER     0x10F
#define SDLK_KP_EQUALS    0x110
#define SDLK_UP           0x111
#define SDLK_DOWN         0x112
#define SDLK_RIGHT        0x113
#define SDLK_LEFT         0x114
#define SDLK_INSERT       0x115
#define SDLK_HOME         0x116
#define SDLK_END          0x117
#define SDLK_PAGEUP       0x118
#define SDLK_PAGEDOWN     0x119
#define SDLK_F1           0x11A
#define SDLK_F2           0x11B
#define SDLK_F3           0x11C
#define SDLK_F4           0x11D
#define SDLK_F5           0x11E
#define SDLK_F6           0x11F
#define SDLK_F7           0x120
#define SDLK_F8           0x121
#define SDLK_F9           0x122
#define SDLK_F10          0x123
#define SDLK_F11          0x124
#define SDLK_F12          0x125
#define SDLK_F13          0x126
#define SDLK_F14          0x127
#define SDLK_F15          0x128
#define SDLK_MODE         0x129
#define SDLK_NUMLOCK      0x12C
#define SDLK_CAPSLOCK     0x12D
#define SDLK_SCROLLOCK    0x12E
#define SDLK_RSHIFT       0x12F
#define SDLK_LSHIFT       0x130
#define SDLK_RCTRL        0x131
#define SDLK_LCTRL        0x132
#define SDLK_RALT         0x133
#define SDLK_LALT         0x134
#define SDLK_RMETA        0x135
#define SDLK_LMETA        0x136
#define SDLK_LAST         0x143

struct SDL_keysym {
	Uint16 scancode;
	Uint16 sym;
	Uint16 mod;
};
typedef struct SDL_keysym SDL_keysym;

/* ------------------------------------------------------------------ */
/* SDL_keyboard.h */

#define KMOD_NONE     0x0000
#define KMOD_SHIFT    0x0001
#define KMOD_CTRL     0x0002
#define KMOD_ALT      0x0004
#define KMOD_NUM      0x0010
#define KMOD_CAPS     0x0020
#define KMOD_MODE     0x0040
#define KMOD_LSHIFT   0x0100
#define KMOD_RSHIFT   0x0200
#define KMOD_LCTRL    0x0400
#define KMOD_RCTRL    0x0800
#define KMOD_LALT     0x1000
#define KMOD_RALT     0x2000
#define KMOD_LMETA    0x4000
#define KMOD_RMETA    0x8000

int SDL_GetModState (void);
const char *SDL_GetKeyName (Uint16 sym);

/* ------------------------------------------------------------------ */
/* SDL_events.h (event struct) */

typedef struct SDL_Event {
	Uint32 type;
	union {
		struct {
			Uint8 state;
			Uint16 x, y;
			Sint16 xrel, yrel;
		} motion;
		struct {
			Uint8 which;
			Uint8 button;
			Uint8 state;
			Uint16 x, y;
		} button;
		struct {
			struct SDL_keysym keysym;
			Uint8 repeat;
		} key;
	} ;
} SDL_Event;

int SDL_PollEvent (SDL_Event *event);

/* ------------------------------------------------------------------ */
/* SDL_mouse.h */

#define SDL_BUTTON_LEFT    1
#define SDL_BUTTON_MIDDLE  2
#define SDL_BUTTON_RIGHT   3
#define SDL_BUTTON_X1      4
#define SDL_BUTTON_X2      5

#define SDL_RELEASED  0
#define SDL_PRESSED   1

/* ------------------------------------------------------------------ */
/* SDL_timer.h */

typedef Uint32 (SDLCALL *SDL_TimerCallback) (Uint32 interval, void *param);

Uint32 SDL_AddTimer (Uint32 ms, SDL_TimerCallback callback, void *param);
void SDL_Delay (Uint32 ms);

/* ------------------------------------------------------------------ */
/* SDL_audio.h */

#define AUDIO_U8    0x0008
#define AUDIO_S8    0x0010
#define AUDIO_U16   0x0020
#define AUDIO_S16   0x0040
#define AUDIO_U16LSB AUDIO_U16
#define AUDIO_S16LSB AUDIO_S16

typedef void (SDLCALL *SDL_AudioCallback) (void *udata, Uint8 *stream, int len);

struct SDL_AudioSpec {
	int freq;
	Uint16 format;
	Uint8 channels;
	Uint8 silence;
	Uint16 samples;
	Uint16 padding;
	Uint32 size;
	SDL_AudioCallback callback;
	void *userdata;
};
typedef struct SDL_AudioSpec SDL_AudioSpec;

int SDL_OpenAudio (const struct SDL_AudioSpec *desired, struct SDL_AudioSpec *obtained);
void SDL_CloseAudio (void);
int SDL_PauseAudio (int pause);
void SDL_LockAudio (void);
void SDL_UnlockAudio (void);
int SDL_LoadWAV (const char *file, struct SDL_AudioSpec *spec,
                 Uint8 **audiobuf, Uint32 *audiolen);
void SDL_FreeWAV (Uint8 *audiobuf);

/* ------------------------------------------------------------------ */
/* SDL_endian.h */

static inline Uint16 wasm_sdl_swap16 (Uint16 x)
{
	return (Uint16) (((x & 0x00ff) << 8) | (x >> 8));
}
/* wasm is little-endian: swapping a BE value is a plain byte swap */
#define SDL_SwapBE16(x) wasm_sdl_swap16 (x)

#endif /* WASM_SDL12_H */
