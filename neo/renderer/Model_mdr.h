// Model_mdr.h
//

#ifndef __MODEL_MDR_H__
#define __MODEL_MDR_H__

#include "idlib/math/Vector.h"

//#define	LL(x) x=LittleLong(x)
#define	LL(x) x=x

/*
==============================================================================

MDR file format

==============================================================================
*/

#define MAX_MD3PATH			64		// from quake3

/*
 * Here are the definitions for Ravensoft's model format of md4. Raven stores their
 * playermodels in .mdr files, in some games, which are pretty much like the md4
 * format implemented by ID soft. It seems like ID's original md4 stuff is not used at all.
 * MDR is being used in EliteForce, JediKnight2 and Soldiers of Fortune2 (I think).
 * So this comes in handy for anyone who wants to make it possible to load player
 * models from these games.
 * This format has bone tags, which is similar to the thing you have in md3 I suppose.
 * Raven has released their version of md3view under GPL enabling me to add support
 * to this codebase. Thanks to Steven Howes aka Skinner for helping with example
 * source code.
 *
 * - Thilo Schulz (arny@ats.s.bawue.de)
 */

#define MDR_IDENT	(('5'<<24)+('M'<<16)+('D'<<8)+'R')
#define MDR_VERSION	2
#define	MDR_MAX_BONES	128

struct mdrWeight_t {
	int			boneIndex;	// these are indexes into the boneReferences,
	float		boneWeight;	// not the global per-frame bone list
	idVec3		offset;
};

struct mdrVertex_t {
	idVec3		normal;
	idVec2		texCoords;
	int			numWeights;
	mdrWeight_t	weights[1];		// variable sized
};

struct mdrTriangle_t {
	int			indexes[3];
};

struct mdrSurface_t {
	int			ident;

	char		name[MAX_MD3PATH];	// polyset name
	char		shader[MAX_MD3PATH];
	int			shaderIndex;	// for in-game use

	int			ofsHeader;	// this will be a negative number

	int			numVerts;
	int			ofsVerts;

	int			numTriangles;
	int			ofsTriangles;

	// Bone references are a set of ints representing all the bones
	// present in any vertex weights for this surface.  This is
	// needed because a model may have surfaces that need to be
	// drawn at different sort times, and we don't want to have
	// to re-interpolate all the bones for each surface.
	int			numBoneReferences;
	int			ofsBoneReferences;

	int			ofsEnd;	// next surface follows
};

struct mdrBone_t {
	float		matrix[3][4];
};

struct mdrFrame_t {
	idVec3		bounds[2]; // bounds of all surfaces of all LOD's for this frame
	idVec3		localOrigin; // midpoint of bounds, used for sphere cull
	float		radius; // dist from localOrigin to corner
	char		name[16];
	mdrBone_t	bones[1]; // [numBones]
};

struct mdrCompBone_t {
	unsigned char Comp[24]; // MC_COMP_BYTES is in MatComp.h, but don't want to couple
};

struct mdrCompFrame_t {
	idVec3          bounds[2];		// bounds of all surfaces of all LOD's for this frame
	idVec3          localOrigin;	// midpoint of bounds, used for sphere cull
	float           radius;			// dist from localOrigin to corner
	mdrCompBone_t   bones[1];		// [numBones]
};

struct mdrLOD_t {
	int			numSurfaces;
	int			ofsSurfaces;		// first surface, others follow
	int			ofsEnd;				// next lod follows
};

struct mdrTag_t {
	int			boneIndex;
	char		name[32];
};

struct mdrHeader_t {
	int			ident;
	int			version;

	char		name[MAX_MD3PATH];	// model name

	// frames and bones are shared by all levels of detail
	int			numFrames;
	int			numBones;
	int			ofsFrames;	// mdrFrame_t[numFrames]

	// each level of detail has completely separate sets of surfaces
	int			numLODs;
	int			ofsLODs;

	int			numTags;
	int			ofsTags;

	int			ofsEnd;	// end of file
};

/*
=============================================================

UNCOMPRESSING BONES

=============================================================
*/

#define MC_BITS_X (16)
#define MC_BITS_Y (16)
#define MC_BITS_Z (16)
#define MC_BITS_VECT (16)

#define MC_SCALE_X (1.0f/64)
#define MC_SCALE_Y (1.0f/64)
#define MC_SCALE_Z (1.0f/64)

#define MC_MASK_X ((1<<(MC_BITS_X))-1)
#define MC_MASK_Y ((1<<(MC_BITS_Y))-1)
#define MC_MASK_Z ((1<<(MC_BITS_Z))-1)
#define MC_MASK_VECT ((1<<(MC_BITS_VECT))-1)

#define MC_SCALE_VECT (1.0f/(float)((1<<(MC_BITS_VECT-1))-2))

#define MC_POS_X (0)
#define MC_SHIFT_X (0)

#define MC_POS_Y ((((MC_BITS_X))/8))
#define MC_SHIFT_Y ((((MC_BITS_X)%8)))

#define MC_POS_Z ((((MC_BITS_X+MC_BITS_Y))/8))
#define MC_SHIFT_Z ((((MC_BITS_X+MC_BITS_Y)%8)))

#define MC_POS_V11 ((((MC_BITS_X+MC_BITS_Y+MC_BITS_Z))/8))
#define MC_SHIFT_V11 ((((MC_BITS_X+MC_BITS_Y+MC_BITS_Z)%8)))

#define MC_POS_V12 ((((MC_BITS_X+MC_BITS_Y+MC_BITS_Z+MC_BITS_VECT))/8))
#define MC_SHIFT_V12 ((((MC_BITS_X+MC_BITS_Y+MC_BITS_Z+MC_BITS_VECT)%8)))

#define MC_POS_V13 ((((MC_BITS_X+MC_BITS_Y+MC_BITS_Z+MC_BITS_VECT*2))/8))
#define MC_SHIFT_V13 ((((MC_BITS_X+MC_BITS_Y+MC_BITS_Z+MC_BITS_VECT*2)%8)))

#define MC_POS_V21 ((((MC_BITS_X+MC_BITS_Y+MC_BITS_Z+MC_BITS_VECT*3))/8))
#define MC_SHIFT_V21 ((((MC_BITS_X+MC_BITS_Y+MC_BITS_Z+MC_BITS_VECT*3)%8)))

#define MC_POS_V22 ((((MC_BITS_X+MC_BITS_Y+MC_BITS_Z+MC_BITS_VECT*4))/8))
#define MC_SHIFT_V22 ((((MC_BITS_X+MC_BITS_Y+MC_BITS_Z+MC_BITS_VECT*4)%8)))

#define MC_POS_V23 ((((MC_BITS_X+MC_BITS_Y+MC_BITS_Z+MC_BITS_VECT*5))/8))
#define MC_SHIFT_V23 ((((MC_BITS_X+MC_BITS_Y+MC_BITS_Z+MC_BITS_VECT*5)%8)))

#define MC_POS_V31 ((((MC_BITS_X+MC_BITS_Y+MC_BITS_Z+MC_BITS_VECT*6))/8))
#define MC_SHIFT_V31 ((((MC_BITS_X+MC_BITS_Y+MC_BITS_Z+MC_BITS_VECT*6)%8)))

#define MC_POS_V32 ((((MC_BITS_X+MC_BITS_Y+MC_BITS_Z+MC_BITS_VECT*7))/8))
#define MC_SHIFT_V32 ((((MC_BITS_X+MC_BITS_Y+MC_BITS_Z+MC_BITS_VECT*7)%8)))

#define MC_POS_V33 ((((MC_BITS_X+MC_BITS_Y+MC_BITS_Z+MC_BITS_VECT*8))/8))
#define MC_SHIFT_V33 ((((MC_BITS_X+MC_BITS_Y+MC_BITS_Z+MC_BITS_VECT*8)%8)))

ID_INLINE void MC_UnCompress(float mat[3][4], const unsigned char * comp)
{
	int val;

	val = (int)((unsigned short *)(comp))[0];
	val -= 1 << (MC_BITS_X - 1);
	mat[0][3] = ((float)(val))*MC_SCALE_X;

	val = (int)((unsigned short *)(comp))[1];
	val -= 1 << (MC_BITS_Y - 1);
	mat[1][3] = ((float)(val))*MC_SCALE_Y;

	val = (int)((unsigned short *)(comp))[2];
	val -= 1 << (MC_BITS_Z - 1);
	mat[2][3] = ((float)(val))*MC_SCALE_Z;

	val = (int)((unsigned short *)(comp))[3];
	val -= 1 << (MC_BITS_VECT - 1);
	mat[0][0] = ((float)(val))*MC_SCALE_VECT;

	val = (int)((unsigned short *)(comp))[4];
	val -= 1 << (MC_BITS_VECT - 1);
	mat[0][1] = ((float)(val))*MC_SCALE_VECT;

	val = (int)((unsigned short *)(comp))[5];
	val -= 1 << (MC_BITS_VECT - 1);
	mat[0][2] = ((float)(val))*MC_SCALE_VECT;


	val = (int)((unsigned short *)(comp))[6];
	val -= 1 << (MC_BITS_VECT - 1);
	mat[1][0] = ((float)(val))*MC_SCALE_VECT;

	val = (int)((unsigned short *)(comp))[7];
	val -= 1 << (MC_BITS_VECT - 1);
	mat[1][1] = ((float)(val))*MC_SCALE_VECT;

	val = (int)((unsigned short *)(comp))[8];
	val -= 1 << (MC_BITS_VECT - 1);
	mat[1][2] = ((float)(val))*MC_SCALE_VECT;


	val = (int)((unsigned short *)(comp))[9];
	val -= 1 << (MC_BITS_VECT - 1);
	mat[2][0] = ((float)(val))*MC_SCALE_VECT;

	val = (int)((unsigned short *)(comp))[10];
	val -= 1 << (MC_BITS_VECT - 1);
	mat[2][1] = ((float)(val))*MC_SCALE_VECT;

	val = (int)((unsigned short *)(comp))[11];
	val -= 1 << (MC_BITS_VECT - 1);
	mat[2][2] = ((float)(val))*MC_SCALE_VECT;
}

/*
==============================================================================

MDX/M (Ghoul 2) file format (SoF2/JK2/JK3) - V3 (I think)

==============================================================================
*/

#define MDXM_IDENT			(('M'<<24)+('G'<<16)+('L'<<8)+'2')
#define MDXA_IDENT			(('A'<<24)+('G'<<16)+('L'<<8)+'2')

#define MAX_TAGNAME_BYTES	32	// matches MDR, can be changed if nec.

//
// normal version numbers...
//
#define MDXM_VERSION 6
#define MDXA_VERSION 6

// (Note that since there is now a "<modelname>_info.txt" file written out by carcass any changes made in here that
//		introduce new data should also be reflected in the info-output)

// 32 bit-flags for ghoul2 bone properties...  (all undefined fields will be blank)
//
#define G2BONEFLAG_ALWAYSXFORM	0x00000001

// same thing but for surfaces...  (Carcass will only generate 1st 2 flags, others are ingame
//
#define G2SURFACEFLAG_ISBOLT		0x00000001
#define G2SURFACEFLAG_OFF			0x00000002	// saves strcmp()ing for "_off" in surface names
#define G2SURFACEFLAG_SPARE0		0x00000004	// future-expansion fields, saves invalidating models if we add more
#define G2SURFACEFLAG_SPARE1		0x00000008	//   
#define G2SURFACEFLAG_SPARE2		0x00000010	// 
#define G2SURFACEFLAG_SPARE3		0x00000020	// 
#define G2SURFACEFLAG_SPARE4		0x00000040	// 
#define G2SURFACEFLAG_SPARE5		0x00000080	// 
//
#define G2SURFACEFLAG_NODESCENDANTS 0x00000100	// ingame-stuff, never generated by Carcass....
#define G2SURFACEFLAG_GENERATED		0x00000200	//

// triangle side-ordering stuff for tags...
//
#define iG2_TRISIDE_MIDDLE				1
#define iG2_TRISIDE_LONGEST				0
#define iG2_TRISIDE_SHORTEST			2

#define fG2_BONEWEIGHT_RECIPROCAL_MULT	((float)(1.0f/1023.0f))
#define iG2_BITS_PER_BONEREF			5
#define iMAX_G2_BONEREFS_PER_SURFACE	(1<<iG2_BITS_PER_BONEREF)	// (32)
#define iMAX_G2_BONEWEIGHTS_PER_VERT	4	// can't just be blindly increased, affects cache size etc

#define iG2_BONEWEIGHT_TOPBITS_SHIFT	((iG2_BITS_PER_BONEREF * iMAX_G2_BONEWEIGHTS_PER_VERT) - 8)	// 8 bits because of 8 in the BoneWeight[] array entry
#define iG2_BONEWEIGHT_TOPBITS_AND		0x300	// 2 bits, giving 10 total, or 10 bits, for 1023/1024 above


#define sDEFAULT_GLA_NAME "*default"	// used when making special simple ghoul2 models, usually from MD3 files

//
// these structs are defined here purely because of structure dependancy order...
//
/*

struct mdxmWeight_t {
	int			boneIndex;		// these are indexes into the surface boneReferences, not the global bone index
	float		boneWeight;		// not the global per-frame bone list

	// I'm defining this '<' operator so this struct can be used with an STL <set>...
	// (note that I've defined it using '>' internally, so it sorts with higher weights being "less", for distance weight-culling
	//
	#ifdef __cplusplus
	bool operator < (const mdxmWeight_t& _X) const {return (boneWeight>_X.boneWeight);}
	#endif
};
*/

///
// GLA part 1
///
struct mdxaCompQuatBone_t {
    unsigned char Comp[14];

