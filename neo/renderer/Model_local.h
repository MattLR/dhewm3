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

#ifndef __MODEL_LOCAL_H__
#define __MODEL_LOCAL_H__

#include "idlib/geometry/JointTransform.h"
#include "renderer/Model.h"

/*
===============================================================================

	Static model

===============================================================================
*/

class idRenderModelStatic : public idRenderModel {
public:
	// the inherited public interface
	static idRenderModel *		Alloc();

								idRenderModelStatic();
	virtual						~idRenderModelStatic();

	virtual void				InitFromFile( const char *fileName );
	virtual void				PartialInitFromFile( const char *fileName );
	virtual void				PurgeModel();
	virtual void				Reset() {};
	virtual void				LoadModel();
	virtual bool				IsLoaded() const;
	virtual void				SetLevelLoadReferenced( bool referenced );
	virtual bool				IsLevelLoadReferenced();
	virtual void				TouchData();
	virtual void				InitEmpty( const char *name );
	virtual void				AddSurface( modelSurface_t surface );
	virtual void				FinishSurfaces();
	virtual void				FreeVertexCache();
	virtual const char *		Name() const;
	virtual void				Print() const;
	virtual void				List() const;
	virtual int					Memory() const;
	virtual ID_TIME_T				Timestamp() const;
	virtual int					NumSurfaces() const;
	virtual int					NumBaseSurfaces() const;
	virtual const modelSurface_t *Surface( int surfaceNum ) const;
	virtual srfTriangles_t *	AllocSurfaceTriangles( int numVerts, int numIndexes ) const;
	virtual void				FreeSurfaceTriangles( srfTriangles_t *tris ) const;
	virtual srfTriangles_t *	ShadowHull() const;
	virtual bool				IsStaticWorldModel() const;
	virtual dynamicModel_t		IsDynamicModel() const;
	virtual bool				IsDefaultModel() const;
	virtual bool				IsReloadable() const;
	virtual idRenderModel *		InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel );
	virtual int					NumJoints( void ) const;
	virtual const idMD5Joint *	GetJoints( void ) const;
	virtual jointHandle_t		GetJointHandle( const char *name ) const;
	virtual const char *		GetJointName( jointHandle_t handle ) const;
	virtual const idJointQuat *	GetDefaultPose( void ) const;
	virtual int					NearestJoint( int surfaceNum, int a, int b, int c ) const;
	virtual idBounds			Bounds( const struct renderEntity_s *ent ) const;
	virtual void				ReadFromDemoFile( class idDemoFile *f );
	virtual void				WriteToDemoFile( class idDemoFile *f );
	virtual float				DepthHack() const;

	virtual  void				SetJoint(int j, int k, idStr n);

	void						MakeDefaultModel();

	bool						LoadASE( const char *fileName );
	bool						LoadLWO( const char *fileName );
	bool						LoadFLT( const char *fileName );
	bool						LoadMA( const char *filename );

	bool						ConvertASEToModelSurfaces( const struct aseModel_s *ase );
	bool						ConvertLWOToModelSurfaces( const struct st_lwObject *lwo );
	bool						ConvertMAToModelSurfaces (const struct maModel_s *ma );

	struct aseModel_s *			ConvertLWOToASE( const struct st_lwObject *obj, const char *fileName );

	bool						DeleteSurfaceWithId( int id );
	void						DeleteSurfacesWithNegativeId( void );
	bool						FindSurfaceWithId( int id, int &surfaceNum );

public:
	idList<modelSurface_t>		surfaces;
	idBounds					bounds;
	int							overlaysAdded;

