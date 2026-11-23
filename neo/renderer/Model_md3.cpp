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
		for (j = 0; j < surf->numTriangles; j++, tri++) {
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
R_TAN_DecodeFrame0

Decodes the (first frame) vertex positions of a surface.
Positions are value * scale + offset, using the model's first frame.
=================
*/
static void R_TAN_DecodeFrame0( idDrawVert* out, const tanSurface_t* surf, const tanFrame_t* frame0) {
	const tanXyzNormal_t* xyz = (const tanXyzNormal_t*)((const byte*)surf + surf->ofsXyzNormals );

	for ( int i = 0; i < surf->numVerts; i++, xyz++ ) {
		out[i].xyz.x = xyz->xyz[0] * frame0->scale[0] + frame0->offset[0];
		out[i].xyz.y = xyz->xyz[1] * frame0->scale[1] + frame0->offset[1];
		out[i].xyz.z = xyz->xyz[2] * frame0->scale[2] + frame0->offset[2];
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
				if ( token.Icmp("path") == 0 ) {
					idToken pathToken;

					if ( !lexer.ReadTokenOnLine(&pathToken) )
					{
						common->Warning(
							"Missing path value in TIKI file '%s'",
							fileName
						);
						continue;
					}

					tikiPath = pathToken.c_str();

					// Remove any trailing slash so AppendPath()
					// can join the directory and filename cleanly.
					while ( tikiPath.Length() > 0 )
					{
						char lastChar =
							tikiPath[tikiPath.Length() - 1];

						if ( lastChar != '/' && lastChar != '\\' ) {
							break;
						}

						tikiPath.StripTrailing('/');
						tikiPath.StripTrailing('\\');
					}

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

	// tag pose of the first frame
	SetupTagPose();

	CreateSurfaces();

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
	return DM_STATIC;
}

/*
====================
idRenderModelTAN::GetDefaultPose
====================
*/
const idJointQuat* idRenderModelTAN::GetDefaultPose(void) const {
	return defaultPose.Ptr();
}

/*
====================
idRenderModelTAN::SetupTagPose

Stores the first frame tag positions as joint quats so the game side can read them
====================
*/
void idRenderModelTAN::SetupTagPose() {
	if ( tan == NULL || tan->numTags <= 0 ) {
		return;
	}
	defaultPose.SetGranularity(1);
	defaultPose.SetNum(tan->numTags);

	for ( int i = 0; i < tan->numTags; i++ ) {
		const tanTagData_t* data = (const tanTagData_t*)((const byte*)tan + tan->ofsTags[i] + sizeof(tanTagName_t));	// frame 0
		idJointQuat& pose = defaultPose[i];

		pose.t.Set(data->origin[0], data->origin[1], data->origin[2]);
		pose.q = idMat3(
			idVec3(data->axis[0][0], data->axis[0][1], data->axis[0][2]),
			idVec3(data->axis[1][0], data->axis[1][1], data->axis[1][2]),
			idVec3(data->axis[2][0], data->axis[2][1], data->axis[2][2])).ToQuat();
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
		R_TAN_DecodeFrame0(tri->verts, surface, frame0);

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
	R_TAN_DecodeFrame0(verts, surf, (const tanFrame_t*)((const byte*)tan + tan->ofsFrames));

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
	R_IdentifySilEdges(&tri, true);	// allocates and sets tri.silEdges

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
