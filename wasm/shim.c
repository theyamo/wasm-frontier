/*
 * wasm/shim.c — SDL 1.2 + legacy GL shim for the WebAssembly port.
 *
 * Implements the platform layer for the browser build:
 *
 *  - Video: the 320x200 ST framebuffer. screen.c renders into a GL
 *    texture via glTexSubImage2D (captured here) and presents it with
 *    SDL_GL_SwapBuffers (a 2D canvas putImageData, see wasm/glue.js).
 *    Every other GL call is a no-op: the game runs with the R_OLD
 *    software renderer (forced in Screen_Init), so the 3D view also
 *    comes from the framebuffer. The legacy GL 3D renderer (R_GL) gets
 *    rewritten for WebGL in a later phase.
 *
 *  - Scheduling: wasm is single-threaded and Start680x0() is an infinite
 *    loop in C, so it has to return periodically to let the JS event
 *    loop run. The generated loop (as68k/output_c.c) calls
 *    wasm_68k_yield_check() at its dispatch back-edge; when the chunk's
 *    wall-clock budget is done we return, and JS re-enters
 *    Start680x0() on the next animation frame (Module.__fe2.resume).
 *    The 20ms VBL tick is driven from here (emscripten_get_now), which
 *    replaces SDL_AddTimer — JS timers cannot fire while C is running.
 *
 *  - Input: DOM listeners (wasm/glue.js) call wasm_key_event() /
 *    wasm_mouse_motion() / wasm_mouse_button(); the game's VBL pump
 *    (Call_HostUpdate) drains them through SDL_PollEvent().
 *
 *  - Audio: not implemented yet (later phase). Init fails gracefully and
 *    the game runs without sound.
 */

#include <string.h>
#include <stdio.h>
#include <emscripten.h>

#include "sdl12.h"
#include "GL/gl.h"
#include "GL/glu.h"

#include "../m68000.h"        /* FlagException, GetReg, line_no */

#define FE2_FB_W        320
#define FE2_FB_H        200

/* diagnostic + scheduling helpers, defined with the scheduling code below.
 * fe2_dump_trace is exported to JS (so it must not be static). */
static void fe2_note_unwound (const char *where);
static int fe2_fire_due_vbls (void);
void fe2_dump_trace (void);
static int fe2_do_yield (void);

/* emulator state defined in _host.c (not declared in m68000.h) */
extern signed int rdest;

/* Number of Call_Idle()/SDL_Delay() calls since the last yield check. The
 * guest's "wait for vertical blank" loops call Call_Idle every iteration,
 * so a high count means it is spinning with nothing to do and the loop can
 * hand the rest of the frame back to the browser. Reset at each check so
 * the occasional Call_Idle during real work does not trip it. */
static int fe2_idle_calls;

/* yield chunk budget and VBL period, in microseconds */
#define FE2_CHUNK_MS    16.0
#define FE2_VBL_MS      20.0

/* ==================================================================== */
/* event queue                                                          */

#define EVQ_SIZE        256

struct evq_item {
	int type;
	int x, y, xrel, yrel;
	int button;
	int sym, scancode, mod;
};

static struct evq_item evq[EVQ_SIZE];
static int evq_head, evq_tail;
static int ev_state[1024];        /* SDL_EventState, indexed by event type */
static int fe2_modstate;          /* last seen modifier state */
static int fe2_event_count;       /* test hook: events pushed */

static void ev_push (struct evq_item *e)
{
	int next = (evq_tail + 1) % EVQ_SIZE;
	if (next == evq_head) return;         /* full: drop */
	evq[evq_tail] = *e;
	evq_tail = next;
	fe2_event_count++;
}

EMSCRIPTEN_KEEPALIVE
int fe2_get_event_count (void) { return fe2_event_count; }

/* --- called from JS (see wasm/glue.js) --- */

EMSCRIPTEN_KEEPALIVE
void wasm_key_event (int down, int sym, int scancode, int mod)
{
	struct evq_item e;
	memset (&e, 0, sizeof e);
	e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	e.sym = sym;
	e.scancode = scancode;
	e.mod = mod;
	fe2_modstate = mod;
	ev_push (&e);
}

