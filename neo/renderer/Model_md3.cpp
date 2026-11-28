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
#include "idlib/Lexer.h"

#include "renderer/Model_md3.h"

/***********************************************************************

	idRenderModelMD3

***********************************************************************/

#define	LL(x) x=LittleInt(x)

/*
=================
R_MD3_RangeOK

Checks that count elements of elemSize bytes starting at ofs fit inside limit.
64 bit maths so hostile counts can't overflow.
=================
*/
static bool R_MD3_RangeOK( int ofs, long long count, int elemSize, int limit ) {
	if ( count < 0 ) {
		return false;
	}
	if ( count == 0 ) {
		return true;
	}
	if ( ofs < 0 ) {
		return false;
	}
	return ( (long long)ofs + count * (long long)elemSize ) <= (long long)limit;
}

/*
=================
R_MD3_SwapAndValidate

Byte swaps the whole file in place and validates every offset, count and
index before anything dereferences them.

Returns false (after printing a warning) if the file can't be used.
=================
*/
static bool R_MD3_SwapAndValidate( md3Header_t *md3, int size, const char *fileName ) {
	int i, j;

	LL(md3->ident);
	LL(md3->version);
	LL(md3->flags);
	LL(md3->numFrames);
	LL(md3->numTags);
	LL(md3->numSurfaces);
	LL(md3->numSkins);
	LL(md3->ofsFrames);
	LL(md3->ofsTags);
	LL(md3->ofsSurfaces);
	LL(md3->ofsEnd);
	md3->name[MAX_MD3PATH-1] = 0;

	if ( md3->ident != MD3_IDENT ) {
		common->Warning( "InitFromFile: %s is not an MD3 file (bad ident)", fileName );
		return false;
	}
	if ( md3->version != MD3_VERSION ) {
		common->Warning( "InitFromFile: %s has wrong version (%i should be %i)", fileName, md3->version, MD3_VERSION );
		return false;
	}
	if ( md3->ofsEnd != size ) {
		common->Warning( "InitFromFile: %s has a bad ofsEnd", fileName );
		return false;
	}
	if ( md3->numFrames < 1 ) {
		common->Warning( "InitFromFile: %s has no frames", fileName );
		return false;
	}
	if ( md3->numTags < 0 || md3->numSurfaces < 0 ) {
		common->Warning( "InitFromFile: %s has negative tag or surface counts", fileName );
		return false;
	}
	if ( !R_MD3_RangeOK( md3->ofsFrames, md3->numFrames, sizeof( md3Frame_t ), size ) ||
		 !R_MD3_RangeOK( md3->ofsTags, (long long)md3->numTags * md3->numFrames, sizeof( md3Tag_t ), size ) ) {
		common->Warning( "InitFromFile: %s has frame or tag data outside the file", fileName );
		return false;
	}

	// swap all the frames
	md3Frame_t *frame = (md3Frame_t *)( (byte *)md3 + md3->ofsFrames );
	for ( i = 0; i < md3->numFrames; i++, frame++ ) {
		frame->radius = LittleFloat( frame->radius );
		for ( j = 0; j < 3; j++ ) {
			frame->bounds[0][j] = LittleFloat( frame->bounds[0][j] );
			frame->bounds[1][j] = LittleFloat( frame->bounds[1][j] );
			frame->localOrigin[j] = LittleFloat( frame->localOrigin[j] );
		}
	}

	// swap all the tags
	md3Tag_t *tag = (md3Tag_t *)( (byte *)md3 + md3->ofsTags );
	for ( i = 0; i < md3->numTags * md3->numFrames; i++, tag++ ) {
		tag->name[MAX_MD3PATH-1] = 0;
		for ( j = 0; j < 3; j++ ) {
			tag->origin[j] = LittleFloat( tag->origin[j] );
			tag->axis[0][j] = LittleFloat( tag->axis[0][j] );
			tag->axis[1][j] = LittleFloat( tag->axis[1][j] );
			tag->axis[2][j] = LittleFloat( tag->axis[2][j] );
		}
	}

	// swap and validate all the surfaces
	int surfOfs = md3->ofsSurfaces;
	for ( i = 0; i < md3->numSurfaces; i++ ) {
		if ( !R_MD3_RangeOK( surfOfs, 1, sizeof( md3Surface_t ), size ) ) {
			common->Warning( "InitFromFile: %s has surface %i outside the file", fileName, i );
			return false;
		}

		md3Surface_t *surf = (md3Surface_t *)( (byte *)md3 + surfOfs );

		LL(surf->ident);
		LL(surf->flags);
		LL(surf->numFrames);
		LL(surf->numShaders);
		LL(surf->numTriangles);
		LL(surf->ofsTriangles);
		LL(surf->numVerts);
		LL(surf->ofsShaders);
		LL(surf->ofsSt);
		LL(surf->ofsXyzNormals);
		LL(surf->ofsEnd);
		surf->name[MAX_MD3PATH-1] = 0;

		if ( surf->ofsEnd < (int)sizeof( md3Surface_t ) || !R_MD3_RangeOK( surfOfs, surf->ofsEnd, 1, size ) ) {
			common->Warning( "InitFromFile: %s has a bad ofsEnd in surface %i", fileName, i );
			return false;
		}
		if ( surf->numVerts < 0 || surf->numVerts > MD3_MAX_VERTS ) {
			common->Warning( "InitFromFile: %s has more than %i verts on a surface (%i)", fileName, MD3_MAX_VERTS, surf->numVerts );
			return false;
		}
		if ( surf->numTriangles < 0 || surf->numTriangles > MD3_MAX_TRIANGLES ) {
			common->Warning( "InitFromFile: %s has more than %i triangles on a surface (%i)", fileName, MD3_MAX_TRIANGLES, surf->numTriangles );
			return false;
		}
		if ( surf->numShaders < 0 || surf->numShaders > MD3_MAX_SHADERS ) {
			common->Warning( "InitFromFile: %s has a bad shader count on surface %i (%i)", fileName, i, surf->numShaders );
			return false;
		}
		// vertex data is looked up by the model's frame number, so every surface needs all frames
		if ( surf->numFrames < md3->numFrames ) {
			common->Warning( "InitFromFile: %s surface %i has fewer frames (%i) than the model (%i)", fileName, i, surf->numFrames, md3->numFrames );
			return false;
		}

		const int end = surf->ofsEnd;
		const long long nv = surf->numVerts;
		if ( !R_MD3_RangeOK( surf->ofsShaders, surf->numShaders, sizeof( md3Shader_t ), end ) ||
			 !R_MD3_RangeOK( surf->ofsTriangles, surf->numTriangles, sizeof( md3Triangle_t ), end ) ||
			 !R_MD3_RangeOK( surf->ofsSt, nv, sizeof( md3St_t ), end ) ||
			 !R_MD3_RangeOK( surf->ofsXyzNormals, nv * surf->numFrames, sizeof( md3XyzNormal_t ), end ) ) {
			common->Warning( "InitFromFile: %s has surface %i data outside the surface", fileName, i );
			return false;
		}

		// change to surface identifier
		surf->ident = 0;	//SF_MD3;

		// lowercase the surface name so skin compares are faster
		int slen = (int)strlen( surf->name );
		for ( j = 0; j < slen; j++ ) {
			surf->name[j] = tolower( (unsigned char)surf->name[j] );
		}

		// strip off a trailing _1 or _2
		// this is a crutch for q3data being a mess
		j = (int)strlen( surf->name );
		if ( j > 2 && surf->name[j-2] == '_' ) {
			surf->name[j-2] = 0;
		}

		// shader names must terminate (shaderIndex is filled in by the caller)
		md3Shader_t *shader = (md3Shader_t *)( (byte *)surf + surf->ofsShaders );
		for ( j = 0; j < surf->numShaders; j++, shader++ ) {
			shader->name[MAX_MD3PATH-1] = 0;
		}

		// swap and range check the triangles
		md3Triangle_t *tri = (md3Triangle_t *)( (byte *)surf + surf->ofsTriangles );
		for ( j = 0; j < surf->numTriangles; j++, tri++ ) {
			LL(tri->indexes[0]);
			LL(tri->indexes[1]);
			LL(tri->indexes[2]);
			for ( int k = 0; k < 3; k++ ) {
				if ( tri->indexes[k] < 0 || tri->indexes[k] >= surf->numVerts ) {
					common->Warning( "InitFromFile: %s has an out of range vertex index in surface %i", fileName, i );
					return false;
				}
			}
		}

		// swap all the ST
		md3St_t *st = (md3St_t *)( (byte *)surf + surf->ofsSt );
		for ( j = 0; j < surf->numVerts; j++, st++ ) {
			st->st[0] = LittleFloat( st->st[0] );
			st->st[1] = LittleFloat( st->st[1] );
		}

		// swap all the XyzNormals
		md3XyzNormal_t *xyz = (md3XyzNormal_t *)( (byte *)surf + surf->ofsXyzNormals );
		for ( j = 0; j < surf->numVerts * surf->numFrames; j++, xyz++ ) {
			xyz->xyz[0] = LittleShort( xyz->xyz[0] );
			xyz->xyz[1] = LittleShort( xyz->xyz[1] );
			xyz->xyz[2] = LittleShort( xyz->xyz[2] );
			xyz->normal = LittleShort( xyz->normal );
		}

		surfOfs += surf->ofsEnd;
	}

	return true;
}

// DG: added constructor to make sure all members are initialized
idRenderModelMD3::idRenderModelMD3() : index(-1), dataSize(0), md3(NULL), numLods(0)
{}

// DG: added destructor to clean up the newly added silInfos (and md3 which used to leak)
idRenderModelMD3::~idRenderModelMD3() {
	FreeData();
}

/*
=================
idRenderModelMD3::FreeData

Releases everything InitFromFile() allocated.  Called from the destructor and
at the start of InitFromFile(), so reloading a model after a purge doesn't
leak the old file data, append a second copy of the silInfos or double count
dataSize.
=================
*/
void idRenderModelMD3::FreeData() {
	srfTriangles_t tri;
	memset( &tri, 0, sizeof( tri ) );

	for ( int i = 0, n = silInfos.Num(); i < n; ++i ) {
		silInfo_t& silInfo = silInfos[i];
		// Note: Because tri.sil* was allocated by specialized allocators only available
		//       in tr.trisurf.cpp, they need to be free'd with them as well.
		//       The easiest way to do this is by putting them into a srfTriangles_t first
		tri.silIndexes = silInfo.silIndexes;
		tri.silEdges = silInfo.silEdges;

		R_FreeStaticTriSurfSilIndexes( &tri );
		R_FreeStaticTriSurfSilEdges( &tri );

		if ( sizeof(md3Triangle_t::indexes[0]) != sizeof(silInfo.indexes[0]) ) {
			tri.indexes = silInfo.indexes;
			R_FreeStaticTriSurfIndexes( &tri );
		} // else silInfos[i].indexes points directly into the corresponding data in this->md3
	}
	silInfos.Clear();
	shaders.Clear();
	defaultPose.Clear();

	if ( md3 != NULL ) {
		Mem_Free( md3 );
		md3 = NULL;
	}
	dataSize = 0;
}

/*
=================
idRenderModelMD3::InitFromFile
=================
*/
void idRenderModelMD3::InitFromFile( const char *fileName ) {
	int					i, j;
	md3Surface_t		*surf;
	md3Shader_t			*shader;
	void				*buffer = NULL;
	int					size;
	int					ofsEnd;

	// make reloading safe
	FreeData();

	name = fileName;

	size = fileSystem->ReadFile( fileName, &buffer, NULL );
	// DG: without the int cast, an unsigned comparison is made which fails for size = -1
	if ( size <= (int)sizeof(md3Header_t) ) {
		if ( buffer != NULL ) {
			fileSystem->FreeFile( buffer );
		}
		// DG: must make it a default model so failure can be detected elsewhere
		MakeDefaultModel();
		return;
	}

	// the header is validated properly in R_MD3_SwapAndValidate(), this only has
	// to be good enough to size the copy without reading past the end of the file
	ofsEnd = LittleInt( ((md3Header_t *)buffer)->ofsEnd );
	if ( ofsEnd < (int)sizeof(md3Header_t) || ofsEnd > size ) {
		common->Warning( "InitFromFile: %s has a bad ofsEnd (%i, file is %i bytes)", fileName, ofsEnd, size );
		fileSystem->FreeFile( buffer );
		MakeDefaultModel();
		return;
	}

	md3 = (md3Header_t *)Mem_Alloc( ofsEnd );
	memcpy( md3, buffer, ofsEnd );
	fileSystem->FreeFile( buffer );
	dataSize = ofsEnd;

	if ( !R_MD3_SwapAndValidate( md3, ofsEnd, fileName ) ) {
		// don't keep a half swapped file around, the model is a default model now
		FreeData();
		MakeDefaultModel();
		return;
	}

	// register the shaders and build the shadow info for every surface
	surf = (md3Surface_t *)( (byte *)md3 + md3->ofsSurfaces );
	for ( i = 0; i < md3->numSurfaces; i++ ) {

		shader = (md3Shader_t *)( (byte *)surf + surf->ofsShaders );
		for ( j = 0; j < surf->numShaders; j++, shader++ ) {

			// Dynamix - hack for Raven shaders (Just Elite Force for md3?): the first
			// character of the path can be garbage, the real paths all start with "models/"
			if ( memcmp( shader->name + 1, "odel", 4 ) == 0 ) {
				shader->name[0] = 'm';
			}

			const idMaterial *sh = declManager->FindMaterial( shader->name );
			// DG: md3Shader_t must use an index to the material instead of a pointer,
			//     otherwise the sizes are wrong on 64bit and we get data corruption
			shader->shaderIndex = ( sh != NULL ) ? shaders.AddUnique( sh ) : -1;
		}

		silInfos.Append( BuildSilInfo( surf ) );

		// find the next surface
		surf = (md3Surface_t *)( (byte *)surf + surf->ofsEnd );
	}

	// initial tag pose, so GetDefaultPose() is never uninitialised garbage
	defaultPose.SetGranularity( 1 );
	defaultPose.SetNum( md3->numTags );
	UpdateTagPose( 0, 0, 0.0f );

	// If this is only one frame generate the static model information to fix
	// loading issues and allow physics generation
	if ( md3->numFrames == 1 ) {
		CreateSurfaces();
	}

	// idRenderModelStatic::InitFromFile() does this, without it IsLoaded()
	// stays false after the first purge and the model gets reloaded on every lookup
	purged = false;
}

/*
=================
idRenderModelMD3::IsDynamicModel
=================
*/
dynamicModel_t idRenderModelMD3::IsDynamicModel() const {
	// md3 is NULL if loading failed and this is a default model now
	if ( md3 == NULL || md3->numFrames == 1 ) {
		return DM_STATIC;
	}
	return DM_CACHED;
}

/*
====================
idRenderModelMD3::GetDefaultPose
====================
*/
const idJointQuat* idRenderModelMD3::GetDefaultPose(void) const {
	return defaultPose.Ptr();
}

/*
====================
idRenderModelMD3::UpdateTagPose

Stores the tag positions as joint quats so the game side can read them
====================
*/
void idRenderModelMD3::UpdateTagPose( int frame, int oldframe, float backlerp ) {
	if ( md3 == NULL || md3->numTags <= 0 ) {
		return;
	}
	if ( defaultPose.Num() != md3->numTags ) {
		defaultPose.SetNum( md3->numTags );
	}

	const md3Tag_t *tags = (const md3Tag_t *)( (const byte *)md3 + md3->ofsTags );
	const md3Tag_t *tag = tags + frame * md3->numTags;
	const md3Tag_t *oldTag = tags + oldframe * md3->numTags;
	const float frontlerp = 1.0f - backlerp;
	idJointQuat *pose = defaultPose.Ptr();

	for ( int i = 0; i < md3->numTags; i++, oldTag++, tag++, pose++ ) {
		const idQuat oldQuat = idMat3( oldTag->axis[0], oldTag->axis[1], oldTag->axis[2] ).ToQuat();
		const idQuat newQuat = idMat3( tag->axis[0], tag->axis[1], tag->axis[2] ).ToQuat();

		pose->t.Lerp( oldTag->origin, tag->origin, frontlerp );
		pose->q.Slerp( oldQuat, newQuat, frontlerp );
	}
}