    // I'm defining this '<' operator so this struct can be used as an STL <map> key...
    bool operator < (const mdxaCompQuatBone_t& _X) const { return (memcmp(Comp, _X.Comp, sizeof(Comp)) < 0); }
};


//#ifndef MDXABONEDEF
typedef struct {
    float matrix[3][4];
} mdxaBone_t;
//#endif

// end GLA

// mdxHeader_t  - this contains the header for the file, with sanity checking and version checking, plus number of lod's to be expected
typedef struct mdxmHeader_t {
	// 
	// ( first 3 fields are same format as MD3/MDR so we can apply easy model-format-type checks )
	//
	int			ident;				// "IDP3" = MD3, "RDM5" = MDR, "2LGM"(GL2 Mesh) = MDX   (cruddy char order I know, but I'm following what was there in other versions)
	int			version;			// 1,2,3 etc as per format revision
	char		name[MAX_MD3PATH];	// model name (eg "models/players/marine.glm")	// note: extension supplied
	char		animName[MAX_MD3PATH];// name of animation file this mesh requires	// note: extension missing
	int			animIndex;			// filled in by game (carcass defaults it to 0)

	int			numBones;			// (for ingame version-checks only, ensure we don't ref more bones than skel file has)

	int			numLODs;
	int			ofsLODs;

	int			numSurfaces;		// now that surfaces are drawn hierarchically, we have same # per LOD
	int			ofsSurfHierarchy;

	int			ofsEnd;				// EOF, which of course gives overall file size
} mdxmHeader_t;

// for each surface (doesn't actually need a struct for this, just makes source clearer)
typedef struct {
	int offsets[1];		// variable sized (mdxmHeader_t->numSurfaces), each offset points to a mdxmSurfHierarchy_t below
} mdxmHierarchyOffsets_t;

// for each surface...	(mdxmHeader_t->numSurfaces)
// mdxmSurfHierarchy_t - contains hierarchical info for surfaces...
typedef struct {
	char		name[MAX_MD3PATH];
	unsigned int flags;
	char		shader[MAX_MD3PATH];
	int			shaderIndex;		// for in-game use (carcass defaults to 0)
	int			parentIndex;		// this points to the index in the file of the parent surface. -1 if null/root
	int			numChildren;		// number of surfaces which are children of this one
	int			childIndexes[1];	// [mdxmSurfHierarch_t->numChildren] (variable sized)
} mdxmSurfHierarchy_t;	// struct size = (int)( &((mdxmSurfHierarch_t *)0)->childIndexes[ mdxmSurfHierarch_t->numChildren ] );

// for each LOD...  (mdxmHeader_t->numLODs)
// mdxLOD_t - this contains the header for this LOD. Contains num of surfaces, offset to surfaces and offset to next LOD. Surfaces are shader sorted, so each surface = 1 shader

typedef struct {
	// (used to contain numSurface/ofsSurfaces fields, but these are same per LOD level now)
	int			ofsEnd;				// offset to next LOD
} mdxmLOD_t;


typedef struct {	// added in GLM version 3 for ingame use at Jake's request
	int offsets[1];		// variable sized (mdxmHeader_t->numSurfaces), each offset points to surfaces below
} mdxmLODSurfOffset_t;

// for each surface... (mdxmHeader_t->numSurfaces)
// mdxSurface_t - reuse of header format containing surface name, number of bones, offset to poly data and number of polys, offset to vertex information, and number of verts. NOTE offsets are relative to this header.
typedef struct {
	int			ident;				// this one field at least should be kept, since the game-engine may switch-case (but currently=0 in carcass)

	int			thisSurfaceIndex;	// 0...mdxmHeader_t->numSurfaces-1 (because of how ingame renderer works)

	int			ofsHeader;			// this will be a negative number, pointing back to main header

	int			numVerts;
	int			ofsVerts;

	int			numTriangles;
	int			ofsTriangles;

	// Bone references are a set of ints representing all the bones
	// present in any vertex weights for this surface.  This is
	// needed because a model may have surfaces that need to be
	// drawn at different sort times, and we don't want to have
	// to re-interpolate all the bones for each surface.
	//
	int			numBoneReferences;
	int			ofsBoneReferences;

	int			ofsEnd;				// next surface follows

} mdxmSurface_t;


// for each triangle...	(mdxmSurface_t->numTriangles)
// mdxTriangle_t - contains indexes into verts. One struct entry per poly.

typedef struct {
	int			indexes[3];
} mdxmTriangle_t;

// for each vert... (mdxmSurface_t->numVerts)
// mdxVertex_t - this is an array with number of verts from the surface definition as its bounds. It contains normal info, texture coors and number of weightings for this bone
// (this is now kept at 32 bytes for cache-aligning)
typedef struct {
	idVec3			normal;
	idVec3			vertCoords;

	// packed int...
	unsigned int	uiNmWeightsAndBoneIndexes;	// 32 bits.  format: 
	// 31 & 30:  0..3 (= 1..4) weight count
	// 29 & 28 (spare)
	//	2 bit pairs at 20,22,24,26 are 2-bit overflows from 4 BonWeights below (20=[0], 22=[1]) etc)
	//  5-bits each (iG2_BITS_PER_BONEREF) for boneweights
	// effectively a packed int, each bone weight converted from 0..1 float to 0..255 int...
	//  promote each entry to float and multiply by fG2_BONEWEIGHT_RECIPROCAL_MULT to convert.
	byte			BoneWeightings[iMAX_G2_BONEWEIGHTS_PER_VERT];	// 4
} mdxmVertex_t;

//
// GLA stuff - part 2
//
 
// 
// mdxaHeader_t  - this contains the header for the file, with sanity checking and version checking, plus number of lod's to be expected
//
typedef struct mdxaHeader_t {
    // 
    // ( first 3 fields are same format as MD3/MDR so we can apply easy model-format-type checks )
    //
    int			ident;				// 	"IDP3" = MD3, "RDM5" = MDR, "2LGA"(GL2 Anim) = MDXA
    int			version;			// 1,2,3 etc as per format revision
    //
    char		name[MAX_MD3PATH];	// GLA name (eg "skeletons/marine")	// note: extension missing
    float		fScale;				// will be zero if build before this field was defined, else scale it was built with

    // frames and bones are shared by all levels of detail
    //
    int			numFrames;
    int			ofsFrames;			// points at mdxaFrame_t array
    int			numBones;			// (no offset to these since they're inside the frames array)
    int			ofsCompBonePool;	// offset to global compressed-bone pool that all frames use
    int			ofsSkel;			// offset to mdxaSkel_t info

    int			ofsEnd;				// EOF, which of course gives overall file size
} mdxaHeader_t;

// for each bone... (doesn't actually need a struct for this, just makes source clearer)
// {
typedef struct
{
    int offsets[1];		// variable sized (mdxaHeader_t->numBones), each offset points to an mdxaSkel_t below
} mdxaSkelOffsets_t;
// }

// for each bone...	 (mdxaHeader_t->numBones)       
// {
        // mdxaSkel_t - contains hierarchical info only...

typedef struct {
    char		name[MAX_MD3PATH];	// name of bone
    unsigned int flags;
    int			parent;				// index of bone that is parent to this one, -1 = NULL/root
    mdxaBone_t	BasePoseMat;		// base pose
    mdxaBone_t	BasePoseMatInv;		// inverse, to save run-time calc
    int			numChildren;		// number of children bones
    int			children[1];		// [mdxaSkel_t->numChildren] (variable sized)
} mdxaSkel_t;	// struct size = (int)( &((mdxaSkel_t *)0)->children[ mdxaSkel_t->numChildren ] );
// }


    // (offset @ mdxaHeader_t->ofsFrames)
    //
    //  array of 3 byte indices here (hey, 25% saving over 4-byte really adds up)...
    //
    //
    // access as follows to get the index for a given <iFrameNum, iBoneNum>
    //
    // (iFrameNum * mdxaHeader_t->numBones * 3) + (iBoneNum * 3)
    //
    //  then read the int at that location and AND it with 0x00FFFFFF. I use the struct below simply for easy searches
typedef struct
{
    byte iIndex[3];
   // int iIndex;	// this struct for pointing purposes, need to and with 0x00FFFFFF to be meaningful
} mdxaIndex_t;
//
// (note that there's then an alignement-pad here to get the next struct back onto 32-bit alignement)
//
// this index then points into the following...


// Compressed-bone pool that all frames use  (mdxaHeader_t->ofsCompBonePool)  (defined at end because size unknown until end)
// for each bone in pool (unknown number, no actual total stored at the moment)... 
// {
        // mdxaCompBone_t  (defined at file top because of struct dependancy)
// }



// these are convenience functions that I can invoked in code. Do NOT change them (because this is a shared file), 
//	but if you want to copy the logic out and use your own versions then fine...
//
static inline int G2_GetVertWeights(const mdxmVertex_t* pVert)
{
	int iNumWeights = (pVert->uiNmWeightsAndBoneIndexes >> 30) + 1;	// 1..4 count 

	return iNumWeights;
}

static inline int G2_GetVertBoneIndex(const mdxmVertex_t* pVert, const int iWeightNum)
{
	int iBoneIndex = (pVert->uiNmWeightsAndBoneIndexes >> (iG2_BITS_PER_BONEREF * iWeightNum)) & ((1 << iG2_BITS_PER_BONEREF) - 1);

	return iBoneIndex;
}

static inline float G2_GetVertBoneWeight(const mdxmVertex_t* pVert, const int iWeightNum, float& fTotalWeight, int iNumWeights)
{
	float fBoneWeight;

	if (iWeightNum == iNumWeights - 1) {
		fBoneWeight = 1.0f - fTotalWeight;
	} else {
		int iTemp = pVert->BoneWeightings[iWeightNum];
		iTemp |= (pVert->uiNmWeightsAndBoneIndexes >> (iG2_BONEWEIGHT_TOPBITS_SHIFT + (iWeightNum * 2))) & iG2_BONEWEIGHT_TOPBITS_AND;

		fBoneWeight = fG2_BONEWEIGHT_RECIPROCAL_MULT * iTemp;
		fTotalWeight += fBoneWeight;
	}

	return fBoneWeight;
}														 
// for each vert... (mdxmSurface_t->numVerts)  (seperated from mdxmVertex_t struct for cache reasons)
// mdxVertex_t - this is an array with number of verts from the surface definition as its bounds. It contains normal info, texture coors and number of weightings for this bone

typedef struct {
	idVec2			texCoords;
} mdxmVertexTexCoord_t;






struct GLAStoredBasePoseJoint {
    const char* name;
    int parent;
    int flags;
    float basePoseMat[12];
    float basePoseMatInv[12];
};

static const int GLA_STORED_BASEPOSE_NUM_JOINTS = 53;
static const int GLA_STORED_BASEPOSE_NUM_FRAMES = 676;
static const float GLA_STORED_BASEPOSE_SCALE = 0.639999986f;

