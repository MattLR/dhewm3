// Model_skb.cpp
//
// Static (default pose only) loader for .skb v4 skeletal models, see Model_skb.h.
//

#include "sys/platform.h"
#include "renderer/tr_local.h"
#include "renderer/Model_local.h"

#include "renderer/Model_skb.h"

int		LittleLong3(int l) { return l; }

// Catch a layout mistake at compile time rather than as garbage geometry.
static_assert( sizeof( skbHeader_t )	== 96, "skbHeader_t must match the on-disk size" );
static_assert( sizeof( skbBoneInfo_t )	== 72, "skbBoneInfo_t must match the on-disk size" );
static_assert( sizeof( skbBone_t )		== 16, "skbBone_t must match the on-disk size" );
static_assert( sizeof( skbSurface_t )	== 96, "skbSurface_t must match the on-disk size" );
static_assert( sizeof( skbTriangle_t )	== 12, "skbTriangle_t must match the on-disk size" );
static_assert( sizeof( skbVertex_t )	== 24, "skbVertex_t must match the on-disk size" );
static_assert( sizeof( skbWeight_t )	== 20, "skbWeight_t must match the on-disk size" );

// A bone in model space: v' = r * v + t
struct skbBoneMat_t {
	float	r[3][3];
	float	t[3];
};

/*
========================
SKB_InRange

True if [ofs, ofs + count * stride) lies inside a buffer of the given size.
Done in 64 bit so hostile counts can't wrap around.
========================
*/
static bool SKB_InRange( int ofs, int count, int stride, int size ) {
	if ( ofs < 0 || count < 0 ) {
		return false;
	}
	return (long long)ofs + (long long)count * (long long)stride <= (long long)size;
}

/*
========================
SKB_QuatToMat3

Standard rotation matrix of a quaternion (column vector convention, v' = m * v).
The quaternion is normalised first, the file only stores 16 bits per component.
========================
*/
static void SKB_QuatToMat3( float x, float y, float z, float w, float m[3][3] ) {
	float len = sqrtf( x * x + y * y + z * z + w * w );

	if ( len > 1e-6f ) {
		float inv = 1.0f / len;
		x *= inv;
		y *= inv;
		z *= inv;
		w *= inv;
	} else {
		x = y = z = 0.0f;
		w = 1.0f;
	}

	const float xx = x * x, yy = y * y, zz = z * z;
	const float xy = x * y, xz = x * z, yz = y * z;
	const float wx = w * x, wy = w * y, wz = w * z;

	m[0][0] = 1.0f - 2.0f * ( yy + zz );
	m[0][1] = 2.0f * ( xy - wz );
	m[0][2] = 2.0f * ( xz + wy );

	m[1][0] = 2.0f * ( xy + wz );
	m[1][1] = 1.0f - 2.0f * ( xx + zz );
	m[1][2] = 2.0f * ( yz - wx );

	m[2][0] = 2.0f * ( xz - wy );
	m[2][1] = 2.0f * ( yz + wx );
	m[2][2] = 1.0f - 2.0f * ( xx + yy );
}

/*
========================
idRenderModelSKB::idRenderModelSKB
========================
*/
idRenderModelSKB::idRenderModelSKB() {
}

/*
========================
idRenderModelSKB::InitFromFile
========================
*/
void idRenderModelSKB::InitFromFile( const char *fileName ) {
	name = fileName;
	LoadModel();
}

/*
========================
idRenderModelSKB::LoadModel
========================
*/
void idRenderModelSKB::LoadModel( void ) {
	void	*buffer = NULL;
	int		len;

	if ( !purged ) {
		PurgeModel();
	}
	purged = false;
	defaulted = false;

	len = fileSystem->ReadFile( name, &buffer, &timeStamp );
	if ( len <= 0 || buffer == NULL ) {
		common->Warning( "idRenderModelSKB: couldn't read %s", name.c_str() );
		MakeDefaultModel();
		return;
	}

	const bool loaded = LoadSKBFile( (const byte *)buffer, len );
	fileSystem->FreeFile( buffer );

	if ( !loaded ) {
		// MakeDefaultModel() throws away any surfaces we already added
		MakeDefaultModel();
		return;
	}

	// bounds, tangents, silhouettes and surface merging, same as the other static loaders
	FinishSurfaces();
}

/*
========================
idRenderModelSKB::FindSurfaceMaterial

.skb surfaces only carry a name ("material10"), the shader for it normally comes from
the .tiki. Until the tiki is hooked up this looks for a material called <surface name>
in the same directory as the model, same as the MDS loader does.
========================
*/
const idMaterial *idRenderModelSKB::FindSurfaceMaterial( const char *surfaceName ) const {
	idStr path;

	name.ExtractFilePath( path );		// includes the trailing '/'
	return declManager->FindMaterial( va( "%s%s", path.c_str(), surfaceName ), true );
}

