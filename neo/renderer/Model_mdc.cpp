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

#include "sys/platform.h"
#include "framework/Session.h"
#include "renderer/tr_local.h"
#include "renderer/Model_local.h"

#include "renderer/Model_mdc.h"

/***********************************************************************

	idMDCMesh

***********************************************************************/

// DG: added constructor to make sure all members are initialized
idRenderModelMDC::idRenderModelMDC() : index(-1), dataSize(0), mdc(NULL), numLods(0)
{}

// DG: added destructor to clean up the newly added silInfos (and MDC which used to leak)
idRenderModelMDC::~idRenderModelMDC() {
	srfTriangles_t tri;
	memset( &tri, 0, sizeof( tri ) );

	for ( int i=0, n=silInfos.Num() ; i < n ; ++i ) {
		silInfo_t& silInfo = silInfos[i];
		// Note: Because tri.sil* was allocated by specialized allocators only available
		//       in tr.trisurf.cpp, they need to be free'd with them as well.
		//       The easiest way to do this is by putting them into a srfTriangles_t first
		tri.silIndexes = silInfo.silIndexes;
		tri.silEdges = silInfo.silEdges;

		R_FreeStaticTriSurfSilIndexes( &tri );
		R_FreeStaticTriSurfSilEdges( &tri );

		if ( sizeof(mdcTriangle_t::indexes[0]) != sizeof(silInfo.indexes[0]) ) {
			tri.indexes = silInfo.indexes;
			R_FreeStaticTriSurfIndexes( &tri );
		} // else silInfos[i].indexes points directly into the corresponding data in this->MDC
	}
	silInfos.Clear();

	Mem_Free(mdc);
}

#define	LL(x) x=LittleInt(x)

