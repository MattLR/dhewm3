// Model_mds.h
//

#ifndef __MODEL_MDS_H__
#define __MODEL_MDS_H__


#include "idlib/math/Vector.h"

//#define	LL(x) x=LittleLong(x)
#define	LL(x) x=x

/*
==============================================================================

MDS file format (Wolfenstein Skeletal Format)

==============================================================================
*/

#define MDS_IDENT           ( ( 'W' << 24 ) + ( 'S' << 16 ) + ( 'D' << 8 ) + 'M' )
#define MDS_VERSION         4
#define MDS_MAX_VERTS       6000
#define MDS_MAX_TRIANGLES   8192
#define MDS_MAX_BONES       128
#define MDS_MAX_SURFACES    32
#define MDS_MAX_TAGS        128

#define MDS_TRANSLATION_SCALE   ( 1.0 / 64 )

struct mdsWeight_t {
	int			boneIndex;		// these are indexes into the boneReferences,
	float		boneWeight;		// not the global per-frame bone list
	idVec3		offset;
};

struct mdsVertex_t {
	idVec3		normal;
	idVec2		texCoords;
	int			numWeights;
	int			fixedParent;	// stay equi-distant from this parent
	float		fixedDist;
	mdsWeight_t	weights[1];		// variable sized
};

struct mdsTriangle_t {
	int			indexes[3];
};

struct mdsSurface_t {
	int			ident;

	char		name[MAX_MD3PATH];	// polyset name
	char		shader[MAX_MD3PATH];
	int			shaderIndex;	// for in-game use

	int			minLod;

	int			ofsHeader;	// this will be a negative number

	int			numVerts;
	int			ofsVerts;

	int			numTriangles;
	int			ofsTriangles;

	int ofsCollapseMap;           // numVerts * int

	// Bone references are a set of ints representing all the bones
	// present in any vertex weights for this surface.  This is
	// needed because a model may have surfaces that need to be
	// drawn at different sort times, and we don't want to have
	// to re-interpolate all the bones for each surface.
	int			numBoneReferences;
	int			ofsBoneReferences;

	int			ofsEnd;		// next surface follows
};

struct mdsBoneFrameCompressed_t {
	//float		angles[3];
	//float		ofsAngles[2];
	short		angles[4];            // to be converted to axis at run-time (this is also better for lerping)
	short		ofsAngles[2];         // PITCH/YAW, head in this direction from parent to go to the offset position
} ;

// NOTE: this only used at run-time
struct mdsBoneFrame_t {
	float matrix[3][3];             // 3x3 rotation
	idVec3 translation;             // translation vector
} ;

struct mdsFrame_t {
	idVec3		bounds[2];		// bounds of all surfaces of all LOD's for this frame
	idVec3		localOrigin;	// midpoint of bounds, used for sphere cull
	float		radius;			// dist from localOrigin to corner
	idVec3		parentOffset;	// one bone is an ascendant of all other bones, it starts the hierachy at this position
	mdsBoneFrameCompressed_t 	bones[1];		// [numBones]
};


struct mdsLOD_t {
	int			numSurfaces;
	int			ofsSurfaces;		// first surface, others follow
	int			ofsEnd;				// next lod follows
};

struct mdsTag_t {
	char		name[MAX_MD3PATH];	// name of tag
	float		torsoWeight;
	int			boneIndex;			// our index in the bones

};

#define BONEFLAG_TAG        1       // this bone is actually a tag

struct mdsBoneInfo_t {
	char name[MAX_MD3PATH];         // name of bone
	int parent;                     // not sure if this is required, no harm throwing it in
	float torsoWeight;              // scale torso rotation about torsoParent by this
	float parentDist;
	int flags;
};


struct mdsHeader_t {
	int			ident;
	int			version;

	char		name[MAX_MD3PATH];	// model name

	float		lodScale;
	float		lodBias;

	// frames and bones are shared by all levels of detail
	int			numFrames;
	int			numBones;
	int			ofsFrames;			// md4Frame_t[numFrames]
	int			ofsBones;           // mdsBoneInfo_t[numBones]
	int			torsoParent;        // index of bone that is the parent of the torso

	int			numSurfaces;
	int			ofsSurfaces;

	// tag data
	int         numTags;
	int         ofsTags;			// mdsTag_t[numTags]

	int			ofsEnd;				// end of file
} ;

/*
===============================================================================

	RtCW MDS animated model

===============================================================================
*/
// Dynamix
class idRenderModelMDS : public idRenderModelStatic {
public:
	idRenderModelMDS();
	~idRenderModelMDS();

	virtual void				InitFromFile(const char* fileName);
	virtual dynamicModel_t		IsDynamicModel() const;
	virtual idBounds			Bounds(const class idRenderEntity* ent) const;
	virtual void				TouchData();
	virtual void				LoadModel();
	virtual int					Memory() const;
	virtual	idRenderModel*		InstantiateDynamicModel(const struct renderEntity_s* ent, const struct viewDef_s* view, idRenderModel* cachedModel);
	virtual int					NumJoints(void) const;
	virtual const idMD5Joint*	GetJoints(void) const;
	virtual jointHandle_t		GetJointHandle(const char* name) const;
	virtual const char*			GetJointName(jointHandle_t handle) const;
	virtual const				idJointQuat* GetDefaultPose(void) const;
	virtual int					NearestJoint(int surfaceNum, int a, int b, int c) const;
private:
	bool						LoadMDSFile(void* buffer, int filesize);
	void						RenderFramesToModel(idRenderModelStatic* staticModel, const struct viewDef_s* view, int scale, int frame, const renderEntity_t* ent2);
	bool						WriteAnimToDisk();
	void						CalcBones(idList<idJointMat>& boneData, int currentFrame);
	void						ParseAnims(idList <animInfo_t>& animInfo, int &numAnims);

