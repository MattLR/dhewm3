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

	idRenderModelMDC

***********************************************************************/

#define	LL(x) x=LittleInt(x)

/*
=================
R_MDC_RangeOK

Checks that count elements of elemSize bytes starting at ofs fit inside limit.
64 bit maths so hostile counts can't overflow.
=================
*/
static bool R_MDC_RangeOK( int ofs, long long count, int elemSize, int limit ) {
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
R_MDC_SwapAndValidate

Byte swaps the whole file in place and validates every offset, count and
index before anything dereferences them.  Nothing in the MD3 loader checks
any of this, so a truncated or hostile file reads out of bounds there.

Returns false (after printing a warning) if the file can't be used.
=================
*/
static bool R_MDC_SwapAndValidate( mdcHeader_t *mdc, int size, const char *fileName ) {
	int i, j;

	LL(mdc->ident);
	LL(mdc->version);
	LL(mdc->flags);
	LL(mdc->numFrames);
	LL(mdc->numTags);
	LL(mdc->numSurfaces);
	LL(mdc->numSkins);
	LL(mdc->ofsFrames);
	LL(mdc->ofsTagNames);	// was never swapped
	LL(mdc->ofsTags);
	LL(mdc->ofsSurfaces);
	LL(mdc->ofsEnd);
	mdc->name[MAX_MDCPATH-1] = 0;

	if ( mdc->ident != MDC_IDENT ) {
		common->Warning( "InitFromFile: %s is not an MDC file (bad ident)", fileName );
		return false;
	}
	if ( mdc->version != MDC_VERSION ) {
		common->Warning( "InitFromFile: %s has wrong version (%i should be %i)", fileName, mdc->version, MDC_VERSION );
		return false;
	}
	if ( mdc->ofsEnd != size ) {
		common->Warning( "InitFromFile: %s has a bad ofsEnd", fileName );
		return false;
	}
	if ( mdc->numFrames < 1 ) {
		common->Warning( "InitFromFile: %s has no frames", fileName );
		return false;
	}
	if ( mdc->numTags < 0 || mdc->numSurfaces < 0 ) {
		common->Warning( "InitFromFile: %s has negative tag or surface counts", fileName );
		return false;
	}
	if ( !R_MDC_RangeOK( mdc->ofsFrames, mdc->numFrames, sizeof( mdcFrame_t ), size ) ||
		 !R_MDC_RangeOK( mdc->ofsTagNames, mdc->numTags, sizeof( mdcTagName_t ), size ) ||
		 !R_MDC_RangeOK( mdc->ofsTags, (long long)mdc->numTags * mdc->numFrames, sizeof( mdcTag_t ), size ) ) {
		common->Warning( "InitFromFile: %s has frame or tag data outside the file", fileName );
		return false;
	}

	// swap all the frames
	mdcFrame_t *frame = (mdcFrame_t *)( (byte *)mdc + mdc->ofsFrames );
	for ( i = 0; i < mdc->numFrames; i++, frame++ ) {
		frame->radius = LittleFloat( frame->radius );
		for ( j = 0; j < 3; j++ ) {
			frame->bounds[0][j] = LittleFloat( frame->bounds[0][j] );
			frame->bounds[1][j] = LittleFloat( frame->bounds[1][j] );
			frame->localOrigin[j] = LittleFloat( frame->localOrigin[j] );
		}
	}

	// tag names are plain chars, just make sure they terminate
	mdcTagName_t *tagName = (mdcTagName_t *)( (byte *)mdc + mdc->ofsTagNames );
	for ( i = 0; i < mdc->numTags; i++, tagName++ ) {
		tagName->name[MAX_MDCPATH-1] = 0;
	}

	// swap all the tags
	mdcTag_t *tag = (mdcTag_t *)( (byte *)mdc + mdc->ofsTags );
	for ( i = 0; i < mdc->numTags * mdc->numFrames; i++, tag++ ) {
		for ( j = 0; j < 3; j++ ) {
			tag->xyz[j] = LittleShort( tag->xyz[j] );
			tag->angles[j] = LittleShort( tag->angles[j] );
		}
	}

	// swap and validate all the surfaces
	int surfOfs = mdc->ofsSurfaces;
	for ( i = 0; i < mdc->numSurfaces; i++ ) {
		if ( !R_MDC_RangeOK( surfOfs, 1, sizeof( mdcSurface_t ), size ) ) {
			common->Warning( "InitFromFile: %s has surface %i outside the file", fileName, i );
			return false;
		}

		mdcSurface_t *surf = (mdcSurface_t *)( (byte *)mdc + surfOfs );

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
		surf->name[MAX_MDCPATH-1] = 0;

		if ( surf->ofsEnd < (int)sizeof( mdcSurface_t ) || !R_MDC_RangeOK( surfOfs, surf->ofsEnd, 1, size ) ) {
			common->Warning( "InitFromFile: %s has a bad ofsEnd in surface %i", fileName, i );
			return false;
		}
		if ( surf->numVerts < 0 || surf->numVerts > MDC_MAX_VERTS ) {
			common->Warning( "InitFromFile: %s has more than %i verts on a surface (%i)", fileName, MDC_MAX_VERTS, surf->numVerts );
			return false;
		}
		if ( surf->numTriangles < 0 || surf->numTriangles > MDC_MAX_TRIANGLES ) {
			common->Warning( "InitFromFile: %s has more than %i triangles on a surface (%i)", fileName, MDC_MAX_TRIANGLES, surf->numTriangles );
			return false;
		}
		if ( surf->numShaders < 0 || surf->numShaders > MDC_MAX_SHADERS ) {
			common->Warning( "InitFromFile: %s has a bad shader count on surface %i (%i)", fileName, i, surf->numShaders );
			return false;
		}
		if ( surf->numBaseFrames < 1 || surf->numCompFrames < 0 ) {
			common->Warning( "InitFromFile: %s has bad base/comp frame counts on surface %i", fileName, i );
			return false;
		}

		const int end = surf->ofsEnd;
		const long long nv = surf->numVerts;
		if ( !R_MDC_RangeOK( surf->ofsShaders, surf->numShaders, sizeof( mdcShader_t ), end ) ||
			 !R_MDC_RangeOK( surf->ofsTriangles, surf->numTriangles, sizeof( mdcTriangle_t ), end ) ||
			 !R_MDC_RangeOK( surf->ofsSt, nv, sizeof( mdcSt_t ), end ) ||
			 !R_MDC_RangeOK( surf->ofsXyzNormals, nv * surf->numBaseFrames, sizeof( mdcXyzNormal_t ), end ) ||
			 !R_MDC_RangeOK( surf->ofsXyzCompressed, nv * surf->numCompFrames, sizeof( mdcXyzCompressed_t ), end ) ||
			 !R_MDC_RangeOK( surf->ofsFrameBaseFrames, mdc->numFrames, sizeof( short ), end ) ||
			 !R_MDC_RangeOK( surf->ofsFrameCompFrames, mdc->numFrames, sizeof( short ), end ) ) {
			common->Warning( "InitFromFile: %s has surface %i data outside the surface", fileName, i );
			return false;
		}

		// change to surface identifier
		surf->ident = 0;	//SF_MDC;

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
		mdcShader_t *shader = (mdcShader_t *)( (byte *)surf + surf->ofsShaders );
		for ( j = 0; j < surf->numShaders; j++, shader++ ) {
			shader->name[MAX_MDCPATH-1] = 0;
		}

		// swap and range check the triangles
		mdcTriangle_t *tri = (mdcTriangle_t *)( (byte *)surf + surf->ofsTriangles );
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
		mdcSt_t *st = (mdcSt_t *)( (byte *)surf + surf->ofsSt );
		for ( j = 0; j < surf->numVerts; j++, st++ ) {
			st->st[0] = LittleFloat( st->st[0] );
			st->st[1] = LittleFloat( st->st[1] );
		}

		// swap all the base frame XyzNormals
		mdcXyzNormal_t *xyz = (mdcXyzNormal_t *)( (byte *)surf + surf->ofsXyzNormals );
		for ( j = 0; j < surf->numVerts * surf->numBaseFrames; j++, xyz++ ) {
			xyz->xyz[0] = LittleShort( xyz->xyz[0] );
			xyz->xyz[1] = LittleShort( xyz->xyz[1] );
			xyz->xyz[2] = LittleShort( xyz->xyz[2] );
			xyz->normal = LittleShort( xyz->normal );
		}

		// swap all the XyzCompressed
		mdcXyzCompressed_t *xyzComp = (mdcXyzCompressed_t *)( (byte *)surf + surf->ofsXyzCompressed );
		for ( j = 0; j < surf->numVerts * surf->numCompFrames; j++, xyzComp++ ) {
			LL(xyzComp->ofsVec);
		}

		// swap and range check the per-frame base frame / comp frame indexes
		short *ps = (short *)( (byte *)surf + surf->ofsFrameBaseFrames );
		for ( j = 0; j < mdc->numFrames; j++, ps++ ) {
			*ps = LittleShort( *ps );
			if ( *ps < 0 || *ps >= surf->numBaseFrames ) {
				common->Warning( "InitFromFile: %s has a bad base frame index in surface %i", fileName, i );
				return false;
			}
		}
		ps = (short *)( (byte *)surf + surf->ofsFrameCompFrames );
		for ( j = 0; j < mdc->numFrames; j++, ps++ ) {
			*ps = LittleShort( *ps );
			if ( *ps >= surf->numCompFrames ) {	// -1 means "this frame is a base frame"
				common->Warning( "InitFromFile: %s has a bad comp frame index in surface %i", fileName, i );
				return false;
			}
		}

		surfOfs += surf->ofsEnd;
	}

	return true;
}

/*
=================
R_MDC_TagQuat

Decodes the compressed tag angles (pitch, yaw, roll) into a quaternion
=================
*/
static idQuat R_MDC_TagQuat( const mdcTag_t *tag ) {
	return idAngles(
		tag->angles[0] * (float)MDC_TAG_ANGLE_SCALE,
		tag->angles[1] * (float)MDC_TAG_ANGLE_SCALE,
		tag->angles[2] * (float)MDC_TAG_ANGLE_SCALE ).ToQuat();
}

// DG: added constructor to make sure all members are initialized
idRenderModelMDC::idRenderModelMDC() : index(-1), dataSize(0), mdc(NULL), numLods(0)
{}

// DG: added destructor to clean up the newly added silInfos (and MDC which used to leak)
idRenderModelMDC::~idRenderModelMDC() {
	FreeData();
}

/*
=================
idRenderModelMDC::FreeData

Releases everything InitFromFile() allocated.  Called from the destructor and
at the start of InitFromFile(), so reloading a model after a purge doesn't
leak the old file data, append a second copy of the silInfos or double count
dataSize.
=================
*/
void idRenderModelMDC::FreeData() {
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

		if ( sizeof(mdcTriangle_t::indexes[0]) != sizeof(silInfo.indexes[0]) ) {
			tri.indexes = silInfo.indexes;
			R_FreeStaticTriSurfIndexes( &tri );
		} // else silInfos[i].indexes points directly into the corresponding data in this->mdc
	}
	silInfos.Clear();
	shaders.Clear();
	defaultPose.Clear();

	if ( mdc != NULL ) {
		Mem_Free( mdc );
		mdc = NULL;
	}
	dataSize = 0;
}