static const GLAStoredBasePoseJoint GLA_STORED_BASEPOSE_JOINTSD3[GLA_STORED_BASEPOSE_NUM_JOINTS] = {
    // 0: model_root
    {
        "model_root", -1, 0,
        { -0.0f, 0.639999986f, 0.0f, -0.0f, -0.639999986f, 0.0f, 0.639999986f, 0.0f, -0.0f, 0.0f, 0.0f, 0.0f },
        { -0.0f, 1.5625f, 0.0f, -0.0f, -1.5625f, 0.0f, 1.5625f, 0.0f, -0.0f, 0.0f, 0.0f, 0.0f }
    },
    // 1: pelvis
    {
        "pelvis", 0, 0,
        { -0.0f, -2.79752879e-08f, 2.79752879e-08f, 8.31999932e-06f, 2.79752879e-08f, 0.639999986f, -2.79752879e-08f, 0.385154575f, -0.639999986f, 2.79752879e-08f, 0.639999986f, 35.5866013f },
        { -1.5625f, -6.82990446e-08f, 1.5625f, 55.6040649f, 6.82990446e-08f, -7.73509861e-23f, -6.82990446e-08f, 1.05957733e-05f, -6.82990446e-08f, 1.5625f, 6.82990446e-08f, -0.601804018f }
    },
    // 2: Motion
    {
        "Motion", 1, 1,
        { -0.0f, 0.639999986f, 0.0f, -0.0f, -0.639999986f, 0.0f, 0.639999986f, 0.385154575f, -0.0f, 0.0f, 0.0f, 36.4193802f },
        { -0.0f, 1.5625f, 0.0f, -0.0f, -1.5625f, 0.0f, 1.5625f, -0.601804018f, -0.0f, 0.0f, 0.0f, -56.9052849f }
    },
    // 3: lfemurYZ
    {
        "lfemurYZ", 1, 0,
        { -0.0444516689f, 0.165308878f, 0.00962380413f, -3.61215162f, 0.0019474437f, 0.618207395f, -0.0019474437f, -0.206205443f, 0.638451457f, 0.00962380413f, -0.616682291f, 35.5866013f },
        { -1.50929534f, 0.40358609f, -1.50557196f, -52.1427498f, 0.00475449953f, 0.108524583f, -0.00475449953f, -19.8383636f, -0.404241502f, -1.50557196f, 0.0234956108f, 3.36793184f }
    },
    // 4: lfemurX
    {
        "lfemurX", 3, 0,
        { -0.0444516689f, 0.165308878f, 0.00962373056f, -5.51320362f, 0.00194751972f, 0.618207395f, -0.00194751972f, 0.304988801f, 0.638451457f, 0.00962373056f, -0.616682291f, 28.4947548f },
        { -1.50929534f, 0.40358609f, -1.50557196f, -40.6427536f, 0.00475468859f, 0.108524598f, -0.00475468859f, -19.8383656f, -0.404241502f, -1.50557196f, 0.023495432f, 3.36793447f }
    },
    // 5: ltibia
    {
        "ltibia", 3, 0,
        { -0.0447542369f, 0.165304273f, 0.00970191136f, -7.41425514f, 0.00194768352f, 0.618207335f, -0.00194768352f, 0.816182375f, 0.638430297f, 0.00970191136f, -0.616661608f, 21.4029102f },
        { -1.50929546f, 0.403574914f, -1.50552154f, -29.1411552f, 0.00475508859f, 0.109263271f, -0.00475508859f, -19.8383636f, -0.404241472f, -1.50552154f, 0.0236863066f, 3.38174963f }
    },
    // 6: ltalus
    {
        "ltalus", 5, 0,
        { 0.509034753f, 0.0988667756f, -0.132834643f, -11.1501312f, 0.00194470456f, 0.618207335f, -0.00194470456f, 1.82762814f, 0.38791728f, -0.132834643f, -0.375111967f, 7.46635723f },
        { -1.50929523f, 0.241373882f, -0.915800571f, -6.41764784f, 0.00474782381f, -1.24276054f, -0.00474782381f, -19.838377f, -0.40424186f, -0.915800571f, -0.324303329f, -3.61055827f }
    },
    // 7: rfemurYZ
    {
        "rfemurYZ", 1, 0,
        { -0.0446015969f, -0.165307105f, -0.0096628163f, 3.61216831f, -0.00194727979f, 0.618207216f, 0.00194727979f, -0.206205443f, 0.638440967f, -0.0096628163f, -0.61667192f, 35.5866013f },
        { -1.50929511f, -0.403581828f, -1.50554669f, -52.1419373f, -0.00475410512f, 0.108890638f, 0.00475410512f, 19.8384361f, 0.404242784f, -1.50554669f, -0.0235908665f, 3.38017082f }
    },
    // 8: rfemurX
    {
        "rfemurX", 7, 0,
        { -0.0446015969f, -0.165307105f, -0.00966274273f, 5.51319933f, -0.00194735592f, 0.618207216f, 0.00194735592f, 0.306712955f, 0.638440967f, -0.00966274273f, -0.61667192f, 28.494875f },
        { -1.50929511f, -0.403581798f, -1.50554669f, -40.6419373f, -0.00475429185f, 0.108890638f, 0.00475429185f, 19.8384342f, 0.404242784f, -1.50554669f, -0.0235906839f, 3.38017297f }
    },
    // 9: rtibia
    {
        "rtibia", 7, 0,
        { -0.0446015969f, -0.165307105f, -0.00966252107f, 7.41423082f, -0.00194758421f, 0.618207216f, 0.00194758421f, 0.819631338f, 0.638440967f, -0.00966252107f, -0.61667192f, 21.4031467f },
        { -1.50929511f, -0.403581828f, -1.50554669f, -29.1419373f, -0.00475484831f, 0.108890623f, 0.00475484831f, 19.8384323f, 0.404242724f, -1.50554669f, -0.0235901475f, 3.38018012f }
    },
    // 10: rtalus
    {
        "rtalus", 9, 0,
        { 0.509126365f, -0.0988308042f, 0.13286157f, 11.1501703f, -0.00195072498f, 0.618207335f, 0.00195072498f, 1.82762754f, 0.387797028f, 0.13286157f, -0.37499705f, 7.46636152f },
        { -1.50929511f, -0.241286144f, -0.915520191f, -6.41693544f, -0.00476249866f, -1.24298441f, 0.00476249866f, 19.8384094f, 0.404241979f, -0.915520191f, 0.324369073f, -3.61195326f }
    },
    // 11: lower_lumbar
    {
        "lower_lumbar", 1, 0,
        { -0.0f, -2.79752879e-08f, 2.79752879e-08f, 8.31999932e-06f, 2.79752879e-08f, 0.639999986f, -2.79752879e-08f, 0.385154575f, -0.639999986f, 2.79752879e-08f, 0.639999986f, 41.3465996f },
        { -1.5625f, -6.82990517e-08f, 1.56250012f, 64.6040649f, 6.82990446e-08f, -7.73509924e-23f, -6.82990446e-08f, 1.02023705e-05f, -6.82990589e-08f, 1.56250012f, 6.82990446e-08f, -0.601804018f }
    },
    // 12: upper_lumbar
    {
        "upper_lumbar", 11, 0,
        { -0.0f, -2.79752879e-08f, 2.79752879e-08f, 8.31999932e-06f, 2.79752879e-08f, 0.639999986f, -2.79752879e-08f, 0.385154575f, -0.639999986f, 2.79752879e-08f, 0.639999986f, 46.4666023f },
        { -1.5625f, -6.82990446e-08f, 1.5625f, 72.6040649f, 6.82990446e-08f, -7.73509861e-23f, -6.82990446e-08f, 9.8526798e-06f, -6.82990446e-08f, 1.5625f, 6.82990446e-08f, -0.601804018f }
    },
    // 13: thoracic
    {
        "thoracic", 12, 0,
        { -0.0f, -2.79752879e-08f, 2.79752879e-08f, 8.31999932e-06f, 2.79752879e-08f, 0.639999986f, -2.79752879e-08f, 0.385154575f, -0.639999986f, 2.79752879e-08f, 0.639999986f, 51.5866013f },
        { -1.5625f, -6.82990446e-08f, 1.5625f, 80.6040649f, 6.82990446e-08f, -7.73509861e-23f, -6.82990446e-08f, 9.50298909e-06f, -6.82990446e-08f, 1.5625f, 6.82990446e-08f, -0.601804018f }
    },
    // 14: cervical
    {
        "cervical", 13, 0,
        { -0.0f, -2.79752879e-08f, 2.79752879e-08f, 8.31999932e-06f, 2.79752879e-08f, 0.639999986f, -2.79752879e-08f, 0.385154575f, -0.639999986f, 2.79752879e-08f, 0.639999986f, 56.7066002f },
        { -1.5625f, -6.82990375e-08f, 1.56249988f, 88.6040573f, 6.82990446e-08f, -7.73509861e-23f, -6.82990446e-08f, 9.15329747e-06f, -6.82990446e-08f, 1.56249988f, 6.82990446e-08f, -0.601804018f }
    },
    // 15: cranium
    {
        "cranium", 14, 0,
        { -0.0f, 0.0111695249f, 2.79710264e-08f, 8.31999932e-06f, 2.79752879e-08f, 0.639902532f, -2.79752879e-08f, 0.385154575f, -0.639999986f, 2.79710264e-08f, 0.639902532f, 59.5994034f },
        { -1.56226206f, 0.0272693485f, 1.56226206f, 93.1098862f, 6.82990375e-08f, 1.30519095e-16f, -6.82990375e-08f, 1.62524974f, 0.0272693448f, 1.56226206f, 6.82886352e-08f, -0.601803958f }
    },
    // 16: ceyebrow - actual parent 52
    {
        "ceyebrow", 15, 0,
        { 0.671975553f, -7.87042609e-09f, 1.03615605e-09f, -0.0406073593f, 7.87042609e-09f, 0.695679963f, -7.87042609e-09f, -3.5395565f, -0.180054352f, 1.03615605e-09f, 0.180054352f, 63.1472435f },
        { -1.43744254f, -1.62621969e-08f, 0.372035652f, 28.407568f, 1.62621969e-08f, -1.38846326f, -1.62621969e-08f, -0.0583709404f, -2.14094742e-09f, 0.372035652f, 2.14094809e-09f, -86.3607864f }
    },
    // 17: jaw - actual parent 52
    {
        "jaw", 15, 0,
        { 0.593453109f, -1.88969949e-07f, -5.32140731e-08f, -0.0405951962f, 1.88969949e-07f, 0.695679963f, -1.88969949e-07f, -0.529887974f, 0.363020778f, -5.32140731e-08f, -0.363020778f, 60.7829056f },
        { -1.43744254f, -3.90457473e-07f, -0.750088394f, -44.942791f, 3.90457444e-07f, -1.22621703f, -3.90457444e-07f, -0.0583467819f, 1.09953085e-07f, -0.750088394f, -1.09953085e-07f, -74.9304962f }
    },
    // 18: lblip2 - actual parent 52
    {
        "lblip2", 15, 0,
        { -0.402593493f, 0.564284682f, -0.00250827405f, -0.569948792f, 0.557704747f, 0.406874835f, -0.557704747f, -3.87403369f, -0.104184978f, -0.00250827405f, 0.0589227341f, 60.0335503f },
        { -0.840701461f, 1.16594827f, 0.121748574f, 4.75089455f, 1.15235245f, 0.831855237f, -1.15235245f, 5.71924305f, 0.17761156f, 0.121748574f, -0.00518269604f, 86.1579285f }
    },
    // 19: leye - actual parent 52
    {
        "leye", 15, 0,
        { 0.695679963f, 1.32922562e-15f, -6.08182802e-08f, -1.13838911f, -1.32922562e-15f, -0.695679963f, 1.32922562e-15f, -2.84735417f, 3.04091401e-08f, -6.08182802e-08f, 3.04091401e-08f, 62.5774155f },
        { 1.43744254f, 2.58276185e-15f, 6.09596498e-08f, 4.09291172f, -2.66603175e-15f, -1.43744254f, 2.66603175e-15f, 1.63636124f, -1.23824293e-07f, 6.09596498e-08f, -1.25665238e-07f, 89.9514389f }
    },
    // 20: rblip2 - actual parent 52
    {
        "rblip2", 15, 0,
        { -0.402593881f, -0.564284444f, 0.00250844145f, 0.488737285f, -0.557704508f, 0.406875193f, 0.557704508f, -3.87403369f, -0.104185067f, 0.00250844145f, 0.0589227043f, 60.0335503f },
        { -0.840702236f, -1.16594756f, 0.121748522f, 4.65619946f, -1.15235186f, 0.831855953f, 1.15235186f, -5.78753614f, -0.177611828f, 0.121748522f, 0.00518303737f, 86.1575089f }
    },
    // 21: ltlip2 - actual parent 52
    {
        "ltlip2", 15, 0,
        { -0.401934117f, 0.563360214f, -0.10758473f, -0.569948792f, 0.567055941f, 0.39372769f, -0.567055941f, -3.87403369f, -0.0294472016f, -0.10758473f, -0.0710274652f, 60.1697044f },
        { -0.813536108f, 1.16403782f, -0.146759838f, -11.3844118f, 1.17167425f, 0.830492854f, -1.17167425f, 5.6896944f, 0.177705273f, -0.146759838f, -0.222295955f, 85.7339478f }
    },
    // 22: rtlip2 - actual parent 52
    {
        "rtlip2", 15, 0,
        { -0.401934415f, -0.563359976f, 0.107584678f, 0.488737285f, -0.567055702f, 0.393727988f, 0.567055702f, -3.87403369f, -0.0294471588f, 0.107584678f, -0.0710273907f, 60.1697044f },
        { -0.813537002f, -1.16403747f, -0.146759734f, -11.47894f, -1.17167401f, 0.83049345f, 1.17167401f, -5.75576162f, -0.177705243f, -0.146759734f, 0.222295865f, 85.7158966f }
    },
    // 23: reye - actual parent 52
    {
        "reye", 15, 0,
        { 0.695679963f, 1.32922562e-15f, -6.08182802e-08f, 1.1385715f, -1.32922562e-15f, -0.695679963f, 1.32922562e-15f, -2.84735346f, 3.04091401e-08f, -6.08182802e-08f, 3.04091401e-08f, 62.5774155f },
        { 1.43744254f, 2.58276185e-15f, 6.09596498e-08f, 4.09291077f, -2.74930102e-15f, -1.43744254f, 2.74930102e-15f, -1.636639f, -1.25729272e-07f, 6.09596498e-08f, -1.25665238e-07f, 89.9514389f }
    },
    // 24: rclavical
    {
        "rclavical", 13, 0,
        { -0.0802132487f, -0.63495338f, 0.0802132487f, 8.31999932e-06f, -2.77546945e-08f, 3.50623242e-09f, 2.77546945e-08f, 0.385154575f, -0.63495338f, 0.0802132487f, 0.0f, 54.5089493f },
        { -8.56013926e-09f, -1.55017936f, 0.0f, 0.0754389316f, -6.77604888e-08f, 0.195833147f, 6.77604888e-08f, -85.1702347f, -1.5625f, 0.0f, 0.195833147f, -0.597053349f }
    },
    // 25: rhumerus
    {
        "rhumerus", 13, 0,
        { 0.011188359f, -0.427266389f, -0.0167533029f, 5.92285395f, 0.0124697136f, -0.47619614f, -0.0124697136f, 1.13338363f, -0.639780641f, -0.0167533029f, -0.476359367f, 54.5089493f },
        { 1.16258812f, -1.04313076f, -1.16298676f, -57.2458344f, 0.0304436404f, -0.0273153316f, -0.0304436404f, -63.7307968f, -1.0434885f, -1.16298676f, -0.0409016199f, -2.01256728f }
    },
    // 26: rhumerusX
    {
        "rhumerusX", 25, 0,
        { 0.011188359f, -0.427266389f, -0.0167532451f, 9.55461693f, 0.0124696372f, -0.47619614f, -0.0124696372f, 1.03828287f, -0.639780641f, -0.0167532451f, -0.476359367f, 50.4598923f },
        { 1.16258812f, -1.04313076f, -1.16298676f, -48.7458305f, 0.0304434542f, -0.0273153316f, -0.0304434542f, -63.7307892f, -1.0434885f, -1.16298676f, -0.0409014858f, -2.01255989f }
    },
    // 27: rradius
    {
        "rradius", 25, 0,
        { 0.0463275202f, -0.425700217f, -0.0402062982f, 13.1863813f, 0.0124696381f, -0.47619608f, -0.0124696381f, 0.943181455f, -0.63819921f, -0.0402062982f, -0.475639611f, 46.4108391f },
        { 1.16258824f, -1.03930736f, -1.16122961f, -40.2956161f, 0.0304434542f, -0.113104306f, -0.0304434542f, -63.7308006f, -1.0434885f, -1.16122961f, -0.0981599241f, 0.201997235f }
    },
    // 28: rradiusX
    {
        "rradiusX", 27, 0,
        { 0.0463275202f, -0.425700217f, -0.0402062982f, 16.093914f, 0.0124696381f, -0.47619608f, -0.0124696381f, 0.626764774f, -0.63819921f, -0.0402062982f, -0.475639611f, 43.16222f },
        { 1.16258824f, -1.03930736f, -1.16122961f, -33.4656143f, 0.0304434542f, -0.113104306f, -0.0304434542f, -63.730793f, -1.0434885f, -1.16122961f, -0.0981599241f, 0.201996699f }
    },
    // 29: rhand
    {
        "rhand", 27, 0,
        { 0.104581289f, -0.373349518f, -0.0801192522f, 19.0014458f, 0.0224098843f, -0.513605893f, -0.0224098843f, 0.310347497f, -0.630999625f, -0.0801192522f, -0.509188473f, 39.9136009f },
        { 1.25392079f, -0.911497891f, -1.24313581f, -32.3774872f, 0.0547116548f, -0.255325377f, -0.0547116548f, -60.9546242f, -0.930642605f, -1.24313581f, -0.195603654f, 2.70958233f }
    },
    // 30: r_d1_j1
    {
        "r_d1_j1", 27, 0,
        { 0.300884515f, 0.0453159809f, 0.382409394f, 19.1050816f, -0.355222553f, 0.511184394f, 0.355222553f, -0.639149427f, 0.439187258f, 0.382409394f, -0.563040793f, 39.2239494f },
        { -1.24800885f, 0.110634811f, -1.3746115f, -55.5618668f, -0.867242515f, -0.734581411f, 0.867242515f, 38.635994f, 0.3630023f, -1.3746115f, 0.933616817f, -8.27091312f }
    },
    // 31: r_d1_j2
    {
        "r_d1_j2", 27, 0,
        { 0.0934876353f, -0.129667133f, 0.081731759f, 18.9963226f, -0.0633519143f, 0.621374547f, 0.0633519143f, -1.36127222f, 0.629957616f, 0.081731759f, -0.619714856f, 37.872654f },
        { -1.51702774f, -0.316570163f, -1.51297581f, -50.9760437f, -0.154667765f, -0.228241295f, 0.154667765f, 41.9336243f, 0.340750545f, -1.51297581f, 0.199540421f, -5.50882578f }
    },
    // 32: r_d2_j1
    {
        "r_d2_j1", 27, 0,
        { 0.105187587f, 0.100812674f, -0.625318766f, 20.9897461f, 0.621957183f, 0.0917238668f, -0.621957183f, -0.944297612f, 0.10818892f, -0.625318766f, -0.623195291f, 37.370018f },
        { -0.223935217f, 0.2461247f, -1.52147293f, -61.7810669f, 1.51845014f, -0.256805688f, -1.51845014f, -7.66503525f, -0.292520851f, -1.52147293f, -1.5266571f, -24.7305832f }
    },
    // 33: r_d2_j2
    {
        "r_d2_j2", 27, 0,
        { 0.090051353f, 0.22131215f, -0.59369266f, 20.7830811f, 0.620583177f, 0.0902770311f, -0.620583177f, -1.15993214f, 0.127934664f, -0.59369266f, -0.593726933f, 36.0924683f },
        { -0.220402941f, 0.540312946f, -1.44952869f, -63.29142f, 1.51509571f, -0.219851941f, -1.51509571f, -8.43587208f, -0.311951935f, -1.44952869f, -1.44944501f, -12.6959381f }
    },
    // 34: r_d4_j1
    {
        "r_d4_j1", 27, 0,
        { 0.0272114016f, 0.215767592f, -0.601832092f, 21.02948f, 0.639074147f, -0.0290264282f, -0.639074147f, 0.38052544f, -0.0210668445f, -0.601832092f, -0.60191685f, 37.1531944f },
        { 0.0708653033f, 0.526776314f, -1.46952355f, -65.7006073f, 1.56023943f, -0.0664340779f, -1.56023943f, -2.57335711f, -0.045132257f, -1.46952355f, -1.46931636f, -11.2634764f }
    },
    // 35: r_d4_j2
    {
        "r_d4_j2", 27, 0,
        { 0.051236704f, 0.519697726f, -0.371787131f, 20.6022549f, 0.637866855f, -0.0358964056f, -0.637866855f, 0.326646388f, 0.010033899f, -0.371787131f, -0.369985163f, 35.96138f },
        { 0.0876377225f, 1.26879346f, -0.903284132f, -58.6642113f, 1.5572921f, -0.125089616f, -1.5572921f, -4.62539387f, -0.092558831f, -0.903284132f, -0.907683492f, 27.0354652f }
    },
    // 36: rhang_tag_bone
    {
        "rhang_tag_bone", 27, 0,
        { 0.0779962316f, 0.584731877f, -0.0407851338f, 20.0762711f, -0.0774149969f, 0.256953716f, 0.0774149969f, -0.109498881f, 0.630494654f, -0.0407851338f, -0.248204023f, 36.9480553f },
        { -0.627328455f, 1.42756796f, -0.605966806f, -51.0286865f, -0.189001456f, -0.190420479f, 0.189001456f, -39.795723f, -1.41850054f, -0.605966806f, -0.0995730832f, -11.3725471f }
    },
    // 37: lclavical
    {
        "lclavical", 13, 0,
        { -0.0802132711f, 0.63495338f, -0.0802132711f, 8.31999932e-06f, 2.77546945e-08f, 3.5062333e-09f, -2.77546945e-08f, 0.385154575f, -0.63495338f, -0.0802132711f, 0.0f, 54.5089493f },
        { -8.56014104e-09f, 1.55017924f, 0.0f, 0.0754131451f, 6.77604888e-08f, 0.195833191f, -6.77604888e-08f, 85.1702347f, 1.5625f, 0.0f, -0.195833206f, -0.597056508f }
    },
    // 38: lhumerus
    {
        "lhumerus", 13, 0,
        { 0.011188359f, 0.42726633f, 0.01675326f, -5.92283678f, -0.0124696577f, -0.47619614f, 0.0124696577f, 1.13338363f, -0.639780641f, 0.01675326f, -0.476359367f, 54.5089493f },
        { 1.16258836f, 1.04313076f, -1.16298687f, -57.2458572f, -0.0304435063f, -0.0273153316f, 0.0304435063f, 63.7307739f, 1.04348838f, -1.16298687f, 0.0409015231f, -2.01256061f }
    },
    // 39: lhumerusX
    {
        "lhumerusX", 38, 0,
        { 0.011188359f, 0.42726633f, 0.0167532042f, -9.55460167f, -0.0124695813f, -0.47619614f, 0.0124695813f, 1.03828287f, -0.639780641f, 0.0167532042f, -0.476359367f, 50.4598923f },
        { 1.16258836f, 1.04313076f, -1.16298687f, -48.7458534f, -0.0304433201f, -0.0273153316f, 0.0304433201f, 63.7307739f, 1.04348838f, -1.16298687f, 0.0409013815f, -2.01255369f }
    },
    // 40: lradius
    {
        "lradius", 38, 0,
        { 0.0463275202f, 0.425700128f, 0.0402062498f, -13.1863661f, -0.0124695823f, -0.47619617f, 0.0124695823f, 0.943181455f, -0.63819921f, 0.0402062498f, -0.475639671f, 46.4108391f },
        { 1.16258848f, 1.03930712f, -1.16122985f, -40.2956467f, -0.0304433089f, -0.113104299f, 0.0304433089f, 63.7307739f, 1.04348838f, -1.16122985f, 0.0981598198f, 0.20200561f }
    },
    // 41: lradiusX
    {
        "lradiusX", 40, 0,
        { 0.0463275202f, 0.425700128f, 0.0402062498f, -16.0938969f, -0.0124695823f, -0.47619617f, 0.0124695823f, 0.626764774f, -0.63819921f, 0.0402062498f, -0.475639671f, 43.16222f },
        { 1.16258848f, 1.03930712f, -1.16122985f, -33.4656448f, -0.0304433089f, -0.113104299f, 0.0304433089f, 63.7307663f, 1.04348838f, -1.16122985f, 0.0981598198f, 0.202005073f }
    },
    // 42: lhand
    {
        "lhand", 40, 0,
        { 0.10458117f, 0.373349756f, 0.0801191181f, -19.0014305f, -0.0224097557f, -0.513605773f, 0.0224097557f, 0.310347497f, -0.630999684f, 0.0801191181f, -0.509188235f, 39.9136009f },
        { 1.2539202f, 0.911498487f, -1.24313557f, -32.3774796f, -0.0547112972f, -0.255325168f, 0.0547112972f, 60.9546204f, 0.93064326f, -1.24313557f, 0.195603311f, 2.70958805f }
    },
    // 43: l_d4_j1
    {
        "l_d4_j1", 40, 0,
        { 0.0272120275f, -0.215766922f, 0.601832271f, -21.02948f, -0.639074087f, -0.0290261619f, 0.639074087f, 0.38052544f, -0.0210663546f, 0.601832271f, -0.601917088f, 37.1531944f },
        { 0.0708646551f, -0.526774704f, -1.46952403f, -65.700592f, -1.56023967f, -0.0664356202f, 1.56023967f, 2.57341504f, 0.045134183f, -1.46952403f, 1.4693172f, -11.2635508f }
    },
    // 44: l_d4_j2
    {
        "l_d4_j2", 40, 0,
        { 0.051236704f, -0.519697666f, 0.37178725f, -20.6022549f, -0.637866855f, -0.0358963497f, 0.637866855f, 0.326645106f, 0.0100339772f, 0.37178725f, -0.369985253f, 35.9613838f },
        { 0.0876375735f, -1.26879323f, -0.903284431f, -58.6642189f, -1.55729198f, -0.125089601f, 1.55729198f, 4.62540007f, 0.0925590098f, -0.903284431f, 0.90768373f, 27.0354595f }
    },
    // 45: l_d2_j1
    {
        "l_d2_j1", 40, 0,
        { 0.105187699f, -0.10081213f, 0.625318825f, -20.9897461f, -0.621957183f, 0.0917238146f, 0.621957183f, -0.944297612f, 0.108188778f, 0.625318825f, -0.62319535f, 37.370018f },
        { -0.223935097f, -0.246123359f, -1.52147305f, -61.7810364f, -1.51845014f, -0.256805897f, 1.51845014f, 7.66503763f, 0.292520881f, -1.52147305f, 1.52665734f, -24.7306423f }
    },
    // 46: l_d2_j2
    {
        "l_d2_j2", 40, 0,
        { 0.0900514275f, -0.221311942f, 0.59369266f, -20.7830811f, -0.620583117f, 0.0902770907f, 0.620583117f, -1.15993214f, 0.127934694f, 0.59369266f, -0.593726993f, 36.0924683f },
        { -0.22040309f, -0.540312469f, -1.44952893f, -63.29142f, -1.51509571f, -0.219852149f, 1.51509571f, 8.43587303f, 0.311952084f, -1.44952893f, 1.44944537f, -12.695962f }
    },
    // 47: l_d1_j1
    {
        "l_d1_j1", 40, 0,
        { 0.300884604f, -0.0453164317f, -0.382409275f, -19.1050816f, 0.355222613f, 0.511184394f, -0.355222613f, -0.639149427f, 0.439187109f, -0.382409275f, -0.563040733f, 39.2239494f },
        { -1.24800909f, -0.110635862f, -1.37461126f, -55.5618782f, 0.867242694f, -0.73458159f, -0.867242694f, -38.6359673f, -0.363001585f, -1.37461126f, -0.933616638f, -8.27095032f }
    },
    // 48: l_d1_j2
    {
        "l_d1_j2", 40, 0,
        { 0.0934876204f, 0.129667073f, -0.0817318037f, -18.9963226f, 0.0633519664f, 0.621374547f, -0.0633519664f, -1.36127281f, 0.629957616f, -0.0817318037f, -0.619714856f, 37.872654f },
        { -1.51702774f, 0.316570044f, -1.51297581f, -50.9760437f, 0.154667914f, -0.22824128f, -0.154667914f, -41.9336205f, -0.340750426f, -1.51297581f, -0.199540555f, -5.50882244f }
    },
    // 49: ltail
    {
        "ltail", 3, 0,
        { -0.102067687f, 0.163758755f, -0.0245495532f, -2.53037882f, -0.00194745278f, -0.618207395f, 0.00194745278f, 5.16943836f, -0.631805658f, -0.0245495532f, -0.610217392f, 38.4449387f },
        { 1.50929546f, 0.399801642f, -1.48978853f, -54.9750137f, -0.00475452282f, 0.249188691f, 0.00475452282f, 19.3355427f, 0.404241264f, -1.48978853f, -0.0599354245f, -17.1227741f }
    },
    // 50: rtail
    {
        "rtail", 7, 0,
        { -0.102216057f, -0.163753599f, 0.0245882776f, 2.53021049f, 0.00194728351f, -0.618207216f, -0.00194728351f, 5.16943836f, -0.631781638f, 0.0245882776f, -0.610193908f, 38.4456024f },
        { 1.50929499f, -0.399789035f, -1.48973131f, -54.9720268f, 0.00475411117f, 0.249550939f, -0.00475411117f, -19.335619f, -0.404242903f, -1.48973131f, 0.0600299649f, -17.1358471f }
    },
    // 51: lhang_tag_bone
    {
        "lhang_tag_bone", 40, 0,
        { -0.0779961273f, 0.584731758f, 0.0407850295f, -20.0762768f, -0.0774149224f, -0.256953925f, 0.0774149224f, -0.109498881f, 0.630494654f, 0.0407850295f, 0.248204246f, 36.9480553f },
        { 0.627328992f, 1.42756784f, 0.605967402f, 51.0287132f, -0.189001292f, 0.19042024f, 0.189001292f, -39.7957039f, -1.4185003f, 0.605967402f, 0.0995728374f, -11.3725348f }
    },
    // 52: face
    {
        "face", 15, 1,
        { -0.0f, 0.0111695249f, 2.79710264e-08f, 8.31999932e-06f, 2.79752879e-08f, 0.639902532f, -2.79752879e-08f, 0.385154575f, -0.639999986f, 2.79710264e-08f, 0.639902532f, 59.5994034f },
        { -1.56226206f, 0.0272693485f, 1.56226206f, 93.1098862f, 6.82990375e-08f, 1.30519095e-16f, -6.82990375e-08f, 1.62524974f, 0.0272693448f, 1.56226206f, 6.82886352e-08f, -0.601803958f }
    }
};