/*
=================
idRenderModelMDC::InitFromFile
=================
*/
void idRenderModelMDC::InitFromFile( const char *fileName ) {
	int					i, j;
	mdcHeader_t			*pinmodel;
	mdcFrame_t			*frame;
	mdcSurface_t		*surf;
	mdcShader_t			*shader;
	mdcTriangle_t		*tri;
	mdcSt_t				*st;
	mdcXyzNormal_t		*xyz;
	mdcXyzCompressed_t* xyzComp;
	mdcTag_t			*tag;
	short*				ps;
	void				*buffer = NULL;
	int					version;
	int					size;


	name = fileName;

	size = fileSystem->ReadFile( fileName, &buffer, NULL );
	// DG: without the int cast, an unsigned comparison is made which fails for size = -1
	if ( size <= (int)sizeof(mdcHeader_t) ) {
		if (buffer != NULL) {
			fileSystem->FreeFile( buffer );
		}
		// DG: must make it a default model so failure can be detected elsewhere
		MakeDefaultModel();
		return;
	}

	pinmodel = (mdcHeader_t *)buffer;
	int ident = LittleInt(pinmodel->ident);
	version = LittleInt(pinmodel->version);

	if (ident == MDC_IDENT) {
		if (version != MDC_VERSION) {
			fileSystem->FreeFile(buffer);
			common->Warning("InitFromFile: %s has wrong version (%i should be %i)",
				fileName, version, MDC_VERSION);
			MakeDefaultModel();
			return;
		}
	}

	
	if (version != MDC_VERSION) {
		fileSystem->FreeFile( buffer );
		common->Warning( "InitFromFile: %s has wrong version (%i should be %i)",
				 fileName, version, MDC_VERSION);
		MakeDefaultModel();
		return;
	}

	size = LittleInt(pinmodel->ofsEnd);
	dataSize += size;
	mdc = (mdcHeader_t *)Mem_Alloc( size );

	memcpy (mdc, buffer, LittleInt(pinmodel->ofsEnd) );

	LL(mdc->ident);
	LL(mdc->version);
	LL(mdc->numFrames);
	LL(mdc->numTags);
	LL(mdc->numSurfaces);
	LL(mdc->ofsFrames);
	LL(mdc->ofsTags);
	LL(mdc->ofsSurfaces);
	LL(mdc->ofsEnd);
	LL(mdc->flags);
	LL(mdc->numSkins);

	//Dynamix for testing
	//common->Printf("Blah blah frames %5i\n", mdc->numFrames);
	if (mdc->numFrames < 1 ) {
		common->Warning( "InitFromFile: %s has no frames", fileName );
		fileSystem->FreeFile( buffer );
		MakeDefaultModel();
		return;
	}

	// swap all the frames
	frame = (mdcFrame_t *) ( (byte *)mdc + mdc->ofsFrames );
	for ( i = 0 ; i < mdc->numFrames ; i++, frame++) {
		frame->radius = LittleFloat( frame->radius );
		for ( j = 0 ; j < 3 ; j++ ) {
			frame->bounds[0][j] = LittleFloat( frame->bounds[0][j] );
			frame->bounds[1][j] = LittleFloat( frame->bounds[1][j] );
			frame->localOrigin[j] = LittleFloat( frame->localOrigin[j] );
		}
	}

	// swap all the tags
	tag = (mdcTag_t *) ( (byte *)mdc + mdc->ofsTags );
	for ( i = 0 ; i < mdc->numTags * mdc->numFrames ; i++, tag++) {
		for ( j = 0 ; j < 3 ; j++ ) {
			tag->xyz[j] = LittleShort(tag->xyz[j]);
			tag->angles[j] = LittleShort(tag->angles[j]);
		}
	}

	// swap all the surfaces
	surf = (mdcSurface_t *) ( (byte *)mdc + mdc->ofsSurfaces );
	for ( i = 0 ; i < mdc->numSurfaces ; i++) {

		LL(surf->ident);
		LL(surf->flags);
		LL(surf->numBaseFrames);
		LL(surf->numCompFrames);
		LL(surf->numShaders);
		LL(surf->numTriangles);
		LL(surf->ofsTriangles);
		LL(surf->numVerts);
		LL(surf->ofsShaders);
		LL(surf->ofsSt);
		LL(surf->ofsXyzNormals);
		LL(surf->ofsXyzCompressed);
		LL(surf->ofsFrameBaseFrames);
		LL(surf->ofsFrameCompFrames);
		LL(surf->ofsEnd);

		if ( surf->numVerts > SHADER_MAX_VERTEXES ) {
			common->Error( "InitFromFile: %s has more than %i verts on a surface (%i)",
				fileName, SHADER_MAX_VERTEXES, surf->numVerts );
		}
		if ( surf->numTriangles*3 > SHADER_MAX_INDEXES ) {
			common->Error( "InitFromFile: %s has more than %i triangles on a surface (%i)",
				fileName, SHADER_MAX_INDEXES / 3, surf->numTriangles );
		}

		// change to surface identifier
		surf->ident = 0;	//SF_MDC;

		// lowercase the surface name so skin compares are faster
		int slen = (int)strlen( surf->name );
		for( j = 0; j < slen; j++ ) {
			surf->name[j] = tolower( surf->name[j] );
		}

		// strip off a trailing _1 or _2
		// this is a crutch for q3data being a mess
		j = strlen( surf->name );
		if ( j > 2 && surf->name[j-2] == '_' ) {
			surf->name[j-2] = 0;
		}

		// register the shaders
		shader = (mdcShader_t *) ( (byte *)surf + surf->ofsShaders );
		for ( j = 0 ; j < surf->numShaders ; j++, shader++ ) {
			const idMaterial *sh = declManager->FindMaterial( shader->name );
			// DG: MDCShader_t must use an index to the material instead of a pointer,
			//     otherwise the sizes are wrong on 64bit and we get data corruption
			shader->shaderIndex = (sh != NULL) ? shaders.AddUnique( sh ) : -1;
		}

		// swap all the triangles
		tri = (mdcTriangle_t *) ( (byte *)surf + surf->ofsTriangles );
		for ( j = 0 ; j < surf->numTriangles ; j++, tri++ ) {
			LL(tri->indexes[0]);
			LL(tri->indexes[1]);
			LL(tri->indexes[2]);
		}

		// swap all the ST
		st = (mdcSt_t *) ( (byte *)surf + surf->ofsSt );
		for ( j = 0 ; j < surf->numVerts ; j++, st++ ) {
			st->st[0] = LittleFloat( st->st[0] );
			st->st[1] = LittleFloat( st->st[1] );
		}

		// swap all the XyzNormals
		xyz = (mdcXyzNormal_t *) ( (byte *)surf + surf->ofsXyzNormals );
		for ( j = 0 ; j < surf->numVerts * surf->numBaseFrames ; j++, xyz++ )
		{
			xyz->xyz[0] = LittleShort( xyz->xyz[0] );
			xyz->xyz[1] = LittleShort( xyz->xyz[1] );
			xyz->xyz[2] = LittleShort( xyz->xyz[2] );

			xyz->normal = LittleShort( xyz->normal );
		}

		// swap all the XyzCompressed
		xyzComp = (mdcXyzCompressed_t*)((byte*)surf + surf->ofsXyzCompressed);
		for (j = 0; j < surf->numVerts * surf->numCompFrames; j++, xyzComp++)
		{
			LL(xyzComp->ofsVec);
		}

		// swap the frameBaseFrames
		ps = (short*)((byte*)surf + surf->ofsFrameBaseFrames);
		for (j = 0; j < mdc->numFrames; j++, ps++)
		{
			*ps = LittleShort(*ps);
		}

		// swap the frameCompFrames
		ps = (short*)((byte*)surf + surf->ofsFrameCompFrames);
		for (j = 0; j < mdc->numFrames; j++, ps++)
		{
			*ps = LittleShort(*ps);
		}

		silInfos.Append( BuildSilInfo( surf ) );

		// find the next surface
		surf = (mdcSurface_t *)( (byte *)surf + surf->ofsEnd );
	}

	// Default pose stuff for tags later Dynamix
	defaultPose.SetGranularity(1);
	defaultPose.SetNum(mdc->numTags);

	fileSystem->FreeFile( buffer );
}

