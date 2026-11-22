// Model_skb.h
//
// Static (default pose only) loader for Star Trek Elite Force 2 / MOHAA / FAKK2
// "Skeletor" .skb skeletal models, version 4.
//

#ifndef __MODEL_SKB_H__
#define __MODEL_SKB_H__

/*
==============================================================================

SKB file format (version 4)

	skbHeader_t
	skbBoneInfo_t[numBones]			at ofsBones
	skbSurface_t + data				at ofsSurfaces, chained with ofsEnd
	skbBone_t[numBones]				at ofsBoneBaseFrame  <-- the default (bind) pose

Per surface, relative to the start of the surface header:
	skbTriangle_t[numTriangles]		at ofsTriangles
	skbVertex_t + skbWeight_t[numWeights]	repeated numVerts times (variable
									length records) at ofsVerts
	collapse map					at ofsCollapse (LOD data, not used here)

Vertex weight offsets are in the local space of the bone they reference, so the
default pose position of a vertex is
	sum( boneWeight * ( boneWorld * offset ) )

NOTE: these structs are deliberately named skb* and are not shared with
tiki_shared.h. The skelHeader_t / skelSurface_t in tiki_shared.h describe the
SKD (skeletor mesh) layout; the .skb v4 header has no lodIndex/box/morph fields
but does have ofsBoneBaseFrame, and its surface header is 96 bytes.

==============================================================================
*/

#define SKB_VERSION			4
#define SKB_MAX_BONES		128
#define SKB_MAX_WEIGHTS		32			// per vertex, sanity limit (largest seen so far is 3)
#define SKB_MAX_VERTS		65535		// per surface, sanity limit
#define SKB_NAME_LEN		64

#define SKB_QUAT_SCALE		( 1.0f / 32767.0f )
#define SKB_OFFSET_SCALE	( 1.0f / 64.0f )

// all of these are the on-disk layout, little endian
struct skbHeader_t {
	char		ident[4];				// "SKL "
	int			version;
	char		name[SKB_NAME_LEN];
	int			numSurfaces;
	int			numBones;
	int			ofsBones;				// skbBoneInfo_t[numBones]
	int			ofsSurfaces;			// first surface
	int			ofsBoneBaseFrame;		// skbBone_t[numBones], the default pose
	int			ofsEnd;					// end of file
};

struct skbBoneInfo_t {
	int			parent;					// -1 for a root, otherwise an index lower than this bone's
	int			flags;
	char		name[SKB_NAME_LEN];
};

// one bone of the base frame. Quaternion is x, y, z, w (see the note in
// LoadSKBFile about the sign of w), offset is the translation relative to the parent.
struct skbBone_t {
	short		quat[4];				// * SKB_QUAT_SCALE
	short		offset[3];				// * SKB_OFFSET_SCALE
	short		pad;
};

struct skbSurface_t {
	int			ident;
	char		name[SKB_NAME_LEN];
	int			numTriangles;
	int			numVerts;
	int			numModelVerts;			// LOD, same as numVerts in every file seen so far
	int			ofsTriangles;
	int			ofsVerts;
	int			ofsCollapse;
	int			ofsEnd;					// next surface follows
};

struct skbTriangle_t {
	int			indexes[3];
};

struct skbVertex_t {
	float		normal[3];				// bone local, rotated by the weighted bone rotations just like the offsets
	float		texCoords[2];
	int			numWeights;
	// skbWeight_t weights[numWeights] follow
};

struct skbWeight_t {
	int			boneIndex;
	float		boneWeight;
	float		offset[3];				// in the space of boneIndex
};

#endif /* !__MODEL_SKB_H__ */
