/*
 * wasm/GL/glu.h — stub of the GLU API used by screen.c.
 * All no-ops in the wasm build (R_OLD software renderer). See shim.c.
 */
#ifndef WASM_GLU_H
#define WASM_GLU_H

#include "gl.h"

typedef struct GLUquadricObj GLUquadricObj;
typedef struct GLUtesselator GLUtesselator;
typedef void (*_GLUfuncptr) (void);
typedef void (*GLUtessCallback) (void);

#define GLU_TESS_BEGIN        0x1700
#define GLU_TESS_VERTEX       0x1701
#define GLU_TESS_END          0x1702
#define GLU_TESS_ERROR        0x1703
#define GLU_TESS_EDGE_FLAG    0x1704
#define GLU_TESS_COMBINE      0x1705
#define GLU_TESS_VERTEX_DATA  0x1303

#define GLU_TESS_WINDING_RULE  0x1305
#define GLU_TESS_WINDING_ODD   1

GLUquadricObj *gluNewQuadric (void);
void gluDisk (GLUquadricObj *q, GLdouble inner, GLdouble outer,
              GLint slices, GLint loops);
void gluCylinder (GLUquadricObj *q, GLdouble b1, GLdouble t1, GLdouble h,
                  GLint slices, GLint stacks);
void gluSphere (GLUquadricObj *q, GLdouble radius, GLint slices, GLint stacks);
void gluPerspective (GLdouble fovy, GLdouble aspect,
                     GLdouble zNear, GLdouble zFar);
GLUtesselator *gluNewTess (void);
int gluTessCallback (GLUtesselator *t, GLenum which, GLUtessCallback fn);
int gluTessBeginPolygon (GLUtesselator *t, GLvoid *polygon_data);
int gluTessBeginContour (GLUtesselator *t);
int gluTessEndContour (GLUtesselator *t);
int gluTessEndPolygon (GLUtesselator *t);
int gluTessVertex (GLUtesselator *t, GLdouble coordinates[3], GLvoid *data);
int gluTessNormal (GLUtesselator *t, GLdouble x, GLdouble y, GLdouble z);
int gluTessProperty (GLUtesselator *t, GLenum which, GLdouble data);
const GLubyte *gluErrorString (GLenum error);
int gluProject (GLdouble x, GLdouble y, GLdouble z, const GLdouble m[16],
                const GLdouble p[16], const GLint vp[4],
                GLdouble *outX, GLdouble *outY, GLdouble *outZ);

#endif /* WASM_GLU_H */