/*
=================
idRenderModelMD3::LerpMeshVertexes
=================
*/
void idRenderModelMD3::LerpMeshVertexes ( srfTriangles_t *tri, const struct md3Surface_s *surf, const float backlerp, const int frame, const int oldframe ) const {
	const int numVerts = surf->numVerts;
	const short *xyzBase = (const short *)( (const byte *)surf + surf->ofsXyzNormals );
	const short *newXyz = xyzBase + ( frame * numVerts * 4 );

	idDrawVert *out = tri->verts + tri->numVerts;

	if ( backlerp == 0 ) {
		//
		// just copy the vertexes
		//
		const float scale = (float)MD3_XYZ_SCALE;

		for ( int v = 0; v < numVerts; v++, newXyz += 4, out++ ) {
			out->xyz.x = newXyz[0] * scale;
			out->xyz.y = newXyz[1] * scale;
			out->xyz.z = newXyz[2] * scale;
		}
	} else {
		//
		// interpolate and copy the vertexes
		//
		const short *oldXyz = xyzBase + ( oldframe * numVerts * 4 );
		const float newXyzScale = (float)MD3_XYZ_SCALE * ( 1.0f - backlerp );
		const float oldXyzScale = (float)MD3_XYZ_SCALE * backlerp;

		for ( int v = 0; v < numVerts; v++, oldXyz += 4, newXyz += 4, out++ ) {
			// interpolate the xyz
			out->xyz.x = oldXyz[0] * oldXyzScale + newXyz[0] * newXyzScale;
			out->xyz.y = oldXyz[1] * oldXyzScale + newXyz[1] * newXyzScale;
			out->xyz.z = oldXyz[2] * oldXyzScale + newXyz[2] * newXyzScale;
		}
	}

	tri->numVerts += numVerts;
}

/*
====================
idRenderModelMD3::DrawTags
====================
*/
void idRenderModelMD3::DrawTags( const renderEntity_t *ent, const struct viewDef_s *view ) const {
	if ( md3 == NULL || view == NULL ) {
		return;
	}

	// the frame must be clamped, shaderParms can hold anything
	const int currentFrame = idMath::ClampInt( 0, md3->numFrames - 1, ent->shaderParms[SHADERPARM_MD3_FRAME] );
	const md3Tag_t *tag = (const md3Tag_t *)( (const byte *)md3 + md3->ofsTags ) + currentFrame * md3->numTags;

	const idVec3 offset( 0, 0, r_jointNameOffset.GetFloat() );
	const float scale = r_jointNameScale.GetFloat();

	for ( int i = 0; i < md3->numTags; i++, tag++ ) {
		const idVec3 pos = ent->origin + tag->origin * ent->axis;
		session->rw->DrawText( tag->name, pos + offset, scale, colorWhite, view->renderView.viewaxis, 1 );
	}
}

/*
=================
idRenderModelMD3::GetSurfaceMaterial
=================
*/
const idMaterial *idRenderModelMD3::GetSurfaceMaterial( const md3Surface_t *surface ) const {
	// FIXME: theoretically there can be multiple shaders?
	if ( surface->numShaders <= 0 ) {
		return NULL;
	}
	const md3Shader_t *shader = (const md3Shader_t *)( (const byte *)surface + surface->ofsShaders );
	// DG: turned md3Shader_t::shader (pointer) into an int (index)
	return ( shader->shaderIndex >= 0 ) ? shaders[shader->shaderIndex] : NULL;
}

/*
=================
idRenderModelMD3::FillSurface

Fills an allocated (or reused) surface with the geometry for one frame.
Shared by CreateSurfaces() and InstantiateDynamicModel().
tri->verts must already be allocated for surface->numVerts verts.
=================
*/
void idRenderModelMD3::FillSurface( srfTriangles_t *tri, const md3Surface_t *surface, const silInfo_t &silInfo, float backlerp, int frame, int oldframe ) const {
	// These flags and references must be (re)set after allocation or reuse.
	// deformedSurface prevents silEdges, silIndexes, indexes (and more not used here)
	// from being freed (they belong to silInfo which is freed in FreeData())
	tri->deformedSurface = true;
	tri->tangentsCalculated = false;
	tri->facePlanesCalculated = false;
	tri->numSilEdges = silInfo.numSilEdges;
	tri->silEdges = silInfo.silEdges;
	tri->silIndexes = silInfo.silIndexes;
	tri->indexes = silInfo.indexes;
	tri->numIndexes = silInfo.numIndexes;
	assert( surface->numTriangles * 3 == silInfo.numIndexes );

	// LerpMeshVertexes appends at tri->numVerts, so reset the logical
	// vertex count while keeping the allocated vertex buffer.
	tri->numVerts = 0;
	tri->bounds.Clear();

	LerpMeshVertexes( tri, surface, backlerp, frame, oldframe );

	// Texture coordinates are frame-invariant.  Reassigning them is cheap
	// and also initialises newly allocated vertex buffers.
	const float *texCoords = (const float *)( (const byte *)surface + surface->ofsSt );
	for ( int j = 0; j < surface->numVerts; j++ ) {
		idDrawVert *stri = &tri->verts[j];
		stri->st[0] = texCoords[j * 2 + 0];
		stri->st[1] = texCoords[j * 2 + 1];
	}

	R_BoundTriSurf( tri );
}

/*
=============
idRenderModelMD3::CreateSurfaces

Single frame models get real static surfaces (needed for physics / collision
model generation).
=============
*/
void idRenderModelMD3::CreateSurfaces() {
	this->bounds.Clear();

	const md3Surface_t *surface = (const md3Surface_t *)( (const byte *)md3 + md3->ofsSurfaces );

	for ( int i = 0; i < md3->numSurfaces; i++ ) {
		srfTriangles_t *tri = R_AllocStaticTriSurf();
		R_AllocStaticTriSurfVerts( tri, surface->numVerts );

		FillSurface( tri, surface, silInfos[i], 0.0f, 0, 0 );

		modelSurface_t surf;
		surf.geometry = tri;
		surf.shader = GetSurfaceMaterial( surface );
		surf.id = this->NumSurfaces(); // DG: make sure to initialize id; FIXME: or just set id to 0?
		this->AddSurface( surf );
		this->bounds.AddPoint( tri->bounds[0] );
		this->bounds.AddPoint( tri->bounds[1] );

		// find the next surface
		surface = (const md3Surface_t *)( (const byte *)surface + surface->ofsEnd );
	}
}

/*
=============
idRenderModelMD3::InstantiateDynamicModel

The per-entity snapshot (and the vertex allocations inside it) is kept
between calls and only the animated vertex positions are rewritten.  The
topology (indexes, sil edges) is shared with silInfos and never touched.
=============
*/
idRenderModel *idRenderModelMD3::InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel ) {
	int				i;
	int				surfaceNum;
	md3Surface_t *	surface;
	int				frame, oldframe;
	float			backlerp;
	idRenderModelStatic	*staticModel;

	if ( md3 == NULL ) {
		// failed to load, we are a default model and shouldn't get here
		if ( cachedModel ) {
			delete cachedModel;
		}
		return NULL;
	}

	const int numFrames = md3->numFrames;

	frame = idMath::ClampInt( 0, numFrames - 1, ent->shaderParms[SHADERPARM_MD3_FRAME] );	// probably want to keep frames < 1000 or so
	oldframe = idMath::ClampInt( 0, numFrames - 1, ent->shaderParms[SHADERPARM_MD3_LASTFRAME] );
	backlerp = idMath::ClampFloat( 0.0f, 1.0f, ent->shaderParms[SHADERPARM_MD3_BACKLERP] );

	if ( cachedModel && !r_useCachedDynamicModels.GetBool() ) {
		delete cachedModel;
		cachedModel = NULL;
	}

	// Do not accept snapshots generated by another model type
	if ( cachedModel && idStr::Icmp( cachedModel->Name(), "_MD3_Snapshot_" ) != 0 ) {
		delete cachedModel;
		cachedModel = NULL;
	}

	if ( cachedModel ) {
		assert( dynamic_cast<idRenderModelStatic *>( cachedModel ) != NULL );
		staticModel = static_cast<idRenderModelStatic *>( cachedModel );
	} else {
		staticModel = new idRenderModelStatic;
		staticModel->InitEmpty( "_MD3_Snapshot_" );
	}

	staticModel->bounds.Clear();

	if ( r_showSkel.GetInteger() ) {
		if ( ( view != NULL ) && ( !r_skipSuppress.GetBool() || !ent->suppressSurfaceInViewID || ( ent->suppressSurfaceInViewID != view->renderView.viewID ) ) ) {
			// only draw the tags
			DrawTags( ent, view );
		}
	}

	// Set the tag positions as joint quats so we can get them on the game side
	UpdateTagPose( frame, oldframe, backlerp );

	surface = (md3Surface_t *)( (byte *)md3 + md3->ofsSurfaces );

	for ( i = 0; i < md3->numSurfaces; i++ ) {

		// Surface IDs are stable for the lifetime of this MD3 model.  Keep
		// the modelSurface_t and its vertex allocation in the per-entity
		// snapshot, updating only the animated vertex positions each call.
		modelSurface_t *surf;
		if ( staticModel->FindSurfaceWithId( i, surfaceNum ) ) {
			surf = &staticModel->surfaces[surfaceNum];
		} else {
			modelSurface_t newSurf;
			memset( &newSurf, 0, sizeof( newSurf ) );
			newSurf.id = i;
			staticModel->AddSurface( newSurf );
			if ( !staticModel->FindSurfaceWithId( i, surfaceNum ) ) {
				common->Error( "MD3 snapshot: failed to find newly-added surface %d", i );
			}
			surf = &staticModel->surfaces[surfaceNum];
		}

		// DG: sil edges for shadows
		const silInfo_t &silInfo = silInfos[i];

		// The vertex and index counts determine whether the existing triangle
		// allocation can be reused.  The topology arrays are owned by this
		// model's silInfos, not by the snapshot.
		srfTriangles_t *tri = surf->geometry;
		if ( tri && ( tri->numVerts != surface->numVerts || tri->numIndexes != silInfo.numIndexes ) ) {
			R_FreeStaticTriSurf( tri );
			surf->geometry = NULL;
			tri = NULL;
		}
		if ( !tri ) {
			tri = R_AllocStaticTriSurf();
			R_AllocStaticTriSurfVerts( tri, surface->numVerts );
			surf->geometry = tri;
		} else {
			// Vertex positions are about to change.  Drop any cached GPU
			// copy while retaining the CPU-side vertex allocation.
			R_FreeStaticTriSurfVertexCaches( tri );
		}

		surf->shader = GetSurfaceMaterial( surface );

		FillSurface( tri, surface, silInfo, backlerp, frame, oldframe );

		staticModel->bounds.AddPoint( tri->bounds[0] );
		staticModel->bounds.AddPoint( tri->bounds[1] );

		// find the next surface
		surface = (md3Surface_t *)( (byte *)surface + surface->ofsEnd );
	}

	return staticModel;
}

/*
=====================
idRenderModelMD3::Bounds
=====================
*/
idBounds idRenderModelMD3::Bounds( const struct renderEntity_s *ent ) const {
	idBounds		ret;

	ret.Clear();

	if ( !ent || !md3 ) {
		// just give it the editor bounds
		ret.AddPoint( idVec3( -10, -10, -10 ) );
		ret.AddPoint( idVec3(  10,  10,  10 ) );
		return ret;
	}

	const md3Frame_t *frames = (const md3Frame_t *)( (const byte *)md3 + md3->ofsFrames );
	const int frameNum = idMath::ClampInt( 0, md3->numFrames - 1, ent->shaderParms[SHADERPARM_MD3_FRAME] );
	const int oldFrameNum = idMath::ClampInt( 0, md3->numFrames - 1, ent->shaderParms[SHADERPARM_MD3_LASTFRAME] );

	// DG: use bounds of current frame
	ret.AddPoint( frames[frameNum].bounds[0] );
	ret.AddPoint( frames[frameNum].bounds[1] );

	// while interpolating the mesh is a blend of both frames
	if ( oldFrameNum != frameNum && ent->shaderParms[SHADERPARM_MD3_BACKLERP] > 0.0f ) {
		ret.AddPoint( frames[oldFrameNum].bounds[0] );
		ret.AddPoint( frames[oldFrameNum].bounds[1] );
	}

	return ret;
}