EMSCRIPTEN_KEEPALIVE
void wasm_mouse_motion (int x, int y, int xrel, int yrel)
{
	struct evq_item e;
	memset (&e, 0, sizeof e);
	e.type = SDL_MOUSEMOTION;
	e.x = x; e.y = y;
	e.xrel = xrel; e.yrel = yrel;
	ev_push (&e);
}

EMSCRIPTEN_KEEPALIVE
void wasm_mouse_button (int down, int button, int x, int y)
{
	struct evq_item e;
	memset (&e, 0, sizeof e);
	e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
	e.button = button;
	e.x = x; e.y = y;
	ev_push (&e);
}

int SDL_PollEvent (SDL_Event *e)
{
	struct evq_item *it;

	/* Pop the next event, skipping any type the app disabled with
	 * SDL_EventState(). Real SDL defaults every event type to *enabled*;
	 * the app never enables SDL_KEYDOWN/UP (see screen.c), so those must
	 * default on or the whole keyboard path is silently dropped. */
	for (;;) {
		if (evq_head == evq_tail) return 0;
		it = &evq[evq_head];
		evq_head = (evq_head + 1) % EVQ_SIZE;
		if (ev_state[it->type]) break;
	}

	memset (e, 0, sizeof (*e));
	e->type = it->type;

	switch (it->type) {
	case SDL_MOUSEMOTION:
		e->motion.x = it->x;
		e->motion.y = it->y;
		e->motion.xrel = it->xrel;
		e->motion.yrel = it->yrel;
		break;
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
		e->button.button = it->button;
		e->button.state = (it->type == SDL_MOUSEBUTTONDOWN) ? SDL_PRESSED : SDL_RELEASED;
		e->button.x = it->x;
		e->button.y = it->y;
		break;
	case SDL_KEYDOWN:
	case SDL_KEYUP:
		e->key.keysym.scancode = it->scancode;
		e->key.keysym.sym = it->sym;
		e->key.keysym.mod = it->mod;
		e->key.repeat = 0;
		break;
	default:
		break;
	}

	return 1;
}

int SDL_EventState (int type, int state)
{
	int old;

	if (type < 0 || type >= (int) (sizeof (ev_state) / sizeof (ev_state[0])))
		return 0;
	old = ev_state[type];
	ev_state[type] = state;
	return old;
}

int SDL_GetModState (void)
{
	return fe2_modstate;
}

const char *SDL_GetKeyName (Uint16 sym)
{
	static char buf[16];

	if (sym >= 32 && sym < 127) {
		buf[0] = (char) sym;
		buf[1] = '\0';
		return buf;
	}
	snprintf (buf, sizeof buf, "key%u", (unsigned) sym);
	return buf;
}

/* ==================================================================== */
/* video                                                                */

static unsigned int fe2_fb[FE2_FB_W * FE2_FB_H];   /* RGBA (bytes r,g,b,a) */
static int fe2_frame_count;

static int fe2_video_w = 640, fe2_video_h = 480;
static int fe2_video_setup;

SDL_Surface *SDL_SetVideoMode (int width, int height, int bits, unsigned int flags)
{
	static SDL_Surface surface;

	if (!fe2_video_setup) {
		fe2_video_setup = 1;
		fe2_video_w = width;
		fe2_video_h = height;
		EM_ASM (({ Module.__fe2.setup_video ($0, $1); }), width, height);
	}

	return &surface;
}

struct SDL_VideoInfo *SDL_GetVideoInfo (void)
{
	static struct SDL_PixelFormat fmt;
	static struct SDL_VideoInfo info;

	fmt.BitsPerPixel = 32;
	info.vfmt = &fmt;
	return &info;
}

void SDL_GL_SetAttribute (int attr, int value)
{
	(void) attr;
	(void) value;
}

void SDL_WM_SetCaption (const char *title, const char *icon)
{
	(void) icon;
	if (title)
		EM_ASM (({ Module.__fe2.set_caption (UTF8ToString ($0)); }), title);
}

int SDL_ShowCursor (int toggle)
{
	EM_ASM (({ Module.__fe2.show_cursor ($0 ? 1 : 0); }), toggle);
	return toggle ? 1 : 0;
}

void SDL_WarpMouse (int x, int y)
{
	struct evq_item e;

	EM_ASM (({ Module.__fe2.warp ($0, $1); }), x, y);

	/* SDL also generates a motion event at the new position */
	memset (&e, 0, sizeof e);
	e.type = SDL_MOUSEMOTION;
	e.x = x; e.y = y;
	ev_push (&e);
}