/*
=================
idRenderModelMDC::IsDynamicModel
=================
*/
dynamicModel_t idRenderModelMDC::IsDynamicModel() const {
	return DM_CACHED;
}

/*
====================
idRenderModelMDC::GetDefaultPose
====================
*/
const idJointQuat* idRenderModelMDC::GetDefaultPose(void) const {
	return defaultPose.Ptr();
}

/*
=================
idRenderModelMDC::LerpMeshVertexes
=================
*/
void idRenderModelMDC::LerpMeshVertexes ( srfTriangles_t *tri, const struct mdcSurface_s *surf, const float backlerp, const int frame, const int oldframe ) const {
	short	*oldXyz, *newXyz;
	float	oldXyzScale, newXyzScale;
	int		vertNum;
	int		numVerts;
	idVec3 outNormal; //Filler, unused I think Dynamix

	int oldBase, newBase;
	short* oldComp = NULL, * newComp = NULL; // TTimo: init
	mdcXyzCompressed_t* oldXyzComp = NULL, * newXyzComp = NULL; // TTimo: init
	idVec3 oldOfsVec, newOfsVec;

	newBase = (int)*((short*)((byte*)surf + surf->ofsFrameBaseFrames) + frame);
	//newXyz = (short *)((byte *)surf + surf->ofsXyzNormals) + (frame * surf->numVerts * 4);
	newXyz = (short *)((byte *)surf + surf->ofsXyzNormals) + (newBase * surf->numVerts * 4);
	newXyzScale = MDC_XYZ_SCALE * (1.0 - backlerp);

	numVerts = surf->numVerts;

	bool hasComp;
	hasComp = (surf->numCompFrames > 0);
	if (hasComp) {
		newComp = ((short*)((byte*)surf + surf->ofsFrameCompFrames) + frame);
		if (*newComp >= 0) {
			newXyzComp = (mdcXyzCompressed_t*)((byte*)surf + surf->ofsXyzCompressed)
				+ (*newComp * surf->numVerts);
		}
	}

	newXyzScale = MDC_XYZ_SCALE * (1.0 - backlerp);
	//newNormalScale = 1.0 - backlerp; - Normals unused for now? Dynamix

	if ( backlerp == 0 ) {
		//
		// just copy the vertexes
		//
		for (vertNum=0 ; vertNum < numVerts ; vertNum++, newXyz += 4 ) {

			idDrawVert *outvert = &tri->verts[tri->numVerts];

			outvert->xyz.x = newXyz[0] * newXyzScale;
			outvert->xyz.y = newXyz[1] * newXyzScale;
			outvert->xyz.z = newXyz[2] * newXyzScale;

			// add the compressed ofsVec
			if (hasComp && *newComp >= 0) {
				R_MDC_DecodeXyzCompressed(newXyzComp->ofsVec, newOfsVec);
				newXyzComp++;
				VectorAdd(outvert->xyz, newOfsVec, outvert->xyz);
			}
			tri->numVerts++;
		}
	} else {
		//
		// interpolate and copy the vertex (and normal?)
		//
		oldBase = (int)*((short*)((byte*)surf + surf->ofsFrameBaseFrames) + oldframe);
		//oldXyz = (short *)((byte *)surf + surf->ofsXyzNormals) + (oldframe * surf->numVerts * 4);
		oldXyz = (short*)((byte*)surf + surf->ofsXyzNormals)
			+ (oldBase * surf->numVerts * 4);

		if (hasComp) {
			oldComp = ((short*)((byte*)surf + surf->ofsFrameCompFrames) + oldframe);
			if (*oldComp >= 0) {
				oldXyzComp = (mdcXyzCompressed_t*)((byte*)surf + surf->ofsXyzCompressed)
					+ (*oldComp * surf->numVerts);
			}
		}

		oldXyzScale = MDC_XYZ_SCALE * backlerp;

		for (vertNum = 0; vertNum < numVerts; vertNum++, oldXyz += 4, newXyz += 4 ) {

			idDrawVert *outvert = &tri->verts[tri->numVerts];

			// interpolate the xyz
			outvert->xyz.x = oldXyz[0] * oldXyzScale + newXyz[0] * newXyzScale;
			outvert->xyz.y = oldXyz[1] * oldXyzScale + newXyz[1] * newXyzScale;
			outvert->xyz.z = oldXyz[2] * oldXyzScale + newXyz[2] * newXyzScale;

			// add the compressed ofsVec
			if (hasComp && *newComp >= 0) {
				R_MDC_DecodeXyzCompressed(newXyzComp->ofsVec, newOfsVec, uncompressedNewNormal);
				newXyzComp++;
				VectorMA(outvert->xyz, 1.0 - backlerp, newOfsVec, outvert->xyz);
			}
			if (hasComp && *oldComp >= 0) {
				R_MDC_DecodeXyzCompressed(oldXyzComp->ofsVec, oldOfsVec, uncompressedOldNormal);
				oldXyzComp++;
				VectorMA(outvert->xyz, backlerp, oldOfsVec, outvert->xyz);
			}


			tri->numVerts++;
		}
	}
}