protected:
	int							lastModifiedFrame;
	int							lastArchivedFrame;

	idStr						name;
	srfTriangles_t *			shadowHull;
	bool						isStaticWorldModel;
	bool						defaulted;
	bool						purged;					// eventually we will have dynamic reloading
	bool						fastLoad;				// don't generate tangents and shadow data
	bool						reloadable;				// if not, reloadModels won't check timestamp
	bool						levelLoadReferenced;	// for determining if it needs to be freed
	ID_TIME_T						timeStamp;

	static idCVar				r_mergeModelSurfaces;	// combine model surfaces with the same material
	static idCVar				r_slopVertex;			// merge xyz coordinates this far apart
	static idCVar				r_slopTexCoord;			// merge texture coordinates this far apart
	static idCVar				r_slopNormal;			// merge normals that dot less than this
};

/*
===============================================================================

	MD5 animated model

===============================================================================
*/

class idMD5Mesh {
	friend class				idRenderModelMD5;

public:
								idMD5Mesh();
								~idMD5Mesh();

	void						ParseMesh( idLexer &parser, int numJoints, const idJointMat *joints );
	void						UpdateSurface( const struct renderEntity_s *ent, const idJointMat *joints, modelSurface_t *surf );
	idBounds					CalcBounds( const idJointMat *joints );
	int							NearestJoint( int a, int b, int c ) const;
	int							NumVerts( void ) const;
	int							NumTris( void ) const;
	int							NumWeights( void ) const;

private:
	idList<idVec2>				texCoords;			// texture coordinates
	int							numWeights;			// number of weights
	idVec4 *					scaledWeights;		// joint weights
	int *						weightIndex;		// pairs of: joint offset + bool true if next weight is for next vertex
	const idMaterial *			shader;				// material applied to mesh
	int							numTris;			// number of triangles
	struct deformInfo_s *		deformInfo;			// used to create srfTriangles_t from base frames and new vertexes
	int							surfaceNum;			// number of the static surface created for this mesh

	void						TransformVerts( idDrawVert *verts, const idJointMat *joints );
	void						TransformScaledVerts( idDrawVert *verts, const idJointMat *joints, float scale );
};

class idRenderModelMD5 : public idRenderModelStatic {
public:
	virtual void				InitFromFile( const char *fileName );
	virtual dynamicModel_t		IsDynamicModel() const;
	virtual idBounds			Bounds( const struct renderEntity_s *ent ) const;
	virtual void				Print() const;
	virtual void				List() const;
	virtual void				TouchData();
	virtual void				PurgeModel();
	virtual void				LoadModel();
	virtual int					Memory() const;
	virtual idRenderModel *		InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel );
	virtual int					NumJoints( void ) const;
	virtual const idMD5Joint *	GetJoints( void ) const;
	virtual jointHandle_t		GetJointHandle( const char *name ) const;
	virtual const char *		GetJointName( jointHandle_t handle ) const;
	virtual const idJointQuat *	GetDefaultPose( void ) const;
	virtual int					NearestJoint( int surfaceNum, int a, int b, int c ) const;

private:
	idList<idMD5Joint>			joints;
	idList<idJointQuat>			defaultPose;
	idList<idMD5Mesh>			meshes;

	void						CalculateBounds( const idJointMat *joints );
	void						GetFrameBounds( const renderEntity_t *ent, idBounds &bounds ) const;
	void						DrawJoints( const renderEntity_t *ent, const struct viewDef_s *view ) const;
	void						ParseJoint( idLexer &parser, idMD5Joint *joint, idJointQuat *defaultPose );
};

/*
===============================================================================

	MD3 animated model

===============================================================================
*/

struct md3Header_s;
struct md3Surface_s;

struct silInfo_t {
	int numIndexes;
	int numSilEdges;
	glIndex_t* indexes;
	glIndex_t* silIndexes;
	silEdge_t* silEdges;
};

class idRenderModelMD3 : public idRenderModelStatic {
public:
	idRenderModelMD3();
	~idRenderModelMD3();

	virtual void				InitFromFile( const char *fileName );
	virtual dynamicModel_t		IsDynamicModel() const;
	virtual idRenderModel *		InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel );
	virtual idBounds			Bounds( const struct renderEntity_s *ent ) const;