void SDL_WM_GrabInput (unsigned int state)
{
	EM_ASM (({ Module.__fe2.grab ($0 ? 1 : 0); }), state);
}

const char *SDL_GetError (void)
{
	return "wasm shim: operation not available";
}

int SDL_Init (Uint32 flags)
{
	int i;

	/* unbuffered stdout: the generated interpreter's printf diagnostics
	 * ("Bad jump target", ...) must not be lost if it aborts */
	setvbuf (stdout, NULL, _IONBF, 0);

	/* SDL event types default to enabled. The app only explicitly enables
	 * the mouse events it wants and relies on KEYDOWN/KEYUP being on by
	 * default (this is what real SDL does). */
	for (i = 0; i < (int) (sizeof (ev_state) / sizeof (ev_state[0])); i++)
		ev_state[i] = SDL_ENABLE;

	(void) flags;
	return 0;
}

int SDL_InitSubSystem (Uint32 flags)
{
	/* no audio yet: Audio_Init() handles the failure and continues */
	if (flags & SDL_INIT_AUDIO) {
		fprintf (stderr, "wasm shim: audio not implemented yet\n");
		return -1;
	}
	return 0;
}

int SDL_WasInit (Uint32 flags)
{
	(void) flags;
	return 0;
}

void SDL_Quit (void)
{
	/* No-op on purpose: main() calls this on the way out after
	 * Start680x0() yielded, and the emulator keeps running from here
	 * (the JS loop re-enters Start680x0). Tearing anything down would
	 * kill the game. */
	fe2_note_unwound ("SDL_Quit");
}

/* ==================================================================== */
/* timers                                                               */

Uint32 SDL_AddTimer (Uint32 ms, SDL_TimerCallback callback, void *param)
{
	/* VBLs are generated by wasm_68k_yield_check() instead. */
	(void) ms;
	(void) callback;
	(void) param;
	return 1;
}

void SDL_Delay (Uint32 ms)
{
	/* The game's Call_Idle(): deliver any due VBL and record that the
	 * guest asked to idle. The generated loop counts these per yield
	 * window and hands the rest of the frame back to the browser when the
	 * guest is just spinning (see wasm_68k_yield_check). */
	fe2_idle_calls++;
	fe2_fire_due_vbls ();
	(void) ms;
}

/* ==================================================================== */
/* audio                                                                */
/*
 * SFX only. The game's Call_PlaySFX hostcall calls wasm_play_sfx() (see
 * src/audio.c), which hands (sample, channel) to the WebAudio backend in
 * wasm/glue.js; that plays the corresponding preloaded WAV. So neither the
 * SDL audio API nor audio.c's software mixer is used in the wasm build —
 * the stubs below only satisfy the linker. Music (vorbis) is not done yet.
 */

int wasm_play_sfx (int sample, int channel)
{
	EM_ASM (({ if (Module.__fe2 && Module.__fe2.play_sfx) Module.__fe2.play_sfx ($0, $1); }),
	        sample, channel);
	return 0;
}

/* Persist the guest's save files to IndexedDB. Called from src/hostcall.c
 * after a save file is written or deleted (the /savs dir is an IDBFS). */
void wasm_fs_sync (void)
{
	EM_ASM (({ if (Module.FS && Module.FS.syncfs) Module.FS.syncfs (false, function () {}); }));
}

int SDL_OpenAudio (const SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
	(void) desired;
	(void) obtained;
	return -1;
}

void SDL_CloseAudio (void) {}
int SDL_PauseAudio (int pause) { (void) pause; return 0; }
void SDL_LockAudio (void) {}
void SDL_UnlockAudio (void) {}
void SDL_FreeWAV (Uint8 *audiobuf) { (void) audiobuf; }

int SDL_LoadWAV (const char *file, SDL_AudioSpec *spec, Uint8 **audiobuf, Uint32 *audiolen)
{
	(void) file;
	(void) spec;
	(void) audiobuf;
	(void) audiolen;
	return -1;
}