static const GLAStoredBasePoseJoint GLA_STORED_BASEPOSE_JOINTS[GLA_STORED_BASEPOSE_NUM_JOINTS] = {
    // 0: model_root
    {
        "model_root", -1, 0,
        { 0.639999986f, 0.0f, 0.0f, 0.0f, 0.0f, 0.639999986f, 0.0f, 0.0f, 0.0f, 0.0f, 0.639999986f, 0.0f },
        { 1.5625f, 0.0f, 0.0f, 0.0f, 0.0f, 1.5625f, 0.0f, 0.0f, 0.0f, 0.0f, 1.5625f, 0.0f }
    },
    // 1: pelvis
    {
        "pelvis", 0, 0,
        { -2.79752879e-08f, 0.639999986f, 2.79752879e-08f, -8.31999932e-06f, 0.0f, -2.79752879e-08f, 0.639999986f, 0.385154575f, 0.639999986f, 2.79752879e-08f, 1.2228387e-15f, 35.5866013f },
        { -6.82990446e-08f, -7.73509861e-23f, 1.5625f, -55.6040649f, 1.5625f, -6.82990446e-08f, 6.82990446e-08f, 1.05957733e-05f, 6.82990446e-08f, 1.5625f, 0.0f, -0.601804018f }
    },
    // 2: Motion
    {
        "Motion", 1, 1,
        { 0.639999986f, 0.0f, 0.0f, 0.0f, 0.0f, 0.639999986f, 0.0f, 0.385154575f, 0.0f, 0.0f, 0.639999986f, 36.4193802f },
        { 1.5625f, 0.0f, 0.0f, 0.0f, 0.0f, 1.5625f, 0.0f, -0.601804018f, 0.0f, 0.0f, 1.56250012f, -56.9052849f }
    },
    // 3: lfemurYZ
    {
        "lfemurYZ", 1, 0,
        { 0.165308878f, 0.618207395f, 0.00962380413f, 3.61215162f, 0.0444516689f, -0.0019474437f, -0.638451457f, -0.206205443f, -0.616682291f, 0.165577307f, -0.0434410609f, 35.5866013f },
        { 0.40358609f, 0.108524583f, -1.50557196f, 52.1427498f, 1.50929534f, -0.00475449953f, 0.404241502f, -19.8383636f, 0.0234956108f, -1.5587194f, -0.106057279f, 3.36793184f }
    },
    // 4: lfemurX
    {
        "lfemurX", 3, 0,
        { 0.165308878f, 0.618207395f, 0.00962373056f, 5.51320362f, 0.0444516689f, -0.00194751972f, -0.638451457f, 0.304988801f, -0.616682291f, 0.165577307f, -0.0434410833f, 28.4947548f },
        { 0.40358609f, 0.108524598f, -1.50557196f, 40.6427536f, 1.50929534f, -0.00475468859f, 0.404241502f, -19.8383656f, 0.023495432f, -1.5587194f, -0.106057324f, 3.36793447f }
    },
    // 5: ltibia
    {
        "ltibia", 3, 0,
        { 0.165304273f, 0.618207335f, 0.00970191136f, 7.41425514f, 0.0447542369f, -0.00194768352f, -0.638430297f, 0.816182375f, -0.616661608f, 0.165577278f, -0.0437333733f, 21.4029102f },
        { 0.403574914f, 0.109263271f, -1.50552154f, 29.1411552f, 1.50929546f, -0.00475508859f, 0.404241472f, -19.8383636f, 0.0236863066f, -1.5586679f, -0.106770933f, 3.38174963f }
    },
    // 6: ltalus
    {
        "ltalus", 5, 0,
        { 0.0988667756f, 0.618207335f, -0.132834643f, 11.1501312f, -0.509034753f, -0.00194470456f, -0.38791728f, 1.82762814f, -0.375111967f, 0.165577471f, 0.491401196f, 7.46635723f },
        { 0.241373882f, -1.24276054f, -0.915800571f, 6.41764784f, 1.50929523f, -0.00474782381f, 0.40424186f, -19.838377f, -0.324303329f, -0.947063923f, 1.19971001f, -3.61055827f }
    },
    // 7: rfemurYZ
    {
        "rfemurYZ", 1, 0,
        { -0.165307105f, 0.618207216f, -0.0096628163f, -3.61216831f, 0.0446015969f, 0.00194727979f, -0.638440967f, -0.206205443f, -0.61667192f, -0.165577829f, -0.0435858294f, 35.5866013f },
        { -0.403581828f, 0.108890638f, -1.50554669f, 52.1419373f, 1.50929511f, 0.00475410512f, -0.404242784f, 19.8384361f, -0.0235908665f, -1.55869401f, -0.106410727f, 3.38017082f }
    },
    // 8: rfemurX
    {
        "rfemurX", 7, 0,
        { -0.165307105f, 0.618207216f, -0.00966274273f, -5.51319933f, 0.0446015969f, 0.00194735592f, -0.638440967f, 0.306712955f, -0.61667192f, -0.165577814f, -0.0435858481f, 28.494875f },
        { -0.403581798f, 0.108890638f, -1.50554669f, 40.6419373f, 1.50929511f, 0.00475429185f, -0.404242784f, 19.8384342f, -0.0235906839f, -1.55869401f, -0.106410772f, 3.38017297f }
    },
    // 9: rtibia
    {
        "rtibia", 7, 0,
        { -0.165307105f, 0.618207216f, -0.00966252107f, -7.41423082f, 0.0446015969f, 0.00194758421f, -0.638440967f, 0.819631338f, -0.61667192f, -0.165577814f, -0.0435859039f, 21.4031467f },
        { -0.403581828f, 0.108890623f, -1.50554669f, 29.1419373f, 1.50929511f, 0.00475484831f, -0.404242724f, 19.8384323f, -0.0235901475f, -1.55869401f, -0.106410921f, 3.38018012f }
    },
    // 10: rtalus
    {
        "rtalus", 9, 0,
        { -0.0988308042f, 0.618207335f, 0.13286157f, -11.1501703f, -0.509126365f, 0.00195072498f, -0.387797028f, 1.82762754f, -0.37499705f, -0.165577516f, 0.491488814f, 7.46636152f },
        { -0.241286144f, -1.24298441f, -0.915520191f, 6.41693544f, 1.50929511f, 0.00476249866f, -0.404241979f, 19.8384094f, 0.324369073f, -0.946769953f, 1.19992399f, -3.61195326f }
    },
    // 11: lower_lumbar
    {
        "lower_lumbar", 1, 0,
        { -2.79752879e-08f, 0.639999986f, 2.79752879e-08f, -8.31999932e-06f, 0.0f, -2.79752879e-08f, 0.639999986f, 0.385154575f, 0.639999986f, 2.79752879e-08f, 1.2228387e-15f, 41.3465996f },
        { -6.82990517e-08f, -7.73509924e-23f, 1.56250012f, -64.6040649f, 1.5625f, -6.82990446e-08f, 6.82990589e-08f, 1.02023705e-05f, 6.82990446e-08f, 1.5625f, 0.0f, -0.601804018f }
    },
    // 12: upper_lumbar
    {
        "upper_lumbar", 11, 0,
        { -2.79752879e-08f, 0.639999986f, 2.79752879e-08f, -8.31999932e-06f, 0.0f, -2.79752879e-08f, 0.639999986f, 0.385154575f, 0.639999986f, 2.79752879e-08f, 1.2228387e-15f, 46.4666023f },
        { -6.82990446e-08f, -7.73509861e-23f, 1.5625f, -72.6040649f, 1.5625f, -6.82990446e-08f, 6.82990446e-08f, 9.8526798e-06f, 6.82990446e-08f, 1.5625f, 2.98544618e-15f, -0.601804018f }
    },
    // 13: thoracic
    {
        "thoracic", 12, 0,
        { -2.79752879e-08f, 0.639999986f, 2.79752879e-08f, -8.31999932e-06f, 0.0f, -2.79752879e-08f, 0.639999986f, 0.385154575f, 0.639999986f, 2.79752879e-08f, 1.2228387e-15f, 51.5866013f },
        { -6.82990446e-08f, -7.73509861e-23f, 1.5625f, -80.6040649f, 1.5625f, -6.82990446e-08f, 6.82990446e-08f, 9.50298909e-06f, 6.82990446e-08f, 1.5625f, 2.98544618e-15f, -0.601804018f }
    },
    // 14: cervical
    {
        "cervical", 13, 0,
        { -2.79752879e-08f, 0.639999986f, 2.79752879e-08f, -8.31999932e-06f, 0.0f, -2.79752879e-08f, 0.639999986f, 0.385154575f, 0.639999986f, 2.79752879e-08f, 1.2228387e-15f, 56.7066002f },
        { -6.82990375e-08f, -7.73509861e-23f, 1.56249988f, -88.6040573f, 1.5625f, -6.82990446e-08f, 6.82990446e-08f, 9.15329747e-06f, 6.82990446e-08f, 1.5625f, 0.0f, -0.601804018f }
    },
    // 15: cranium
    {
        "cranium", 14, 0,
        { 0.0111695249f, 0.639902532f, 2.79710264e-08f, -8.31999932e-06f, 0.0f, -2.79752879e-08f, 0.639999986f, 0.385154575f, 0.639902532f, -0.0111695249f, -4.88235452e-10f, 59.5994034f },
        { 0.0272693485f, 1.30519095e-16f, 1.56226206f, -93.1098862f, 1.56226206f, -6.82990375e-08f, -0.0272693448f, 1.62524974f, 6.82886352e-08f, 1.5625f, -1.19198096e-09f, -0.601803958f }
    },
    // 16: ceyebrow - actual parent 52
    {
        "ceyebrow", 15, 0,
        { -7.87042609e-09f, 0.695679963f, 1.03615605e-09f, 0.0406073593f, -0.671975553f, -7.87042609e-09f, 0.180054352f, -3.5395565f, 0.180054352f, 1.03615572e-09f, 0.671975553f, 63.1472435f },
        { -1.62621969e-08f, -1.38846326f, 0.372035652f, -28.407568f, 1.43744254f, -1.62621969e-08f, 2.14094742e-09f, -0.0583709404f, 2.14094809e-09f, 0.372035623f, 1.38846326f, -86.3607864f }
    },
    // 17: jaw - actual parent 52
    {
        "jaw", 15, 0,
        { -1.88969949e-07f, 0.695679963f, -5.32140731e-08f, 0.0405951962f, -0.593453109f, -1.88969949e-07f, -0.363020778f, -0.529887974f, -0.363020778f, -5.32140696e-08f, 0.593453109f, 60.7829056f },
        { -3.90457473e-07f, -1.22621703f, -0.750088394f, 44.942791f, 1.43744254f, -3.90457444e-07f, -1.09953085e-07f, -0.0583467819f, -1.09953085e-07f, -0.750088334f, 1.22621703f, -74.9304962f }
    },
    // 18: lblip2 - actual parent 52
    {
        "lblip2", 15, 0,
        { 0.564284682f, 0.406874835f, -0.00250827405f, 0.569948792f, 0.402593493f, -0.557704747f, 0.104184978f, -3.87403369f, 0.0589227341f, -0.0859587863f, -0.687829792f, 60.0335503f },
        { 1.16594827f, 0.831855237f, 0.121748574f, -4.75089455f, 0.840701461f, -1.15235245f, -0.17761156f, 5.71924305f, -0.00518269604f, 0.215271264f, -1.42122209f, 86.1579285f }
    },
    // 19: leye - actual parent 52
    {
        "leye", 15, 0,
        { 1.32922562e-15f, -0.695679963f, -6.08182802e-08f, 1.13838911f, -0.695679963f, 1.32922562e-15f, -3.04091401e-08f, -2.84735417f, 3.04091401e-08f, 6.08182802e-08f, -0.695679963f, 62.5774155f },
        { 2.58276185e-15f, -1.43744254f, 6.09596498e-08f, -4.09291172f, -1.43744254f, 2.66603175e-15f, 1.23824293e-07f, 1.63636124f, -1.25665238e-07f, -6.28326191e-08f, -1.43744254f, 89.9514389f }
    },
    // 20: rblip2 - actual parent 52
    {
        "rblip2", 15, 0,
        { -0.564284444f, 0.406875193f, 0.00250844145f, -0.488737285f, 0.402593881f, 0.557704508f, 0.104185067f, -3.87403369f, 0.0589227043f, 0.0859588981f, -0.687829792f, 60.0335503f },
        { -1.16594756f, 0.831855953f, 0.121748522f, -4.65619946f, 0.840702236f, 1.15235186f, 0.177611828f, -5.78753614f, 0.00518303737f, 0.215271413f, -1.42122209f, 86.1575089f }
    },
    // 21: ltlip2 - actual parent 52
    {
        "ltlip2", 15, 0,
        { 0.563360214f, 0.39372769f, -0.10758473f, 0.569948792f, 0.401934117f, -0.567055941f, 0.0294472016f, -3.87403369f, -0.0710274652f, -0.0860041305f, -0.686679721f, 60.1697044f },
        { 1.16403782f, 0.830492854f, -0.146759838f, 11.3844118f, 0.813536108f, -1.17167425f, -0.177705273f, 5.6896944f, -0.222295955f, 0.0608449951f, -1.41884577f, 85.7339478f }
    },
    // 22: rtlip2 - actual parent 52
    {
        "rtlip2", 15, 0,
        { -0.563359976f, 0.393727988f, 0.107584678f, -0.488737285f, 0.401934415f, 0.567055702f, 0.0294471588f, -3.87403369f, -0.0710273907f, 0.0860041007f, -0.686679721f, 60.1697044f },
        { -1.16403747f, 0.83049345f, -0.146759734f, 11.47894f, 0.813537002f, 1.17167401f, 0.177705243f, -5.75576162f, 0.222295865f, 0.0608449318f, -1.41884589f, 85.7158966f }
    },
    // 23: reye - actual parent 52
    {
        "reye", 15, 0,
        { 1.32922562e-15f, -0.695679963f, -6.08182802e-08f, -1.1385715f, -0.695679963f, 1.32922562e-15f, -3.04091401e-08f, -2.84735346f, 3.04091401e-08f, 6.08182802e-08f, -0.695679963f, 62.5774155f },
        { 2.58276185e-15f, -1.43744254f, 6.09596498e-08f, -4.09291077f, -1.43744254f, 2.74930102e-15f, 1.25729272e-07f, -1.636639f, -1.25665238e-07f, -6.28326191e-08f, -1.43744254f, 89.9514389f }
    },
    // 24: rclavical
    {
        "rclavical", 13, 0,
        { -0.63495338f, 3.50623242e-09f, 0.0802132487f, -8.31999932e-06f, 0.0802132487f, 2.77546945e-08f, 0.63495338f, 0.385154575f, 0.0f, 0.639999986f, -2.79752879e-08f, 54.5089493f },
        { -1.55017936f, 0.195833147f, 0.0f, -0.0754389316f, 8.56013926e-09f, 6.77604888e-08f, 1.5625f, -85.1702347f, 0.195833147f, 1.55017936f, -6.77959875e-08f, -0.597053349f }
    },
    // 25: rhumerus
    {
        "rhumerus", 13, 0,
        { -0.427266389f, -0.47619614f, -0.0167533029f, -5.92285395f, -0.011188359f, -0.0124697136f, 0.639780641f, 1.13338363f, -0.476359367f, 0.427412838f, 5.62931e-08f, 54.5089493f },
        { -1.04313076f, -0.0273153316f, -1.16298676f, 57.2458344f, -1.16258812f, -0.0304436404f, 1.0434885f, -63.7307968f, -0.0409016199f, 1.56196475f, 1.39146792e-07f, -2.01256728f }
    },
    // 26: rhumerusX
    {
        "rhumerusX", 25, 0,
        { -0.427266389f, -0.47619614f, -0.0167532451f, -9.55461693f, -0.011188359f, -0.0124696372f, 0.639780641f, 1.03828287f, -0.476359367f, 0.427412838f, 5.35119193e-09f, 50.4598923f },
        { -1.04313076f, -0.0273153316f, -1.16298676f, 48.7458305f, -1.16258812f, -0.0304434542f, 1.0434885f, -63.7307892f, -0.0409014858f, 1.56196475f, 1.46744465e-08f, -2.01255989f }
    },
    // 27: rradius
    {
        "rradius", 25, 0,
        { -0.425700217f, -0.47619608f, -0.0402062982f, -13.1863813f, -0.0463275202f, -0.0124696381f, 0.63819921f, 0.943181455f, -0.475639611f, 0.427412868f, -0.0261760261f, 46.4108391f },
        { -1.03930736f, -0.113104306f, -1.16122961f, 40.2956161f, -1.16258824f, -0.0304434542f, 1.0434885f, -63.7308006f, -0.0981599241f, 1.55810368f, -0.0639063194f, 0.201997235f }
    },
    // 28: rradiusX
    {
        "rradiusX", 27, 0,
        { -0.425700217f, -0.47619608f, -0.0402062982f, -16.093914f, -0.0463275202f, -0.0124696381f, 0.63819921f, 0.626764774f, -0.475639611f, 0.427412868f, -0.0261760261f, 43.16222f },
        { -1.03930736f, -0.113104306f, -1.16122961f, 33.4656143f, -1.16258824f, -0.0304434542f, 1.0434885f, -63.730793f, -0.0981599241f, 1.55810368f, -0.0639063194f, 0.201996699f }
    },
    // 29: rhand
    {
        "rhand", 27, 0,
        { -0.373349518f, -0.513605893f, -0.0801192522f, -19.0014458f, -0.104581289f, -0.0224098843f, 0.630999625f, 0.310347497f, -0.509188473f, 0.381191254f, -0.0708544478f, 39.9136009f },
        { -0.911497891f, -0.255325377f, -1.24313581f, 32.3774872f, -1.25392079f, -0.0547116548f, 0.930642605f, -60.9546242f, -0.195603654f, 1.54052651f, -0.172984481f, 2.70958233f }
    },
    // 30: r_d1_j1
    {
        "r_d1_j1", 27, 0,
        { 0.0453159809f, 0.511184394f, 0.382409394f, -19.1050816f, -0.300884515f, 0.355222553f, -0.439187258f, -0.639149427f, -0.563040793f, -0.148685724f, 0.265476137f, 39.2239494f },
        { 0.110634811f, -0.734581411f, -1.3746115f, 55.5618668f, 1.24800885f, 0.867242515f, -0.3630023f, 38.635994f, 0.933616817f, -1.07223451f, 0.648135185f, -8.27091312f }
    },
    // 31: r_d1_j2
    {
        "r_d1_j2", 27, 0,
        { -0.129667133f, 0.621374547f, 0.081731759f, -18.9963226f, -0.0934876353f, 0.0633519143f, -0.629957616f, -1.36127222f, -0.619714856f, -0.139571413f, 0.0779315233f, 37.872654f },
        { -0.316570163f, -0.228241295f, -1.51297581f, 50.9760437f, 1.51702774f, 0.154667765f, -0.340750545f, 41.9336243f, 0.199540421f, -1.53798246f, 0.190262526f, -5.50882578f }
    },
    // 32: r_d2_j1
    {
        "r_d2_j1", 27, 0,
        { 0.100812674f, 0.0917238668f, -0.625318766f, -20.9897461f, -0.105187587f, -0.621957183f, -0.10818892f, -0.944297612f, -0.623195291f, 0.119816549f, -0.0828952417f, 37.370018f },
        { 0.2461247f, -0.256805688f, -1.52147293f, 61.7810669f, 0.223935217f, -1.51845014f, 0.292520851f, -7.66503525f, -1.5266571f, -0.264133096f, -0.202380955f, -24.7305832f }
    },
    // 33: r_d2_j2
    {
        "r_d2_j2", 27, 0,
        { 0.22131215f, 0.0902770311f, -0.59369266f, -20.7830811f, -0.090051353f, -0.620583177f, -0.127934664f, -1.15993214f, -0.593726933f, 0.127775505f, -0.201895386f, 36.0924683f },
        { 0.540312946f, -0.219851941f, -1.44952869f, 63.29142f, 0.220402941f, -1.51509571f, 0.311951935f, -8.43587208f, -1.44944501f, -0.312340468f, -0.492908537f, -12.6959381f }
    },
    // 34: r_d4_j1
    {
        "r_d4_j1", 27, 0,
        { 0.215767592f, -0.0290264282f, -0.601832092f, -21.02948f, -0.0272114016f, -0.639074147f, 0.0210668445f, 0.38052544f, -0.60191685f, 0.0184861757f, -0.216689587f, 37.1531944f },
        { 0.526776314f, -0.0664340779f, -1.46952355f, 65.7006073f, -0.0708653033f, -1.56023943f, 0.045132257f, -2.57335711f, -1.46931636f, 0.0514327027f, -0.529027283f, -11.2634764f }
    },
    // 35: r_d4_j2
    {
        "r_d4_j2", 27, 0,
        { 0.519697726f, -0.0358964056f, -0.371787131f, -20.6022549f, -0.051236704f, -0.637866855f, -0.010033899f, 0.326646388f, -0.369985163f, 0.0379121006f, -0.520839393f, 35.96138f },
        { 1.26879346f, -0.125089616f, -0.903284132f, 58.6642113f, -0.0876377225f, -1.5572921f, 0.092558831f, -4.62539387f, -0.907683492f, -0.0244968254f, -1.27158046f, 27.0354652f }
    },
    // 36: rhang_tag_bone
    {
        "rhang_tag_bone", 27, 0,
        { 0.584731877f, 0.256953716f, -0.0407851338f, -20.0762711f, -0.0779962316f, 0.0774149969f, -0.630494654f, -0.109498881f, -0.248204023f, 0.581017792f, 0.102044426f, 36.9480553f },
        { 1.42756796f, -0.190420479f, -0.605966806f, 51.0286865f, 0.627328455f, 0.189001456f, 1.41850054f, -39.795723f, -0.0995730832f, -1.53929365f, 0.249131933f, -11.3725471f }
    },
    // 37: lclavical
    {
        "lclavical", 13, 0,
        { 0.63495338f, 3.5062333e-09f, -0.0802132711f, -8.31999932e-06f, 0.0802132711f, -2.77546945e-08f, 0.63495338f, 0.385154575f, 0.0f, -0.639999986f, -2.79752879e-08f, 54.5089493f },
        { 1.55017924f, 0.195833191f, 0.0f, -0.0754131451f, 8.56014104e-09f, -6.77604888e-08f, -1.5625f, 85.1702347f, -0.195833206f, 1.55017936f, -6.88894701e-08f, -0.597056508f }
    },
    // 38: lhumerus
    {
        "lhumerus", 13, 0,
        { 0.42726633f, -0.47619614f, 0.01675326f, 5.92283678f, -0.011188359f, 0.0124696577f, 0.639780641f, 1.13338363f, -0.476359367f, -0.427412778f, 1.81392732e-08f, 54.5089493f },
        { 1.04313076f, -0.0273153316f, -1.16298687f, 57.2458572f, -1.16258836f, 0.0304435063f, -1.04348838f, 63.7307739f, 0.0409015231f, 1.56196475f, 4.53691769e-08f, -2.01256061f }
    },
    // 39: lhumerusX
    {
        "lhumerusX", 38, 0,
        { 0.42726633f, -0.47619614f, 0.0167532042f, 9.55460167f, -0.011188359f, 0.0124695813f, 0.639780641f, 1.03828287f, -0.476359367f, -0.427412778f, -3.28026282e-08f, 50.4598923f },
        { 1.04313076f, -0.0273153316f, -1.16298687f, 48.7458534f, -1.16258836f, 0.0304433201f, -1.04348838f, 63.7307739f, 0.0409013815f, 1.56196475f, -7.91031596e-08f, -2.01255369f }
    },
    // 40: lradius
    {
        "lradius", 38, 0,
        { 0.425700128f, -0.47619617f, 0.0402062498f, 13.1863661f, -0.0463275202f, 0.0124695823f, 0.63819921f, 0.943181455f, -0.475639671f, -0.427412748f, -0.0261760689f, 46.4108391f },
        { 1.03930712f, -0.113104299f, -1.16122985f, 40.2956467f, -1.16258848f, 0.0304433089f, -1.04348838f, 63.7307739f, 0.0981598198f, 1.55810368f, -0.0639064386f, 0.20200561f }
    },
    // 41: lradiusX
    {
        "lradiusX", 40, 0,
        { 0.425700128f, -0.47619617f, 0.0402062498f, 16.0938969f, -0.0463275202f, 0.0124695823f, 0.63819921f, 0.626764774f, -0.475639671f, -0.427412748f, -0.0261760689f, 43.16222f },
        { 1.03930712f, -0.113104299f, -1.16122985f, 33.4656448f, -1.16258848f, 0.0304433089f, -1.04348838f, 63.7307663f, 0.0981598198f, 1.55810368f, -0.0639064386f, 0.202005073f }
    },
    // 42: lhand
    {
        "lhand", 40, 0,
        { 0.373349756f, -0.513605773f, 0.0801191181f, 19.0014305f, -0.10458117f, 0.0224097557f, 0.630999684f, 0.310347497f, -0.509188235f, -0.381191462f, -0.0708544031f, 39.9136009f },
        { 0.911498487f, -0.255325168f, -1.24313557f, 32.3774796f, -1.2539202f, 0.0547112972f, -0.93064326f, 60.9546204f, 0.195603311f, 1.54052651f, -0.172984377f, 2.70958805f }
    },
    // 43: l_d4_j1
    {
        "l_d4_j1", 40, 0,
        { -0.215766922f, -0.0290261619f, 0.601832271f, 21.02948f, -0.0272120275f, 0.639074087f, 0.0210663546f, 0.38052544f, -0.601917088f, -0.0184869617f, -0.216688916f, 37.1531944f },
        { -0.526774704f, -0.0664356202f, -1.46952403f, 65.700592f, -0.0708646551f, 1.56023967f, -0.045134183f, 2.57341504f, 1.4693172f, 0.0514315367f, -0.529025674f, -11.2635508f }
    },
    // 44: l_d4_j2
    {
        "l_d4_j2", 40, 0,
        { -0.519697666f, -0.0358963497f, 0.37178725f, 20.6022549f, -0.051236704f, 0.637866855f, -0.0100339772f, 0.326645106f, -0.369985253f, -0.0379121713f, -0.520839274f, 35.9613838f },
        { -1.26879323f, -0.125089601f, -0.903284431f, 58.6642189f, -0.0876375735f, 1.55729198f, -0.0925590098f, 4.62540007f, 0.90768373f, -0.0244970229f, -1.27158034f, 27.0354595f }
    },
    // 45: l_d2_j1
    {
        "l_d2_j1", 40, 0,
        { -0.10081213f, 0.0917238146f, 0.625318825f, 20.9897461f, -0.105187699f, 0.621957183f, -0.108188778f, -0.944297612f, -0.62319535f, -0.119816549f, -0.0828946978f, 37.370018f },
        { -0.246123359f, -0.256805897f, -1.52147305f, 61.7810364f, 0.223935097f, 1.51845014f, -0.292520881f, 7.66503763f, 1.52665734f, -0.264132768f, -0.202379614f, -24.7306423f }
    },
    // 46: l_d2_j2
    {
        "l_d2_j2", 40, 0,
        { -0.221311942f, 0.0902770907f, 0.59369266f, 20.7830811f, -0.0900514275f, 0.620583117f, -0.127934694f, -1.15993214f, -0.593726993f, -0.12777555f, -0.201895118f, 36.0924683f },
        { -0.540312469f, -0.219852149f, -1.44952893f, 63.29142f, 0.22040309f, 1.51509571f, -0.311952084f, 8.43587303f, 1.44944537f, -0.312340617f, -0.49290812f, -12.695962f }
    },
    // 47: l_d1_j1
    {
        "l_d1_j1", 40, 0,
        { -0.0453164317f, 0.511184394f, -0.382409275f, 19.1050816f, -0.300884604f, -0.355222613f, -0.439187109f, -0.639149427f, -0.563040733f, 0.148685426f, 0.265476465f, 39.2239494f },
        { -0.110635862f, -0.73458159f, -1.37461126f, 55.5618782f, 1.24800909f, -0.867242694f, 0.363001585f, -38.6359673f, -0.933616638f, -1.07223427f, 0.648136079f, -8.27095032f }
    },
    // 48: l_d1_j2
    {
        "l_d1_j2", 40, 0,
        { 0.129667073f, 0.621374547f, -0.0817318037f, 18.9963226f, -0.0934876204f, -0.0633519664f, -0.629957616f, -1.36127281f, -0.619714856f, 0.139571369f, 0.0779315084f, 37.872654f },
        { 0.316570044f, -0.22824128f, -1.51297581f, 50.9760437f, 1.51702774f, -0.154667914f, 0.340750426f, -41.9336205f, -0.199540555f, -1.53798258f, 0.190262482f, -5.50882244f }
    },
    // 49: ltail
    {
        "ltail", 3, 0,
        { 0.163758755f, -0.618207395f, -0.0245495532f, 2.53037882f, 0.102067687f, 0.00194745278f, 0.631805658f, 5.16943836f, -0.610217392f, -0.165577218f, 0.0990904868f, 38.4449387f },
        { 0.399801642f, 0.249188691f, -1.48978853f, 54.9750137f, -1.50929546f, 0.00475452282f, -0.404241264f, 19.3355427f, -0.0599354245f, 1.5424943f, 0.241920114f, -17.1227741f }
    },
    // 50: rtail
    {
        "rtail", 7, 0,
        { -0.163753599f, -0.618207216f, 0.0245882776f, -2.53021049f, 0.102216057f, -0.00194728351f, 0.631781638f, 5.16943836f, -0.610193908f, 0.165577859f, 0.0992337242f, 38.4456024f },
        { -0.399789035f, 0.249550939f, -1.48973131f, 54.9720268f, -1.50929499f, -0.00475411117f, 0.404242903f, -19.335619f, 0.0600299649f, 1.54243577f, 0.242269829f, -17.1358471f }
    },
    // 51: lhang_tag_bone
    {
        "lhang_tag_bone", 40, 0,
        { 0.584731758f, -0.256953925f, 0.0407850295f, 20.0762768f, 0.0779961273f, 0.0774149224f, -0.630494654f, -0.109498881f, 0.248204246f, 0.581017733f, 0.102044344f, 36.9480553f },
        { 1.42756784f, 0.19042024f, 0.605967402f, -51.0287132f, -0.627328992f, 0.189001292f, 1.4185003f, -39.7957039f, 0.0995728374f, -1.53929365f, 0.249131694f, -11.3725348f }
    },
    // 52: face
    {
        "face", 15, 1,
        { 0.0111695249f, 0.639902532f, 2.79710264e-08f, -8.31999932e-06f, 0.0f, -2.79752879e-08f, 0.639999986f, 0.385154575f, 0.639902532f, -0.0111695249f, -4.88235452e-10f, 59.5994034f },
        { 0.0272693485f, 1.30519095e-16f, 1.56226206f, -93.1098862f, 1.56226206f, -6.82990375e-08f, -0.0272693448f, 1.62524974f, 6.82886352e-08f, 1.5625f, -1.19198096e-09f, -0.601803958f }
    }
};