/*
=============
idRenderModelMDC::InstantiateDynamicModel
=============
*/
idRenderModel *idRenderModelMDC::InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel ) {
	int				i, j;
	float			backlerp;
	float *			texCoords;
	int				numVerts;
	mdcSurface_t *	surface;
	int				frame, oldframe;
	idRenderModelStatic	*staticModel;

	int numFrames = mdc->numFrames;

	// TODO: these need set by an entity
	frame = idMath::ClampInt(0, numFrames-1, ent->shaderParms[SHADERPARM_MD3_FRAME]);	// probably want to keep frames < 1000 or so
	oldframe = idMath::ClampInt(0, numFrames-1,ent->shaderParms[SHADERPARM_MD3_LASTFRAME]);
	backlerp = ent->shaderParms[SHADERPARM_MD3_BACKLERP];

	if ( cachedModel ) {
		delete cachedModel;
		cachedModel = NULL;
	}

	staticModel = new idRenderModelStatic;
	staticModel->InitEmpty("_MDC_Snapshot_");
	staticModel->bounds.Clear();

	// Dynamix 
	if (r_showSkel.GetInteger()) {
		if ((view != NULL) && (!r_skipSuppress.GetBool() || !ent->suppressSurfaceInViewID || (ent->suppressSurfaceInViewID != view->renderView.viewID))) {
			// only draw the skeleton
			DrawTags(ent, view);
		}
	}

	// Set the tag positions as joint quats so we can get them on the game side
	//int currentFrame = ent->shaderParms[SHADERPARM_MD3_FRAME];
	mdcTag_t* tag;
	mdcTag_t* oldTag;
	idJointQuat* pose;

	pose = defaultPose.Ptr();
	tag = (mdcTag_t*)((byte*)mdc + mdc->ofsTags);
	oldTag = (mdcTag_t*)((byte*)mdc + mdc->ofsTags);
	oldTag += oldframe  * mdc->numTags;
	tag += frame * mdc->numTags;
	float frontlerp = 1.0f - backlerp;
	for (i = 0; i < mdc->numTags; i++, oldTag++, tag++, pose++) {
		idVec3 oldPos;
		idVec3 newPos;
		idVec3 pos;
		idQuat oldJ, newJ;

		oldPos.x = oldTag->xyz[0] * MDC_XYZ_SCALE;
		oldPos.y = oldTag->xyz[1] * MDC_XYZ_SCALE;
		oldPos.z = oldTag->xyz[2] * MDC_XYZ_SCALE;

		newPos.x = tag->xyz[0] * MDC_XYZ_SCALE;
		newPos.y = tag->xyz[1] * MDC_XYZ_SCALE;
		newPos.z = tag->xyz[2] * MDC_XYZ_SCALE;

		//pos.x = oldPos.x * backlerp + newPos.x * frontlerp;
		////pos.y = oldPos.y * backlerp + newPos.y * frontlerp;
		//pos.z = oldPos.z * backlerp + newPos.z * frontlerp;

		pos.Lerp(oldPos, newPos, frontlerp);
		//common->Printf("Old pos %f %f %f\n New pos %f %f %f\n pos: %f %f %f\n", oldPos.x, oldPos.y, oldPos.z
		//	, newPos.x, newPos.y, newPos.z, pos.x, pos.y, pos.z);

		pose->t = pos;
		//pose->t = newPos;
		oldJ = idAngles(
			oldTag->angles[0] * (360.0f / 32700.0f),
			oldTag->angles[1] * (360.0f / 32700.0f),
			oldTag->angles[2] * (360.0f / 32700.0f)
			).ToQuat();

		newJ = idAngles(
			tag->angles[0] * (360.0f / 32700.0f),
			tag->angles[1] * (360.0f / 32700.0f),
			tag->angles[2] * (360.0f / 32700.0f)
		).ToQuat();

		pose->q.Slerp(oldJ, newJ, frontlerp);
	}

	surface = (mdcSurface_t *) ((byte *)mdc + mdc->ofsSurfaces);

	for( i = 0; i < mdc->numSurfaces; i++ ) {

		srfTriangles_t *tri = R_AllocStaticTriSurf();
		R_AllocStaticTriSurfVerts( tri, surface->numVerts );

		// DG: set sil edges for shadows
		silInfo_t& silInfo = silInfos[i];
		tri->numSilEdges = silInfo.numSilEdges;
		tri->silEdges = silInfo.silEdges;
		tri->silIndexes = silInfo.silIndexes;

		tri->indexes = silInfo.indexes;
		tri->numIndexes = silInfo.numIndexes;
		assert(surface->numTriangles * 3 == silInfo.numIndexes);

		// deformedSurface prevents silEdges, silIndexes, indexes (and more not used here)
		// from being freed (they belong to silInfo which is freed in ~idRenderModelMDC())
		tri->deformedSurface = true;

		tri->bounds.Clear();

		modelSurface_t	surf;

		surf.geometry = tri;

		mdcShader_t* shaders = (mdcShader_t *) ((byte *)surface + surface->ofsShaders);
		// FIXME: theoretically there can be multiple shaders?
		// DG: turned MDCShader_t::shader (pointer) into an int (index)
		int shaderIdx = shaders->shaderIndex;
		surf.shader = (shaderIdx >= 0) ? this->shaders[shaderIdx] : NULL;

		LerpMeshVertexes( tri, surface, backlerp, frame, oldframe );

		texCoords = (float *) ((byte *)surface + surface->ofsSt);

		numVerts = surface->numVerts;
		for ( j = 0; j < numVerts; j++ ) {
			idDrawVert *stri = &tri->verts[j];
			stri->st[0] = texCoords[j*2+0];
			stri->st[1] = texCoords[j*2+1];
		}

		R_BoundTriSurf( tri );

		surf.id = staticModel->NumSurfaces(); // DG: make sure to initialize id; FIXME: or just set id to 0?
		staticModel->AddSurface( surf );
		staticModel->bounds.AddPoint( surf.geometry->bounds[0] );
		staticModel->bounds.AddPoint( surf.geometry->bounds[1] );

		// find the next surface
		surface = (mdcSurface_t *)( (byte *)surface + surface->ofsEnd );
	}

	return staticModel;
}