/*
=================
idRenderModelMDC::InitFromFile
=================
*/
void idRenderModelMDC::InitFromFile( const char *fileName ) {
	int					i, j;
	mdcSurface_t		*surf;
	mdcShader_t			*shader;
	void				*buffer = NULL;
	int					size;
	int					ofsEnd;

	// make reloading safe
	FreeData();

	name = fileName;

	size = fileSystem->ReadFile( fileName, &buffer, NULL );
	// DG: without the int cast, an unsigned comparison is made which fails for size = -1
	if ( size <= (int)sizeof(mdcHeader_t) ) {
		if ( buffer != NULL ) {
			fileSystem->FreeFile( buffer );
		}
		// DG: must make it a default model so failure can be detected elsewhere
		MakeDefaultModel();
		return;
	}

	// the header is validated properly in R_MDC_SwapAndValidate(), this only has
	// to be good enough to size the copy without reading past the end of the file
	ofsEnd = LittleInt( ((mdcHeader_t *)buffer)->ofsEnd );
	if ( ofsEnd < (int)sizeof(mdcHeader_t) || ofsEnd > size ) {
		common->Warning( "InitFromFile: %s has a bad ofsEnd (%i, file is %i bytes)", fileName, ofsEnd, size );
		fileSystem->FreeFile( buffer );
		MakeDefaultModel();
		return;
	}

	mdc = (mdcHeader_t *)Mem_Alloc( ofsEnd );
	memcpy( mdc, buffer, ofsEnd );
	fileSystem->FreeFile( buffer );
	dataSize = ofsEnd;

	if ( !R_MDC_SwapAndValidate( mdc, ofsEnd, fileName ) ) {
		// don't keep a half swapped file around, the model is a default model now
		FreeData();
		MakeDefaultModel();
		return;
	}

	// register the shaders and build the shadow info for every surface
	surf = (mdcSurface_t *)( (byte *)mdc + mdc->ofsSurfaces );
	for ( i = 0; i < mdc->numSurfaces; i++ ) {

		shader = (mdcShader_t *)( (byte *)surf + surf->ofsShaders );
		for ( j = 0; j < surf->numShaders; j++, shader++ ) {

			// Dynamix - hack for Raven shaders: the first character of the path
			// can be garbage, the real paths all start with "models/"
			if ( memcmp( shader->name + 1, "odel", 4 ) == 0 ) {
				shader->name[0] = 'm';
			}

			const idMaterial *sh = declManager->FindMaterial( shader->name );
			// DG: mdcShader_t must use an index to the material instead of a pointer,
			//     otherwise the sizes are wrong on 64bit and we get data corruption
			shader->shaderIndex = ( sh != NULL ) ? shaders.AddUnique( sh ) : -1;
		}

		silInfos.Append( BuildSilInfo( surf ) );

		// find the next surface
		surf = (mdcSurface_t *)( (byte *)surf + surf->ofsEnd );
	}

	// initial tag pose, so GetDefaultPose() is never uninitialised garbage
	defaultPose.SetGranularity( 1 );
	defaultPose.SetNum( mdc->numTags );
	UpdateTagPose( 0, 0, 0.0f );

	// If this is only one frame generate the static model information to fix
	// loading issues and allow physics generation (same as the MD3 loader)
	if ( mdc->numFrames == 1 ) {
		CreateSurfaces();
	}

	// idRenderModelStatic::InitFromFile() does this, without it IsLoaded()
	// stays false after the first purge and the model gets reloaded on every lookup
	purged = false;
}

