// Model_sil.h
//
// Shared stencil-shadow (silhouette) support for the skeletal render models
// (MDR / MDX / MDS / MDM).
//
// Silhouette connectivity (indexes, silIndexes, silEdges) depends only on mesh
// topology, not on the pose, so it is built once per surface (lazily, from the
// first skinned pose) and then shared by every per-frame srfTriangles_t. This is
// the same idea as idRenderModelMD3::BuildSilInfo() and idMD5Mesh::deformInfo.
//
// The cache is kept in a small registry keyed by the owning model pointer, so
// no changes to the model class declarations are needed. Each model only has to:
//   - call rvmSilCache_Apply() once per surface, AFTER its verts are skinned
//     and tri->numVerts is set, and BEFORE R_BoundTriSurf/R_DeriveTangents
//   - call rvmSilCache_Free( this ) from LoadModel() (topology may change) and
//     from its destructor
//
// Header-only on purpose, so no build-system change is required.

#ifndef __MODEL_SIL_H__
#define __MODEL_SIL_H__

struct rvmSilInfo_t {
	bool			built;
	int				numIndexes;
	glIndex_t* indexes;
	int				numSilEdges;
	silEdge_t* silEdges;
	int* silIndexes;
};

class rvmSilCache {
public:
	idList<rvmSilInfo_t>	infos;

	rvmSilInfo_t& Get(int surfNum) {
		while (infos.Num() <= surfNum) {
			rvmSilInfo_t empty;
			memset(&empty, 0, sizeof(empty));
			infos.Append(empty);
		}
		return infos[surfNum];
	}

	void					Clear() {
		srfTriangles_t tri;
		memset(&tri, 0, sizeof(tri));
		for (int i = 0; i < infos.Num(); i++) {
			rvmSilInfo_t& s = infos[i];
			if (!s.built) {
				continue;
			}
			// these were made by the trisurf allocators, so they must be freed
			// by them too - the easiest way is to put them into a tri first
			tri.indexes = s.indexes;
			tri.silIndexes = s.silIndexes;
			tri.silEdges = s.silEdges;
			R_FreeStaticTriSurfSilIndexes(&tri);
			R_FreeStaticTriSurfSilEdges(&tri);
			R_FreeStaticTriSurfIndexes(&tri);
		}
		infos.Clear();
	}
};

struct rvmSilOwner_t {
	const void* owner;
	rvmSilCache* cache;
};

// registry of caches; intentionally never destroyed (avoids static-destruction
// order problems with the engine heap at shutdown)
inline idList<rvmSilOwner_t>& rvmSilRegistry() {
	static idList<rvmSilOwner_t>* reg = new idList<rvmSilOwner_t>;
	return *reg;
}

inline rvmSilCache* rvmSilCache_Get(const void* owner) {
	idList<rvmSilOwner_t>& reg = rvmSilRegistry();
	for (int i = 0; i < reg.Num(); i++) {
		if (reg[i].owner == owner) {
			return reg[i].cache;
		}
	}
	rvmSilOwner_t o;
	o.owner = owner;
	o.cache = new rvmSilCache;
	reg.Append(o);
	return o.cache;
}

inline void rvmSilCache_Free(const void* owner) {
	idList<rvmSilOwner_t>& reg = rvmSilRegistry();
	for (int i = 0; i < reg.Num(); i++) {
		if (reg[i].owner == owner) {
			common->Printf("rvmSilCache: free owner=%p\n", owner);
			reg[i].cache->Clear();
			delete reg[i].cache;
			reg.RemoveIndex(i);
			return;
		}
	}
}