static ID_INLINE void GLA_LoadStoredJointMatColumnMajor(idJointMat& joint, const float m[12]) {
    idMat3 rot;

    // m is row-major 3x4:
    // [ m0  m1  m2  m3  ]
    // [ m4  m5  m6  m7  ]
    // [ m8  m9  m10 m11 ]
    rot[0][0] = m[0];
    rot[0][1] = m[1];
    rot[0][2] = m[2];

    rot[1][0] = m[4];
    rot[1][1] = m[5];
    rot[1][2] = m[6];

    rot[2][0] = m[8];
    rot[2][1] = m[9];
    rot[2][2] = m[10];

    // Convert row-major rotation rows into Doom 3 idJointMat/idMat3 convention.
    rot.TransposeSelf();

    joint.SetRotation(rot);
    joint.SetTranslation(idVec3(m[3], m[7], m[11]));
}

static ID_INLINE void GLA_LoadStoredBasePoseJoints(idList<idJointMat>& joints) {
    joints.SetNum(GLA_STORED_BASEPOSE_NUM_JOINTS);
    for (int i = 0; i < GLA_STORED_BASEPOSE_NUM_JOINTS; i++) {
        GLA_LoadStoredJointMatColumnMajor(joints[i], GLA_STORED_BASEPOSE_JOINTS[i].basePoseMat);
    }
}