private:
	int							index;			// model = tr.models[model->index]
	int							dataSize;		// just for listing purposes
	struct md3Header_s *		md3;			// only if type == MOD_MESH
	int							numLods;
	idList<const idMaterial*>	shaders;		// DG: md3Shader_t::shaderIndex indexes into this array

	// DG: added the following so we can generate/store sil edges for shadows
	idList<silInfo_t>			silInfos;		// used to create srfTriangles_t from base frames and new vertexes (md3->numSurfaces entries)

	struct silInfo_t			BuildSilInfo(struct md3Surface_s* surf);

	void						LerpMeshVertexes( srfTriangles_t *tri, const struct md3Surface_s *surf, const float backlerp, const int frame, const int oldframe ) const;
};

/*
===============================================================================

	MDC animated model

===============================================================================
*/

struct mdcHeader_s;
struct mdcSurface_s;

class idRenderModelMDC : public idRenderModelStatic {
public:
	idRenderModelMDC();
	~idRenderModelMDC();

	virtual void				InitFromFile(const char* fileName);
	virtual dynamicModel_t		IsDynamicModel() const;
	virtual idRenderModel* InstantiateDynamicModel(const struct renderEntity_s* ent, const struct viewDef_s* view, idRenderModel* cachedModel);
	virtual idBounds			Bounds(const struct renderEntity_s* ent) const;

private:
	int							index;			// model = tr.models[model->index]
	int							dataSize;		// just for listing purposes
	struct mdcHeader_s*			mdc;			// only if type == MOD_MESH
	int							numLods;
	idList<const idMaterial*>	shaders;		// DG: mdcShader_t::shaderIndex indexes into this array

	// DG: added the following so we can generate/store sil edges for shadows
	idList<silInfo_t>			silInfos;		// used to create srfTriangles_t from base frames and new vertexes (mdc->numSurfaces entries)

	struct silInfo_t			BuildSilInfo(struct mdcSurface_s* surf);

	void						LerpMeshVertexes(srfTriangles_t* tri, const struct mdcSurface_s* surf, const float backlerp, const int frame, const int oldframe) const;
};

/*
===============================================================================

	Liquid model

===============================================================================
*/

class idRenderModelLiquid : public idRenderModelStatic {
public:
								idRenderModelLiquid();

	virtual void				InitFromFile( const char *fileName );
	virtual dynamicModel_t		IsDynamicModel() const;
	virtual idRenderModel *		InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel );
	virtual idBounds			Bounds( const struct renderEntity_s *ent ) const;

	virtual void				Reset();
	void						IntersectBounds( const idBounds &bounds, float displacement );

private:
	modelSurface_t				GenerateSurface( float lerp );
	void						WaterDrop( int x, int y, float *page );
	void						Update( void );

	int							verts_x;
	int							verts_y;
	float						scale_x;
	float						scale_y;
	int							time;
	int							liquid_type;
	int							update_tics;
	int							seed;

	idRandom					random;

	const idMaterial *			shader;
	struct deformInfo_s	*		deformInfo;		// used to create srfTriangles_t from base frames
											// and new vertexes

	float						density;
	float						drop_height;
	int							drop_radius;
	float						drop_delay;

	idList<float>				pages;
	float *						page1;
	float *						page2;

	idList<idDrawVert>			verts;

	int							nextDropTime;

};

/*
===============================================================================

	PRT model

===============================================================================
*/

class idRenderModelPrt : public idRenderModelStatic {
public:
								idRenderModelPrt();

	virtual void				InitFromFile( const char *fileName );
	virtual void				TouchData();
	virtual dynamicModel_t		IsDynamicModel() const;
	virtual idRenderModel *		InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel );
	virtual idBounds			Bounds( const struct renderEntity_s *ent ) const;
	virtual float				DepthHack() const;
	virtual int					Memory() const;

	float						SofteningRadius( const int stage ) const;	// #3878 soft particles