/*
====================
rvmSilCache_Apply

tri			a surface whose tri->verts are already skinned and whose
			tri->numVerts is already set. tri->indexes must NOT be allocated.
srcTris		the surface's triangle index list (surface-relative)
numIndexes	numTriangles * 3
baseVertex	offset added to each source index (0 for one-tri-per-surface)

Builds the silhouette info on first use, then points the tri at the shared
arrays and flags it as a deformed surface so the engine will not free them.
====================
*/
inline void rvmSilCache_Apply(const void* owner, int surfNum, srfTriangles_t* tri,
	const int* srcTris, int numIndexes, int baseVertex) {
#ifdef RVM_SIL_DISABLE
	// A/B test switch: the old behaviour. The tri owns its indexes, no silhouette data,
	// deformedSurface untouched, so the engine frees everything normally.
	R_AllocStaticTriSurfIndexes(tri, numIndexes);
	for (int j = 0; j < numIndexes; j++) {
		tri->indexes[j] = baseVertex + srcTris[j];
	}
	tri->numIndexes = numIndexes;
	return;
#endif
	rvmSilInfo_t& si = rvmSilCache_Get(owner)->Get(surfNum);
	assert(!si.built || si.numIndexes == numIndexes);	// reloads must call rvmSilCache_Free()

	if (!si.built) {
		srfTriangles_t t;
		memset(&t, 0, sizeof(t));
		t.verts = tri->verts;
		t.numVerts = tri->numVerts;
		t.numIndexes = numIndexes;
		R_AllocStaticTriSurfIndexes(&t, numIndexes);
		for (int j = 0; j < numIndexes; j++) {
			t.indexes[j] = baseVertex + srcTris[j];
		}
		R_CreateSilIndexes(&t);
		// animated, so coplanar edges in this pose may not be coplanar in others
		R_IdentifySilEdges(&t, false);

		si.numIndexes = t.numIndexes;
		si.indexes = t.indexes;
		si.numSilEdges = t.numSilEdges;
		si.silEdges = t.silEdges;
		si.silIndexes = t.silIndexes;
		si.built = true;
	}

	tri->indexes = si.indexes;
	tri->numIndexes = si.numIndexes;
	tri->silIndexes = si.silIndexes;
	tri->silEdges = si.silEdges;
	tri->numSilEdges = si.numSilEdges;
	// these arrays belong to the cache, don't let the engine free them with the tri
	tri->deformedSurface = true;
}


// Name given to the per-entity snapshot model, so InstantiateDynamicModel() can tell
// whether the cachedModel the engine hands back is one of ours.
#define MDR_SNAPSHOT_NAME "_MDR_Snapshot_"
#define GLM_SNAPSHOT_NAME "_GLM_Snapshot_"
#define MDS_SNAPSHOT_NAME "_MDS_Snapshot_"
#define MDM_SNAPSHOT_NAME "_MDM_Snapshot_"

/*
====================
rvmAcquireSurface

md3/md5 style: the snapshot model and each surface's srfTriangles_t live for as long as
the entity keeps its cached dynamic model, and only the vertex data is rewritten each
frame. This returns the surface with the given id (adding it the first time) with a tri
that is ready to have its verts rewritten: counts and flags reset, GPU copies dropped.
Callers then write tri->verts, bump tri->numVerts, and call rvmSilCache_Apply().
====================
*/
inline modelSurface_t* rvmAcquireSurface(idRenderModelStatic* model, int id, int numVerts) {
	int surfNum;

	if (!model->FindSurfaceWithId(id, surfNum)) {
		modelSurface_t newSurf;
		memset(&newSurf, 0, sizeof(newSurf));
		newSurf.id = id;
		model->AddSurface(newSurf);
		if (!model->FindSurfaceWithId(id, surfNum)) {
			common->Error("rvmAcquireSurface: failed to find newly-added surface %d", id);
		}
	}

	modelSurface_t* surf = &model->surfaces[surfNum];
	srfTriangles_t* tri = surf->geometry;

	// numVerts still holds last frame's count here, which is what we allocated for
	if (tri && tri->numVerts != numVerts) {
		R_FreeStaticTriSurf(tri);
		surf->geometry = tri = NULL;
	}
	if (!tri) {
		tri = R_AllocStaticTriSurf();
		R_AllocStaticTriSurfVerts(tri, numVerts);
		surf->geometry = tri;
	}
	else {
		// vertex positions are about to change: drop any cached GPU copy but keep the CPU arrays
		R_FreeStaticTriSurfVertexCaches(tri);
	}

	// indexes/silIndexes/silEdges belong to the rvmSilCache, never to the tri
	tri->deformedSurface = true;
	tri->tangentsCalculated = false;
	tri->facePlanesCalculated = false;
	tri->numVerts = 0;			// callers append, as before
	tri->bounds.Clear();
	return surf;
}

#endif /* !__MODEL_SIL_H__ */