static ID_INLINE void GLA_LoadStoredInverseBasePoseJoints(idList<idJointMat>& joints) {
    joints.SetNum(GLA_STORED_BASEPOSE_NUM_JOINTS);
    for (int i = 0; i < GLA_STORED_BASEPOSE_NUM_JOINTS; i++) {
        GLA_LoadStoredJointMatColumnMajor(joints[i], GLA_STORED_BASEPOSE_JOINTS[i].basePoseMatInv);
    }
}

static void SetJointMatFromRowMajor3x4(idJointMat& joint, const float m[12]) {
    idMat3 rot;

    rot[0][0] = m[0];  rot[0][1] = m[1];  rot[0][2] = m[2];
    rot[1][0] = m[4];  rot[1][1] = m[5];  rot[1][2] = m[6];
    rot[2][0] = m[8];  rot[2][1] = m[9];  rot[2][2] = m[10];

    // Same convention as your working basePose loader.
    rot.TransposeSelf();

    joint.SetRotation(rot);
    joint.SetTranslation(idVec3(m[3], m[7], m[11]));
}

static idJointMat ConvertDoomJointToGLAJoint(const idJointMat& in) {
    const float* m = in.ToFloatPtr();

    idJointMat out;
    float* o = out.ToFloatPtr();

    const float s = 0.64f;

    
    // Transpose 3x3 rotation and apply GLA scale.
    o[0] = m[0] * s;
    o[1] = m[4] * s;
    o[2] = m[8] * s;

    o[4] = m[1] * s;
    o[5] = m[5] * s;
    o[6] = m[9] * s;

    o[8] = m[2] * s;
    o[9] = m[6] * s;
    o[10] = m[10] * s;

    // Translation is already in the right axis order, just scale it.
    o[3] = m[3] * s;
    o[7] = m[7] * s;
    o[11] = m[11] * s;
    
    /*
    // I think the matrices were fine the whole time to be honest
        // Transpose 3x3 rotation and apply GLA scale.
    o[0] = m[0] * s;
    o[1] = m[1] * s;
    o[2] = m[2] * s;

    o[4] = m[4] * s;
    o[5] = m[5] * s;
    o[6] = m[6] * s;

    o[8] = m[8] * s;
    o[9] = m[9] * s;
    o[10] = m[10] * s;

    // Translation is already in the right axis order, just scale it.
    o[3] = m[3] * s;
    o[7] = m[7] * s;
    o[11] = m[11] * s;
    */

    return out;
}