private:
	void						SetSofteningRadii(); // #3878 soft particles

	const idDeclParticle *		particleSystem;
	idList<float>				softeningRadii; // #3878 soft particles
};

/*
===============================================================================

	Beam model

===============================================================================
*/

class idRenderModelBeam : public idRenderModelStatic {
public:
	virtual dynamicModel_t		IsDynamicModel() const;
	virtual bool				IsLoaded() const;
	virtual idRenderModel *		InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel );
	virtual idBounds			Bounds( const struct renderEntity_s *ent ) const;
};

/*
===============================================================================

	Beam model

===============================================================================
*/
#define MAX_TRAIL_PTS	20

struct Trail_t {
	int							lastUpdateTime;
	int							duration;

	idVec3						pts[MAX_TRAIL_PTS];
	int							numPoints;
};

class idRenderModelTrail : public idRenderModelStatic {
	idList<Trail_t>				trails;
	int							numActive;
	idBounds					trailBounds;

public:
								idRenderModelTrail();

	virtual dynamicModel_t		IsDynamicModel() const;
	virtual bool				IsLoaded() const;
	virtual idRenderModel *		InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel );
	virtual idBounds			Bounds( const struct renderEntity_s *ent ) const;

	int							NewTrail( idVec3 pt, int duration );
	void						UpdateTrail( int index, idVec3 pt );
	void						DrawTrail( int index, const struct renderEntity_s *ent, srfTriangles_t *tri, float globalAlpha );
};

/*
===============================================================================

	Lightning model

===============================================================================
*/

class idRenderModelLightning : public idRenderModelStatic {
public:
	virtual dynamicModel_t		IsDynamicModel() const;
	virtual bool				IsLoaded() const;
	virtual idRenderModel *		InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel );
	virtual idBounds			Bounds( const struct renderEntity_s *ent ) const;
};

/*
================================================================================

	idRenderModelSprite

================================================================================
*/
class idRenderModelSprite : public idRenderModelStatic {
public:
	virtual	dynamicModel_t	IsDynamicModel() const;
	virtual	bool			IsLoaded() const;
	virtual	idRenderModel *	InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel );
	virtual	idBounds		Bounds( const struct renderEntity_s *ent ) const;
};

struct animInfo_t {
	idStr		animName;
	int			animInfo[6];	// The 6 int values from wolfanim
};

/*
===============================================================================

	Raven MDR animated model

===============================================================================
*/
// jmarshall
class rvmRenderModelMDR : public idRenderModelStatic {
public:
	rvmRenderModelMDR();
	~rvmRenderModelMDR();

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
	virtual const idJointQuat*	GetDefaultPose(void) const;
	virtual int					NearestJoint(int surfaceNum, int a, int b, int c) const;
private:
	bool						LoadMDRFile(void* buffer, int filesize);
	void						RenderFramesToModel(idRenderModelStatic* staticModel, const struct idRenderEntityParms* ent, const struct viewDef_s* view, const idMaterial* overrideMaterial, int scale, int frame, const renderEntity_t* ent2);
	bool						WriteAnimToDisk();
	void						CalcBones(idList<idJointMat>& boneData, int currentFrame);
	void						ParseAnims(idList <animInfo_t>& animInfo, int& numAnims);

	idList<idMD5Joint>			joints;
	idList<idJointQuat>			defaultPose;
	//idList<idMDRMesh>			meshes;

	int							numLods;

	idBounds					bounds;

	idList<struct mdrFrame_t*>	frames;
	idList<const idMaterial*>	materials;

	int							dataSize;
	struct mdrHeader_t*			modelData;

	idRenderModelStatic*		cachedModel;

	int							cFrame;

	void						DrawJoints(const renderEntity_t* ent, const struct viewDef_s* view) const;

};
// jmarshall end

