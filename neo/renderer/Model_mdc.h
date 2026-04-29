/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#ifndef __MODEL_MDC_H__
#define __MODEL_MDC_H__

/*
========================================================================

.MDc triangle model file format

Private structures used by the MDc loader.

========================================================================
*/
#define MDC_IDENT			(('C'<<24)+('P'<<16)+('D'<<8)+'I')
#define MDC_VERSION			2

// surface geometry should not exceed these limits
#define	SHADER_MAX_VERTEXES	1000
#define	SHADER_MAX_INDEXES	(6*SHADER_MAX_VERTEXES)

// limits
#define MDC_MAX_LODS		4
#define	MDC_MAX_TRIANGLES	8192	// per surface
#define MDC_MAX_VERTS		4096	// per surface
#define MDC_MAX_SHADERS		256		// per surface
#define MDC_MAX_FRAMES		1024	// per model
#define	MDC_MAX_SURFACES	32		// per model
#define MDC_MAX_TAGS		16		// per frame
#define MAX_MDCPATH			64		// from quake3

// vertex scales
#define	MDC_XYZ_SCALE		(1.0/64)

#define MDC_DIST_SCALE      0.05    // lower for more accuracy, but less range
#define MDC_MAX_OFS         127.0   // to be safe
#define MDC_MAX_DIST        ( MDC_MAX_OFS * MDC_DIST_SCALE )

#define R_MDC_DecodeXyzCompressed( ofsVec, out ) \
	( out )[0] = ( (float)( ( ofsVec ) & 255 ) - MDC_MAX_OFS ) * MDC_DIST_SCALE; \
	( out )[1] = ( (float)( ( ofsVec >> 8 ) & 255 ) - MDC_MAX_OFS ) * MDC_DIST_SCALE; \
	( out )[2] = ( (float)( ( ofsVec >> 16 ) & 255 ) - MDC_MAX_OFS ) * MDC_DIST_SCALE; \
;

typedef struct mdcXyzCompressed_s {
	unsigned int ofsVec;			// offset direction from the last base frame
	//	unsigned short	ofsVec;
} mdcXyzCompressed_t;

typedef struct {
	char name[MAX_MDCPATH];           // tag name
} mdcTagName_t;

#define MDC_TAG_ANGLE_SCALE ( 360.0 / 32700.0 )

typedef struct mdcFrame_s {
	idVec3		bounds[2];
	idVec3		localOrigin;
	float		radius;
	char		name[16];
} mdcFrame_t;

typedef struct mdcTag_s {
	short xyz[3];
	short angles[3];
} mdcTag_t;

/*
** mdcSurface_t
**
** CHUNK			SIZE
** header			sizeof( md3Surface_t )
** shaders			sizeof( md3Shader_t ) * numShaders
** triangles[0]		sizeof( md3Triangle_t ) * numTriangles
** st				sizeof( md3St_t ) * numVerts
** XyzNormals		sizeof( md3XyzNormal_t ) * numVerts * numBaseFrames
** XyzCompressed	sizeof( mdcXyzCompressed ) * numVerts * numCompFrames
** frameBaseFrames	sizeof( short ) * numFrames
** frameCompFrames	sizeof( short ) * numFrames (-1 if frame is a baseFrame)
*/
typedef struct mdcSurface_s {
	int			ident;				//

	char		name[MAX_MDCPATH];	// polyset name

	int			flags;
	int			numCompFrames;          // all surfaces in a model should have the same
	int			numBaseFrames;          // ditto

	int			numShaders;			// all surfaces in a model should have the same
	int			numVerts;

	int			numTriangles;
	int			ofsTriangles;

	int			ofsShaders;			// offset from start of MDcSurface_t
	int			ofsSt;				// texture coords are common for all frames
	int			ofsXyzNormals;		// numVerts * numFrames
	int			ofsXyzCompressed;       // numVerts * numCompFrames

	int			ofsFrameBaseFrames;     // numFrames
	int			ofsFrameCompFrames;     // numFrames

	int			ofsEnd;				// next surface follows
} mdcSurface_t;

typedef struct {
	char				name[MAX_MDCPATH];
	//const idMaterial *	shader;			// for in-game use
	int					shaderIndex; // DG: can't use a pointer, that breaks on 64bit!
} mdcShader_t;

typedef struct {
	int			indexes[3];
} mdcTriangle_t;

typedef struct {
	float		st[2];
} mdcSt_t;

typedef struct {
	short		xyz[3];
	short		normal;
} mdcXyzNormal_t;

typedef struct mdcHeader_s {
	int			ident;
	int			version;

	char		name[MAX_MDCPATH];	// model name

	int			flags;

	int			numFrames;
	int			numTags;
	int			numSurfaces;

	int			numSkins;

	int			ofsFrames;			// offset for first frame
	int			ofsTagNames;                // numTags
	int			ofsTags;			// numFrames * numTags
	int			ofsSurfaces;		// first surface, others follow

	int			ofsEnd;				// end of file
} mdcHeader_t;

#endif /* !__MODEL_MDC_H__ */
