// Model_skb.h
//
// Loader for Star Trek Elite Force 2 / MOHAA / FAKK2 "Skeletor" .skb skeletal models, version 4,
// skinned and animated by the Doom 3 animator, with .ska -> .md5anim conversion.
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

class idTikFile;
class idSKBSkeleton;
struct skaQuat_t;

// The mesh as it is kept in memory: weights already point at joints (not .skb bones), see idSKBSkeleton
struct skbDynWeight_t {
	int			joint;
	float		weight;
	float		offset[3];				// in the space of the joint
};

struct skbDynVert_t {
	float		normal[3];				// in the space of the joints, same as the offsets
	float		st[2];
	int			firstWeight;			// into the shared weight list
	int			numWeights;
};

struct skbDynSurface_t {
	const idMaterial* shader;
	int			firstVert, numVerts;	// into the shared vertex list
	int			firstIndex, numIndexes;	// into the shared index list, relative to firstVert
};

/*
===============================================================================

	SKB model: skinned, animated by the Doom 3 animator

	Like the MDS / MDM loaders. The mesh stays in memory as weights, the skeleton is turned into
	Doom 3 joints (see the notes on "origin" below), and every time an entity is drawn the mesh is
	skinned with the joints the animator worked out. The first time a .tik is loaded its .ska files
	are converted to .md5anim files (see below), which is what the animator plays.

	It can be fed either a .skb directly, or a .tik, in which case the .tik decides:
	  * which .skb to load (the first "skelmodel")
	  * the material of each surface ("surface <name> shader <shader>")
	  * which surfaces aren't drawn ("flags nodraw")
	  * surfaces taken from another .skb ("replacesurface"). Their weights are matched to the
		joints of the main .skb by bone name, and their offsets are used as they are, so the
		two files are expected to share a bind pose.
	Everything else in the .tik is left alone, see Tik_parse.h.

	A .skb on its own has no animations to convert (the .ska files are only known to the .tik).

===============================================================================
*/
class idRenderModelSKB : public idRenderModelStatic {
public:
	idRenderModelSKB();
	~idRenderModelSKB();

	virtual void				InitFromFile(const char* fileName);
	virtual dynamicModel_t		IsDynamicModel() const;
	virtual idBounds			Bounds(const class idRenderEntity* ent) const;
	virtual void				TouchData();
	virtual void				LoadModel();
	virtual int					Memory() const;
	virtual idRenderModel* InstantiateDynamicModel(const struct renderEntity_s* ent, const struct viewDef_s* view, idRenderModel* cachedModel);
	virtual int					NumJoints(void) const;
	virtual const idMD5Joint* GetJoints(void) const;
	virtual jointHandle_t		GetJointHandle(const char* name) const;
	virtual const char* GetJointName(jointHandle_t handle) const;
	virtual const idJointQuat* GetDefaultPose(void) const;
	virtual int					NearestJoint(int surfaceNum, int a, int b, int c) const;

private:
	bool						LoadTik();
	bool						LoadRawSKB();
	bool						BuildFromSKB(const char* skbName, const idTikFile* tik);
	bool						WriteAnimsToDisk(const idTikFile& tik) const;
	const idMaterial* FindSurfaceMaterial(const char* surfaceName, const idTikFile* tik, const char* fallbackDir) const;
	void						ClearData();
	void						BuildJoints();
	void						SkinSurfaces(idRenderModelStatic* dst, const idJointMat* jointMats);
	void						DrawJoints(const renderEntity_t* ent, const struct viewDef_s* view) const;

	idSKBSkeleton* skeleton;			// owned
	idList<idMD5Joint>			joints;
	idList<idJointQuat>			defaultPose;
	idList<skbDynSurface_t>		dynSurfaces;
	idList<skbDynVert_t>		dynVerts;
	idList<skbDynWeight_t>		dynWeights;
	idList<int>					dynIndexes;
	bool						skinTranspose;		// see BuildJoints
	int							dataSize;
};

/*
===============================================================================

	Animation export: .ska -> .md5anim

	.ska files are never played back, the first time a .tik is loaded they are
	converted to .md5anim files next to them (skipped if the .md5anim exists, delete it to
	regenerate), the same way the MDS / MDM loaders do it, so the Doom 3 animator does the rest.

	Doom 3 wants one joint called "origin" that comes first and is the only joint without a
	parent. The .skb skeletons don't follow that (the weapons have "origin" last, under the
	spine, the characters have several joints without a parent), so the joint list is rebuilt:
	  * a bone called "origin" is used if there is one. It becomes the root, and the parent
		links between it and the old root are reversed.
	  * if there isn't one an identity "origin" is added.
	  * any other bone without a parent is parented to "origin".
	Every bone keeps its model space position and orientation, only the way it is expressed
	changes, and the joints are put in an order where parents come first.

	The same joint list (names, order, parents) has to be used by whatever writes the
	.md5mesh, and JointOfBone() gives the number to put in the weights.

===============================================================================
*/

