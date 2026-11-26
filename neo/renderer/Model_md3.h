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
// pos = value * frame.scale + frame.offset

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


#define MD2_IDENT			(('2'<<24)+('P'<<16)+('D'<<8)+'I')	// "IDP2"
#define MD2_VERSION			8

// sanity limits (Quake 2 itself allows 2048 xyz / 4096 triangles)
#define MD2_MAX_VERTS		4096	// xyz positions per frame
#define MD2_MAX_ST			16384	// texture coordinates
#define MD2_MAX_TRIANGLES	8192
#define MD2_MAX_SKINS		32
#define MD2_MAX_EXPANDED	65536	// unique (xyz, st) pairs after splitting
#define MAX_MD2PATH			64

typedef struct md2Header_s {
	int			ident;
	int			version;

	int			skinWidth;
	int			skinHeight;
	int			frameSize;		// bytes per frame

	int			numSkins;
	int			numXyz;
	int			numSt;
	int			numTris;
	int			numGlCmds;		// unused
	int			numFrames;

	int			ofsSkins;		// numSkins * md2Skin_t
	int			ofsSt;			// numSt * md2St_t
	int			ofsTris;		// numTris * md2Triangle_t
	int			ofsFrames;		// numFrames * frameSize
	int			ofsGlCmds;		// unused
	int			ofsEnd;			// end of file
} md2Header_t;

typedef struct {
	char		name[MAX_MD2PATH];
} md2Skin_t;

typedef struct {
	short		s, t;			// in skin pixels, divide by skinWidth / skinHeight
} md2St_t;

typedef struct {
	short		xyz[3];			// indexes into the frame's xyz list
	short		st[3];			// indexes into the st list
} md2Triangle_t;

typedef struct {
	unsigned char	v[3];		// pos = v * frame.scale + frame.translate
	unsigned char	normalIndex;	// index into the Quake 2 normal table (unused, normals are derived)
} md2Vertex_t;

// followed by numXyz md2Vertex_t, the frame is header.frameSize bytes in total
typedef struct md2Frame_s {
	float		scale[3];
	float		translate[3];
	char		name[16];
} md2Frame_t;

typedef char md2CheckHeaderSize[(sizeof(md2Header_t) == 68) ? 1 : -1];
typedef char md2CheckFrameSize[(sizeof(md2Frame_t) == 40) ? 1 : -1];
typedef char md2CheckTriSize[(sizeof(md2Triangle_t) == 12) ? 1 : -1];
typedef char md2CheckVertSize[(sizeof(md2Vertex_t) == 4) ? 1 : -1];

/*
========================================================================

.MDL triangle model file format (Quake 1, "IDPO" version 6)

Private structures used by the MDL loader.  Everything is little endian.
This is NOT the Half-Life "IDST" .mdl format.

File layout, in order (all of it variable length from the skins on, so
nothing after the header is at a fixed offset and nothing is aligned):

	mdlHeader_t
	numSkins  * skin       int type (0 = single, 1 = group), then either
							 skinWidth*skinHeight palette indexes, or
							 int numSkins, float times[n], n * (w*h indexes)
	numVerts  * mdlSt_t
	numTris   * mdlTriangle_t
	numFrames * frame      int type (0 = single, 1 = group), then either
							 one simple frame, or
							 int numFrames, trivertx bboxMin, bboxMax,
							 float times[n], n * simple frame
	simple frame:          trivertx bboxMin, bboxMax, char name[16],
							 numVerts * trivertx

========================================================================
*/

#define MDL_IDENT			(('O'<<24)+('P'<<16)+('D'<<8)+'I')	// "IDPO"
#define MDL_IDENT_HL		(('T'<<24)+('S'<<16)+('D'<<8)+'I')	// "IDST", Half-Life, not supported
#define MDL_VERSION			6

// sanity limits (Quake 1 itself allows 2000 verts / 2048 triangles / 256 frames)
#define MDL_MAX_VERTS		4096
#define MDL_MAX_TRIANGLES	8192
#define MDL_MAX_FRAMES		4096	// after flattening frame groups
#define MDL_MAX_SKINS		256
#define MDL_MAX_SKIN_SIZE	4096	// width / height
#define MDL_MAX_GROUP		4096	// entries in one skin / frame group
#define MDL_MAX_EXPANDED	65536	// unique (xyz, seam) pairs after splitting

// Embedded skins are palette indexed.  They are converted with the Quake palette
// (gfx/palette.lmp from pak0, it must be in the VFS) into generated images and materials.
#define MDL_GENERATE_SKINS	1
#define MDL_PALETTE_FILE	"gfx/palette.lmp"
#define MDL_MAX_USED_SKINS	16		// skins (after flattening skin groups) that get a texture
#define MDL_NEAREST_FILTER	0		// 1 for the crisp pixelated Quake look

// 0 keeps the file's triangle order (same as MD2/MD3).  Set to 1 if models
// render inside out in your tree.
#define MDL_FLIP_WINDING	0

typedef struct mdlHeader_s {
	int			ident;
	int			version;

	float		scale[3];			// pos = v * scale + translate (global, not per frame)
	float		translate[3];
	float		boundingRadius;
	float		eyePosition[3];

	int			numSkins;
	int			skinWidth;
	int			skinHeight;
	int			numVerts;
	int			numTris;
	int			numFrames;
	int			syncType;
	int			flags;
	float		size;
} mdlHeader_t;

typedef struct {
	int			onSeam;				// nonzero: back facing triangles use s + skinWidth/2
	int			s, t;				// in skin pixels
} mdlSt_t;

typedef struct {
	int			facesFront;			// zero: this triangle is a back face for texture seam purposes
	int			vertex[3];
} mdlTriangle_t;

typedef struct {
	unsigned char	v[3];
	unsigned char	normalIndex;	// index into the Quake normal table (unused, normals are derived)
} mdlVertex_t;

typedef char mdlCheckHeaderSize[(sizeof(mdlHeader_t) == 84) ? 1 : -1];
typedef char mdlCheckStSize[(sizeof(mdlSt_t) == 12) ? 1 : -1];
typedef char mdlCheckTriSize[(sizeof(mdlTriangle_t) == 16) ? 1 : -1];
typedef char mdlCheckVertSize[(sizeof(mdlVertex_t) == 4) ? 1 : -1];

#endif /* !__MODEL_MD3_H__ */