/*
=====================
idRenderModelMD3::BuildSilInfo
=====================
*/
silInfo_t idRenderModelMD3::BuildSilInfo( md3Surface_t *surf ) {
	//
	// build the information that will be common to all animations of this surface:
	// silhouette edge connectivity and normal / tangent generation information
	//
	int numVerts = surf->numVerts;
	silInfo_t ret = {0};

	md3XyzNormal_t *xyzs = (md3XyzNormal_t *) ( (byte *)surf + surf->ofsXyzNormals );

	bool onStack;
	idDrawVert *verts = (idDrawVert*)Mem_MallocA( numVerts*sizeof(idDrawVert), onStack );

	for ( int i=0; i < numVerts; ++i ) {
		idDrawVert& v = verts[i];
		v.Clear();
		// Note: MD3 defines these for all frames - using frame 0 here
		short* xyz = xyzs[i].xyz;
		v.xyz.x = xyz[0] * float(MD3_XYZ_SCALE);
		v.xyz.y = xyz[1] * float(MD3_XYZ_SCALE);
		v.xyz.z = xyz[2] * float(MD3_XYZ_SCALE);
		// Note: idDrawVert has more fields, but R_CreateSilIndexes() and R_IdentifySilEdges()
		// only use .xyz
	}

	int numIndexes = surf->numTriangles * 3;
	md3Triangle_t *md3TriIdx = (md3Triangle_t *) ( (byte *)surf + surf->ofsTriangles );
	int *triIndexes = (int*)md3TriIdx->indexes;

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
	bool omitCoplanarEdges = (md3->numFrames == 1);
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
idRenderModelMD3::GetJointHandle
====================
*/
jointHandle_t idRenderModelMD3::GetJointHandle( const char *name ) const {
	if ( md3 == NULL ) {
		return INVALID_JOINT;
	}

	const md3Tag_t *tag = (const md3Tag_t *)( (const byte *)md3 + md3->ofsTags );

	for ( int i = 0; i < md3->numTags; i++, tag++ ) {
		if ( idStr::Icmp( tag->name, name ) == 0 ) {
			return (jointHandle_t)i;
		}
	}

	return INVALID_JOINT;
}

/*
=====================
idRenderModelMD3::GetJointName
=====================
*/
const char *idRenderModelMD3::GetJointName( jointHandle_t handle ) const {
	if ( md3 == NULL || handle < 0 || handle >= md3->numTags ) {
		return "<invalid joint>";
	}

	const md3Tag_t *tag = (const md3Tag_t *)( (const byte *)md3 + md3->ofsTags );
	return ( tag + handle )->name;
}

// Testing Dynamix 

/*
=================
R_TAN_RangeOK

Checks that count elements of elemSize bytes starting at ofs fit inside limit.
64 bit maths so hostile counts can't overflow.
=================
*/
static bool R_TAN_RangeOK( int ofs, long long count, int elemSize, int limit ) {
	if ( count < 0 ) {
		return false;
	}
	if ( count == 0 ) {
		return true;
	}
	if ( ofs < 0 ) {
		return false;
	}
	return ((long long)ofs + count * (long long)elemSize) <= (long long)limit;
}

/*
=================
R_TAN_SwapFloats
=================
*/
static void R_TAN_SwapFloats(float* f, int count) {
	for ( int i = 0; i < count; i++ ) {
		f[i] = LittleFloat(f[i]);
	}
}

/*
=================
R_TAN_SwapAndValidate

Byte swaps the whole file in place and validates every offset, count and
index before anything dereferences them.
Returns false (after printing a warning) if the file can't be used.
=================
*/
static bool R_TAN_SwapAndValidate( tanHeader_t* tan, int size, const char* fileName ) {
	int i, j;

	LL(tan->ident);
	LL(tan->version);
	LL(tan->numFrames);
	LL(tan->numTags);
	LL(tan->numSurfaces);
	tan->totalTime = LittleFloat(tan->totalTime);
	R_TAN_SwapFloats(tan->totalDelta, 3);
	LL(tan->ofsFrames);
	LL(tan->ofsSurfaces);
	for ( i = 0; i < TAN_MAX_TAGS; i++ ) {
		LL(tan->ofsTags[i]);
	}
	LL(tan->ofsEnd);
	tan->name[MAX_TANPATH - 1] = 0;

	if ( tan->ident != TAN_IDENT ) {
		common->Warning("InitFromFile: %s is not a TAN file (bad ident)", fileName);
		return false;
	}
	if ( tan->version != TAN_VERSION ) {
		common->Warning("InitFromFile: %s has wrong version (%i should be %i)", fileName, tan->version, TAN_VERSION);
		return false;
	}
	if ( tan->ofsEnd != size ) {
		common->Warning("InitFromFile: %s has a bad ofsEnd", fileName);
		return false;
	}
	if ( tan->numFrames < 1 ) {
		common->Warning("InitFromFile: %s has no frames", fileName);
		return false;
	}
	if ( tan->numTags < 0 || tan->numTags > TAN_MAX_TAGS || tan->numSurfaces < 0 ) {
		common->Warning("InitFromFile: %s has a bad tag or surface count", fileName);
		return false;
	}
	if ( !R_TAN_RangeOK(tan->ofsFrames, tan->numFrames, sizeof(tanFrame_t), size) ) {
		common->Warning("InitFromFile: %s has frame data outside the file", fileName);
		return false;
	}

	if ( tan->numFrames > 1 ) {
		common->DWarning("%s has %i frames, only the first frame is loaded (static TAN loader)", fileName, tan->numFrames);
	}

	// swap all the frames
	tanFrame_t* frame = (tanFrame_t*)((byte*)tan + tan->ofsFrames);
	for ( i = 0; i < tan->numFrames; i++, frame++ ) {
		R_TAN_SwapFloats((float*)frame, sizeof(tanFrame_t) / sizeof(float));
	}

	// swap all the tags: name, then numFrames * tag data
	for ( i = 0; i < tan->numTags; i++) {
		if ( !R_TAN_RangeOK(tan->ofsTags[i], 1, sizeof(tanTagName_t), size) ||
			!R_TAN_RangeOK(tan->ofsTags[i] + (int)sizeof(tanTagName_t), tan->numFrames, sizeof(tanTagData_t), size) ) {
			common->Warning("InitFromFile: %s has tag %i outside the file", fileName, i);
			return false;
		}
		tanTagName_t* tagName = (tanTagName_t*)((byte*)tan + tan->ofsTags[i]);
		tagName->name[MAX_TANPATH - 1] = 0;

		tanTagData_t* tagData = (tanTagData_t*)(tagName + 1);
		for ( j = 0; j < tan->numFrames; j++, tagData++ ) {
			R_TAN_SwapFloats((float*)tagData, sizeof(tanTagData_t) / sizeof(float));
		}
	}

	// swap and validate all the surfaces
	int surfOfs = tan->ofsSurfaces;
	for ( i = 0; i < tan->numSurfaces; i++ ) {
		if ( !R_TAN_RangeOK(surfOfs, 1, sizeof(tanSurface_t), size) ) {
			common->Warning("InitFromFile: %s has surface %i outside the file", fileName, i);
			return false;
		}

		tanSurface_t* surf = (tanSurface_t*)((byte*)tan + surfOfs);

		LL(surf->ident);
		LL(surf->numFrames);
		LL(surf->numVerts);
		LL(surf->numModelVerts);
		LL(surf->numTriangles);
		LL(surf->ofsTriangles);
		LL(surf->ofsCollapseMap);
		LL(surf->ofsSt);
		LL(surf->ofsXyzNormals);
		LL(surf->ofsEnd);
		surf->name[MAX_TANPATH - 1] = 0;

		if ( surf->ofsEnd < (int)sizeof(tanSurface_t) || !R_TAN_RangeOK(surfOfs, surf->ofsEnd, 1, size) ) {
			common->Warning("InitFromFile: %s has a bad ofsEnd in surface %i", fileName, i);
			return false;
		}
		if ( surf->numVerts < 0 || surf->numVerts > TAN_MAX_VERTS ) {
			common->Warning("InitFromFile: %s has more than %i verts on a surface (%i)", fileName, TAN_MAX_VERTS, surf->numVerts);
			return false;
		}
		if ( surf->numTriangles < 0 || surf->numTriangles > TAN_MAX_TRIANGLES ) {
			common->Warning("InitFromFile: %s has more than %i triangles on a surface (%i)", fileName, TAN_MAX_TRIANGLES, surf->numTriangles);
			return false;
		}
		if ( surf->numFrames < 1 ) {
			common->Warning("InitFromFile: %s surface %i has no frames", fileName, i);
			return false;
		}

		const int end = surf->ofsEnd;
		const long long nv = surf->numVerts;
		if ( !R_TAN_RangeOK(surf->ofsTriangles, surf->numTriangles, sizeof(tanTriangle_t), end) ||
			!R_TAN_RangeOK(surf->ofsSt, nv, sizeof(tanSt_t), end) ||
			!R_TAN_RangeOK(surf->ofsXyzNormals, nv * surf->numFrames, sizeof(tanXyzNormal_t), end) ) {
			common->Warning("InitFromFile: %s has surface %i data outside the surface", fileName, i);
			return false;
		}
		// the collapse map is LOD data we don't use, so it is neither swapped nor required to be valid

		// swap and range check the triangles
		tanTriangle_t* tri = (tanTriangle_t*)((byte*)surf + surf->ofsTriangles);
		for ( j = 0; j < surf->numTriangles; j++, tri++ ) {
			LL(tri->indexes[0]);
			LL(tri->indexes[1]);
			LL(tri->indexes[2]);
			for ( int k = 0; k < 3; k++ ) {
				if ( tri->indexes[k] < 0 || tri->indexes[k] >= surf->numVerts ) {
					common->Warning("InitFromFile: %s has an out of range vertex index in surface %i", fileName, i);
					return false;
				}
			}
		}

		// swap all the ST
		tanSt_t* st = (tanSt_t*)((byte*)surf + surf->ofsSt);
		for ( j = 0; j < surf->numVerts; j++, st++ ) {
			st->st[0] = LittleFloat(st->st[0]);
			st->st[1] = LittleFloat(st->st[1]);
		}

		// swap the vertex positions (the packed normal is two plain bytes)
		tanXyzNormal_t* xyz = (tanXyzNormal_t*)((byte*)surf + surf->ofsXyzNormals);
		for ( j = 0; j < surf->numVerts * surf->numFrames; j++, xyz++ ) {
			xyz->xyz[0] = (unsigned short)LittleShort((short)xyz->xyz[0]);
			xyz->xyz[1] = (unsigned short)LittleShort((short)xyz->xyz[1]);
			xyz->xyz[2] = (unsigned short)LittleShort((short)xyz->xyz[2]);
		}

		surfOfs += surf->ofsEnd;
	}

	return true;
}

/*
=================
R_TAN_DecodeFrame

Decodes the (first frame) vertex positions of a surface.
Positions are value * scale + offset, using the model's first frame.
=================
*/
static void R_TAN_DecodeFrame( idDrawVert* out, const tanSurface_t* surf, const tanFrame_t* frame, int frameNum ) {
	const tanXyzNormal_t* xyz = (const tanXyzNormal_t*)((const byte*)surf + surf->ofsXyzNormals);

	xyz += frameNum * surf->numVerts;

	for ( int i = 0; i < surf->numVerts; i++, xyz++ )
	{
		out[i].xyz.x =
			xyz->xyz[0] * frame->scale[0] + frame->offset[0];

		out[i].xyz.y =
			xyz->xyz[1] * frame->scale[1] + frame->offset[1];

		out[i].xyz.z =
			xyz->xyz[2] * frame->scale[2] + frame->offset[2];
	}
}

idRenderModelTAN::idRenderModelTAN() : index(-1), dataSize(0), tan(NULL)
{
}

idRenderModelTAN::~idRenderModelTAN() {
	FreeData();
}

/*
=================
idRenderModelTAN::FreeData

Releases everything InitFromFile() allocated.  Called from the destructor and
at the start of InitFromFile() so reloading doesn't leak.
=================
*/
void idRenderModelTAN::FreeData() {
	srfTriangles_t tri;
	memset(&tri, 0, sizeof(tri));

	for ( int i = 0, n = silInfos.Num(); i < n; ++i ) {
		silInfo_t& silInfo = silInfos[i];
		// Note: tri.sil* and tri.indexes were allocated by specialized allocators only
		//       available in tr_trisurf.cpp, so they have to be freed with them as well.
		tri.silIndexes = silInfo.silIndexes;
		tri.silEdges = silInfo.silEdges;
		tri.indexes = silInfo.indexes;

		R_FreeStaticTriSurfSilIndexes(&tri);
		R_FreeStaticTriSurfSilEdges(&tri);
		R_FreeStaticTriSurfIndexes(&tri);
	}
	silInfos.Clear();
	shaders.Clear();
	defaultPose.Clear();

	if ( tan != NULL ) {
		Mem_Free(tan);
		tan = NULL;
	}
	dataSize = 0;
}

static bool TIKI_ReadPathToken( idLexer& lexer, idStr& result )
{
	idToken token;

	result.Clear();

	if ( !lexer.ReadTokenOnLine(&token) ) {
		return false;
	}

	result = token.c_str();

	while ( lexer.ReadTokenOnLine(&token) ) {
		result += token.c_str();
	}

	return true;
}

/*
=========================
idRenderModelTAN::ParseTikiFile

Reads TIKI metadata:
- Model path
- Surface material assignments
- Animation names and TAN filenames

Does not perform animation processing.
=========================
*/
bool idRenderModelTAN::ParseTikiFile( const char* fileName )
{
	idLexer lexer(LEXFL_NOFATALERRORS | LEXFL_ALLOWPATHNAMES);
	idToken token;

	// Clear any previous TIKI data.
	tikiPath.Clear();
	tikiMaterials.Clear();
	tikiAnimations.Clear();

	if ( !lexer.LoadFile(fileName) ) {
		common->Warning("Couldn't load TIKI file '%s'", fileName);
		return false;
	}

	while ( lexer.ReadToken(&token) ) {
		// =========================================================
		// SETUP BLOCK
		// =========================================================
		if ( token.Icmp("setup") == 0 ) {
			if ( !lexer.ExpectTokenString("{") ) {
				common->Warning(
					"Expected '{' after setup in '%s'",
					fileName
				);
				lexer.FreeSource();
				return false;
			}

			while ( lexer.ReadToken(&token) )
			{
				if  ( token.Icmp("}") == 0 ) {
					break;
				}

				// -------------------------------------------------
				// MODEL DIRECTORY
				// -------------------------------------------------
				if (token.Icmp("path") == 0)
				{
					idStr pathValue;

					if (!TIKI_ReadPathToken(lexer, pathValue))
					{
						common->Warning(
							"Missing path value in TIKI file '%s'",
							fileName
						);
						continue;
					}

					// Replace hyphens if required by the asset naming convention.
					// Do NOT replace them in model paths unless the actual directory
					// on disk uses underscores.

					tikiPath = pathValue;

					common->Printf(
						"TIKI: setup path = '%s'\n",
						tikiPath.c_str()
					);
				}

				// -------------------------------------------------
				// SURFACE MATERIAL OVERRIDES
				// -------------------------------------------------
				else if ( token.Icmp("surface") == 0 ) {
					idToken surfaceName;
					idToken shaderKeyword;
					idStr shaderName;

					if ( lexer.ReadTokenOnLine(&surfaceName) &&
						lexer.ReadTokenOnLine(&shaderKeyword) &&
						shaderKeyword.Icmp("shader") == 0 &&
						TIKI_ReadPathToken(lexer, shaderName) )
					{
						// Replace hyphens with underscores.
						for ( int i = 0; i < shaderName.Length(); i++ ) {
							if ( shaderName[i] == '-' ) {
								shaderName[i] = '_';
							}
						}

						tikiMaterials.Set(
							surfaceName.c_str(),
							shaderName.c_str()
						);

						common->Printf(
							"TIKI: surface='%s', shader='%s'\n",
							surfaceName.c_str(),
							shaderName.c_str()
						);
					}

					lexer.SkipRestOfLine();
				}

				// -------------------------------------------------
				// OTHER SETUP PROPERTIES
				// -------------------------------------------------
				else {
					lexer.SkipRestOfLine();
				}
			}
		}

		// =========================================================
		// ANIMATIONS BLOCK
		// =========================================================
		else if ( token.Icmp("animations") == 0 ) {
			if ( !lexer.ExpectTokenString("{") ) {
				common->Warning(
					"Expected '{' after animations in '%s'",
					fileName
				);
				lexer.FreeSource();
				return false;
			}

			while ( lexer.ReadToken(&token) )
			{
				if ( token.Icmp("}") == 0 ) {
					break;
				}

				// Animation name, e.g. idle, walk, run.
				tikiAnimation_t animation;
				animation.name = token.c_str();

				idStr modelFile;

				if ( !TIKI_ReadPathToken(lexer, modelFile) ) {
					common->Warning(
						"Missing model filename for animation '%s' in '%s'",
						animation.name.c_str(),
						fileName
					);
					continue;
				}

				// -------------------------------------------------
				// RESOLVE MODEL FILE PATH
				// -------------------------------------------------
				//
				// Example:
				//
				// tikiPath:
				// models/item/armor_large_shield
				//
				// modelFile:
				// armor_large_shield.tan
				//
				// result:
				// models/item/armor_large_shield/armor_large_shield.tan
				//
				// -------------------------------------------------

				if ( tikiPath.Length() > 0 ) {
					animation.modelFile = tikiPath;
					animation.modelFile.AppendPath(modelFile.c_str());
				} else {
					animation.modelFile = modelFile.c_str();
				}

				common->Printf(
					"TIKI: animation='%s', directory='%s', "
					"filename='%s', resolved='%s'\n",
					animation.name.c_str(),
					tikiPath.c_str(),
					modelFile.c_str(),
					animation.modelFile.c_str()
				);

				tikiAnimations.Append(animation);

				// -------------------------------------------------
				// OPTIONAL ANIMATION EVENT BLOCK
				// -------------------------------------------------
				//
				// idle armor_large_shield.tan
				// {
				//     ...
				// }
				//
				// -------------------------------------------------

				if ( lexer.CheckTokenString("{") ) {
					lexer.SkipBracedSection(false);
				}
			}
		}

		// =========================================================
		// OTHER TOP-LEVEL BLOCKS
		// =========================================================
		else {
			if ( lexer.CheckTokenString("{") ) {
				lexer.SkipBracedSection(false);
			} else {
				lexer.SkipRestOfLine();
			}
		}
	}

	lexer.FreeSource();

	// =============================================================
	// VALIDATION
	// =============================================================

	if ( tikiAnimations.Num() == 0 ) {
		common->Warning(
			"No animations found in TIKI file '%s'",
			fileName
		);
		return false;
	}

	return true;
}

/*
=================
idRenderModelTAN::InitFromFile
=================
*/
void idRenderModelTAN::InitFromFile( const char* fileName ) {
	int i;
	tanSurface_t* surf;
	void* buffer = NULL;
	int size;
	int ofsEnd;

	FreeData();

	name = fileName;

	idStr extension;
	idStr tanFileName = fileName;

	tanFileName.ExtractFileExtension(extension);

	// If we're loading a TIKI, parse it first.
	if ( extension.Icmp("tik") == 0 ) {
		if ( !ParseTikiFile(fileName) ) {
			MakeDefaultModel();
			return;
		}

		// For now, use the first animation's TAN file.
		tanFileName = tikiAnimations[0].modelFile;

		common->Printf( "TIKI model: %s\n", tanFileName.c_str() );
	}

		// Read the actual binary TAN file.
	size = fileSystem->ReadFile( tanFileName.c_str(), &buffer, NULL );
	if ( size <= (int)sizeof(tanHeader_t) ) {
		if ( buffer != NULL ) {
			fileSystem->FreeFile(buffer);
		}
		// must make it a default model so failure can be detected elsewhere
		MakeDefaultModel();
		return;
	}

	// the header is validated properly in R_TAN_SwapAndValidate(), this only has
	// to be good enough to size the copy without reading past the end of the file
	ofsEnd = LittleInt(((tanHeader_t*)buffer)->ofsEnd);
	if ( ofsEnd < (int)sizeof(tanHeader_t) || ofsEnd > size ) {
		common->Warning("InitFromFile: %s has a bad ofsEnd (%i, file is %i bytes)", fileName, ofsEnd, size);
		fileSystem->FreeFile(buffer);
		MakeDefaultModel();
		return;
	}

	tan = (tanHeader_t*)Mem_Alloc(ofsEnd);
	memcpy(tan, buffer, ofsEnd);
	fileSystem->FreeFile(buffer);
	dataSize = ofsEnd;

	if ( !R_TAN_SwapAndValidate(tan, ofsEnd, fileName) ) {
		// don't keep a half swapped file around, the model is a default model now
		FreeData();
		MakeDefaultModel();
		return;
	}

	// register the materials and build the shadow info for every surface
	surf = (tanSurface_t*)((byte*)tan + tan->ofsSurfaces);
	for ( i = 0; i < tan->numSurfaces; i++ ) {
		// TAN surfaces only carry a surface name (the real shader assignment lives in the
		// .tik script), so the surface name is used as the material name
		const idMaterial* sh = FindMaterialFor(surf);
		shaders.AddUnique(sh);

		silInfos.Append(BuildSilInfo(surf));

		// find the next surface
		surf = (tanSurface_t*)((byte*)surf + surf->ofsEnd);
	}

	// Initialise tag poses from frame zero.
	UpdateTagPose(0, 0, 0.0f);

	// Single-frame models can use static geometry.
	// Animated models generate geometry per entity.
	if (tan->numFrames == 1)
	{
		CreateSurfaces();
	}

	// Mark the model as successfully loaded.
	purged = false;

	// idRenderModelStatic::InitFromFile() does this, without it IsLoaded()
	// stays false after the first purge and the model gets reloaded on every lookup
	purged = false;
}

/*
=================
idRenderModelTAN::IsDynamicModel
=================
*/
dynamicModel_t idRenderModelTAN::IsDynamicModel() const {
	if ( tan == NULL || tan->numFrames <= 1 ) {
		return DM_STATIC;
	}

	return DM_CACHED;
}

/*
====================
idRenderModelTAN::GetDefaultPose
====================
*/
const idJointQuat* idRenderModelTAN::GetDefaultPose( void ) const {
	return defaultPose.Ptr();
}

void idRenderModelTAN::LerpMeshVertexes( srfTriangles_t* tri, const tanSurface_s* surf, float backlerp, int frame, int oldframe ) const {
	const int numVerts = surf->numVerts;

	const tanFrame_t* frames =
		(const tanFrame_t*)((const byte*)tan + tan->ofsFrames);

	const tanFrame_t& newFrame = frames[frame];
	const tanFrame_t& oldFrame = frames[oldframe];

	const tanXyzNormal_t* xyzBase =
		(const tanXyzNormal_t*)((const byte*)surf +
			surf->ofsXyzNormals);

	const tanXyzNormal_t* newXyz =
		xyzBase + frame * numVerts;

	const tanXyzNormal_t* oldXyz =
		xyzBase + oldframe * numVerts;

	idDrawVert* out = tri->verts;

	const float frontlerp = 1.0f - backlerp;

	for ( int i = 0; i < numVerts; i++ ) {
		idVec3 newPos;
		idVec3 oldPos;

		newPos.x =
			newXyz[i].xyz[0] * newFrame.scale[0] +
			newFrame.offset[0];

		newPos.y =
			newXyz[i].xyz[1] * newFrame.scale[1] +
			newFrame.offset[1];

		newPos.z =
			newXyz[i].xyz[2] * newFrame.scale[2] +
			newFrame.offset[2];

		if ( backlerp > 0.0f ) {
			oldPos.x =
				oldXyz[i].xyz[0] * oldFrame.scale[0] +
				oldFrame.offset[0];

			oldPos.y =
				oldXyz[i].xyz[1] * oldFrame.scale[1] +
				oldFrame.offset[1];

			oldPos.z =
				oldXyz[i].xyz[2] * oldFrame.scale[2] +
				oldFrame.offset[2];

			out[i].xyz = oldPos * backlerp + newPos * frontlerp;
		} else {
			out[i].xyz = newPos;
		}
	}

	tri->numVerts = numVerts;
}

void idRenderModelTAN::FillSurface( srfTriangles_t* tri, const tanSurface_s* surface, const silInfo_t& silInfo, float backlerp, int frame, int oldframe ) const {
	tri->deformedSurface = true;
	tri->tangentsCalculated = false;
	tri->facePlanesCalculated = false;

	tri->numSilEdges = silInfo.numSilEdges;
	tri->silEdges = silInfo.silEdges;
	tri->silIndexes = silInfo.silIndexes;
	tri->indexes = silInfo.indexes;
	tri->numIndexes = silInfo.numIndexes;

	tri->numVerts = 0;
	tri->bounds.Clear();

	LerpMeshVertexes(
		tri,
		surface,
		backlerp,
		frame,
		oldframe
	);

	const tanSt_t* st =
		(const tanSt_t*)((const byte*)surface + surface->ofsSt);

	for ( int i = 0; i < surface->numVerts; i++ )
	{
		tri->verts[i].st[0] = st[i].st[0];
		tri->verts[i].st[1] = st[i].st[1];
	}

	R_BoundTriSurf(tri);
}

void idRenderModelTAN::UpdateTagPose( int frame, int oldframe, float backlerp )
{
	if ( tan == NULL || tan->numTags <= 0 ) {
		return;
	}

	frame = idMath::ClampInt(0, tan->numFrames - 1, frame);
	oldframe = idMath::ClampInt(0, tan->numFrames - 1, oldframe);
	backlerp = idMath::ClampFloat(0.0f, 1.0f, backlerp);

	if ( defaultPose.Num() != tan->numTags ) {
		defaultPose.SetNum(tan->numTags);
	}

	const float frontlerp = 1.0f - backlerp;

	for ( int i = 0; i < tan->numTags; i++ ) {
		const tanTagName_t* tagName =
			(const tanTagName_t*)((const byte*)tan + tan->ofsTags[i]);

		const tanTagData_t* tagData =
			(const tanTagData_t*)(tagName + 1);

		const tanTagData_t& newTag = tagData[frame];
		const tanTagData_t& oldTag = tagData[oldframe];

		idJointQuat& pose = defaultPose[i];

		idVec3 oldOrigin(
			oldTag.origin[0],
			oldTag.origin[1],
			oldTag.origin[2]);

		idVec3 newOrigin(
			newTag.origin[0],
			newTag.origin[1],
			newTag.origin[2]);

		idMat3 oldAxis(
			idVec3(oldTag.axis[0][0], oldTag.axis[0][1], oldTag.axis[0][2]),
			idVec3(oldTag.axis[1][0], oldTag.axis[1][1], oldTag.axis[1][2]),
			idVec3(oldTag.axis[2][0], oldTag.axis[2][1], oldTag.axis[2][2]));

		idMat3 newAxis(
			idVec3(newTag.axis[0][0], newTag.axis[0][1], newTag.axis[0][2]),
			idVec3(newTag.axis[1][0], newTag.axis[1][1], newTag.axis[1][2]),
			idVec3(newTag.axis[2][0], newTag.axis[2][1], newTag.axis[2][2]));

		pose.t.Lerp(oldOrigin, newOrigin, frontlerp);
		pose.q.Slerp(oldAxis.ToQuat(), newAxis.ToQuat(), frontlerp);
	}
}

/*
=============
idRenderModelTAN::CreateSurfaces

Builds the static surfaces from the first frame.
=============
*/
void idRenderModelTAN::CreateSurfaces() {
	this->bounds.Clear();

	const tanFrame_t* frame0 = (const tanFrame_t*)((const byte*)tan + tan->ofsFrames);
	const tanSurface_t* surface = (const tanSurface_t*)((const byte*)tan + tan->ofsSurfaces);

	for ( int i = 0; i < tan->numSurfaces; i++ ) {
		const silInfo_t& silInfo = silInfos[i];

		// find the next surface first, we may skip this one
		const tanSurface_t* next = (const tanSurface_t*)((const byte*)surface + surface->ofsEnd);

		if ( silInfo.numIndexes <= 0 || surface->numVerts <= 0 ) {
			// nothing but degenerate triangles
			surface = next;
			continue;
		}

		srfTriangles_t* tri = R_AllocStaticTriSurf();
		R_AllocStaticTriSurfVerts(tri, surface->numVerts);

		// These references belong to silInfo and are freed in FreeData().
		// deformedSurface prevents the renderer from freeing them with the surface.
		tri->deformedSurface = true;
		tri->numSilEdges = silInfo.numSilEdges;
		tri->silEdges = silInfo.silEdges;
		tri->silIndexes = silInfo.silIndexes;
		tri->indexes = silInfo.indexes;
		tri->numIndexes = silInfo.numIndexes;
		tri->numVerts = surface->numVerts;

		for ( int j = 0; j < surface->numVerts; j++ ) {
			tri->verts[j].Clear();
		}
		R_TAN_DecodeFrame(tri->verts, surface, frame0, 0);

		const tanSt_t* st = (const tanSt_t*)((const byte*)surface + surface->ofsSt);
		for ( int j = 0; j < surface->numVerts; j++ ) {
			tri->verts[j].st[0] = st[j].st[0];
			tri->verts[j].st[1] = st[j].st[1];
		}

		R_BoundTriSurf(tri);

		modelSurface_t surf;
		surf.geometry = tri;
		surf.shader = FindMaterialFor(surface);
		surf.id = this->NumSurfaces();
		this->AddSurface(surf);
		this->bounds.AddPoint(tri->bounds[0]);
		this->bounds.AddPoint(tri->bounds[1]);

		surface = next;
	}
}

idRenderModel* idRenderModelTAN::InstantiateDynamicModel(
	const renderEntity_s* ent,
	const viewDef_s* view,
	idRenderModel* cachedModel)
{
	if ( tan == NULL ) {
		if ( cachedModel ) {
			delete cachedModel;
		}

		return NULL;
	}

	if ( tan->numFrames <= 1 ) {
		return this;
	}

	const int numFrames = tan->numFrames;

	const int frame = idMath::ClampInt(
		0,
		numFrames - 1,
		ent->shaderParms[SHADERPARM_MD3_FRAME]);

	const int oldframe = idMath::ClampInt(
		0,
		numFrames - 1,
		ent->shaderParms[SHADERPARM_MD3_LASTFRAME]);

	const float backlerp = idMath::ClampFloat(
		0.0f,
		1.0f,
		ent->shaderParms[SHADERPARM_MD3_BACKLERP]);

	// Do not reuse another model type's snapshot.
	if ( cachedModel && idStr::Icmp(cachedModel->Name(), "_TAN_Snapshot_") != 0 ) {
		delete cachedModel;
		cachedModel = NULL;
	}

	idRenderModelStatic* staticModel;

	if ( cachedModel )
	{
		staticModel = static_cast<idRenderModelStatic*>(cachedModel);
	} else {
		staticModel = new idRenderModelStatic;
		staticModel->InitEmpty("_TAN_Snapshot_");
	}

	staticModel->bounds.Clear();

	// Update tag positions for the current animation frames.
	UpdateTagPose(frame, oldframe, backlerp);

	const tanSurface_t* surface =
		(const tanSurface_t*)((const byte*)tan + tan->ofsSurfaces);

	for ( int i = 0; i < tan->numSurfaces; i++ )
	{
		const silInfo_t& silInfo = silInfos[i];

		if ( silInfo.numIndexes <= 0 || surface->numVerts <= 0 )
		{
			surface = (const tanSurface_t*)((const byte*)surface + surface->ofsEnd);
			continue;
		}

		int surfaceNum;
		modelSurface_t* modelSurf;

		if ( staticModel->FindSurfaceWithId(i, surfaceNum) ) {
			modelSurf = &staticModel->surfaces[surfaceNum];
		} else {
			modelSurface_t newSurf;
			memset(&newSurf, 0, sizeof(newSurf));

			newSurf.id = i;
			staticModel->AddSurface(newSurf);

			if ( !staticModel->FindSurfaceWithId(i, surfaceNum) ) {
				common->Error(
					"TAN snapshot: failed to find newly-added surface %d",
					i);
			}

			modelSurf = &staticModel->surfaces[surfaceNum];
		}

		srfTriangles_t* tri = modelSurf->geometry;

		if ( tri && (tri->numVerts != surface->numVerts || tri->numIndexes != silInfo.numIndexes) ) {
			R_FreeStaticTriSurf(tri);
			modelSurf->geometry = NULL;
			tri = NULL;
		}

		if ( !tri ) {
			tri = R_AllocStaticTriSurf();
			R_AllocStaticTriSurfVerts(tri, surface->numVerts);
			modelSurf->geometry = tri;
		} else {
			R_FreeStaticTriSurfVertexCaches(tri);
		}

		modelSurf->shader = FindMaterialFor(surface);

		FillSurface(
			tri,
			surface,
			silInfo,
			backlerp,
			frame,
			oldframe);

		staticModel->bounds.AddPoint(tri->bounds[0]);
		staticModel->bounds.AddPoint(tri->bounds[1]);

		surface = (const tanSurface_t*)((const byte*)surface + surface->ofsEnd);
	}

	return staticModel;
}

idBounds idRenderModelTAN::Bounds( const renderEntity_s* ent ) const
{
	idBounds ret;
	ret.Clear();

	if ( !ent || !tan ) {
		ret.AddPoint(idVec3(-10, -10, -10));
		ret.AddPoint(idVec3(10, 10, 10));
		return ret;
	}

	const tanFrame_t* frames =
		(const tanFrame_t*)((const byte*)tan + tan->ofsFrames);

	const int frame = idMath::ClampInt(
		0,
		tan->numFrames - 1,
		ent->shaderParms[SHADERPARM_MD3_FRAME]);

	const int oldframe = idMath::ClampInt(
		0,
		tan->numFrames - 1,
		ent->shaderParms[SHADERPARM_MD3_LASTFRAME]);

	ret.AddPoint(idVec3(
		frames[frame].bounds[0][0],
		frames[frame].bounds[0][1],
		frames[frame].bounds[0][2]));

	ret.AddPoint(idVec3(
		frames[frame].bounds[1][0],
		frames[frame].bounds[1][1],
		frames[frame].bounds[1][2]));

	if  ( oldframe != frame && ent->shaderParms[SHADERPARM_MD3_BACKLERP] > 0.0f ) {
		ret.AddPoint(idVec3(
			frames[oldframe].bounds[0][0],
			frames[oldframe].bounds[0][1],
			frames[oldframe].bounds[0][2]));

		ret.AddPoint(idVec3(
			frames[oldframe].bounds[1][0],
			frames[oldframe].bounds[1][1],
			frames[oldframe].bounds[1][2]));
	}

	return ret;
}

/*
=============
idRenderModelTAN::FindMaterialFor
=============
*/
const idMaterial* idRenderModelTAN::FindMaterialFor( const tanSurface_t* surface ) const {

	const char* shaderName = tikiMaterials.GetString(surface->name, surface->name);

	return declManager->FindMaterial(shaderName);
}

/*
=====================
idRenderModelTAN::BuildSilInfo

Builds the index list (without degenerate triangles) and the silhouette
edge connectivity used for stencil shadows.  The result owns its index array.
=====================
*/
silInfo_t idRenderModelTAN::BuildSilInfo( tanSurface_t* surf ) {
	const int numVerts = surf->numVerts;
	silInfo_t ret = { 0 };

	if ( numVerts <= 0 || surf->numTriangles <= 0 ) {
		return ret;
	}

	const tanTriangle_t* tris = (const tanTriangle_t*)((const byte*)surf + surf->ofsTriangles);

	// count the real triangles
	int numGood = 0;
	for ( int i = 0; i < surf->numTriangles; i++ ) {
		const int* ix = tris[i].indexes;
		if ( ix[0] != ix[1] && ix[0] != ix[2] && ix[1] != ix[2] ) {
			numGood++;
		}
	}
	if ( numGood == 0 ) {
		return ret;
	}

	bool onStack;
	idDrawVert* verts = (idDrawVert*)Mem_MallocA(numVerts * sizeof(idDrawVert), onStack);
	for ( int i = 0; i < numVerts; ++i ) {
		verts[i].Clear();
	}
	// only .xyz is used by R_CreateSilIndexes() and R_IdentifySilEdges()
	R_TAN_DecodeFrame( verts, surf, (const tanFrame_t*)((const byte*)tan + tan->ofsFrames), 0 );

	srfTriangles_t tri;
	memset(&tri, 0, sizeof(tri));

	tri.verts = verts;
	tri.numVerts = numVerts;
	tri.numIndexes = numGood * 3;
	R_AllocStaticTriSurfIndexes(&tri, tri.numIndexes);

	int n = 0;
	for ( int i = 0; i < surf->numTriangles; i++ ) {
		const int* ix = tris[i].indexes;
		if ( ix[0] != ix[1] && ix[0] != ix[2] && ix[1] != ix[2] ) {
			tri.indexes[n++] = ix[0];
			tri.indexes[n++] = ix[1];
			tri.indexes[n++] = ix[2];
		}
	}

	R_CreateSilIndexes(&tri);	// allocates and sets tri.silIndexes
	// static model, so coplanar edges can't become silhouette edges later
	//R_IdentifySilEdges(&tri, true);	// allocates and sets tri.silEdges
	R_IdentifySilEdges(&tri, tan->numFrames == 1);

	ret.numIndexes = tri.numIndexes;
	ret.numSilEdges = tri.numSilEdges;
	ret.indexes = tri.indexes;
	ret.silIndexes = tri.silIndexes;
	ret.silEdges = tri.silEdges;

	Mem_FreeA(verts, onStack);

	return ret;
}

/*
====================
idRenderModelTAN::GetJointHandle
====================
*/
jointHandle_t idRenderModelTAN::GetJointHandle( const char* name ) const {
	if ( tan == NULL ) {
		return INVALID_JOINT;
	}

	for ( int i = 0; i < tan->numTags; i++ ) {
		const tanTagName_t* tagName = (const tanTagName_t*)((const byte*)tan + tan->ofsTags[i]);
		if ( idStr::Icmp(tagName->name, name) == 0 ) {
			return (jointHandle_t)i;
		}
	}

	return INVALID_JOINT;
}

/*
=====================
idRenderModelTAN::GetJointName
=====================
*/
const char* idRenderModelTAN::GetJointName( jointHandle_t handle ) const {
	if ( tan == NULL || handle < 0 || handle >= tan->numTags ) {
		return "<invalid joint>";
	}

	return ((const tanTagName_t*)((const byte*)tan + tan->ofsTags[handle]))->name;
}

static ID_INLINE const md2Frame_t* R_MD2_Frame( const md2Header_t* md2, int frame ) {
	return (const md2Frame_t*)((const byte*)md2 + md2->ofsFrames + frame * md2->frameSize);
}

static ID_INLINE const md2Vertex_t* R_MD2_FrameVerts( const md2Frame_t* frame ) {
	return (const md2Vertex_t*)(frame + 1);
}

/*
=================
R_MD2_RangeOK

Checks that count elements of elemSize bytes starting at ofs fit inside limit.
64 bit maths so hostile counts can't overflow.
=================
*/
static bool R_MD2_RangeOK( int ofs, long long count, long long elemSize, int limit ) {
	if ( count < 0 ) {
		return false;
	}
	if ( count == 0 ) {
		return true;
	}
	if ( ofs < 0 ) {
		return false;
	}
	return ((long long)ofs + count * elemSize) <= (long long)limit;
}

/*
=================
R_MD2_SwapAndValidate

Byte swaps the whole file in place and validates every offset, count and
index before anything dereferences them.
Returns false (after printing a warning) if the file can't be used.
=================
*/
static bool R_MD2_SwapAndValidate( md2Header_t* md2, int size, const char* fileName ) {
	int i, j;

	// the header is 17 ints
	int* hdr = (int*)md2;
	for ( i = 0; i < (int)(sizeof(md2Header_t) / sizeof(int)); i++ ) {
		LL(hdr[i]);
	}

	if ( md2->ident != MD2_IDENT ) {
		common->Warning("InitFromFile: %s is not an MD2 file (bad ident)", fileName);
		return false;
	}
	if ( md2->version != MD2_VERSION ) {
		common->Warning("InitFromFile: %s has wrong version (%i should be %i)", fileName, md2->version, MD2_VERSION);
		return false;
	}
	if ( md2->ofsEnd != size ) {
		common->Warning("InitFromFile: %s has a bad ofsEnd", fileName);
		return false;
	}
	if ( md2->numFrames < 1 ) {
		common->Warning("InitFromFile: %s has no frames", fileName);
		return false;
	}
	if ( md2->numXyz < 1 || md2->numXyz > MD2_MAX_VERTS ) {
		common->Warning("InitFromFile: %s has a bad vertex count (%i, max %i)", fileName, md2->numXyz, MD2_MAX_VERTS);
		return false;
	}
	if ( md2->numSt < 1 || md2->numSt > MD2_MAX_ST ) {
		common->Warning("InitFromFile: %s has a bad texture coordinate count (%i, max %i)", fileName, md2->numSt, MD2_MAX_ST);
		return false;
	}
	if ( md2->numTris < 1 || md2->numTris > MD2_MAX_TRIANGLES ) {
		common->Warning("InitFromFile: %s has a bad triangle count (%i, max %i)", fileName, md2->numTris, MD2_MAX_TRIANGLES);
		return false;
	}
	if ( md2->numSkins < 0 || md2->numSkins > MD2_MAX_SKINS ) {
		common->Warning("InitFromFile: %s has a bad skin count (%i)", fileName, md2->numSkins);
		return false;
	}
	if ( md2->skinWidth < 1 || md2->skinHeight < 1 ) {
		common->Warning("InitFromFile: %s has a bad skin size", fileName);
		return false;
	}
	if ( md2->frameSize < (int)sizeof(md2Frame_t) + md2->numXyz * (int)sizeof(md2Vertex_t) ) {
		common->Warning("InitFromFile: %s has a frame size (%i) too small for %i vertexes", fileName, md2->frameSize, md2->numXyz);
		return false;
	}
	if ( !R_MD2_RangeOK(md2->ofsSkins, md2->numSkins, sizeof(md2Skin_t), size) ||
		!R_MD2_RangeOK(md2->ofsSt, md2->numSt, sizeof(md2St_t), size) ||
		!R_MD2_RangeOK(md2->ofsTris, md2->numTris, sizeof(md2Triangle_t), size) ||
		!R_MD2_RangeOK(md2->ofsFrames, md2->numFrames, md2->frameSize, size) ) {
		common->Warning("InitFromFile: %s has data outside the file", fileName);
		return false;
	}

	// skin names are plain chars, just make sure they terminate
	md2Skin_t* skin = (md2Skin_t*)((byte*)md2 + md2->ofsSkins);
	for ( i = 0; i < md2->numSkins; i++, skin++ ) {
		skin->name[MAX_MD2PATH - 1] = 0;
	}

	// swap the texture coordinates
	md2St_t* st = (md2St_t*)((byte*)md2 + md2->ofsSt);
	for ( i = 0; i < md2->numSt; i++, st++ ) {
		st->s = LittleShort(st->s);
		st->t = LittleShort(st->t);
	}

	// swap and range check the triangles
	md2Triangle_t* tri = (md2Triangle_t*)((byte*)md2 + md2->ofsTris);
	for ( i = 0; i < md2->numTris; i++, tri++ ) {
		for ( j = 0; j < 3; j++ ) {
			tri->xyz[j] = LittleShort(tri->xyz[j]);
			tri->st[j] = LittleShort(tri->st[j]);
			if (tri->xyz[j] < 0 || tri->xyz[j] >= md2->numXyz || tri->st[j] < 0 || tri->st[j] >= md2->numSt) {
				common->Warning("InitFromFile: %s has an out of range vertex or st index in triangle %i", fileName, i);
				return false;
			}
		}
	}

	// swap the frame scale / translate (vertexes are bytes)
	for ( i = 0; i < md2->numFrames; i++ ) {
		md2Frame_t* frame = (md2Frame_t*)((byte*)md2 + md2->ofsFrames + i * md2->frameSize);
		for ( j = 0; j < 3; j++ ) {
			frame->scale[j] = LittleFloat(frame->scale[j]);
			frame->translate[j] = LittleFloat(frame->translate[j]);
		}
		frame->name[15] = 0;
	}

	return true;
}

idRenderModelMD2::idRenderModelMD2() : dataSize(0), md2(NULL), material(NULL) {
	memset(&silInfo, 0, sizeof(silInfo));
}

idRenderModelMD2::~idRenderModelMD2() {
	FreeData();
}

/*
=================
idRenderModelMD2::FreeData

Releases everything InitFromFile() allocated.  Called from the destructor and
at the start of InitFromFile() so reloading doesn't leak.
=================
*/
void idRenderModelMD2::FreeData() {
	srfTriangles_t tri;
	memset(&tri, 0, sizeof(tri));

	// Note: tri.sil* and tri.indexes were allocated by specialized allocators only
	//       available in tr_trisurf.cpp, so they have to be freed with them as well.
	tri.silIndexes = silInfo.silIndexes;
	tri.silEdges = silInfo.silEdges;
	tri.indexes = silInfo.indexes;
	R_FreeStaticTriSurfSilIndexes(&tri);
	R_FreeStaticTriSurfSilEdges(&tri);
	R_FreeStaticTriSurfIndexes(&tri);
	memset(&silInfo, 0, sizeof(silInfo));

	vertXyz.Clear();
	vertSt.Clear();
	frameBounds.Clear();
	material = NULL;

	if (md2 != NULL) {
		Mem_Free(md2);
		md2 = NULL;
	}
	dataSize = 0;
}

/*
=================
idRenderModelMD2::DecodeFrame

Decodes the xyz positions of one frame (numXyz entries) into out
=================
*/
static void R_MD2_DecodeFrame( idVec3* out, const md2Header_t* md2, int frameNum ) {
	const md2Frame_t* frame = R_MD2_Frame(md2, frameNum);
	const md2Vertex_t* v = R_MD2_FrameVerts(frame);

	for ( int i = 0; i < md2->numXyz; i++, v++ ) {
		out[i].x = v->v[0] * frame->scale[0] + frame->translate[0];
		out[i].y = v->v[1] * frame->scale[1] + frame->translate[1];
		out[i].z = v->v[2] * frame->scale[2] + frame->translate[2];
	}
}

/*
=================
idRenderModelMD2::BuildMesh

Splits the mesh into unique (xyz, st) vertices.  Fills vertXyz / vertSt and
returns the triangle index list (degenerate triangles removed) in indexes.
=================
*/
bool idRenderModelMD2::BuildMesh( const char* fileName, idList<int>& indexes ) {
	const md2St_t* sts = (const md2St_t*)((const byte*)md2 + md2->ofsSt);
	const md2Triangle_t* tris = (const md2Triangle_t*)((const byte*)md2 + md2->ofsTris);

	idList<int> vertStIndex;	// st index of each split vertex
	idList<int> next;			// next split vertex with the same xyz
	int* head = (int*)Mem_Alloc(md2->numXyz * sizeof(int));	// first split vertex for each xyz
	for ( int i = 0; i < md2->numXyz; i++ ) {
		head[i] = -1;
	}

	indexes.Clear();
	vertXyz.Clear();

	for ( int t = 0; t < md2->numTris; t++ ) {
		int corner[3];

		for ( int c = 0; c < 3; c++ ) {
			const int xi = tris[t].xyz[c];
			const int si = tris[t].st[c];

			int e = head[xi];
			while ( e >= 0 && vertStIndex[e] != si ) {
				e = next[e];
			}
			if ( e < 0 ) {
				e = vertXyz.Num();
				if ( e >= MD2_MAX_EXPANDED ) {
					common->Warning("InitFromFile: %s has more than %i unique vertexes", fileName, MD2_MAX_EXPANDED);
					Mem_Free(head);
					return false;
				}
				vertXyz.Append(xi);
				vertStIndex.Append(si);
				next.Append(head[xi]);
				head[xi] = e;
			}
			corner[c] = e;
		}

		// drop degenerate triangles
		if ( corner[0] == corner[1] || corner[0] == corner[2] || corner[1] == corner[2] ) {
			continue;
		}
		// same order as MD3 (and Quake 2), no winding flip needed
		indexes.Append(corner[0]);
		indexes.Append(corner[1]);
		indexes.Append(corner[2]);
	}
	Mem_Free(head);

	if ( indexes.Num() == 0 ) {
		common->Warning("InitFromFile: %s has only degenerate triangles", fileName);
		return false;
	}

	// texture coordinates, frame invariant
	const float invW = 1.0f / (float)md2->skinWidth;
	const float invH = 1.0f / (float)md2->skinHeight;
	vertSt.SetNum(vertXyz.Num() * 2);
	for ( int i = 0; i < vertXyz.Num(); i++ ) {
		vertSt[i * 2 + 0] = sts[vertStIndex[i]].s * invW;
		vertSt[i * 2 + 1] = sts[vertStIndex[i]].t * invH;
	}

	return true;
}

/*
=================
idRenderModelMD2::InitFromFile
=================
*/
void idRenderModelMD2::InitFromFile( const char* fileName ) {
	void* buffer = NULL;
	int					size;
	int					ofsEnd;
	idList<int>			indexes;

	// make reloading safe
	FreeData();

	name = fileName;

	size = fileSystem->ReadFile(fileName, &buffer, NULL);
	if ( size <= (int)sizeof(md2Header_t) ) {
		if ( buffer != NULL ) {
			fileSystem->FreeFile(buffer);
		}
		// must make it a default model so failure can be detected elsewhere
		MakeDefaultModel();
		return;
	}

	// the header is validated properly in R_MD2_SwapAndValidate(), this only has
	// to be good enough to size the copy without reading past the end of the file
	ofsEnd = LittleInt(((md2Header_t*)buffer)->ofsEnd);
	if ( ofsEnd < (int)sizeof(md2Header_t) || ofsEnd > size ) {
		common->Warning("InitFromFile: %s has a bad ofsEnd (%i, file is %i bytes)", fileName, ofsEnd, size);
		fileSystem->FreeFile(buffer);
		MakeDefaultModel();
		return;
	}

	md2 = (md2Header_t*)Mem_Alloc(ofsEnd);
	memcpy(md2, buffer, ofsEnd);
	fileSystem->FreeFile(buffer);
	dataSize = ofsEnd;

	if ( !R_MD2_SwapAndValidate(md2, ofsEnd, fileName) || !BuildMesh(fileName, indexes) ) {
		// don't keep a half swapped file around, the model is a default model now
		FreeData();
		MakeDefaultModel();
		return;
	}

	// The first skin is used as the material.  MD2 skins are image paths like
	// "players/male/grunt.pcx", so the extension is dropped to get a material name.
	// Models without a skin get no material and expect a skin / material override.
	if ( md2->numSkins > 0 ) {
		const md2Skin_t* skin = (const md2Skin_t*)((const byte*)md2 + md2->ofsSkins);
		idStr matName = skin->name;
		matName.StripFileExtension();
		material = declManager->FindMaterial(matName.c_str());
	}

	// per frame bounds, MD2 frames don't store any
	idList<idVec3> positions;
	positions.SetNum(md2->numXyz);
	frameBounds.SetNum(md2->numFrames);
	for ( int f = 0; f < md2->numFrames; f++ ) {
		R_MD2_DecodeFrame(positions.Ptr(), md2, f);
		frameBounds[f].Clear();
		for ( int i = 0; i < md2->numXyz; i++ ) {
			frameBounds[f].AddPoint(positions[i]);
		}
	}

	silInfo = BuildSilInfo(indexes);

	// If this is only one frame generate the static model information to fix
	// loading issues and allow physics generation (same as the MD3 loader)
	if (md2->numFrames == 1) {
		CreateSurfaces();
	}

	// idRenderModelStatic::InitFromFile() does this, without it IsLoaded()
	// stays false after the first purge and the model gets reloaded on every lookup
	purged = false;
}

/*
=================
idRenderModelMD2::IsDynamicModel
=================
*/
dynamicModel_t idRenderModelMD2::IsDynamicModel() const {
	// md2 is NULL if loading failed and this is a default model now
	if ( md2 == NULL || md2->numFrames == 1 ) {
		return DM_STATIC;
	}
	return DM_CACHED;
}

/*
=================
idRenderModelMD2::LerpMeshVertexes

Appends the split vertexes for frame / oldframe to tri->verts
=================
*/
void idRenderModelMD2::LerpMeshVertexes( srfTriangles_t* tri, const float backlerp, const int frame, const int oldframe ) const {
	const int numSplit = vertXyz.Num();
	const md2Frame_t* newFrame = R_MD2_Frame(md2, frame);
	const md2Vertex_t* newVerts = R_MD2_FrameVerts(newFrame);

	idDrawVert* out = tri->verts + tri->numVerts;

	if ( backlerp == 0 ) {
		//
		// just copy the vertexes
		//
		for ( int v = 0; v < numSplit; v++, out++ ) {
			const md2Vertex_t& nv = newVerts[vertXyz[v]];
			out->xyz.x = nv.v[0] * newFrame->scale[0] + newFrame->translate[0];
			out->xyz.y = nv.v[1] * newFrame->scale[1] + newFrame->translate[1];
			out->xyz.z = nv.v[2] * newFrame->scale[2] + newFrame->translate[2];
		}
	} else {
		//
		// interpolate and copy the vertexes
		//
		const md2Frame_t* oldFrame = R_MD2_Frame(md2, oldframe);
		const md2Vertex_t* oldVerts = R_MD2_FrameVerts(oldFrame);
		const float frontlerp = 1.0f - backlerp;

		for ( int v = 0; v < numSplit; v++, out++ ) {
			const md2Vertex_t& nv = newVerts[vertXyz[v]];
			const md2Vertex_t& ov = oldVerts[vertXyz[v]];
			for ( int k = 0; k < 3; k++ ) {
				const float newPos = nv.v[k] * newFrame->scale[k] + newFrame->translate[k];
				const float oldPos = ov.v[k] * oldFrame->scale[k] + oldFrame->translate[k];
				out->xyz[k] = oldPos * backlerp + newPos * frontlerp;
			}
		}
	}

	tri->numVerts += numSplit;
}

/*
=================
idRenderModelMD2::FillSurface

Fills an allocated (or reused) surface with the geometry for one frame.
Shared by CreateSurfaces() and InstantiateDynamicModel().
tri->verts must already be allocated for vertXyz.Num() verts.
=================
*/
void idRenderModelMD2::FillSurface( srfTriangles_t* tri, float backlerp, int frame, int oldframe ) const {
	// These flags and references must be (re)set after allocation or reuse.
	// deformedSurface prevents silEdges, silIndexes, indexes (and more not used here)
	// from being freed (they belong to silInfo which is freed in FreeData())
	tri->deformedSurface = true;
	tri->tangentsCalculated = false;
	tri->facePlanesCalculated = false;
	tri->numSilEdges = silInfo.numSilEdges;
	tri->silEdges = silInfo.silEdges;
	tri->silIndexes = silInfo.silIndexes;
	tri->indexes = silInfo.indexes;
	tri->numIndexes = silInfo.numIndexes;

	// LerpMeshVertexes appends at tri->numVerts, so reset the logical
	// vertex count while keeping the allocated vertex buffer.
	tri->numVerts = 0;
	tri->bounds.Clear();

	LerpMeshVertexes(tri, backlerp, frame, oldframe);

	// Texture coordinates are frame-invariant.  Reassigning them is cheap
	// and also initialises newly allocated vertex buffers.
	for ( int j = 0; j < vertXyz.Num(); j++ ) {
		idDrawVert* stri = &tri->verts[j];
		stri->st[0] = vertSt[j * 2 + 0];
		stri->st[1] = vertSt[j * 2 + 1];
	}

	R_BoundTriSurf(tri);
}

/*
=============
idRenderModelMD2::CreateSurfaces

Single frame models get real static surfaces (needed for physics / collision
model generation).
=============
*/
void idRenderModelMD2::CreateSurfaces() {
	this->bounds.Clear();

	srfTriangles_t* tri = R_AllocStaticTriSurf();
	R_AllocStaticTriSurfVerts(tri, vertXyz.Num());

	FillSurface(tri, 0.0f, 0, 0);

	modelSurface_t surf;
	surf.geometry = tri;
	surf.shader = material;
	surf.id = this->NumSurfaces();
	this->AddSurface(surf);
	this->bounds.AddPoint(tri->bounds[0]);
	this->bounds.AddPoint(tri->bounds[1]);
}

/*
=============
idRenderModelMD2::InstantiateDynamicModel

The per-entity snapshot (and the vertex allocation inside it) is kept between
calls and only the animated vertex positions are rewritten.  The topology
(indexes, sil edges) is shared with silInfo and never touched.
Frame, last frame and backlerp come from the same shaderParms as the MD3 loader.
=============
*/
idRenderModel* idRenderModelMD2::InstantiateDynamicModel( const struct renderEntity_s* ent, const struct viewDef_s* view, idRenderModel* cachedModel ) {
	int				surfaceNum;
	int				frame, oldframe;
	float			backlerp;
	idRenderModelStatic* staticModel;

	if ( md2 == NULL ) {
		// failed to load, we are a default model and shouldn't get here
		if ( cachedModel ) {
			delete cachedModel;
		}
		return NULL;
	}

	const int numFrames = md2->numFrames;

	frame = idMath::ClampInt(0, numFrames - 1, ent->shaderParms[SHADERPARM_MD3_FRAME]);
	oldframe = idMath::ClampInt(0, numFrames - 1, ent->shaderParms[SHADERPARM_MD3_LASTFRAME]);
	backlerp = idMath::ClampFloat(0.0f, 1.0f, ent->shaderParms[SHADERPARM_MD3_BACKLERP]);

	if ( cachedModel && !r_useCachedDynamicModels.GetBool() ) {
		delete cachedModel;
		cachedModel = NULL;
	}

	// Do not accept snapshots generated by another model type
	if ( cachedModel && idStr::Icmp(cachedModel->Name(), "_MD2_Snapshot_") != 0 ) {
		delete cachedModel;
		cachedModel = NULL;
	}

	if ( cachedModel ) {
		assert(dynamic_cast<idRenderModelStatic*>(cachedModel) != NULL);
		staticModel = static_cast<idRenderModelStatic*>(cachedModel);
	} else {
		staticModel = new idRenderModelStatic;
		staticModel->InitEmpty("_MD2_Snapshot_");
	}

	staticModel->bounds.Clear();

	// Surface IDs are stable for the lifetime of this model.  Keep the
	// modelSurface_t and its vertex allocation in the per-entity snapshot.
	modelSurface_t* surf;
	if ( staticModel->FindSurfaceWithId(0, surfaceNum) ) {
		surf = &staticModel->surfaces[surfaceNum];
	} else {
		modelSurface_t newSurf;
		memset(&newSurf, 0, sizeof(newSurf));
		newSurf.id = 0;
		staticModel->AddSurface(newSurf);
		if ( !staticModel->FindSurfaceWithId(0, surfaceNum) ) {
			common->Error("MD2 snapshot: failed to find newly-added surface");
		}
		surf = &staticModel->surfaces[surfaceNum];
	}

	// The vertex and index counts determine whether the existing triangle
	// allocation can be reused.  The topology arrays are owned by this
	// model's silInfo, not by the snapshot.
	srfTriangles_t* tri = surf->geometry;
	if ( tri && (tri->numVerts != vertXyz.Num() || tri->numIndexes != silInfo.numIndexes) ) {
		R_FreeStaticTriSurf(tri);
		surf->geometry = NULL;
		tri = NULL;
	}
	if ( !tri ) {
		tri = R_AllocStaticTriSurf();
		R_AllocStaticTriSurfVerts(tri, vertXyz.Num());
		surf->geometry = tri;
	} else {
		// Vertex positions are about to change.  Drop any cached GPU
		// copy (ambient, shadow and index caches) while retaining the
		// CPU-side vertex allocation.
		R_FreeStaticTriSurfVertexCaches(tri);
	}

	surf->shader = material;

	FillSurface(tri, backlerp, frame, oldframe);

	staticModel->bounds.AddPoint(tri->bounds[0]);
	staticModel->bounds.AddPoint(tri->bounds[1]);

	return staticModel;
}

/*
=====================
idRenderModelMD2::Bounds
=====================
*/
idBounds idRenderModelMD2::Bounds( const struct renderEntity_s* ent ) const {
	idBounds		ret;

	ret.Clear();

	if ( !ent || !md2 || frameBounds.Num() == 0 ) {
		// just give it the editor bounds
		ret.AddPoint(idVec3(-10, -10, -10));
		ret.AddPoint(idVec3(10, 10, 10));
		return ret;
	}

	const int frameNum = idMath::ClampInt(0, md2->numFrames - 1, ent->shaderParms[SHADERPARM_MD3_FRAME]);
	const int oldFrameNum = idMath::ClampInt(0, md2->numFrames - 1, ent->shaderParms[SHADERPARM_MD3_LASTFRAME]);

	ret = frameBounds[frameNum];

	// while interpolating the mesh is a blend of both frames
	if ( oldFrameNum != frameNum && ent->shaderParms[SHADERPARM_MD3_BACKLERP] > 0.0f ) {
		ret.AddBounds(frameBounds[oldFrameNum]);
	}

	return ret;
}

/*
=====================
idRenderModelMD2::BuildSilInfo

Builds the silhouette edge connectivity used for stencil shadows.  The result
owns its own copy of the index list.
=====================
*/
silInfo_t idRenderModelMD2::BuildSilInfo( const idList<int>& indexes ) {
	const int numSplit = vertXyz.Num();
	silInfo_t ret = { 0 };

	// positions of frame 0, expanded to the split vertexes.  Only .xyz is used by
	// R_CreateSilIndexes() and R_IdentifySilEdges().
	idList<idVec3> positions;
	positions.SetNum(md2->numXyz);
	R_MD2_DecodeFrame(positions.Ptr(), md2, 0);

	bool onStack;
	idDrawVert* verts = (idDrawVert*)Mem_MallocA(numSplit * sizeof(idDrawVert), onStack);
	for ( int i = 0; i < numSplit; ++i ) {
		verts[i].Clear();
		verts[i].xyz = positions[vertXyz[i]];
	}

	srfTriangles_t tri;
	memset(&tri, 0, sizeof(tri));

	tri.verts = verts;
	tri.numVerts = numSplit;
	tri.numIndexes = indexes.Num();
	R_AllocStaticTriSurfIndexes(&tri, tri.numIndexes);
	for ( int i = 0; i < tri.numIndexes; i++ ) {
		tri.indexes[i] = indexes[i];
	}

	R_CreateSilIndexes(&tri);	// allocates and sets tri.silIndexes
	// if there's only one frame (no animation), the omitCoplanarEdges optimization can be used
	// (otherwise it can't because edges that are coplanar in frame 0 may not be in another frame)
	R_IdentifySilEdges(&tri, md2->numFrames == 1);	// allocates and sets tri.silEdges

	ret.numIndexes = tri.numIndexes;
	ret.numSilEdges = tri.numSilEdges;
	ret.indexes = tri.indexes;
	ret.silIndexes = tri.silIndexes;
	ret.silEdges = tri.silEdges;

	Mem_FreeA(verts, onStack);

	return ret;
}

/***********************************************************************

	idRenderModelMDL

	Quake 1 .mdl loader, derived from the MD2 loader (which is derived from
	the MD3 loader): snapshot caching, stencil shadow info, validation.

	Unlike MD2/MD3 the file isn't kept in memory.  Everything after the header
	is variable length and unaligned, so the file is parsed once with bounds
	checked reads into aligned arrays: the frame positions (3 bytes per vertex
	per frame, frame groups flattened into plain frames) and the split mesh.

	Embedded skins are palette indexed pixel data.  They are decoded with
	gfx/palette.lmp into generated images ("mdlskin/<model>") with a generated
	material each, unless an authored material with the model's name exists.

***********************************************************************/

#define	LL(x) x=LittleInt(x)

/*
=================
mdlReader

Bounds checked sequential reader over the raw file
=================
*/
struct mdlReader_t {
	const byte* data;
	int				size;
	int				pos;
	bool			ok;

	mdlReader_t(const byte* d, int s) : data(d), size(s), pos(0), ok(true) {}

	bool Skip(long long n) {
		if ( !ok || n < 0 || (long long)pos + n >(long long)size ) {
			ok = false;
			return false;
		}
		pos += (int)n;
		return true;
	}
	const byte* Take(long long n) {
		const int start = pos;
		return Skip(n) ? data + start : NULL;
	}
	int ReadInt() {
		const byte* p = Take(4);
		if ( p == NULL ) {
			return 0;
		}
		int v;
		memcpy(&v, p, 4);	// the file is unaligned, don't cast
		return LittleInt(v);
	}
};

/*
=================
Generated skin images

Images created from memory have to be able to regenerate themselves (vid_restart,
level change purges), and the generator callback gets nothing but the idImage, so
the decoded pixels are kept in a registry looked up by image name.
=================
*/
struct mdlSkinImage_t {
	idStr				name;
	int					width;
	int					height;
	idList<byte>		rgba;
};
static idList<mdlSkinImage_t>	mdlSkinImages;

/*
=================
R_MDL_GetPalette

The 768 byte Quake palette from gfx/palette.lmp.  If it isn't in the VFS a grey
ramp is used so the model is at least visible, and a warning is printed once.
=================
*/
static const byte* R_MDL_GetPalette() {
	static byte palette[768];
	static int state = 0;	// 0 = not tried, 1 = loaded, 2 = fallback

	if ( state == 0 ) {
		void* buf = NULL;
		const int len = fileSystem->ReadFile(MDL_PALETTE_FILE, &buf, NULL);
		if ( buf != NULL && len >= 768 ) {
			memcpy(palette, buf, 768);
			state = 1;
		} else {
			common->Warning("MDL: couldn't load %s, Quake models will be grey. Put the Quake palette in the VFS.", MDL_PALETTE_FILE);
			for ( int i = 0; i < 256; i++ ) {
				palette[i * 3 + 0] = palette[i * 3 + 1] = palette[i * 3 + 2] = (byte)i;
			}
			state = 2;
		}
		if ( buf != NULL ) {
			fileSystem->FreeFile(buf);
		}
	}
	return palette;
}

/*
=================
R_MDL_SkinImageGenerator
=================
*/
static void R_MDL_SkinImageGenerator( idImage* image ) {
	for ( int i = 0; i < mdlSkinImages.Num(); i++ ) {
		const mdlSkinImage_t& s = mdlSkinImages[i];
		if ( idStr::Icmp(s.name.c_str(), image->imgName.c_str()) == 0 ) {
			image->GenerateImage(s.rgba.Ptr(), s.width, s.height,
#if MDL_NEAREST_FILTER
				TF_NEAREST,
#else
				TF_DEFAULT,
#endif
				true, TR_REPEAT, TD_DIFFUSE);
			return;
		}
	}
	image->MakeDefault();
}

/*
=================
R_MDL_NextPow2
=================
*/
static int R_MDL_NextPow2( int v ) {
	int p = 1;
	while ( p < v ) {
		p <<= 1;
	}
	return p;
}

/*
=================
R_MDL_Resample

Resamples an RGBA image.  The image system only accepts power of two sizes and Quake
skins often aren't (e.g. 190x112).  Texture coordinates are normalized by the skin size
in the file, so stretching the whole image keeps them valid, for the generated image
as well as for an authored replacement of any size.
Nearest keeps the hard Quake pixels, otherwise it is bilinear.
=================
*/
static void R_MDL_Resample( const byte* in, int inW, int inH, byte* out, int outW, int outH, bool nearest ) {
	for ( int y = 0; y < outH; y++ ) {
		const float fy = (y + 0.5f) * inH / outH - 0.5f;
		for ( int x = 0; x < outW; x++ ) {
			const float fx = (x + 0.5f) * inW / outW - 0.5f;
			byte* dst = out + (y * outW + x) * 4;

			if ( nearest ) {
				int sx = (int)(fx + 0.5f);
				int sy = (int)(fy + 0.5f);
				sx = sx < 0 ? 0 : (sx >= inW ? inW - 1 : sx);
				sy = sy < 0 ? 0 : (sy >= inH ? inH - 1 : sy);
				memcpy(dst, in + (sy * inW + sx) * 4, 4);
				continue;
			}

			int x0 = (int)floorf(fx);
			int y0 = (int)floorf(fy);
			const float wx = fx - x0;
			const float wy = fy - y0;
			int x1 = x0 + 1;
			int y1 = y0 + 1;
			x0 = x0 < 0 ? 0 : (x0 >= inW ? inW - 1 : x0);
			x1 = x1 < 0 ? 0 : (x1 >= inW ? inW - 1 : x1);
			y0 = y0 < 0 ? 0 : (y0 >= inH ? inH - 1 : y0);
			y1 = y1 < 0 ? 0 : (y1 >= inH ? inH - 1 : y1);

			const byte* p00 = in + (y0 * inW + x0) * 4;
			const byte* p10 = in + (y0 * inW + x1) * 4;
			const byte* p01 = in + (y1 * inW + x0) * 4;
			const byte* p11 = in + (y1 * inW + x1) * 4;
			for ( int k = 0; k < 4; k++ ) {
				const float top = p00[k] + (p10[k] - p00[k]) * wx;
				const float bottom = p01[k] + (p11[k] - p01[k]) * wx;
				dst[k] = (byte)(top + (bottom - top) * wy + 0.5f);
			}
		}
	}
}

/*
=================
R_MDL_RegisterSkinImage

Decodes palette indexes to RGBA, resamples to a power of two size and (re)registers
them under name.  Palette index 255 is transparent, as in GLQuake.  Returns true if any pixel is transparent.
=================
*/
static bool R_MDL_RegisterSkinImage( const char* name, int width, int height, const byte* indexes ) {
	const byte* palette = R_MDL_GetPalette();
	const int numPixels = width * height;
	bool hasAlpha = false;

	int slot = -1;
	for ( int i = 0; i < mdlSkinImages.Num(); i++ ) {
		if ( idStr::Icmp(mdlSkinImages[i].name.c_str(), name) == 0 ) {
			slot = i;
			break;
		}
	}
	if ( slot < 0 ) {
		mdlSkinImage_t blank;
		blank.name = name;
		blank.width = blank.height = 0;
		mdlSkinImages.Append(blank);
		slot = mdlSkinImages.Num() - 1;
	}

	mdlSkinImage_t& img = mdlSkinImages[slot];

	// decode the palette indexes
	idList<byte> decoded;
	decoded.SetNum(numPixels * 4);
	byte* dst = decoded.Ptr();
	for ( int i = 0; i < numPixels; i++, dst += 4 ) {
		const int idx = indexes[i];
		dst[0] = palette[idx * 3 + 0];
		dst[1] = palette[idx * 3 + 1];
		dst[2] = palette[idx * 3 + 2];
		dst[3] = (idx == 255) ? 0 : 255;
		hasAlpha |= (idx == 255);
	}

	// the image system needs power of two sizes
	const int potW = R_MDL_NextPow2(width);
	const int potH = R_MDL_NextPow2(height);
	img.width = potW;
	img.height = potH;
	img.rgba.SetNum(potW * potH * 4);
	if ( potW == width && potH == height ) {
		memcpy(img.rgba.Ptr(), decoded.Ptr(), numPixels * 4);
	} else {
#if MDL_NEAREST_FILTER
		const bool nearest = true;
#else
		const bool nearest = false;
#endif
		R_MDL_Resample(decoded.Ptr(), width, height, img.rgba.Ptr(), potW, potH, nearest);
	}
	return hasAlpha;
}

/*
=================
R_MDL_ImageFileExists

True if an image file with this name exists in the VFS (a mod's own replacement texture)
=================
*/
static bool R_MDL_ImageFileExists( const char* name ) {
	static const char* extensions[] = { ".tga", ".png", ".jpg" };

	for ( int i = 0; i < 3; i++ ) {
		const idStr path = idStr(name) + extensions[i];
		if ( fileSystem->ReadFile(path.c_str(), NULL, NULL) != -1 ) {
			return true;
		}
	}
	return false;
}

/*
=================
idRenderModelMDL::BuildMaterial

Returns the material for the model:
 1. an authored material named like the model ("progs/soldier") if one exists
 2. otherwise, if an image file with that name exists, the implicit material for it
 3. otherwise one generated from the embedded skin 0, with the same name
 4. otherwise the default material for that name
Skin N (N > 0) of a model gets a generated "<name>_skinN" material, which a skin
decl can remap to.  A generated material is a plain lit diffusemap, and perforated
if the skin uses palette index 255.

The generated image has the same name as its material.  The material is created
through the implicit material path (FindMaterial() on an unknown name), so it
belongs to no source file.  CreateNewDecl() would attach it to a file that doesn't
exist on disk, and reloadDecls then fails trying to re-read that file.  The implicit
material's default text maps the image of the same name, which is why the image has
to exist before the material is first looked up.
=================
*/
const idMaterial* idRenderModelMDL::BuildMaterial( const char* matName, int skinWidth, int skinHeight, const idList<byte>& skinData, int numSkinsLoaded ) {
	// authored materials always win, this is how mods replace the Quake skins
	const idMaterial* authored = declManager->FindMaterial(matName, false);
	if ( authored != NULL ) {
		return authored;
	}

#if MDL_GENERATE_SKINS
	const int skinBytes = skinWidth * skinHeight;
	const idMaterial* first = NULL;

	for ( int i = 0; i < numSkinsLoaded; i++ ) {
		idStr skinName = matName;
		if ( i > 0 ) {
			skinName += va("_skin%i", i);
		}

		const idMaterial* mat;

		if ( R_MDL_ImageFileExists(skinName.c_str()) ) {
			// a mod ships its own texture for this skin, the implicit material uses it
			mat = declManager->FindMaterial(skinName.c_str());
		} else {
			const bool hasAlpha = R_MDL_RegisterSkinImage(skinName.c_str(), skinWidth, skinHeight, skinData.Ptr() + i * skinBytes);
			globalImages->ImageFromFunction(skinName.c_str(), R_MDL_SkinImageGenerator);

			// creates the implicit material (no source file) and parses its default text
			mat = declManager->FindMaterial(skinName.c_str());

			// replace the default text and force a reparse, SetText() alone doesn't mark the
			// decl as unparsed so the material would stay on the default definition
			idMaterial* editable = const_cast<idMaterial*>(mat);
			editable->SetText(va("{\n\t%s\n\tdiffusemap \"%s\"\n}\n", hasAlpha ? "perforated" : "", skinName.c_str()));
			editable->Invalidate();

			// parse it now, that is what finds the image
			mat = declManager->FindMaterial(skinName.c_str());
		}

		if ( i == 0 ) {
			first = mat;
		}
	}
	if ( first != NULL ) {
		return first;
	}
#endif

	// no skins in the file (or generation is off), the default material makes it visible
	return declManager->FindMaterial(matName);
}

idRenderModelMDL::idRenderModelMDL() : dataSize(0), numXyz(0), numFrames(0), material(NULL) {
	memset(&silInfo, 0, sizeof(silInfo));
	scale[0] = scale[1] = scale[2] = 1.0f;
	translate[0] = translate[1] = translate[2] = 0.0f;
}

idRenderModelMDL::~idRenderModelMDL() {
	FreeData();
}

/*
=================
idRenderModelMDL::FreeData

Releases everything InitFromFile() allocated.  Called from the destructor and
at the start of InitFromFile() so reloading doesn't leak.
=================
*/
void idRenderModelMDL::FreeData() {
	srfTriangles_t tri;
	memset(&tri, 0, sizeof(tri));

	// Note: tri.sil* and tri.indexes were allocated by specialized allocators only
	//       available in tr_trisurf.cpp, so they have to be freed with them as well.
	tri.silIndexes = silInfo.silIndexes;
	tri.silEdges = silInfo.silEdges;
	tri.indexes = silInfo.indexes;
	R_FreeStaticTriSurfSilIndexes(&tri);
	R_FreeStaticTriSurfSilEdges(&tri);
	R_FreeStaticTriSurfIndexes(&tri);
	memset(&silInfo, 0, sizeof(silInfo));

	framePos.Clear();
	vertXyz.Clear();
	vertSt.Clear();
	frameBounds.Clear();
	material = NULL;
	numXyz = 0;
	numFrames = 0;
	dataSize = 0;
}

/*
=================
idRenderModelMDL::Parse

Reads the whole file into framePos / scale / translate and returns the raw
texture coordinate and triangle tables for BuildMesh().
Returns false (after printing a warning) if the file can't be used.
=================
*/
bool idRenderModelMDL::Parse( const byte* data, int size, const char* fileName, idList<int>& stData, idList<int>& triData, int& skinWidth, int& skinHeight, idList<byte>& skinData, int& numSkinsLoaded ) {
	mdlReader_t r(data, size);
	mdlHeader_t h;
	int i, j;

	const byte* p = r.Take(sizeof(mdlHeader_t));
	if ( p == NULL ) {
		common->Warning("InitFromFile: %s is too small", fileName);
		return false;
	}
	memcpy(&h, p, sizeof(h));

	LL(h.ident);
	LL(h.version);
	for ( i = 0; i < 3; i++ ) {
		h.scale[i] = LittleFloat(h.scale[i]);
		h.translate[i] = LittleFloat(h.translate[i]);
	}
	LL(h.numSkins);
	LL(h.skinWidth);
	LL(h.skinHeight);
	LL(h.numVerts);
	LL(h.numTris);
	LL(h.numFrames);

	if ( h.ident == MDL_IDENT_HL ) {
		common->Warning("InitFromFile: %s is a Half-Life model (IDST), only Quake 1 (IDPO) models are supported", fileName);
		return false;
	}
	if ( h.ident != MDL_IDENT ) {
		common->Warning("InitFromFile: %s is not an MDL file (bad ident)", fileName);
		return false;
	}
	if ( h.version != MDL_VERSION ) {
		common->Warning("InitFromFile: %s has wrong version (%i should be %i)", fileName, h.version, MDL_VERSION);
		return false;
	}
	if ( h.numVerts < 1 || h.numVerts > MDL_MAX_VERTS ) {
		common->Warning("InitFromFile: %s has a bad vertex count (%i, max %i)", fileName, h.numVerts, MDL_MAX_VERTS);
		return false;
	}
	if ( h.numTris < 1 || h.numTris > MDL_MAX_TRIANGLES ) {
		common->Warning("InitFromFile: %s has a bad triangle count (%i, max %i)", fileName, h.numTris, MDL_MAX_TRIANGLES);
		return false;
	}
	if ( h.numFrames < 1 || h.numFrames > MDL_MAX_FRAMES ) {
		common->Warning("InitFromFile: %s has a bad frame count (%i, max %i)", fileName, h.numFrames, MDL_MAX_FRAMES);
		return false;
	}
	if ( h.numSkins < 0 || h.numSkins > MDL_MAX_SKINS ) {
		common->Warning("InitFromFile: %s has a bad skin count (%i)", fileName, h.numSkins);
		return false;
	}
	if ( h.skinWidth < 1 || h.skinWidth > MDL_MAX_SKIN_SIZE || h.skinHeight < 1 || h.skinHeight > MDL_MAX_SKIN_SIZE ) {
		common->Warning("InitFromFile: %s has a bad skin size (%i x %i)", fileName, h.skinWidth, h.skinHeight);
		return false;
	}

	const long long skinBytes = (long long)h.skinWidth * h.skinHeight;

	// skins: palette indexed pixels, kept (up to MDL_MAX_USED_SKINS, groups flattened)
	// to generate textures from
	skinData.Clear();
	numSkinsLoaded = 0;
	for ( i = 0; i < h.numSkins && r.ok; i++ ) {
		const int type = r.ReadInt();
		int inGroup = 1;

		if ( type != 0 ) {
			inGroup = r.ReadInt();
			if ( inGroup < 1 || inGroup > MDL_MAX_GROUP ) {
				r.ok = false;
				break;
			}
			r.Skip((long long)inGroup * 4);	// intervals
		}
		for ( j = 0; j < inGroup && r.ok; j++ ) {
			const byte* pixels = r.Take(skinBytes);
			if (pixels != NULL && numSkinsLoaded < MDL_MAX_USED_SKINS) {
				const int base = skinData.Num();
				skinData.SetNum(base + (int)skinBytes);
				memcpy(skinData.Ptr() + base, pixels, (size_t)skinBytes);
				numSkinsLoaded++;
			}
		}
	}
	if ( !r.ok ) {
		common->Warning("InitFromFile: %s has truncated or bad skin data", fileName);
		return false;
	}

	// texture coordinates
	stData.SetNum(h.numVerts * 3);
	for ( i = 0; i < h.numVerts * 3; i++ ) {
		stData[i] = r.ReadInt();
	}
	// triangles
	triData.SetNum(h.numTris * 4);
	for ( i = 0; i < h.numTris; i++ ) {
		for ( j = 0; j < 4; j++ ) {
			triData[i * 4 + j] = r.ReadInt();
		}
		for ( j = 1; j < 4; j++ ) {
			if ( triData[i * 4 + j] < 0 || triData[i * 4 + j] >= h.numVerts ) {
				common->Warning("InitFromFile: %s has an out of range vertex index in triangle %i", fileName, i);
				return false;
			}
		}
	}
	if ( !r.ok ) {
		common->Warning("InitFromFile: %s has truncated texture coordinate or triangle data", fileName);
		return false;
	}

	// frames, groups are flattened into plain frames
	// Preallocate no more than the rest of the file can actually hold, a hostile
	// header must not be able to make us allocate for frames that aren't there.
	// (Frames inside groups can exceed numFrames, those grow the array below.)
	const int bytesPerFrame = h.numVerts * (int)sizeof(mdlVertex_t) + 8 + 16;
	const int framesInFile = (size - r.pos) / bytesPerFrame;
	framePos.SetNum(((h.numFrames < framesInFile) ? h.numFrames : framesInFile) * h.numVerts * 3);
	int frameCount = 0;
	for ( i = 0; i < h.numFrames && r.ok; i++ ) {
		const int type = r.ReadInt();
		int inGroup = 1;

		if ( type != 0 ) {
			inGroup = r.ReadInt();
			if ( inGroup < 1 || inGroup > MDL_MAX_GROUP ) {
				r.ok = false;
				break;
			}
			r.Skip(8);					// group bbox min / max
			r.Skip((long long)inGroup * 4);	// intervals
		}

		for ( j = 0; j < inGroup && r.ok; j++ ) {
			if ( frameCount >= MDL_MAX_FRAMES ) {
				common->Warning("InitFromFile: %s has more than %i frames once groups are flattened", fileName, MDL_MAX_FRAMES);
				return false;
			}
			r.Skip(8 + 16);	// bbox min / max, name
			const byte* verts = r.Take((long long)h.numVerts * sizeof(mdlVertex_t));
			if ( verts == NULL ) {
				break;
			}
			// keep the 3 position bytes, drop the normal index
			if ( frameCount >= framePos.Num() / (h.numVerts * 3) ) {
				// more frames in groups than the header announced, grow
				framePos.SetNum((frameCount + 1) * h.numVerts * 3);
			}
			byte* dst = framePos.Ptr() + frameCount * h.numVerts * 3;
			for ( int v = 0; v < h.numVerts; v++ ) {
				dst[v * 3 + 0] = verts[v * 4 + 0];
				dst[v * 3 + 1] = verts[v * 4 + 1];
				dst[v * 3 + 2] = verts[v * 4 + 2];
			}
			frameCount++;
		}
	}
	if ( !r.ok || frameCount < 1 ) {
		common->Warning("InitFromFile: %s has truncated or bad frame data", fileName);
		return false;
	}
	framePos.SetNum(frameCount * h.numVerts * 3);

	numXyz = h.numVerts;
	numFrames = frameCount;
	for ( i = 0; i < 3; i++ ) {
		scale[i] = h.scale[i];
		translate[i] = h.translate[i];
	}
	skinWidth = h.skinWidth;
	skinHeight = h.skinHeight;
	return true;
}

/*
=================
idRenderModelMDL::DecodeFrame

Decodes the xyz positions of one frame (numXyz entries) into out
=================
*/
void idRenderModelMDL::DecodeFrame( idVec3* out, int frame ) const {
	const byte* v = framePos.Ptr() + frame * numXyz * 3;

	for ( int i = 0; i < numXyz; i++, v += 3 ) {
		out[i].x = v[0] * scale[0] + translate[0];
		out[i].y = v[1] * scale[1] + translate[1];
		out[i].z = v[2] * scale[2] + translate[2];
	}
}

/*
=================
idRenderModelMDL::BuildMesh

Quake draws a vertex with a shifted s coordinate when it is on the texture
seam and used by a back facing triangle, so the unique vertices are
(xyz index, shifted or not).  Fills vertXyz / vertSt and returns the triangle
index list (degenerate triangles removed) in indexes.
=================
*/
bool idRenderModelMDL::BuildMesh( const char* fileName, const idList<int>& stData, const idList<int>& triData, int skinWidth, int skinHeight, idList<int>& indexes ) {
	idList<int> variant;		// 0 = plain, 1 = seam shifted, of each split vertex
	idList<int> next;			// next split vertex with the same xyz
	int* head = (int*)Mem_Alloc(numXyz * sizeof(int));	// first split vertex for each xyz
	for ( int i = 0; i < numXyz; i++ ) {
		head[i] = -1;
	}

	indexes.Clear();
	vertXyz.Clear();

	const int numTris = triData.Num() / 4;
	for ( int t = 0; t < numTris; t++ ) {
		const bool front = triData[t * 4 + 0] != 0;
		int corner[3];

		for ( int c = 0; c < 3; c++ ) {
			const int xi = triData[t * 4 + 1 + c];
			const int var = (!front && stData[xi * 3 + 0] != 0) ? 1 : 0;

			int e = head[xi];
			while ( e >= 0 && variant[e] != var ) {
				e = next[e];
			}
			if ( e < 0 ) {
				e = vertXyz.Num();
				if ( e >= MDL_MAX_EXPANDED ) {
					common->Warning("InitFromFile: %s has more than %i unique vertexes", fileName, MDL_MAX_EXPANDED);
					Mem_Free(head);
					return false;
				}
				vertXyz.Append(xi);
				variant.Append(var);
				next.Append(head[xi]);
				head[xi] = e;
			}
			corner[c] = e;
		}

		// drop degenerate triangles
		if ( corner[0] == corner[1] || corner[0] == corner[2] || corner[1] == corner[2] ) {
			continue;
		}
#if MDL_FLIP_WINDING
		indexes.Append(corner[0]);
		indexes.Append(corner[2]);
		indexes.Append(corner[1]);
#else
		indexes.Append(corner[0]);
		indexes.Append(corner[1]);
		indexes.Append(corner[2]);
#endif
	}
	Mem_Free(head);

	if ( indexes.Num() == 0 ) {
		common->Warning("InitFromFile: %s has only degenerate triangles", fileName);
		return false;
	}

	// texture coordinates, frame invariant
	vertSt.SetNum(vertXyz.Num() * 2);
	for ( int i = 0; i < vertXyz.Num(); i++ ) {
		float s = (float)stData[vertXyz[i] * 3 + 1];
		const float tc = (float)stData[vertXyz[i] * 3 + 2];
		if ( variant[i] ) {
			s += skinWidth * 0.5f;
		}
		vertSt[i * 2 + 0] = (s + 0.5f) / (float)skinWidth;
		vertSt[i * 2 + 1] = (tc + 0.5f) / (float)skinHeight;
	}

	return true;
}

/*
=================
idRenderModelMDL::InitFromFile
=================
*/
void idRenderModelMDL::InitFromFile( const char* fileName ) {
	void* buffer = NULL;
	int					size;
	idList<int>			stData, triData, indexes;
	idList<byte>		skinData;
	int					skinWidth = 1, skinHeight = 1, numSkinsLoaded = 0;

	// make reloading safe
	FreeData();

	name = fileName;

	size = fileSystem->ReadFile(fileName, &buffer, NULL);
	if ( size <= (int)sizeof(mdlHeader_t) ) {
		if ( buffer != NULL ) {
			fileSystem->FreeFile(buffer);
		}
		// must make it a default model so failure can be detected elsewhere
		MakeDefaultModel();
		return;
	}

	const bool good = Parse((const byte*)buffer, size, fileName, stData, triData, skinWidth, skinHeight, skinData, numSkinsLoaded);
	fileSystem->FreeFile(buffer);

	if ( !good || !BuildMesh(fileName, stData, triData, skinWidth, skinHeight, indexes) ) {
		// the model is a default model now
		FreeData();
		MakeDefaultModel();
		return;
	}
	dataSize = framePos.Num() + vertXyz.Num() * (sizeof(int) + 2 * sizeof(float));

	// MDL files have no skin names, so the material is named after the model itself
	// without the extension, e.g. "progs/soldier.mdl" -> "progs/soldier".  An authored
	// material of that name is used if there is one, otherwise the embedded skin is
	// turned into a material.
	idStr matName = fileName;
	matName.StripFileExtension();
	material = BuildMaterial(matName.c_str(), skinWidth, skinHeight, skinData, numSkinsLoaded);

	// per frame bounds
	idList<idVec3> positions;
	positions.SetNum(numXyz);
	frameBounds.SetNum(numFrames);
	for ( int f = 0; f < numFrames; f++ ) {
		DecodeFrame(positions.Ptr(), f);
		frameBounds[f].Clear();
		for ( int i = 0; i < numXyz; i++ ) {
			frameBounds[f].AddPoint(positions[i]);
		}
	}

	silInfo = BuildSilInfo(indexes);

	// If this is only one frame generate the static model information to fix
	// loading issues and allow physics generation (same as the MD3 loader)
	if ( numFrames == 1 ) {
		CreateSurfaces();
	}

	// idRenderModelStatic::InitFromFile() does this, without it IsLoaded()
	// stays false after the first purge and the model gets reloaded on every lookup
	purged = false;
}

/*
=================
idRenderModelMDL::IsDynamicModel
=================
*/
dynamicModel_t idRenderModelMDL::IsDynamicModel() const {
	// no frames if loading failed and this is a default model now
	if ( numFrames <= 1 ) {
		return DM_STATIC;
	}
	return DM_CACHED;
}

/*
=================
idRenderModelMDL::LerpMeshVertexes

Appends the split vertexes for frame / oldframe to tri->verts
=================
*/
void idRenderModelMDL::LerpMeshVertexes( srfTriangles_t* tri, const float backlerp, const int frame, const int oldframe ) const {
	const int numSplit = vertXyz.Num();
	const byte* newVerts = framePos.Ptr() + frame * numXyz * 3;

	idDrawVert* out = tri->verts + tri->numVerts;

	if ( backlerp == 0 ) {
		//
		// just copy the vertexes
		//
		for ( int v = 0; v < numSplit; v++, out++ ) {
			const byte* nv = newVerts + vertXyz[v] * 3;
			out->xyz.x = nv[0] * scale[0] + translate[0];
			out->xyz.y = nv[1] * scale[1] + translate[1];
			out->xyz.z = nv[2] * scale[2] + translate[2];
		}
	} else {
		//
		// interpolate and copy the vertexes
		//
		const byte* oldVerts = framePos.Ptr() + oldframe * numXyz * 3;
		const float frontlerp = 1.0f - backlerp;

		for  ( int v = 0; v < numSplit; v++, out++ ) {
			const byte* nv = newVerts + vertXyz[v] * 3;
			const byte* ov = oldVerts + vertXyz[v] * 3;
			for ( int k = 0; k < 3; k++ ) {
				// scale and translate are shared by all frames, so lerp the bytes
				out->xyz[k] = (ov[k] * backlerp + nv[k] * frontlerp) * scale[k] + translate[k];
			}
		}
	}

	tri->numVerts += numSplit;
}

/*
=================
idRenderModelMDL::FillSurface

Fills an allocated (or reused) surface with the geometry for one frame.
Shared by CreateSurfaces() and InstantiateDynamicModel().
tri->verts must already be allocated for vertXyz.Num() verts.
=================
*/
void idRenderModelMDL::FillSurface( srfTriangles_t* tri, float backlerp, int frame, int oldframe ) const {
	// These flags and references must be (re)set after allocation or reuse.
	// deformedSurface prevents silEdges, silIndexes, indexes (and more not used here)
	// from being freed (they belong to silInfo which is freed in FreeData())
	tri->deformedSurface = true;
	tri->tangentsCalculated = false;
	tri->facePlanesCalculated = false;
	tri->numSilEdges = silInfo.numSilEdges;
	tri->silEdges = silInfo.silEdges;
	tri->silIndexes = silInfo.silIndexes;
	tri->indexes = silInfo.indexes;
	tri->numIndexes = silInfo.numIndexes;

	// LerpMeshVertexes appends at tri->numVerts, so reset the logical
	// vertex count while keeping the allocated vertex buffer.
	tri->numVerts = 0;
	tri->bounds.Clear();

	LerpMeshVertexes(tri, backlerp, frame, oldframe);

	// Texture coordinates are frame-invariant.  Reassigning them is cheap
	// and also initialises newly allocated vertex buffers.
	for ( int j = 0; j < vertXyz.Num(); j++ ) {
		idDrawVert* stri = &tri->verts[j];
		stri->st[0] = vertSt[j * 2 + 0];
		stri->st[1] = vertSt[j * 2 + 1];
	}

	R_BoundTriSurf(tri);
}

/*
=============
idRenderModelMDL::CreateSurfaces

Single frame models get real static surfaces (needed for physics / collision
model generation).
=============
*/
void idRenderModelMDL::CreateSurfaces() {
	this->bounds.Clear();

	srfTriangles_t* tri = R_AllocStaticTriSurf();
	R_AllocStaticTriSurfVerts(tri, vertXyz.Num());

	FillSurface(tri, 0.0f, 0, 0);

	modelSurface_t surf;
	surf.geometry = tri;
	surf.shader = material;
	surf.id = this->NumSurfaces();
	this->AddSurface(surf);
	this->bounds.AddPoint(tri->bounds[0]);
	this->bounds.AddPoint(tri->bounds[1]);
}

/*
=============
idRenderModelMDL::InstantiateDynamicModel

The per-entity snapshot (and the vertex allocation inside it) is kept between
calls and only the animated vertex positions are rewritten.  The topology
(indexes, sil edges) is shared with silInfo and never touched.
Frame, last frame and backlerp come from the same shaderParms as the MD3 loader.
Frame numbers count flattened frames, frame groups are expanded in file order.
=============
*/
idRenderModel* idRenderModelMDL::InstantiateDynamicModel( const struct renderEntity_s* ent, const struct viewDef_s* view, idRenderModel* cachedModel ) {
	int				surfaceNum;
	int				frame, oldframe;
	float			backlerp;
	idRenderModelStatic* staticModel;

	if ( numFrames < 1 ) {
		// failed to load, we are a default model and shouldn't get here
		if (cachedModel) {
			delete cachedModel;
		}
		return NULL;
	}

	frame = idMath::ClampInt(0, numFrames - 1, ent->shaderParms[SHADERPARM_MD3_FRAME]);
	oldframe = idMath::ClampInt(0, numFrames - 1, ent->shaderParms[SHADERPARM_MD3_LASTFRAME]);
	backlerp = idMath::ClampFloat(0.0f, 1.0f, ent->shaderParms[SHADERPARM_MD3_BACKLERP]);

	if ( cachedModel && !r_useCachedDynamicModels.GetBool() ) {
		delete cachedModel;
		cachedModel = NULL;
	}

	// Do not accept snapshots generated by another model type
	if ( cachedModel && idStr::Icmp(cachedModel->Name(), "_MDL_Snapshot_") != 0 ) {
		delete cachedModel;
		cachedModel = NULL;
	}

	if ( cachedModel ) {
		assert(dynamic_cast<idRenderModelStatic*>(cachedModel) != NULL);
		staticModel = static_cast<idRenderModelStatic*>(cachedModel);
	} else {
		staticModel = new idRenderModelStatic;
		staticModel->InitEmpty("_MDL_Snapshot_");
	}

	staticModel->bounds.Clear();

	// Surface IDs are stable for the lifetime of this model.  Keep the
	// modelSurface_t and its vertex allocation in the per-entity snapshot.
	modelSurface_t* surf;
	if ( staticModel->FindSurfaceWithId(0, surfaceNum) ) {
		surf = &staticModel->surfaces[surfaceNum];
	} else {
		modelSurface_t newSurf;
		memset(&newSurf, 0, sizeof(newSurf));
		newSurf.id = 0;
		staticModel->AddSurface(newSurf);
		if ( !staticModel->FindSurfaceWithId(0, surfaceNum) ) {
			common->Error("MDL snapshot: failed to find newly-added surface");
		}
		surf = &staticModel->surfaces[surfaceNum];
	}

	// The vertex and index counts determine whether the existing triangle
	// allocation can be reused.  The topology arrays are owned by this
	// model's silInfo, not by the snapshot.
	srfTriangles_t* tri = surf->geometry;
	if ( tri && (tri->numVerts != vertXyz.Num() || tri->numIndexes != silInfo.numIndexes) ) {
		R_FreeStaticTriSurf(tri);
		surf->geometry = NULL;
		tri = NULL;
	}
	if ( !tri ) {
		tri = R_AllocStaticTriSurf();
		R_AllocStaticTriSurfVerts(tri, vertXyz.Num());
		surf->geometry = tri;
	} else {
		// Vertex positions are about to change.  Drop any cached GPU
		// copy (ambient, shadow and index caches) while retaining the
		// CPU-side vertex allocation.
		R_FreeStaticTriSurfVertexCaches(tri);
	}

	surf->shader = material;

	FillSurface(tri, backlerp, frame, oldframe);

	staticModel->bounds.AddPoint(tri->bounds[0]);
	staticModel->bounds.AddPoint(tri->bounds[1]);

	return staticModel;
}

/*
=====================
idRenderModelMDL::Bounds
=====================
*/
idBounds idRenderModelMDL::Bounds( const struct renderEntity_s* ent ) const {
	idBounds		ret;

	ret.Clear();

	if ( !ent || numFrames < 1 || frameBounds.Num() == 0 ) {
		// just give it the editor bounds
		ret.AddPoint(idVec3(-10, -10, -10));
		ret.AddPoint(idVec3(10, 10, 10));
		return ret;
	}

	const int frameNum = idMath::ClampInt(0, numFrames - 1, ent->shaderParms[SHADERPARM_MD3_FRAME]);
	const int oldFrameNum = idMath::ClampInt(0, numFrames - 1, ent->shaderParms[SHADERPARM_MD3_LASTFRAME]);

	ret = frameBounds[frameNum];

	// while interpolating the mesh is a blend of both frames
	if ( oldFrameNum != frameNum && ent->shaderParms[SHADERPARM_MD3_BACKLERP] > 0.0f ) {
		ret.AddBounds(frameBounds[oldFrameNum]);
	}

	return ret;
}

/*
=====================
idRenderModelMDL::BuildSilInfo

Builds the silhouette edge connectivity used for stencil shadows.  The result
owns its own copy of the index list.
=====================
*/
silInfo_t idRenderModelMDL::BuildSilInfo( const idList<int>& indexes ) {
	const int numSplit = vertXyz.Num();
	silInfo_t ret = { 0 };

	// positions of frame 0, expanded to the split vertexes.  Only .xyz is used by
	// R_CreateSilIndexes() and R_IdentifySilEdges().
	idList<idVec3> positions;
	positions.SetNum(numXyz);
	DecodeFrame(positions.Ptr(), 0);

	bool onStack;
	idDrawVert* verts = (idDrawVert*)Mem_MallocA(numSplit * sizeof(idDrawVert), onStack);
	for ( int i = 0; i < numSplit; ++i ) {
		verts[i].Clear();
		verts[i].xyz = positions[vertXyz[i]];
	}

	srfTriangles_t tri;
	memset(&tri, 0, sizeof(tri));

	tri.verts = verts;
	tri.numVerts = numSplit;
	tri.numIndexes = indexes.Num();
	R_AllocStaticTriSurfIndexes(&tri, tri.numIndexes);
	for ( int i = 0; i < tri.numIndexes; i++ ) {
		tri.indexes[i] = indexes[i];
	}

	R_CreateSilIndexes(&tri);	// allocates and sets tri.silIndexes
	// if there's only one frame (no animation), the omitCoplanarEdges optimization can be used
	// (otherwise it can't because edges that are coplanar in frame 0 may not be in another frame)
	R_IdentifySilEdges(&tri, numFrames == 1);	// allocates and sets tri.silEdges

	ret.numIndexes = tri.numIndexes;
	ret.numSilEdges = tri.numSilEdges;
	ret.indexes = tri.indexes;
	ret.silIndexes = tri.silIndexes;
	ret.silEdges = tri.silEdges;

	Mem_FreeA(verts, onStack);

	return ret;
}