	idList<idMD5Joint>			joints;
	idList<idJointQuat>			defaultPose;
	//idList<idMDRMesh>			meshes;

	int							numLods;

	idBounds					bounds;

	idList<struct mdsFrame_t*>	frames;
	idList<const idMaterial*>	materials;

	int							dataSize;
	struct mdsHeader_t*			modelData;

	idRenderModelStatic*		cachedModel;

	int							cFrame;

	void						DrawJoints(const renderEntity_t* ent, const struct viewDef_s* view) const;

};

/*
==============================================================================

MDM file format (Wolfenstein Skeletal Mesh) - V3

==============================================================================
*/

#define MDM_IDENT           ( ( 'W' << 24 ) + ( 'M' << 16 ) + ( 'D' << 8 ) + 'M' )
#define MDM_VERSION         3
#define MDM_MAX_VERTS       6000
#define MDM_MAX_TRIANGLES   8192
#define MDM_MAX_SURFACES    32
#define MDM_MAX_TAGS        128

#define MDM_TRANSLATION_SCALE   ( 1.0 / 64 )

typedef struct mdmWeight_s {
	int boneIndex;              // these are indexes into the boneReferences,
	float boneWeight;           // not the global per-frame bone list
	idVec3 offset;
} mdmWeight_t;

typedef struct mdmVertex_s {
	idVec3 normal;
	idVec2 texCoords;
	int numWeights;
	mdmWeight_t weights[1];     // variable sized
} mdmVertex_t;

typedef struct mdmTriangle_s {
	int indexes[3];
} mdmTriangle_t;

typedef struct mdmSurface_s {
	int ident;

	char name[MAX_MD3PATH];           // polyset name
	char shader[MAX_MD3PATH];
	int shaderIndex;                // for in-game use

	int minLod;

	int ofsHeader;                  // this will be a negative number

	int numVerts;
	int ofsVerts;

	int numTriangles;
	int ofsTriangles;

	int ofsCollapseMap;           // numVerts * int

	// Bone references are a set of ints representing all the bones
	// present in any vertex weights for this surface.  This is
	// needed because a model may have surfaces that need to be
	// drawn at different sort times, and we don't want to have
	// to re-interpolate all the bones for each surface.
	int numBoneReferences;
	int ofsBoneReferences;

	int ofsEnd;                     // next surface follows
} mdmSurface_t;

typedef struct mdmLOD_s {
	int numSurfaces;
	int ofsSurfaces;                // first surface, others follow
	int ofsEnd;                     // next lod follows
} mdmLOD_t;

// Tags always only have one parent bone
typedef struct mdmTag_s {
	char name[MAX_MD3PATH];           // name of tag
	idVec3 axis[3];

	int boneIndex;
	idVec3 offset;

	int numBoneReferences;
	int ofsBoneReferences;

	int ofsEnd;                     // next tag follows
} mdmTag_t;

typedef struct mdmHeader_t {
	int ident;
	int version;

	char name[MAX_MD3PATH];           // model name

	float lodScale;
	float lodBias;

	int numSurfaces;
	int ofsSurfaces;

	// tag data
	int numTags;
	int ofsTags;

	int ofsEnd;                     // end of file
};

/*
===============================================================================

	ET MDM/X Skeletal Model

===============================================================================
*/
// Dynamix
class idRenderModelMDM : public idRenderModelStatic {
public:
	idRenderModelMDM();
	~idRenderModelMDM();

	virtual void				InitFromFile(const char* fileName);
	virtual dynamicModel_t		IsDynamicModel() const;
	virtual idBounds			Bounds(const class idRenderEntity* ent) const;
	virtual void				TouchData();
	virtual void				LoadModel();
	virtual int					Memory() const;
	virtual	idRenderModel*		InstantiateDynamicModel(const struct renderEntity_s* ent, const struct viewDef_s* view, idRenderModel* cachedModel);
	virtual int					NumJoints(void) const;
	virtual const idMD5Joint*	GetJoints(void) const;
	virtual jointHandle_t		GetJointHandle(const char* name) const;
	virtual const char*			GetJointName(jointHandle_t handle) const;
	virtual const				idJointQuat* GetDefaultPose(void) const;
	virtual int					NearestJoint(int surfaceNum, int a, int b, int c) const;	
	virtual void				SetJoint(int index, int parentNum, idStr name);
private:
	bool						LoadMDMFile(void* buffer, int filesize);
	void						RenderFramesToModel(idRenderModelStatic* staticModel, const struct viewDef_s* view, int scale, int frame, const renderEntity_t* ent2);

	idList<idMD5Joint>			joints;
	idList<idJointQuat>			defaultPose;
	//idList<idMDMMesh>			meshes;

	int							numLods;

	idBounds					bounds;

	idList<const idMaterial*>	materials;

	int							dataSize;
	struct mdmHeader_t*			modelData;

	idRenderModelStatic*		cachedModel;

	void						DrawJoints(const renderEntity_t* ent, const struct viewDef_s* view) const;

};


#endif /* !__MODEL_MDS_H__ */