/*
========================
idRenderModelSKB::LoadSKBFile

Returns false, with a warning, if the file is not a usable v4 skb. Every offset and count
read from the file is checked against the file size before it is used.
========================
*/
bool idRenderModelSKB::LoadSKBFile( const byte *buffer, int filesize ) {
	const char		*modName = name.c_str();
	skbHeader_t		header;
	skbBoneMat_t	bones[SKB_MAX_BONES];
	int				numBones;
	int				numAdded = 0;
	int				totalVerts = 0;
	int				totalTris = 0;
	int				i, j, k;

	if ( filesize < (int)sizeof( header ) ) {
		common->Warning( "idRenderModelSKB: %s is too small to be a skb", modName );
		return false;
	}

	memcpy( &header, buffer, sizeof( header ) );
	header.version			= LittleLong3( header.version );
	header.numSurfaces		= LittleLong3( header.numSurfaces );
	header.numBones			= LittleLong3( header.numBones );
	header.ofsBones			= LittleLong3( header.ofsBones );
	header.ofsSurfaces		= LittleLong3( header.ofsSurfaces );
	header.ofsBoneBaseFrame	= LittleLong3( header.ofsBoneBaseFrame );
	header.ofsEnd			= LittleLong3( header.ofsEnd );

	if ( memcmp( header.ident, "SKL ", 4 ) != 0 ) {
		common->Warning( "idRenderModelSKB: %s is not a skb file", modName );
		return false;
	}

	if ( header.version != SKB_VERSION ) {
		// version 3 has no base frame in the header, so there is no default pose to read
		common->Warning( "idRenderModelSKB: %s is version %i, only version %i has a default pose", modName, header.version, SKB_VERSION );
		return false;
	}

	if ( header.ofsEnd > filesize ) {
		common->Warning( "idRenderModelSKB: header of %s is broken, declared size %i but file is %i", modName, header.ofsEnd, filesize );
		return false;
	}

	numBones = header.numBones;
	if ( numBones < 1 || numBones > SKB_MAX_BONES ) {
		common->Warning( "idRenderModelSKB: %s has %i bones (limit is %i)", modName, numBones, SKB_MAX_BONES );
		return false;
	}

	if ( header.numSurfaces < 1 ) {
		common->Warning( "idRenderModelSKB: %s has no surfaces", modName );
		return false;
	}

	if ( !SKB_InRange( header.ofsBones, numBones, sizeof( skbBoneInfo_t ), filesize ) ||
		 !SKB_InRange( header.ofsBoneBaseFrame, numBones, sizeof( skbBone_t ), filesize ) ) {
		common->Warning( "idRenderModelSKB: %s has bone data outside the file", modName );
		return false;
	}

	// ---- default pose: turn the base frame into one model space matrix per bone ----

	for ( i = 0; i < numBones; i++ ) {
		skbBoneInfo_t	info;
		skbBone_t		base;
		float			lr[3][3];
		float			lt[3];
		int				parent;

		memcpy( &info, buffer + header.ofsBones + i * sizeof( info ), sizeof( info ) );
		memcpy( &base, buffer + header.ofsBoneBaseFrame + i * sizeof( base ), sizeof( base ) );

		// the loop below needs the parent's matrix, so parents have to come first
		// (they do in every file seen so far)
		parent = LittleLong3( info.parent );
		if ( parent < -1 || parent >= i ) {
			common->Warning( "idRenderModelSKB: %s bone %i has parent %i, which doesn't come before it", modName, i, parent );
			return false;
		}

		// The w component is stored with the opposite sign to the usual x,y,z,w quaternion
		// (equivalent to using the transpose of the rotation). This is what makes the
		// weighted vertex offsets of each bone agree with each other, checked against the
		// weapon and character models.
		SKB_QuatToMat3(
			LittleShort( base.quat[0] ) * SKB_QUAT_SCALE,
			LittleShort( base.quat[1] ) * SKB_QUAT_SCALE,
			LittleShort( base.quat[2] ) * SKB_QUAT_SCALE,
			-LittleShort( base.quat[3] ) * SKB_QUAT_SCALE,
			lr );

		lt[0] = LittleShort( base.offset[0] ) * SKB_OFFSET_SCALE;
		lt[1] = LittleShort( base.offset[1] ) * SKB_OFFSET_SCALE;
		lt[2] = LittleShort( base.offset[2] ) * SKB_OFFSET_SCALE;

		if ( parent < 0 ) {
			memcpy( bones[i].r, lr, sizeof( lr ) );
			memcpy( bones[i].t, lt, sizeof( lt ) );
		} else {
			const skbBoneMat_t &p = bones[parent];

			for ( j = 0; j < 3; j++ ) {
				for ( k = 0; k < 3; k++ ) {
					bones[i].r[j][k] = p.r[j][0] * lr[0][k] + p.r[j][1] * lr[1][k] + p.r[j][2] * lr[2][k];
				}
				bones[i].t[j] = p.r[j][0] * lt[0] + p.r[j][1] * lt[1] + p.r[j][2] * lt[2] + p.t[j];
			}
		}
	}

	// ---- surfaces ----

	int surfOfs = header.ofsSurfaces;

	for ( i = 0; i < header.numSurfaces; i++ ) {
		skbSurface_t	surf;
		char			surfName[SKB_NAME_LEN + 1];
		int				surfEnd, vertOfs, triOfs;

		if ( !SKB_InRange( surfOfs, 1, sizeof( surf ), header.ofsEnd ) ) {
			common->Warning( "idRenderModelSKB: %s surface %i header is outside the file", modName, i );
			return false;
		}

		memcpy( &surf, buffer + surfOfs, sizeof( surf ) );
		surf.numTriangles	= LittleLong3( surf.numTriangles );
		surf.numVerts		= LittleLong3( surf.numVerts );
		surf.ofsTriangles	= LittleLong3( surf.ofsTriangles );
		surf.ofsVerts		= LittleLong3( surf.ofsVerts );
		surf.ofsEnd			= LittleLong3( surf.ofsEnd );

		memcpy( surfName, surf.name, SKB_NAME_LEN );
		surfName[SKB_NAME_LEN] = '\0';

		if ( surf.ofsEnd < (int)sizeof( surf ) || !SKB_InRange( surfOfs, surf.ofsEnd, 1, header.ofsEnd ) ) {
			common->Warning( "idRenderModelSKB: %s surface %i (%s) has a bad size", modName, i, surfName );
			return false;
		}
		surfEnd = surfOfs + surf.ofsEnd;

		if ( surf.numVerts < 0 || surf.numVerts > SKB_MAX_VERTS || surf.numTriangles < 0 || surf.numTriangles > SKB_MAX_VERTS * 2 ) {
			common->Warning( "idRenderModelSKB: %s surface %i (%s) has %i verts and %i triangles", modName, i, surfName, surf.numVerts, surf.numTriangles );
			return false;
		}

		// nothing to draw, skip it but keep walking the chain
		if ( surf.numVerts == 0 || surf.numTriangles == 0 ) {
			surfOfs = surfEnd;
			continue;
		}

		// check the offsets relative to the surface first, so the additions below can't overflow
		if ( surf.ofsTriangles < (int)sizeof( surf ) || surf.ofsVerts < (int)sizeof( surf ) ||
			 !SKB_InRange( surf.ofsTriangles, surf.numTriangles, sizeof( skbTriangle_t ), surf.ofsEnd ) ||
			 surf.ofsVerts > surf.ofsEnd ) {
			common->Warning( "idRenderModelSKB: %s surface %i (%s) has data outside the surface", modName, i, surfName );
			return false;
		}
		triOfs = surfOfs + surf.ofsTriangles;
		vertOfs = surfOfs + surf.ofsVerts;

		srfTriangles_t *tri = R_AllocStaticTriSurf();
		R_AllocStaticTriSurfVerts( tri, surf.numVerts );
		R_AllocStaticTriSurfIndexes( tri, surf.numTriangles * 3 );
		tri->numVerts = surf.numVerts;
		tri->numIndexes = surf.numTriangles * 3;

		// the file has its own normals, don't let R_CleanupTriangles throw them away
		tri->generateNormals = false;

		// vertices are variable length (a header followed by numWeights weights), so walk them
		const byte *vp = buffer + vertOfs;
		const byte *vEnd = buffer + surfEnd;

		for ( j = 0; j < surf.numVerts; j++ ) {
			skbVertex_t	vert;
			float		pos[3] = { 0.0f, 0.0f, 0.0f };
			float		nor[3] = { 0.0f, 0.0f, 0.0f };

			if ( vEnd - vp < (int)sizeof( vert ) ) {
				common->Warning( "idRenderModelSKB: %s surface %i (%s) vertex %i is outside the surface", modName, i, surfName, j );
				R_FreeStaticTriSurf( tri );
				return false;
			}

			memcpy( &vert, vp, sizeof( vert ) );
			vp += sizeof( vert );

			const int numWeights = LittleLong3( vert.numWeights );
			if ( numWeights < 1 || numWeights > SKB_MAX_WEIGHTS || ( vEnd - vp ) / (int)sizeof( skbWeight_t ) < numWeights ) {
				common->Warning( "idRenderModelSKB: %s surface %i (%s) vertex %i has %i weights", modName, i, surfName, j, numWeights );
				R_FreeStaticTriSurf( tri );
				return false;
			}

			float n[3];
			for ( k = 0; k < 3; k++ ) {
				n[k] = LittleFloat( vert.normal[k] );
			}

			for ( k = 0; k < numWeights; k++ ) {
				skbWeight_t w;

				memcpy( &w, vp, sizeof( w ) );
				vp += sizeof( w );

				const int	boneIndex = LittleLong3( w.boneIndex );
				const float	weight = LittleFloat( w.boneWeight );
				const float	off[3] = { LittleFloat( w.offset[0] ), LittleFloat( w.offset[1] ), LittleFloat( w.offset[2] ) };

				if ( boneIndex < 0 || boneIndex >= numBones ) {
					common->Warning( "idRenderModelSKB: %s surface %i (%s) vertex %i uses bone %i", modName, i, surfName, j, boneIndex );
					R_FreeStaticTriSurf( tri );
					return false;
				}

				const skbBoneMat_t &b = bones[boneIndex];

				for ( int c = 0; c < 3; c++ ) {
					pos[c] += weight * ( b.r[c][0] * off[0] + b.r[c][1] * off[1] + b.r[c][2] * off[2] + b.t[c] );
					nor[c] += weight * ( b.r[c][0] * n[0] + b.r[c][1] * n[1] + b.r[c][2] * n[2] );
				}
			}

			// a NaN or absurd position would poison the model bounds and everything that culls with them
			if ( !( pos[0] > -1e9f && pos[0] < 1e9f && pos[1] > -1e9f && pos[1] < 1e9f && pos[2] > -1e9f && pos[2] < 1e9f ) ) {
				common->Warning( "idRenderModelSKB: %s surface %i (%s) vertex %i has an invalid position", modName, i, surfName, j );
				R_FreeStaticTriSurf( tri );
				return false;
			}

			float nlen = sqrtf( nor[0] * nor[0] + nor[1] * nor[1] + nor[2] * nor[2] );
			if ( nlen > 1e-6f ) {
				nor[0] /= nlen;
				nor[1] /= nlen;
				nor[2] /= nlen;
			} else {
				nor[0] = 0.0f;
				nor[1] = 0.0f;
				nor[2] = 1.0f;
			}

			idDrawVert *dv = &tri->verts[j];
			memset( (void *)dv, 0, sizeof( *dv ) );
			dv->xyz[0] = pos[0];
			dv->xyz[1] = pos[1];
			dv->xyz[2] = pos[2];
			dv->normal[0] = nor[0];
			dv->normal[1] = nor[1];
			dv->normal[2] = nor[2];
			dv->st[0] = LittleFloat( vert.texCoords[0] );
			dv->st[1] = LittleFloat( vert.texCoords[1] );
		}

		// The stored winding already is the one Doom 3 wants (the normal comes out of
		// (c - a) x (b - a)), so the indexes are copied as they are. The Blender importer
		// swaps two of them because Blender uses the opposite winding, don't copy that.
		for ( j = 0; j < surf.numTriangles; j++ ) {
			skbTriangle_t t;

			memcpy( &t, buffer + triOfs + j * sizeof( t ), sizeof( t ) );

			for ( k = 0; k < 3; k++ ) {
				const int index = LittleLong3( t.indexes[k] );

				if ( index < 0 || index >= surf.numVerts ) {
					common->Warning( "idRenderModelSKB: %s surface %i (%s) triangle %i uses vertex %i of %i", modName, i, surfName, j, index, surf.numVerts );
					R_FreeStaticTriSurf( tri );
					return false;
				}
				tri->indexes[j * 3 + k] = (glIndex_t)index;
			}
		}

		R_BoundTriSurf( tri );

		modelSurface_t surface;
		memset( &surface, 0, sizeof( surface ) );
		surface.id = i;
		surface.geometry = tri;
		surface.shader = FindSurfaceMaterial( surfName );
		AddSurface( surface );

		numAdded++;
		totalVerts += surf.numVerts;
		totalTris += surf.numTriangles;

		surfOfs = surfEnd;
	}

	if ( numAdded == 0 ) {
		common->Warning( "idRenderModelSKB: %s has nothing to draw", modName );
		return false;
	}

	common->DPrintf( "idRenderModelSKB: %s: %i surfaces, %i bones, %i verts, %i triangles\n", modName, numAdded, numBones, totalVerts, totalTris );
	return true;
}