/*
=================
idRenderModelMDC::IsDynamicModel
=================
*/
dynamicModel_t idRenderModelMDC::IsDynamicModel() const {
	// mdc is NULL if loading failed and this is a default model now
	if ( mdc == NULL || mdc->numFrames == 1 ) {
		return DM_STATIC;
	}
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
====================
idRenderModelMDC::UpdateTagPose

Stores the tag positions as joint quats so the game side can read them
====================
*/
void idRenderModelMDC::UpdateTagPose( int frame, int oldframe, float backlerp ) {
	if ( mdc == NULL || mdc->numTags <= 0 ) {
		return;
	}
	if ( defaultPose.Num() != mdc->numTags ) {
		defaultPose.SetNum( mdc->numTags );
	}

	const mdcTag_t *tags = (const mdcTag_t *)( (const byte *)mdc + mdc->ofsTags );
	const mdcTag_t *tag = tags + frame * mdc->numTags;
	const mdcTag_t *oldTag = tags + oldframe * mdc->numTags;
	const float frontlerp = 1.0f - backlerp;
	idJointQuat *pose = defaultPose.Ptr();

	for ( int i = 0; i < mdc->numTags; i++, oldTag++, tag++, pose++ ) {
		idVec3 oldPos, newPos;

		oldPos.x = oldTag->xyz[0] * (float)MDC_XYZ_SCALE;
		oldPos.y = oldTag->xyz[1] * (float)MDC_XYZ_SCALE;
		oldPos.z = oldTag->xyz[2] * (float)MDC_XYZ_SCALE;

		newPos.x = tag->xyz[0] * (float)MDC_XYZ_SCALE;
		newPos.y = tag->xyz[1] * (float)MDC_XYZ_SCALE;
		newPos.z = tag->xyz[2] * (float)MDC_XYZ_SCALE;

		pose->t.Lerp( oldPos, newPos, frontlerp );
		pose->q.Slerp( R_MDC_TagQuat( oldTag ), R_MDC_TagQuat( tag ), frontlerp );
	}
}

/*
=================
idRenderModelMDC::LerpMeshVertexes

Base frames hold full 16 bit positions, compressed frames hold a packed
byte offset per vertex from the base frame they belong to.
=================
*/
void idRenderModelMDC::LerpMeshVertexes ( srfTriangles_t *tri, const struct mdcSurface_s *surf, const float backlerp, const int frame, const int oldframe ) const {
	const int numVerts = surf->numVerts;
	const short *frameBaseFrames = (const short *)( (const byte *)surf + surf->ofsFrameBaseFrames );
	const short *frameCompFrames = (const short *)( (const byte *)surf + surf->ofsFrameCompFrames );
	const short *xyzBase = (const short *)( (const byte *)surf + surf->ofsXyzNormals );
	const mdcXyzCompressed_t *compBase = (const mdcXyzCompressed_t *)( (const byte *)surf + surf->ofsXyzCompressed );
	const bool hasComp = ( surf->numCompFrames > 0 );

	const short *newXyz = xyzBase + frameBaseFrames[frame] * numVerts * 4;
	const mdcXyzCompressed_t *newComp = NULL;
	if ( hasComp && frameCompFrames[frame] >= 0 ) {
		newComp = compBase + frameCompFrames[frame] * numVerts;
	}

	idDrawVert *out = tri->verts + tri->numVerts;
	idVec3 ofs;

	if ( backlerp == 0 ) {
		//
		// just copy the vertexes
		//
		const float scale = (float)MDC_XYZ_SCALE;

		for ( int v = 0; v < numVerts; v++, newXyz += 4, out++ ) {
			out->xyz.x = newXyz[0] * scale;
			out->xyz.y = newXyz[1] * scale;
			out->xyz.z = newXyz[2] * scale;

			// add the compressed ofsVec
			if ( newComp != NULL ) {
				R_MDC_DecodeXyzCompressed( newComp[v].ofsVec, ofs );
				out->xyz += ofs;
			}
		}
	} else {
		//
		// interpolate and copy the vertexes
		//
		const short *oldXyz = xyzBase + frameBaseFrames[oldframe] * numVerts * 4;
		const mdcXyzCompressed_t *oldComp = NULL;
		if ( hasComp && frameCompFrames[oldframe] >= 0 ) {
			oldComp = compBase + frameCompFrames[oldframe] * numVerts;
		}

		const float frontlerp = 1.0f - backlerp;
		const float newXyzScale = (float)MDC_XYZ_SCALE * frontlerp;
		const float oldXyzScale = (float)MDC_XYZ_SCALE * backlerp;

		for ( int v = 0; v < numVerts; v++, oldXyz += 4, newXyz += 4, out++ ) {
			// interpolate the xyz
			out->xyz.x = oldXyz[0] * oldXyzScale + newXyz[0] * newXyzScale;
			out->xyz.y = oldXyz[1] * oldXyzScale + newXyz[1] * newXyzScale;
			out->xyz.z = oldXyz[2] * oldXyzScale + newXyz[2] * newXyzScale;

			// add the compressed ofsVec of both frames, weighted
			if ( newComp != NULL ) {
				R_MDC_DecodeXyzCompressed( newComp[v].ofsVec, ofs );
				out->xyz += ofs * frontlerp;
			}
			if ( oldComp != NULL ) {
				R_MDC_DecodeXyzCompressed( oldComp[v].ofsVec, ofs );
				out->xyz += ofs * backlerp;
			}
		}
	}

	tri->numVerts += numVerts;
}

/*
=================
idRenderModelMDC::GetSurfaceMaterial
=================
*/
const idMaterial *idRenderModelMDC::GetSurfaceMaterial( const mdcSurface_t *surface ) const {
	// FIXME: theoretically there can be multiple shaders?
	if ( surface->numShaders <= 0 ) {
		return NULL;
	}
	const mdcShader_t *shader = (const mdcShader_t *)( (const byte *)surface + surface->ofsShaders );
	// DG: turned mdcShader_t::shader (pointer) into an int (index)
	return ( shader->shaderIndex >= 0 ) ? shaders[shader->shaderIndex] : NULL;
}

/*
=================
idRenderModelMDC::FillSurface

Fills an allocated (or reused) surface with the geometry for one frame.
Shared by CreateSurfaces() and InstantiateDynamicModel().
tri->verts must already be allocated for surface->numVerts verts.
=================
*/
void idRenderModelMDC::FillSurface( srfTriangles_t *tri, const mdcSurface_t *surface, const silInfo_t &silInfo, float backlerp, int frame, int oldframe ) const {
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
idRenderModelMDC::CreateSurfaces

Single frame models get real static surfaces (needed for physics / collision
model generation), same as the MD3 loader.
=============
*/
void idRenderModelMDC::CreateSurfaces() {
	this->bounds.Clear();

	const mdcSurface_t *surface = (const mdcSurface_t *)( (const byte *)mdc + mdc->ofsSurfaces );

	for ( int i = 0; i < mdc->numSurfaces; i++ ) {
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
		surface = (const mdcSurface_t *)( (const byte *)surface + surface->ofsEnd );
	}
}

/*
=============
idRenderModelMDC::InstantiateDynamicModel

Same caching scheme as the MD3 loader: the per-entity snapshot (and the
vertex allocations inside it) is kept between calls and only the animated
vertex positions are rewritten.  The topology (indexes, sil edges) is shared
with silInfos and never touched.
=============
*/
idRenderModel *idRenderModelMDC::InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel ) {
	int				i;
	int				surfaceNum;
	mdcSurface_t *	surface;
	int				frame, oldframe;
	float			backlerp;
	idRenderModelStatic	*staticModel;

	if ( mdc == NULL ) {
		// failed to load, we are a default model and shouldn't get here
		if ( cachedModel ) {
			delete cachedModel;
		}
		return NULL;
	}

	const int numFrames = mdc->numFrames;

	frame = idMath::ClampInt( 0, numFrames - 1, ent->shaderParms[SHADERPARM_MD3_FRAME] );	// probably want to keep frames < 1000 or so
	oldframe = idMath::ClampInt( 0, numFrames - 1, ent->shaderParms[SHADERPARM_MD3_LASTFRAME] );
	backlerp = idMath::ClampFloat( 0.0f, 1.0f, ent->shaderParms[SHADERPARM_MD3_BACKLERP] );

	if ( cachedModel && !r_useCachedDynamicModels.GetBool() ) {
		delete cachedModel;
		cachedModel = NULL;
	}

	// Do not accept snapshots generated by another model type
	if ( cachedModel && idStr::Icmp( cachedModel->Name(), "_MDC_Snapshot_" ) != 0 ) {
		delete cachedModel;
		cachedModel = NULL;
	}

	if ( cachedModel ) {
		assert( dynamic_cast<idRenderModelStatic *>( cachedModel ) != NULL );
		staticModel = static_cast<idRenderModelStatic *>( cachedModel );
	} else {
		staticModel = new idRenderModelStatic;
		staticModel->InitEmpty( "_MDC_Snapshot_" );
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

	surface = (mdcSurface_t *)( (byte *)mdc + mdc->ofsSurfaces );

	for ( i = 0; i < mdc->numSurfaces; i++ ) {

		// Surface IDs are stable for the lifetime of this model.  Keep the
		// modelSurface_t and its vertex allocation in the per-entity snapshot.
		modelSurface_t *surf;
		if ( staticModel->FindSurfaceWithId( i, surfaceNum ) ) {
			surf = &staticModel->surfaces[surfaceNum];
		} else {
			modelSurface_t newSurf;
			memset( &newSurf, 0, sizeof( newSurf ) );
			newSurf.id = i;
			staticModel->AddSurface( newSurf );
			if ( !staticModel->FindSurfaceWithId( i, surfaceNum ) ) {
				common->Error( "MDC snapshot: failed to find newly-added surface %d", i );
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
			// copy (ambient, shadow and index caches) while retaining the
			// CPU-side vertex allocation.
			R_FreeStaticTriSurfVertexCaches( tri );
		}

		surf->shader = GetSurfaceMaterial( surface );

		FillSurface( tri, surface, silInfo, backlerp, frame, oldframe );

		staticModel->bounds.AddPoint( tri->bounds[0] );
		staticModel->bounds.AddPoint( tri->bounds[1] );

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
idBounds idRenderModelMDC::Bounds( const struct renderEntity_s *ent ) const {
	idBounds		ret;

	ret.Clear();

	if ( !ent || !mdc ) {
		// just give it the editor bounds
		ret.AddPoint( idVec3( -10, -10, -10 ) );
		ret.AddPoint( idVec3(  10,  10,  10 ) );
		return ret;
	}

	const mdcFrame_t *frames = (const mdcFrame_t *)( (const byte *)mdc + mdc->ofsFrames );
	const int frameNum = idMath::ClampInt( 0, mdc->numFrames - 1, ent->shaderParms[SHADERPARM_MD3_FRAME] );
	const int oldFrameNum = idMath::ClampInt( 0, mdc->numFrames - 1, ent->shaderParms[SHADERPARM_MD3_LASTFRAME] );

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
idRenderModelMDC::BuildSilInfo
=====================
*/
silInfo_t idRenderModelMDC::BuildSilInfo( mdcSurface_t *surf ) {
	//
	// build the information that will be common to all animations of this surface:
	// silhouette edge connectivity and normal / tangent generation information
	//
	int numVerts = surf->numVerts;
	silInfo_t ret = {0};

	bool onStack;
	idDrawVert *verts = (idDrawVert *)Mem_MallocA( numVerts * sizeof( idDrawVert ), onStack );

	int numIndexes = surf->numTriangles * 3;
	mdcTriangle_t *mdcTriIdx = (mdcTriangle_t *)( (byte *)surf + surf->ofsTriangles );
	int *triIndexes = (int *)mdcTriIdx->indexes;

	srfTriangles_t tri;
	memset( &tri, 0, sizeof( tri ) );

	for ( int i = 0; i < numVerts; ++i ) {
		verts[i].Clear();
	}
	tri.verts = verts;

	// Use frame 0 *including* its compressed offsets, so this is exactly what
	// the first animation frame looks like.  (Only .xyz is used by
	// R_CreateSilIndexes() and R_IdentifySilEdges().)
	tri.numVerts = 0;
	LerpMeshVertexes( &tri, surf, 0.0f, 0, 0 );

	tri.numIndexes = numIndexes;
	if ( sizeof( triIndexes[0] ) == sizeof( tri.indexes[0] ) ) {
		tri.indexes = triIndexes;
	} else {
		R_AllocStaticTriSurfIndexes( &tri, numIndexes );
		// don't memcpy, so we can change the index type from int to short without changing the interface
		for ( int i = 0; i < numIndexes; i++ ) {
			tri.indexes[i] = triIndexes[i];
		}
	}
	R_CreateSilIndexes( &tri ); // allocates and sets tri.silIndexes with triSilIndexAllocator
	// Only if the whole model has a single frame (no animation) can the
	// omitCoplanarEdges optimization be used, otherwise edges that are coplanar
	// in frame 0 may not be in another frame.  NOTE: this must test the model's
	// frame count, not surf->numBaseFrames: a surface with one base frame can
	// still be animated through its compressed frames.
	bool omitCoplanarEdges = ( mdc->numFrames == 1 );
	R_IdentifySilEdges( &tri, omitCoplanarEdges ); // allocates and sets tri.silEdges with triSilEdgeAllocator

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
void idRenderModelMDC::DrawTags( const renderEntity_t *ent, const struct viewDef_s *view ) const {
	if ( mdc == NULL || view == NULL ) {
		return;
	}

	// the frame must be clamped, shaderParms can hold anything
	const int currentFrame = idMath::ClampInt( 0, mdc->numFrames - 1, ent->shaderParms[SHADERPARM_MD3_FRAME] );
	const mdcTag_t *tag = (const mdcTag_t *)( (const byte *)mdc + mdc->ofsTags ) + currentFrame * mdc->numTags;
	const mdcTagName_t *tagName = (const mdcTagName_t *)( (const byte *)mdc + mdc->ofsTagNames );

	const idVec3 offset( 0, 0, r_jointNameOffset.GetFloat() );
	const float scale = r_jointNameScale.GetFloat();

	for ( int i = 0; i < mdc->numTags; i++, tag++, tagName++ ) {
		idVec3 origin;
		origin.x = tag->xyz[0] * (float)MDC_XYZ_SCALE;
		origin.y = tag->xyz[1] * (float)MDC_XYZ_SCALE;
		origin.z = tag->xyz[2] * (float)MDC_XYZ_SCALE;

		const idVec3 pos = ent->origin + origin * ent->axis;
		session->rw->DrawText( tagName->name, pos + offset, scale, colorWhite, view->renderView.viewaxis, 1 );
	}
}

/*
====================
idRenderModelMDC::GetJointHandle
====================
*/
jointHandle_t idRenderModelMDC::GetJointHandle( const char *name ) const {
	if ( mdc == NULL ) {
		return INVALID_JOINT;
	}

	const mdcTagName_t *tagName = (const mdcTagName_t *)( (const byte *)mdc + mdc->ofsTagNames );

	for ( int i = 0; i < mdc->numTags; i++, tagName++ ) {
		if ( idStr::Icmp( tagName->name, name ) == 0 ) {
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
const char *idRenderModelMDC::GetJointName( jointHandle_t handle ) const {
	if ( mdc == NULL || handle < 0 || handle >= mdc->numTags ) {
		return "<invalid joint>";
	}

	const mdcTagName_t *tagName = (const mdcTagName_t *)( (const byte *)mdc + mdc->ofsTagNames );
	return ( tagName + handle )->name;
}
