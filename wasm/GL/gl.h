/*
 * wasm/GL/gl.h — stub of the legacy OpenGL 1.x API used by screen.c.
 *
 * The wasm build runs the game with the R_OLD software renderer, so all
 * GL calls are no-ops EXCEPT glTexSubImage2D (captures the 320x200
 * framebuffer rows) and SDL_GL_SwapBuffers (presents to the canvas).
 * See wasm/shim.c. Constants match real GL.
 */
#ifndef WASM_GL_H
#define WASM_GL_H

#include <stddef.h>

typedef unsigned int   GLenum;
typedef unsigned int   GLuint;
typedef unsigned int   GLbitfield;
typedef int            GLboolean;
typedef int            GLint;
typedef int            GLsizei;
typedef int            GLfixed;
typedef float          GLfloat;
typedef double         GLdouble;
typedef unsigned char  GLubyte;
typedef void           GLvoid;

#define GL_POINTS          0x0000
#define GL_LINES           0x0001
#define GL_LINE_STRIP      0x0003
#define GL_TRIANGLES       0x0004
#define GL_TRIANGLE_STRIP  0x0005
#define GL_TRIANGLE_FAN    0x0006
#define GL_QUADS           0x0007
#define GL_POLYGON         0x0009

#define GL_TEXTURE_2D          0x0DE1
#define GL_LIGHTING            0x0B50
#define GL_CULL_FACE           0x0B44
#define GL_DEPTH_TEST          0x0B71
#define GL_BLEND               0x0BE2
#define GL_NORMALIZE           0x1A00

#define GL_LIGHT0  0x4000
#define GL_LIGHT1  0x4001

#define GL_FRONT         0x0404
#define GL_BACK          0x0405

#define GL_AMBIENT       0x1200
#define GL_DIFFUSE       0x1201
#define GL_POSITION      0x1203

#define GL_FLAT    0x1D00
#define GL_SMOOTH  0x1D01

#define GL_RGBA          0x1908
#define GL_RGB           0x1907
#define GL_UNSIGNED_BYTE 0x1401
#define GL_INT           0x1404

#define GL_TEXTURE_MAG_FILTER  0x2800
#define GL_TEXTURE_MIN_FILTER  0x2801
#define GL_TEXTURE_WRAP_S      0x2802
#define GL_TEXTURE_WRAP_T      0x2803
#define GL_NEAREST             0x2600
#define GL_LINEAR              0x2601
#define GL_CLAMP               0x812F

#define GL_TEXTURE_ENV     0x2300
#define GL_TEXTURE_ENV_MODE 0x2301
#define GL_REPLACE         0x1E01
#define GL_MODULATE        0x2100

#define GL_SRC_ALPHA            0x0302
#define GL_ONE_MINUS_SRC_ALPHA  0x0303

#define GL_COLOR_BUFFER_BIT  0x00004000
#define GL_DEPTH_BUFFER_BIT  0x00000100

#define GL_PROJECTION  0x1701
#define GL_MODELVIEW   0x1700

/* glGet* targets */
#define GL_VIEWPORT           0x0BA2
#define GL_MODELVIEW_MATRIX   0x0BA6
#define GL_PROJECTION_MATRIX  0x0BA7

/* functions used by screen.c — all no-ops in the shim unless noted */
void glViewport (GLint x, GLint y, GLsizei w, GLsizei h);
void glDisable (GLenum cap);
void glEnable (GLenum cap);
void glShadeModel (GLenum mode);
void glClearColor (GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glClear (GLbitfield mask);
void glMatrixMode (GLenum mode);
void glLoadIdentity (void);
void glPushMatrix (void);
void glPopMatrix (void);
void glTranslatef (GLfloat x, GLfloat y, GLfloat z);
void glRotatef (GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void glScalef (GLfloat x, GLfloat y, GLfloat z);
void glMultMatrixf (const GLfloat *m);
void glPointSize (GLfloat size);
void glCullFace (GLenum mode);
void glOrtho (GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
              GLdouble zNear, GLdouble zFar);
void glFlush (void);
void glNormal3fv (const GLfloat *v);
void glMaterialiv (GLenum face, GLenum pname, const GLint *params);
void glBlendFunc (GLenum sfactor, GLenum dfactor);
void glColor3f (GLfloat r, GLfloat g, GLfloat b);
void glColor3ub (GLubyte r, GLubyte g, GLubyte b);
void glColor3ui (GLuint r, GLuint g, GLuint b);
void glColor3uiv (const GLuint *c);
void glTexCoord2f (GLfloat s, GLfloat t);
void glBegin (GLenum mode);
void glEnd (void);
void glVertex2i (GLint x, GLint y);
void glVertex3f (GLfloat x, GLfloat y, GLfloat z);
void glVertex3iv (const GLint *v);
void glVertex3fv (const GLfloat *v);
void glVertex3dv (const GLdouble *v);
void glGenTextures (GLsizei n, GLuint *ids);
void glBindTexture (GLenum target, GLuint tex);
void glTexImage2D (GLenum target, GLint level, GLint internalFormat,
                   GLsizei w, GLsizei h, GLint border,
                   GLenum format, GLenum type, const GLvoid *pixels);
/* captures framebuffer rows — the only "real" GL call in the shim */
void glTexSubImage2D (GLenum target, GLint level, GLint x, GLint y,
                      GLsizei w, GLsizei h, GLenum format, GLenum type,
                      const GLvoid *data);
void glTexParameterf (GLenum target, GLenum pname, GLfloat param);
void glTexEnvf (GLenum target, GLenum pname, GLfloat param);
void glLightfv (GLenum light, GLenum pname, const GLfloat *params);
void glLightiv (GLenum light, GLenum pname, const GLint *params);
void glGetDoublev (GLenum pname, GLdouble *params);
void glGetIntegerv (GLenum pname, GLint *params);
GLenum glGetError (void);

#endif /* WASM_GL_H */