/*
=====================
idRenderModelMDC::Bounds
=====================
*/

idBounds idRenderModelMDC::Bounds(const struct renderEntity_s *ent) const {
	idBounds		ret;

	ret.Clear();

	if (!ent || !mdc) {
		// just give it the editor bounds
		ret.AddPoint(idVec3(-10,-10,-10));
		ret.AddPoint(idVec3( 10, 10, 10));
		return ret;
	}

	mdcFrame_t	*frame = (mdcFrame_t *)( (byte *)mdc + mdc->ofsFrames );
	int frameNum = Max(0, Min(mdc->numFrames-1, (int)ent->shaderParms[SHADERPARM_MD3_FRAME]));
	frame += frameNum; // DG: use bounds of current frame

	ret.AddPoint( frame->bounds[0] );
	ret.AddPoint( frame->bounds[1] );

	return ret;
}

/*
=====================
idRenderModelMDC::BuildSilInfo
=====================
*/

silInfo_t idRenderModelMDC::BuildSilInfo(mdcSurface_t *surf ) {
	//
	// build the information that will be common to all animations of this surface:
	// silhouette edge connectivity and normal / tangent generation information
	//
	int numVerts = surf->numVerts;
	silInfo_t ret = {0};

	mdcXyzNormal_t *xyzs = (mdcXyzNormal_t *) ( (byte *)surf + surf->ofsXyzNormals );

	bool onStack;
	idDrawVert *verts = (idDrawVert*)Mem_MallocA( numVerts*sizeof(idDrawVert), onStack );

	for ( int i=0; i < numVerts; ++i ) {
		idDrawVert& v = verts[i];
		v.Clear();
		// Note: MDC defines these for all frames - using frame 0 here
		short* xyz = xyzs[i].xyz;
		v.xyz.x = xyz[0] * float(MDC_XYZ_SCALE);
		v.xyz.y = xyz[1] * float(MDC_XYZ_SCALE);
		v.xyz.z = xyz[2] * float(MDC_XYZ_SCALE);
		// Note: idDrawVert has more fields, but R_CreateSilIndexes() and R_IdentifySilEdges()
		//       only use .xyz
	}

	int numIndexes = surf->numTriangles * 3;
	mdcTriangle_t * mdcTriIdx = (mdcTriangle_t *) ( (byte *)surf + surf->ofsTriangles );
	int *triIndexes = (int*)mdcTriIdx->indexes;

	srfTriangles_t tri;
	memset( &tri, 0, sizeof( tri ) );

	tri.verts = verts;
	tri.numVerts = numVerts;
	tri.numIndexes = numIndexes;
	if ( sizeof(triIndexes[0]) == sizeof(tri.indexes[0]) ) {
		tri.indexes = triIndexes;
	} else {
		R_AllocStaticTriSurfIndexes( &tri, numIndexes );
		// don't memcpy, so we can change the index type from int to short without changing the interface
		for ( int i = 0 ; i < numIndexes ; i++ ) {
			tri.indexes[i] = triIndexes[i];
		}
	}
	R_CreateSilIndexes(&tri); // allocates and sets tri.silIndexes with triSilIndexAllocator
	// if there's only one frame (no animation), the omitCoplanarEdges optimization can be used
	// (otherwise it can't because edges that are coplanar in frame 0 may not be in another frame)
	bool omitCoplanarEdges = (surf->numBaseFrames == 1);
	R_IdentifySilEdges(&tri, omitCoplanarEdges); // allocates and sets tri.silEdges with triSilEdgeAllocator

	ret.numIndexes = tri.numIndexes;
	ret.numSilEdges = tri.numSilEdges;
	ret.indexes = tri.indexes;
	ret.silIndexes = tri.silIndexes;
	ret.silEdges = tri.silEdges;

	Mem_FreeA( verts, onStack );

	return ret;
}

