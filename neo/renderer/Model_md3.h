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

#ifndef __MODEL_MD3_H__
#define __MODEL_MD3_H__

/*
========================================================================

.MD3 triangle model file format

Private structures used by the MD3 loader.

========================================================================
*/

#define MD3_IDENT			(('3'<<24)+('P'<<16)+('D'<<8)+'I')
#define MD3_VERSION			15

// surface geometry should not exceed these limits
#define	SHADER_MAX_VERTEXES	1000
#define	SHADER_MAX_INDEXES	(6*SHADER_MAX_VERTEXES)

// limits
#define MD3_MAX_LODS		4
#define	MD3_MAX_TRIANGLES	8192	// per surface
#define MD3_MAX_VERTS		4096	// per surface
#define MD3_MAX_SHADERS		256		// per surface
#define MD3_MAX_FRAMES		1024	// per model
#define	MD3_MAX_SURFACES	32		// per model
#define MD3_MAX_TAGS		16		// per frame
#define MAX_MD3PATH			64		// from quake3

// vertex scales
#define	MD3_XYZ_SCALE		(1.0/64)

typedef struct md3Frame_s {
	idVec3		bounds[2];
	idVec3		localOrigin;
	float		radius;
	char		name[16];
} md3Frame_t;

typedef struct md3Tag_s {
	char		name[MAX_MD3PATH];	// tag name
	idVec3		origin;
	idVec3		axis[3];
} md3Tag_t;

/*
** md3Surface_t
**
** CHUNK			SIZE
** header			sizeof( md3Surface_t )
** shaders			sizeof( md3Shader_t ) * numShaders
** triangles[0]		sizeof( md3Triangle_t ) * numTriangles
** st				sizeof( md3St_t ) * numVerts
** XyzNormals		sizeof( md3XyzNormal_t ) * numVerts * numFrames
*/

typedef struct md3Surface_s {
	int			ident;				//

	char		name[MAX_MD3PATH];	// polyset name

	int			flags;
	int			numFrames;			// all surfaces in a model should have the same

	int			numShaders;			// all surfaces in a model should have the same
	int			numVerts;

	int			numTriangles;
	int			ofsTriangles;

	int			ofsShaders;			// offset from start of md3Surface_t
	int			ofsSt;				// texture coords are common for all frames
	int			ofsXyzNormals;		// numVerts * numFrames

	int			ofsEnd;				// next surface follows
} md3Surface_t;

typedef struct {
	char				name[MAX_MD3PATH];
	//const idMaterial *	shader;			// for in-game use
	int					shaderIndex; // DG: can't use a pointer, that breaks on 64bit!
} md3Shader_t;

typedef struct {
	int			indexes[3];
} md3Triangle_t;

typedef struct {
	float		st[2];
} md3St_t;

typedef struct {
	short		xyz[3];
	short		normal;
} md3XyzNormal_t;

typedef struct md3Header_s {
	int			ident;
	int			version;

	char		name[MAX_MD3PATH];	// model name

	int			flags;

	int			numFrames;
	int			numTags;
	int			numSurfaces;

	int			numSkins;

	int			ofsFrames;			// offset for first frame
	int			ofsTags;			// numFrames * numTags
	int			ofsSurfaces;		// first surface, others follow

	int			ofsEnd;				// end of file
} md3Header_t;

// Testing Dynamix
/*
========================================================================

.TAN triangle model file format (FAKK2 / Alice / MOHAA "Tiki animation")

Private structures used by the TAN loader.  Layout taken from the
import_bsp/TAN.py importer in SomaZ/Blender_BSP_Importer.  Everything is
little endian.  Only the static (first frame) data is used by the loader.

Offsets inside a surface are relative to the start of that surface,
the per-tag offsets in the header are absolute (relative to the file start).

========================================================================
*/

// file starts with the bytes 'T','A','N',' '
#define TAN_IDENT			(((' ')<<24)+(('N')<<16)+(('A')<<8)+'T')
#define TAN_VERSION			2

// sanity limits, the format itself has no real ones
#define TAN_MAX_VERTS		65536	// per surface
#define TAN_MAX_TRIANGLES	131072	// per surface
#define TAN_MAX_TAGS		16		// the header only has room for 16 tag offsets
#define MAX_TANPATH			64

// vertex positions are 16 bit values scaled per frame:
//   pos = value * frame.scale + frame.offset

typedef struct tanHeader_s {
	int			ident;
	int			version;

	char		name[MAX_TANPATH];	// model name

	int			numFrames;
	int			numTags;
	int			numSurfaces;

	float		totalTime;
	float		totalDelta[3];

	int			ofsFrames;			// numFrames * tanFrame_t
	int			ofsSurfaces;		// first surface, others follow
	int			ofsTags[TAN_MAX_TAGS];	// each: tanTagName_t followed by numFrames * tanTagData_t
	int			ofsEnd;				// end of file
} tanHeader_t;

typedef struct tanFrame_s {
	float		bounds[2][3];		// min, max
	float		scale[3];
	float		offset[3];
	float		delta[3];
	float		radius;
	float		frameTime;
} tanFrame_t;

/*
** tanSurface_t
**
** CHUNK			SIZE
** header			sizeof( tanSurface_t )
** triangles		sizeof( tanTriangle_t ) * numTriangles
** collapseMap		sizeof( tanCollapse_t ) * numVerts		(LOD data, unused)
** st				sizeof( tanSt_t ) * numVerts
** XyzNormals		sizeof( tanXyzNormal_t ) * numVerts * numFrames
*/
typedef struct tanSurface_s {
	int			ident;

	char		name[MAX_TANPATH];	// polyset name, also used as the material name

	int			numFrames;
	int			numVerts;
	int			numModelVerts;		// same as numVerts in exported files
	int			numTriangles;

	int			ofsTriangles;
	int			ofsCollapseMap;
	int			ofsSt;				// texture coords are common for all frames
	int			ofsXyzNormals;		// numVerts * numFrames
	int			ofsEnd;				// next surface follows
} tanSurface_t;

typedef struct {
	int			indexes[3];			// same winding as MD3
} tanTriangle_t;

typedef struct {
	int			id;
} tanCollapse_t;

typedef struct {
	float		st[2];				// same orientation as MD3
} tanSt_t;

typedef struct {
	unsigned short	xyz[3];
	unsigned char	normal[2];		// latitude / longitude, same packing as MD3 (unused, normals are derived)
} tanXyzNormal_t;

typedef struct {
	char		name[MAX_TANPATH];
} tanTagName_t;

typedef struct {
	float		origin[3];
	float		axis[3][3];
} tanTagData_t;

// catch struct padding / packing surprises at compile time
typedef char tanCheckHeaderSize[(sizeof(tanHeader_t) == 176) ? 1 : -1];
typedef char tanCheckFrameSize[(sizeof(tanFrame_t) == 68) ? 1 : -1];
typedef char tanCheckSurfaceSize[(sizeof(tanSurface_t) == 104) ? 1 : -1];
typedef char tanCheckVertSize[(sizeof(tanXyzNormal_t) == 8) ? 1 : -1];
typedef char tanCheckTagDataSize[(sizeof(tanTagData_t) == 48) ? 1 : -1];

#endif /* !__MODEL_MD3_H__ */