/* ==================================================================== */
/* GL stubs                                                             */
/*
 * Everything but glTexSubImage2D is a no-op: with R_OLD the only GL the
 * game's render path uses is "upload the 320x200 framebuffer" plus the
 * quad that draws it, and the quad is handled at present time.
 */

void glViewport (GLint x, GLint y, GLsizei w, GLsizei h) { (void)x;(void)y;(void)w;(void)h; }
void glDisable (GLenum cap) { (void) cap; }
void glEnable (GLenum cap) { (void) cap; }
void glShadeModel (GLenum mode) { (void) mode; }
void glClearColor (GLfloat r, GLfloat g, GLfloat b, GLfloat a) { (void)r;(void)g;(void)b;(void)a; }
void glClear (GLbitfield mask) { (void) mask; }
void glMatrixMode (GLenum mode) { (void) mode; }
void glLoadIdentity (void) {}
void glPushMatrix (void) {}
void glPopMatrix (void) {}
void glTranslatef (GLfloat x, GLfloat y, GLfloat z) { (void)x;(void)y;(void)z; }
void glRotatef (GLfloat a, GLfloat x, GLfloat y, GLfloat z) { (void)a;(void)x;(void)y;(void)z; }
void glScalef (GLfloat x, GLfloat y, GLfloat z) { (void)x;(void)y;(void)z; }
void glMultMatrixf (const GLfloat *m) { (void) m; }
void glPointSize (GLfloat size) { (void) size; }
void glCullFace (GLenum mode) { (void) mode; }
void glOrtho (GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{ (void)l;(void)r;(void)b;(void)t;(void)n;(void)f; }
void glFlush (void) {}
void glNormal3fv (const GLfloat *v) { (void) v; }
void glMaterialiv (GLenum face, GLenum pname, const GLint *params) { (void)face;(void)pname;(void)params; }
void glBlendFunc (GLenum sfactor, GLenum dfactor) { (void)sfactor;(void)dfactor; }
void glColor3f (GLfloat r, GLfloat g, GLfloat b) { (void)r;(void)g;(void)b; }
void glColor3ub (GLubyte r, GLubyte g, GLubyte b) { (void)r;(void)g;(void)b; }
void glColor3ui (GLuint r, GLuint g, GLuint b) { (void)r;(void)g;(void)b; }
void glColor3uiv (const GLuint *c) { (void) c; }
void glTexCoord2f (GLfloat s, GLfloat t) { (void)s;(void)t; }
void glBegin (GLenum mode) { (void) mode; }
void glEnd (void) {}
void glVertex2i (GLint x, GLint y) { (void)x;(void)y; }
void glVertex3f (GLfloat x, GLfloat y, GLfloat z) { (void)x;(void)y;(void)z; }
void glVertex3iv (const GLint *v) { (void) v; }
void glVertex3fv (const GLfloat *v) { (void) v; }
void glVertex3dv (const GLdouble *v) { (void) v; }
void glBindTexture (GLenum target, GLuint tex) { (void)target;(void)tex; }
void glTexImage2D (GLenum target, GLint level, GLint internalFormat,
                   GLsizei w, GLsizei h, GLint border,
                   GLenum format, GLenum type, const GLvoid *pixels)
{ (void)target;(void)level;(void)internalFormat;(void)w;(void)h;(void)border;(void)format;(void)type;(void)pixels; }
void glTexParameterf (GLenum target, GLenum pname, GLfloat param) { (void)target;(void)pname;(void)param; }
void glTexEnvf (GLenum target, GLenum pname, GLfloat param) { (void)target;(void)pname;(void)param; }
void glLightfv (GLenum light, GLenum pname, const GLfloat *params) { (void)light;(void)pname;(void)params; }
void glLightiv (GLenum light, GLenum pname, const GLint *params) { (void)light;(void)pname;(void)params; }
void glGetDoublev (GLenum pname, GLdouble *params) { (void)pname;(void)params; }
void glGetIntegerv (GLenum pname, GLint *params) { (void)pname;(void)params; }
GLenum glGetError (void) { return 0; }

static GLuint fe2_texid = 1;

void glGenTextures (GLsizei n, GLuint *ids)
{
	GLsizei i;
	for (i = 0; i < n; i++) ids[i] = fe2_texid++;
}

/*
 * The one GL call that does real work: screen.c uploads the palette
 * mapped 320x200 screen, one row per glTexSubImage2D call. The height is
 * 2 in the call, but the source buffer holds a single row (on desktop GL
 * the second row reads past the array — we just copy the first row).
 */
void glTexSubImage2D (GLenum target, GLint level, GLint x, GLint y,
                      GLsizei w, GLsizei h, GLenum format, GLenum type,
                      const GLvoid *data)
{
	const unsigned int *src = data;
	unsigned int *dst;
	int i, n;

	(void) target; (void) level; (void) h; (void) format; (void) type;

	if (x != 0 || y < 0 || y >= FE2_FB_H || !data) return;

	n = w;
	if (n > FE2_FB_W) n = FE2_FB_W;

	dst = fe2_fb + (y * FE2_FB_W);

	for (i = 0; i < n; i++) {
		unsigned int px = src[i];
		/* colour 255 ("transparent" in GL mode) arrives as 0x00000000;
		 * make it opaque black so it does not punch holes in the canvas */
		if (!(px & 0xff000000u)) px = 0xff000000u;
		dst[i] = px;
	}
}

void SDL_GL_SwapBuffers (void)
{
	fe2_frame_count++;
	EM_ASM (({ Module.__fe2.present ($0); }), &fe2_fb[0]);
}

/* --- GLU stubs --- */

/* the header only forward-declares these; the shim needs dummies */
struct GLUquadricObj { int unused; };
struct GLUtesselator { int unused; };
static struct GLUquadricObj fe2_quadric;
static struct GLUtesselator fe2_tess;

GLUquadricObj *gluNewQuadric (void) { return &fe2_quadric; }
void gluDisk (GLUquadricObj *q, GLdouble i0, GLdouble o0, GLint s, GLint l)
{ (void)q;(void)i0;(void)o0;(void)s;(void)l; }
void gluCylinder (GLUquadricObj *q, GLdouble b, GLdouble t, GLdouble h, GLint s, GLint k)
{ (void)q;(void)b;(void)t;(void)h;(void)s;(void)k; }
void gluSphere (GLUquadricObj *q, GLdouble r, GLint s, GLint k)
{ (void)q;(void)r;(void)s;(void)k; }
void gluPerspective (GLdouble fovy, GLdouble aspect, GLdouble zn, GLdouble zf)
{ (void)fovy;(void)aspect;(void)zn;(void)zf; }
GLUtesselator *gluNewTess (void) { return &fe2_tess; }
int gluTessCallback (GLUtesselator *t, GLenum which, GLUtessCallback fn)
{ (void)t;(void)which;(void)fn; return 1; }
int gluTessBeginPolygon (GLUtesselator *t, GLvoid *d) { (void)t;(void)d; return 1; }
int gluTessBeginContour (GLUtesselator *t) { (void) t; return 1; }
int gluTessEndContour (GLUtesselator *t) { (void) t; return 1; }
int gluTessEndPolygon (GLUtesselator *t) { (void) t; return 1; }
int gluTessVertex (GLUtesselator *t, GLdouble c[3], GLvoid *d) { (void)t;(void)c;(void)d; return 1; }
int gluTessNormal (GLUtesselator *t, GLdouble x, GLdouble y, GLdouble z) { (void)t;(void)x;(void)y;(void)z; return 1; }
int gluTessProperty (GLUtesselator *t, GLenum which, GLdouble d) { (void)t;(void)which;(void)d; return 1; }
const GLubyte *gluErrorString (GLenum error) { (void) error; return (const GLubyte *) "glu"; }
int gluProject (GLdouble x, GLdouble y, GLdouble z, const GLdouble m[16],
                const GLdouble p[16], const GLint vp[4],
                GLdouble *ox, GLdouble *oy, GLdouble *oz)
{
	(void)x;(void)y;(void)z;(void)m;(void)p;(void)vp;
	if (ox) *ox = 0.0; if (oy) *oy = 0.0; if (oz) *oz = 0.0;
	return 1;
}

/* ==================================================================== */
/* emulator scheduling: chunk yield + VBL tick                          */
/*
 * The generated Start680x0() loop calls wasm_68k_entry() on entry and
 * wasm_68k_yield_check() from its dispatch back-edge (see
 * as68k/output_c.c). This lets the single-threaded wasm build hand
 * control back to the browser's event loop regularly.
 */

int wasm_yield_cnt;                       /* bumped by the generated loop */

static double fe2_deadline_ms;            /* end of the current chunk */
static double fe2_vbl_next_ms;            /* next 20ms VBL */
static int fe2_vbl_inited;

static int fe2_vbl_count;                 /* test hooks */
static int fe2_yield_count;               /* times the chunk budget expired */
static int fe2_entry_count;               /* times Start680x0 was entered */

/* the 68k source line currently executing (from _host.c, M68K_DEBUG) */
extern int line_no;

/* small ring buffer of where the 68k was at each yield check */
static int fe2_unwound;

EMSCRIPTEN_KEEPALIVE
int fe2_get_vbl_count (void) { return fe2_vbl_count; }

EMSCRIPTEN_KEEPALIVE
int fe2_get_yield_count (void) { return fe2_yield_count; }

EMSCRIPTEN_KEEPALIVE
int fe2_get_frame_count (void) { return fe2_frame_count; }

EMSCRIPTEN_KEEPALIVE
int fe2_get_entry_count (void) { return fe2_entry_count; }

EMSCRIPTEN_KEEPALIVE
int fe2_get_backedge_count (void) { return wasm_yield_cnt; }

EMSCRIPTEN_KEEPALIVE
int fe2_get_line_no (void) { return line_no; }

EMSCRIPTEN_KEEPALIVE
int fe2_get_rdest (void) { return (int) rdest; }

EMSCRIPTEN_KEEPALIVE
int fe2_get_done (void) { return fe2_unwound; }

/* dispatch trace: where the 68k jumped, for post-mortem dumps */
#define FE2_TRACE_LEN 512
static int fe2_trace_jdest[FE2_TRACE_LEN];
static int fe2_trace_line[FE2_TRACE_LEN];
static int fe2_trace_pos;

EMSCRIPTEN_KEEPALIVE
void wasm_68k_trace (int jdest)
{
	int i = fe2_trace_pos++ & (FE2_TRACE_LEN - 1);
	fe2_trace_jdest[i] = jdest;
	fe2_trace_line[i] = line_no;
}

/* 20ms VBL, replacing the native SDL_AddTimer. Only a counter is checked
 * per call: reading the clock means a JS call, so it is gated. */
static int fe2_vbl_countdown;
static double fe2_last_yield_ms;

static int fe2_fire_due_vbls (void)
{
	double now = emscripten_get_now ();

	if (now < fe2_vbl_next_ms) return 0;

	fe2_vbl_next_ms += FE2_VBL_MS;
	if (fe2_vbl_next_ms < now) fe2_vbl_next_ms = now + FE2_VBL_MS;  /* no bursts */
	fe2_vbl_count++;
	FlagException (0);
	return 1;
}

/* Hand the browser a turn and let JS re-enter Start680x0 later. */
static int fe2_do_yield (void)
{
	fe2_last_yield_ms = emscripten_get_now ();
	fe2_yield_count++;
	/* fprintf (stderr, "diag: yield #%d (line %d, rdest 0x%x, deadline %d ms away)\n", */
	/*          fe2_yield_count, line_no, (unsigned) rdest, */
	/*          (int) (fe2_deadline_ms - fe2_last_yield_ms)); */
	EM_ASM (({ Module.__fe2.resume (); }));
	return 1;
}

/*
 * Called at every taken conditional branch (see as68k/output_c.c): the
 * cheapest place to both deliver the VBL and to notice that the guest is
 * idling. Returns 1 when the generated loop should return to JS.
 */
EMSCRIPTEN_KEEPALIVE
int wasm_68k_vbl (void)
{
	/* Deliver due VBLs first: the guest may be spinning in a "wait for
	 * vertical blank" loop, and yielding without delivering one would
	 * starve it. */
	if (--fe2_vbl_countdown <= 0) {
		fe2_vbl_countdown = 256;
		fe2_fire_due_vbls ();
		if (emscripten_get_now () >= fe2_deadline_ms) return fe2_do_yield ();
	}

	/* The guest idled (SDL_Delay): nothing to do beyond the VBL above, so
	 * do NOT yield here — the chunk deadline decides when the browser gets
	 * a turn. Yielding on idle makes a wait loop crawl (one dispatch per
	 * animation frame). */
	return 0;
}

EMSCRIPTEN_KEEPALIVE
void wasm_68k_end_of_switch (void)
{
	fprintf (stderr, "diag: guest fell off the end of the switch (line %d, rdest=0x%x, a7=0x%x)\n",
	         line_no, (unsigned) rdest, (unsigned) GetReg (15));
	fe2_unwound = 1;
	fe2_dump_trace ();
}

EMSCRIPTEN_KEEPALIVE
void wasm_68k_bad_jump (int jdest, int rdest_v)
{
	fprintf (stderr, "diag: bad jump target 0x%x (line %d, rdest=0x%x)\n",
	         (unsigned) jdest, line_no, (unsigned) rdest_v);
	fe2_dump_trace ();
}

EMSCRIPTEN_KEEPALIVE
void wasm_68k_shouldnt_get_here (void)
{
	fprintf (stderr, "diag: handle_exception fell through (line %d, rdest=0x%x)\n",
	         line_no, (unsigned) rdest);
	fe2_dump_trace ();
}

EMSCRIPTEN_KEEPALIVE
void fe2_dump_trace (void)
{
	int i, n, first;

	n = (fe2_trace_pos < FE2_TRACE_LEN) ? fe2_trace_pos : FE2_TRACE_LEN;
	if (n > 24) n = 24;
	first = fe2_trace_pos - n;

	fprintf (stderr, "diag: statedump: line_no=%d rdest=0x%x a7=0x%x vbls=%d entries=%d backedges=%d\n",
	         line_no, (unsigned) rdest, (unsigned) GetReg (15),
	         fe2_vbl_count, fe2_entry_count, wasm_yield_cnt);

	fprintf (stderr, "diag: last %d dispatches (jdest:line):", n);
	for (i = 0; i < n; i++) {
		int k = (first + i) & (FE2_TRACE_LEN - 1);
		fprintf (stderr, " %x:%d", (unsigned) fe2_trace_jdest[k], fe2_trace_line[k]);
	}
	fprintf (stderr, "\n");
}

/* called when main() unwinds: the interpreter loop returned */
static void fe2_note_unwound (const char *where)
{
	if (fe2_unwound) return;
	fe2_unwound = 1;
	fprintf (stderr, "diag: emulator loop returned (%s)\n", where);
	fe2_dump_trace ();
}

EMSCRIPTEN_KEEPALIVE
void wasm_68k_entry (void)
{
	double now = emscripten_get_now ();   /* milliseconds */

	fe2_entry_count++;
	fe2_idle_calls = 0;   /* fresh chunk: restart the idle-call count */

	/* Each entry is one JS-driven chunk (about one animation frame), so
	 * this is the natural place to deliver the next VBL: a guest that
	 * yields from its idle/wait loop gets its vertical blank and can make
	 * progress, exactly like the native build where the SDL timer thread
	 * delivers it. */
	fe2_vbl_countdown = 1;
	fe2_fire_due_vbls ();

	if (!fe2_vbl_inited) {
		fe2_vbl_next_ms = now + FE2_VBL_MS;
		fe2_vbl_inited = 1;
	}
	fe2_deadline_ms = now + FE2_CHUNK_MS;
}

EMSCRIPTEN_KEEPALIVE
int wasm_68k_yield_check (void)
{
	double now = emscripten_get_now ();
	int fired, idle;

	/* 20ms vertical blank (also generated at the 68k branch sites) */
	fired = fe2_fire_due_vbls ();

	/*
	 * Idle detection: the guest's "wait for vertical blank" loops call
	 * Call_Idle (-> SDL_Delay) on every iteration, so a burst of them with
	 * no VBL due means it is spinning with nothing to do. Reset the count
	 * every check so an occasional Call_Idle during real work does not
	 * trigger this.
	 */
	idle = (fe2_idle_calls >= 8);
	fe2_idle_calls = 0;

	/* Chunk budget used up: hand back to the event loop. */
	if (now >= fe2_deadline_ms) return fe2_do_yield ();

	/* Guest idling and no VBL due: yield the rest of the frame instead of
	 * burning CPU to the deadline. JS re-enters us next animation frame,
	 * by which time the VBL fires and the guest wakes up. */
	if (idle && !fired) return fe2_do_yield ();

	return 0;
}