/*
====================
idRenderModelMDC::DrawTags
====================
*/
void idRenderModelMDC::DrawTags(const renderEntity_t* ent, const struct viewDef_s* view) const {
	int					i, j;
	int					num;
	idVec3				pos;
	mdcTag_t* tag;
	mdcTagName_t* tagName;
	idVec3				origin;
	
	int currentFrame = ent->shaderParms[SHADERPARM_MD3_FRAME];
	tag = (mdcTag_t*)((byte*)mdc + mdc->ofsTags);
	tag += currentFrame * mdc->numTags;

	tagName = (mdcTagName_t*)((byte*)mdc + mdc->ofsTagNames);

	for (i = 0; i < mdc->numTags; i++, tag++, tagName++) {
		origin.x = tag->xyz[0] * MDC_XYZ_SCALE;
		origin.y = tag->xyz[1] * MDC_XYZ_SCALE;
		origin.z = tag->xyz[2] * MDC_XYZ_SCALE;
		//tag->axis[0][j] = LittleFloat(tag->axis[0][j]);
		//tag->axis[1][j] = LittleFloat(tag->axis[1][j]);
		//tag->axis[2][j] = LittleFloat(tag->axis[2][j]);

		idVec3	offset(0, 0, r_jointNameOffset.GetFloat());
		float scale = r_jointNameScale.GetFloat();
		pos = ent->origin + origin * ent->axis;
		session->rw->DrawText(tagName->name, pos + offset, scale, colorWhite, view->renderView.viewaxis, 1);
	}
	
	/*
	if ((r_jointNameScale.GetFloat() != 0.0f) && (bounds.Expand(128.0f).ContainsPoint(view->renderView.vieworg - ent->origin))) {
		idVec3	offset(0, 0, r_jointNameOffset.GetFloat());
		float	scale;

		scale = r_jointNameScale.GetFloat();
		num = md3->numTags;
		for (i = 0; i < num; i++) {
			pos = ent->origin + tag->origin * ent->axis;
			session->rw->DrawText(tag->name, pos + offset, scale, colorWhite, view->renderView.viewaxis, 1);
		}
	}
	*/
}

/*
====================
idRenderModelMDC::GetJointHandle
====================
*/
jointHandle_t idRenderModelMDC::GetJointHandle(const char* name) const {
	int	i;

	mdcTagName_t* tagName;

	tagName = (mdcTagName_t*)((byte*)mdc + mdc->ofsTagNames);

	for (i = 0; i < mdc->numTags; i++, tagName++) {
		if (idStr::Icmp(tagName->name, name) == 0) {
			return (jointHandle_t)i;
		}
	}

	return INVALID_JOINT;
}

/*
=====================
idRenderModelMDC::GetJointName
=====================
*/
const char* idRenderModelMDC::GetJointName(jointHandle_t handle) const {
	if ((handle < 0) || (handle >= mdc->numTags)) {
		return "<invalid joint>";
	}

	mdcTagName_t* tagName;

	tagName = (mdcTagName_t*)((byte*)mdc + mdc->ofsTagNames);
	tagName += handle;

	return tagName->name;

}