static idJointMat ConvertGLASkinMatToDoomJointMat( const idJointMat &in ) {
    const float *m = in.ToFloatPtr();

    idJointMat out;
    float *o = out.ToFloatPtr();

    // Transpose rotation back.
    o[0]  = m[0];
    o[1]  = m[4];
    o[2]  = m[8];
    o[3]  = m[3];

    o[4]  = m[1];
    o[5]  = m[5];
    o[6]  = m[9];
    o[7]  = m[7];

    o[8]  = m[2];
    o[9]  = m[6];
    o[10] = m[10];
    o[11] = m[11];

    return out;
}

static ID_INLINE void GLA_BuildSkinMatricesFromStoredInverseBasePose(
    const idJointMat* currentAbsoluteJoints,
    idList<idJointMat>& outSkinMatrices
) {
    idList<idJointMat> inverseBasePose;
    idList<idJointMat> basePose;
    GLA_LoadStoredInverseBasePoseJoints(inverseBasePose);
    GLA_LoadStoredBasePoseJoints(basePose);

    outSkinMatrices.SetNum(GLA_STORED_BASEPOSE_NUM_JOINTS);
    for (int i = 0; i < GLA_STORED_BASEPOSE_NUM_JOINTS; i++) {

        idJointMat currentGLA = ConvertDoomJointToGLAJoint(currentAbsoluteJoints[i]);

       // idJointMat currentGLA = currentAbsoluteJoints[i];

        // This produces: skin = currentAbsolute * inverseBasePose
       // idJointMat skinGLA = currentGLA;
       // skinGLA *= inverseBasePose[i];

        outSkinMatrices[i] = currentGLA;
        outSkinMatrices[i] *= inverseBasePose[i];
        //outSkinMatrices[i] *= currentGLA;


       // outSkinMatrices[i] = currentAbsoluteJoints[i];
       // outSkinMatrices[i] *= inverseBasePose[i];

       // outSkinMatrices[i] = inverseBasePose[i];
       // outSkinMatrices[i] *= currentAbsoluteJoints[i];

       // outSkinMatrices[i] = ConvertGLASkinMatToDoomJointMat(outSkinMatrices[i]);
       
    }
}

static void JointMatFromNative12(idJointMat& joint, const float a[12]) {
    float* o = joint.ToFloatPtr();
    // This matches idJointMat storage exactly:
    //
    // mat[0], mat[1], mat[2],  mat[3]
    // mat[4], mat[5], mat[6],  mat[7]
    // mat[8], mat[9], mat[10], mat[11]
    //
    // where each row is:
    // m[0][axis], m[1][axis], m[2][axis], t[axis]

    o[0] = a[0];
    o[1] = a[1];
    o[2] = a[2];
    o[3] = a[3];

    o[4] = a[4];
    o[5] = a[5];
    o[6] = a[6];
    o[7] = a[7];

    o[8] = a[8];
    o[9] = a[9];
    o[10] = a[10];
    o[11] = a[11];
}
static ID_INLINE void GLA_LSI(idList<idJointMat>& joints) {
    joints.SetNum(GLA_STORED_BASEPOSE_NUM_JOINTS);
    for (int i = 0; i < GLA_STORED_BASEPOSE_NUM_JOINTS; i++) {
        JointMatFromNative12(joints[i], GLA_STORED_BASEPOSE_JOINTS[i].basePoseMatInv);
    }
}

static ID_INLINE void GLA_Skin(
    const idJointMat* currentAbsoluteJoints,
    idList<idJointMat>& outSkinMatrices
) {
    idList<idJointMat> inverseBasePose;
    GLA_LSI(inverseBasePose);

    outSkinMatrices.SetNum(GLA_STORED_BASEPOSE_NUM_JOINTS);
    for (int i = 0; i < GLA_STORED_BASEPOSE_NUM_JOINTS; i++) {

       // outSkinMatrices[i] = currentAbsoluteJoints[i];
        //outSkinMatrices[i] *= inverseBasePose[i];

        outSkinMatrices[i] = inverseBasePose[i];
        outSkinMatrices[i] *= currentAbsoluteJoints[i];
    }
}