// Model_mdr.h
//

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
	int			boneIndex;		// these are indexes into the boneReferences,
	float		boneWeight;		// not the global per-frame bone list
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
	//int			flags; //Fix for offset - don't know what this is Dynamix
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

	int			ofsEnd;		// next surface follows
};

struct mdrBone_t {
	float		matrix[3][4];
};

struct mdrFrame_t {
	idVec3		bounds[2];		// bounds of all surfaces of all LOD's for this frame
	idVec3		localOrigin;		// midpoint of bounds, used for sphere cull
	float		radius;			// dist from localOrigin to corner
	char		name[16];
	mdrBone_t	bones[1];		// [numBones]
};

struct mdrCompBone_t {
	unsigned char Comp[24]; // MC_COMP_BYTES is in MatComp.h, but don't want to couple
};

struct mdrCompFrame_t {
	idVec3          bounds[2];		// bounds of all surfaces of all LOD's for this frame
	idVec3          localOrigin;		// midpoint of bounds, used for sphere cull
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
	char        name[32];
};

struct mdrHeader_t {
	int			ident;
	int			version;

	char		name[MAX_MD3PATH];	// model name

	// frames and bones are shared by all levels of detail
	int			numFrames;
	int			numBones;
	int			ofsFrames;			// mdrFrame_t[numFrames]

	// each level of detail has completely separate sets of surfaces
	int			numLODs;
	int			ofsLODs;

	int			numTags;
	int			ofsTags;

	int			ofsEnd;				// end of file
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

ID_INLINE void MC_UnCompress(float mat[3][4], const unsigned char* comp)
{
	int val;

	val = (int)((unsigned short*)(comp))[0];
	val -= 1 << (MC_BITS_X - 1);
	mat[0][3] = ((float)(val)) * MC_SCALE_X;

	val = (int)((unsigned short*)(comp))[1];
	val -= 1 << (MC_BITS_Y - 1);
	mat[1][3] = ((float)(val)) * MC_SCALE_Y;

	val = (int)((unsigned short*)(comp))[2];
	val -= 1 << (MC_BITS_Z - 1);
	mat[2][3] = ((float)(val)) * MC_SCALE_Z;

	val = (int)((unsigned short*)(comp))[3];
	val -= 1 << (MC_BITS_VECT - 1);
	mat[0][0] = ((float)(val)) * MC_SCALE_VECT;

	val = (int)((unsigned short*)(comp))[4];
	val -= 1 << (MC_BITS_VECT - 1);
	mat[0][1] = ((float)(val)) * MC_SCALE_VECT;

	val = (int)((unsigned short*)(comp))[5];
	val -= 1 << (MC_BITS_VECT - 1);
	mat[0][2] = ((float)(val)) * MC_SCALE_VECT;


	val = (int)((unsigned short*)(comp))[6];
	val -= 1 << (MC_BITS_VECT - 1);
	mat[1][0] = ((float)(val)) * MC_SCALE_VECT;

	val = (int)((unsigned short*)(comp))[7];
	val -= 1 << (MC_BITS_VECT - 1);
	mat[1][1] = ((float)(val)) * MC_SCALE_VECT;

	val = (int)((unsigned short*)(comp))[8];
	val -= 1 << (MC_BITS_VECT - 1);
	mat[1][2] = ((float)(val)) * MC_SCALE_VECT;


	val = (int)((unsigned short*)(comp))[9];
	val -= 1 << (MC_BITS_VECT - 1);
	mat[2][0] = ((float)(val)) * MC_SCALE_VECT;

	val = (int)((unsigned short*)(comp))[10];
	val -= 1 << (MC_BITS_VECT - 1);
	mat[2][1] = ((float)(val)) * MC_SCALE_VECT;

	val = (int)((unsigned short*)(comp))[11];
	val -= 1 << (MC_BITS_VECT - 1);
	mat[2][2] = ((float)(val)) * MC_SCALE_VECT;
}

#endif /* !__MODEL_LOCAL_H__ */