#define SKA_VERSION_3			3			// bones by number, one frame is skelFrame_t
#define SKA_VERSION_4			4			// the same, with a table of bone names ahead of the frames
#define SKA_MAX_FRAMES			16384
#define SKA_BONE_NAME_LEN		32

// all of these are the on-disk layout, little endian
struct skaHeader_t {
	char		ident[4];				// "SKAN"
	int			version;
	char		name[SKB_NAME_LEN];
	int			type;
	int			numFrames;
	int			numBones;
	float		totalTime;
	float		frameTime;
	float		totalDelta[3];			// root motion, not used (md5anim has no place for it)
	int			ofsFrames;				// version 3: the frames. version 4: the bone names
	// version 4 only:
	int			ofsFrames4;				// the frames
};

struct skaBoneName_t {					// version 4
	float		unknown;				// stays close to the length of the bone's offset, what it is for is not known
	char		name[SKA_BONE_NAME_LEN];
};

struct skaFrame_t {						// followed by skbBone_t[numBones]
	float		bounds[2][3];			// min, max
	float		radius;
	float		delta[3];
};

class idSKBAnim {
public:
	idSKBAnim();
	~idSKBAnim();

	bool					Open(const char* fileName);

	int						Version() const { return version; }
	int						NumFrames() const { return numFrames; }
	int						NumBones() const { return numBones; }
	float					FrameTime() const { return frameTime; }
	const char* BoneName(int bone, idStr& out) const;		// NULL for version 3, which has no names
	void					GetBounds(int frame, float bounds[2][3]) const;
	void					GetBone(int frame, int bone, skbBone_t& out) const;	// byte swapped

private:
	idStr					name;
	void* buffer;
	int						version;
	int						numFrames;
	int						numBones;
	float					frameTime;
	int						ofsNames;						// 0 for version 3
	int						ofsFrames;
	int						frameSize;

	idSKBAnim(const idSKBAnim&);				// not copyable, it owns the file buffer
	idSKBAnim& operator=(const idSKBAnim&);
};

class idSKBSkeleton {
public:
	struct joint_t {
		idStr				name;			// spaces replaced by underscores
		int					parent;			// joint number, -1 for "origin"
		int					bone;			// number in the .skb, -1 for an "origin" that was added
	};

	idSKBSkeleton();

	bool					Load(const char* skbName);

	int						NumJoints() const { return joints.Num(); }
	const joint_t& Joint(int i) const { return joints[i]; }
	int						JointOfBone(int bone) const;			// what to put in the weights, -1 if out of range
	bool					AddedOrigin() const { return addedOrigin; }
	int						JointOfName(const char* boneName) const;	// by name, spaces and all; -1 if there is no such joint

	// Model space transform of every joint as the maths sees it, no Doom 3 conventions: v' = r * v + t,
	// r is 9 floats per joint (row major), t is 3. anim NULL gives the .skb base pose.
	void					CalcWorld(const idSKBAnim* anim, const idList<int>* boneOfAnimBone, int frame, idList<float>& r, idList<float>& t) const;

	// Which .skb bone each bone of the animation drives: by name for version 4, by number for
	// version 3. Joints the animation doesn't drive keep their .skb base pose.
	bool					MapAnim(const idSKBAnim& anim, idList<int>& boneOfAnimBone) const;

	// Pose of every joint, relative to its parent (the "origin" in model space), in the form
	// idJointQuat wants. anim NULL gives the .skb base pose. This is the only place with maths in it.
	void					CalcBones(const idSKBAnim* anim, const idList<int>* boneOfAnimBone, int frame, idList<idJointQuat>& pose) const;

	// Writes <skaName without extension>.md5anim. True if it was written or already there.
	bool					WriteAnimToDisk(const char* skaName) const;

private:
	void					BuildWorld(const idSKBAnim* anim, const idList<int>* boneOfAnimBone, int frame, idList<skaQuat_t>& worldQ, idList<double>& worldT) const;

	idList<joint_t>			joints;
	idList<int>				jointOfBone;
	idList<int>				boneParent;		// in the .skb
	idList<skbBone_t>		baseFrame;		// byte swapped
	int						numBones;
	bool					addedOrigin;
};

#endif /* !__MODEL_SKB_H__ */