static idJointMat GLA_InverseBind_To_Doom_InverseBind(const float m[12]) {
    idJointMat out;
    float* o = out.ToFloatPtr();

    const float s = 0.64f;

    // Transpose/remap 3x3 back into Doom idJointMat layout,
    // and remove the inverse-bind 1/s scale.
    o[0] = m[0] * s;
    o[1] = m[4] * s;
    o[2] = m[8] * s;

    o[4] = m[1] * s;
    o[5] = m[5] * s;
    o[6] = m[9] * s;

    o[8] = m[2] * s;
    o[9] = m[6] * s;
    o[10] = m[10] * s;

    // Do NOT scale inverse-bind translation.
    o[3] = m[3];
    o[7] = m[7];
    o[11] = m[11];

    return out;
}

static ID_INLINE void GLA_BuildSkinMatrices_DoomOnly(
    const idJointMat* currentAbsoluteJoints,
    idList<idJointMat>& outSkinMatrices
) {
    outSkinMatrices.SetNum(GLA_STORED_BASEPOSE_NUM_JOINTS);

    for (int i = 0; i < GLA_STORED_BASEPOSE_NUM_JOINTS; i++) {
        idJointMat invBindDoom =
            GLA_InverseBind_To_Doom_InverseBind(
                GLA_STORED_BASEPOSE_JOINTS[i].basePoseMatInv
            );

        outSkinMatrices[i] = invBindDoom;
        outSkinMatrices[i] *= currentAbsoluteJoints[i];
        //outSkinMatrices[i] *= invBindDoom;
    }
}

static idJointMat StoredBasePose_To_DoomBasePose(const float m[12]) {
    idJointMat out;
    float* o = out.ToFloatPtr();

    const float invS = 1.0f / 0.64f;

    // Reverse ConvertDoomJointToGLAJoint:
    // stored = transpose(doom) * 0.64
    o[0] = m[0] * invS;
    o[1] = m[4] * invS;
    o[2] = m[8] * invS;

    o[4] = m[1] * invS;
    o[5] = m[5] * invS;
    o[6] = m[9] * invS;

    o[8] = m[2] * invS;
    o[9] = m[6] * invS;
    o[10] = m[10] * invS;

    o[3] = m[3] * invS;
    o[7] = m[7] * invS;
    o[11] = m[11] * invS;

    return out;
}

static bool InvertJointMatAffine(const idJointMat& in, idJointMat& out) {
    const float* m = in.ToFloatPtr();

    // Convert idJointMat native layout to normal row-major 3x4
    float a00 = m[0], a01 = m[4], a02 = m[8], tx = m[3];
    float a10 = m[1], a11 = m[5], a12 = m[9], ty = m[7];
    float a20 = m[2], a21 = m[6], a22 = m[10], tz = m[11];

    float c00 = a11 * a22 - a12 * a21;
    float c01 = -(a10 * a22 - a12 * a20);
    float c02 = a10 * a21 - a11 * a20;

    float c10 = -(a01 * a22 - a02 * a21);
    float c11 = a00 * a22 - a02 * a20;
    float c12 = -(a00 * a21 - a01 * a20);

    float c20 = a01 * a12 - a02 * a11;
    float c21 = -(a00 * a12 - a02 * a10);
    float c22 = a00 * a11 - a01 * a10;

    float det = a00 * c00 + a01 * c01 + a02 * c02;

    if (fabsf(det) < 1e-8f) {
        return false;
    }

    float invDet = 1.0f / det;

    // inverse 3x3 = transpose(cofactor) / det
    float r00 = c00 * invDet;
    float r01 = c10 * invDet;
    float r02 = c20 * invDet;

    float r10 = c01 * invDet;
    float r11 = c11 * invDet;
    float r12 = c21 * invDet;

    float r20 = c02 * invDet;
    float r21 = c12 * invDet;
    float r22 = c22 * invDet;

    // inverse translation = -(R^-1 * T)
    float itx = -(r00 * tx + r01 * ty + r02 * tz);
    float ity = -(r10 * tx + r11 * ty + r12 * tz);
    float itz = -(r20 * tx + r21 * ty + r22 * tz);

    float* o = out.ToFloatPtr();

    // Convert normal row-major 3x4 back to idJointMat native layout
    o[0] = r00; o[4] = r01; o[8] = r02; o[3] = itx;
    o[1] = r10; o[5] = r11; o[9] = r12; o[7] = ity;
    o[2] = r20; o[6] = r21; o[10] = r22; o[11] = itz;

    return true;
}

static ID_INLINE void GLA_Skin2(
    const idJointMat* currentAbsoluteJoints,
    idList<idJointMat>& outSkinMatrices
) {

    outSkinMatrices.SetNum(GLA_STORED_BASEPOSE_NUM_JOINTS);
    for (int i = 0; i < GLA_STORED_BASEPOSE_NUM_JOINTS; i++) {

        idJointMat baseDoom =
            StoredBasePose_To_DoomBasePose(
                GLA_STORED_BASEPOSE_JOINTS[i].basePoseMat
            );

        idJointMat invBaseDoom;

        if (!InvertJointMatAffine(baseDoom, invBaseDoom)) {
            // handle bad matrix
        }

       // outSkinMatrices[i] = invBaseDoom;
       // outSkinMatrices[i] *= currentAbsoluteJoints[i];
        outSkinMatrices[i] = currentAbsoluteJoints[i];
        outSkinMatrices[i] /= baseDoom;

       // outSkinMatrices[i] *= invBaseDoom;
    }
}

static ID_INLINE idVec3 GLA_ModelToJointLocal_FromAbs12(
    const idVec3& p,
    const float abs12[12]
) {
    // abs12 is normal 3x4 row form:
    //
    // [ r00 r01 r02 tx ]
    // [ r10 r11 r12 ty ]
    // [ r20 r21 r22 tz ]
    //
    // We want inverse(abs) * p.
    // For rotation+uniform-scale bind matrices:
    //
    // local = R^-1 * (p - T)

    const float r00 = abs12[0];
    const float r01 = abs12[1];
    const float r02 = abs12[2];
    const float tx = abs12[3];

    const float r10 = abs12[4];
    const float r11 = abs12[5];
    const float r12 = abs12[6];
    const float ty = abs12[7];

    const float r20 = abs12[8];
    const float r21 = abs12[9];
    const float r22 = abs12[10];
    const float tz = abs12[11];

    const float x = p.x - tx;
    const float y = p.y - ty;
    const float z = p.z - tz;

    // Because your base matrices contain uniform scale 0.64,
    // inverse rotation-scale = transpose(R) / (0.64 * 0.64)
    const float invScaleSq = 1.0f / (0.64f * 0.64f);
   // const float invScaleSq = 1.0f;

    return idVec3(
        (r00 * x + r10 * y + r20 * z) * invScaleSq,
        (r01 * x + r11 * y + r21 * z) * invScaleSq,
        (r02 * x + r12 * y + r22 * z) * invScaleSq
    );
}

static ID_INLINE idVec3 TransformPointByJointMatRaw(
    const idJointMat& bone,
    const idVec3& p
) {
    const float* m = bone.ToFloatPtr();

    return idVec3(
        m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3],
        m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
        m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]
    );
}

static bool InvertJointMatAffine2(const idJointMat& in, idJointMat& out) {
    const float* m = in.ToFloatPtr();

    float a00 = m[0], a01 = m[1], a02 = m[2], tx = m[3];
    float a10 = m[4], a11 = m[5], a12 = m[6], ty = m[7];
    float a20 = m[8], a21 = m[9], a22 = m[10], tz = m[11];

    float det =
        a00 * (a11 * a22 - a12 * a21) -
        a01 * (a10 * a22 - a12 * a20) +
        a02 * (a10 * a21 - a11 * a20);

    if (idMath::Fabs(det) < 1e-8f) {
        return false;
    }

    float invDet = 1.0f / det;

    float r00 = (a11 * a22 - a12 * a21) * invDet;
    float r01 = -(a01 * a22 - a02 * a21) * invDet;
    float r02 = (a01 * a12 - a02 * a11) * invDet;

    float r10 = -(a10 * a22 - a12 * a20) * invDet;
    float r11 = (a00 * a22 - a02 * a20) * invDet;
    float r12 = -(a00 * a12 - a02 * a10) * invDet;

    float r20 = (a10 * a21 - a11 * a20) * invDet;
    float r21 = -(a00 * a21 - a01 * a20) * invDet;
    float r22 = (a00 * a11 - a01 * a10) * invDet;

    float itx = -(r00 * tx + r01 * ty + r02 * tz);
    float ity = -(r10 * tx + r11 * ty + r12 * tz);
    float itz = -(r20 * tx + r21 * ty + r22 * tz);

    float* o = out.ToFloatPtr();

    o[0] = r00; o[1] = r01; o[2] = r02; o[3] = itx;
    o[4] = r10; o[5] = r11; o[6] = r12; o[7] = ity;
    o[8] = r20; o[9] = r21; o[10] = r22; o[11] = itz;

    return true;
}

static void StoredBasePoseToDoom(const float in[12], idJointMat& out) {
    float* o = out.ToFloatPtr();

    const float invS = 1.0f / 0.64f;

    // stored base = transpose(doom) * 0.64
    o[0] = in[0] * invS;
    o[1] = in[4] * invS;
    o[2] = in[8] * invS;
    o[3] = in[3] * invS;

    o[4] = in[1] * invS;
    o[5] = in[5] * invS;
    o[6] = in[9] * invS;
    o[7] = in[7] * invS;

    o[8] = in[2] * invS;
    o[9] = in[6] * invS;
    o[10] = in[10] * invS;
    o[11] = in[11] * invS;
}

static void BuildSkinMatrices_Doom3(
    const idJointMat* currentAbsoluteJoints,
    idList<idJointMat>& outSkinMatrices
) {
    outSkinMatrices.SetNum(GLA_STORED_BASEPOSE_NUM_JOINTS);

    for (int i = 0; i < GLA_STORED_BASEPOSE_NUM_JOINTS; i++) {
        idJointMat baseDoom;
        StoredBasePoseToDoom(
            GLA_STORED_BASEPOSE_JOINTS[i].basePoseMat,
            baseDoom
        );

        idJointMat invBaseDoom;
        if (!InvertJointMatAffine2(baseDoom, invBaseDoom)) {
            outSkinMatrices[i] = currentAbsoluteJoints[i];
            continue;
        }

        //outSkinMatrices[i] = currentAbsoluteJoints[i];
        //outSkinMatrices[i] *= invBaseDoom;

        outSkinMatrices[i] = invBaseDoom;
        outSkinMatrices[i] *= currentAbsoluteJoints[i];
    }
}

/////////
// I think these are fine to use as they're just mathematical functions
///////////
void MC_UnCompressQuat(float mat[3][4], const unsigned char* comp)
{
    float w, x, y, z, f;
    float fTx;
    float fTy;
    float fTz;
    float fTwx;
    float fTwy;
    float fTwz;
    float fTxx;
    float fTxy;
    float fTxz;
    float fTyy;
    float fTyz;
    float fTzz;

    const unsigned short* pwIn = (unsigned short*)comp;

    w = *pwIn++;
    w /= 16383.0f;
    w -= 2.0f;
    x = *pwIn++;
    x /= 16383.0f;
    x -= 2.0f;
    y = *pwIn++;
    y /= 16383.0f;
    y -= 2.0f;
    z = *pwIn++;
    z /= 16383.0f;
    z -= 2.0f;

    fTx = 2.0f * x;
    fTy = 2.0f * y;
    fTz = 2.0f * z;
    fTwx = fTx * w;
    fTwy = fTy * w;
    fTwz = fTz * w;
    fTxx = fTx * x;
    fTxy = fTy * x;
    fTxz = fTz * x;
    fTyy = fTy * y;
    fTyz = fTz * y;
    fTzz = fTz * z;

    // rot...
    //
    mat[0][0] = 1.0f - (fTyy + fTzz);
    mat[0][1] = fTxy - fTwz;
    mat[0][2] = fTxz + fTwy;
    mat[1][0] = fTxy + fTwz;
    mat[1][1] = 1.0f - (fTxx + fTzz);
    mat[1][2] = fTyz - fTwx;
    mat[2][0] = fTxz - fTwy;
    mat[2][1] = fTyz + fTwx;
    mat[2][2] = 1.0f - (fTxx + fTyy);

    // xlat...
    //
    f = *pwIn++;
    f /= 64;
    f -= 512;
    mat[0][3] = f;

    f = *pwIn++;
    f /= 64;
    f -= 512;
    mat[1][3] = f;

    f = *pwIn++;
    f /= 64;
    f -= 512;
    mat[2][3] = f;
}

static inline int G2_GetBonePoolIndex(const mdxaHeader_t* pMDXAHeader, int iFrame, int iBone)
{
    const int iOffsetToIndex = (iFrame * pMDXAHeader->numBones * 3) + (iBone * 3);
    mdxaIndex_t* pIndex = (mdxaIndex_t*)((byte*)pMDXAHeader + pMDXAHeader->ofsFrames + iOffsetToIndex);

    return (pIndex->iIndex[2] << 16) + (pIndex->iIndex[1] << 8) + (pIndex->iIndex[0]);
}

/*static inline*/ void UnCompressBone(
    float mat[3][4],
    int iBoneIndex,
    const mdxaHeader_t* pMDXAHeader,
    int iFrame)
{
    mdxaCompQuatBone_t* pCompBonePool =
        (mdxaCompQuatBone_t*)((byte*)pMDXAHeader + pMDXAHeader->ofsCompBonePool);
    MC_UnCompressQuat(
        mat,
        pCompBonePool[G2_GetBonePoolIndex(pMDXAHeader, iFrame, iBoneIndex)].Comp);
}

#endif /* !__MODEL_MDR_H__ */