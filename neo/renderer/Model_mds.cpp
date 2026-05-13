// Model_mds.cpp
//

#include "sys/platform.h"
#include "framework/Session.h"
#include "renderer/tr_local.h"
#include "renderer/Model_local.h"
#include "renderer/Model_mds.h"

int		LittleLong2(int l) { return l; }

ID_INLINE void LocalAddScaledMatrixTransformVectorTranslate(idVec3 in, float s, idMat3 mat, idVec3 tr, idVec3 out) {
	out[0] += s * (in[0] * mat[0][0] + in[1] * mat[0][1] + in[2] * mat[0][2] + tr[0]);
	out[1] += s * (in[0] * mat[1][0] + in[1] * mat[1][1] + in[2] * mat[1][2] + tr[1]);
	out[2] += s * (in[0] * mat[2][0] + in[1] * mat[2][1] + in[2] * mat[2][2] + tr[2]);
}


/*
========================
idRenderModelMDS::idRenderModelMDS
========================
*/
idRenderModelMDS::idRenderModelMDS() {
	dataSize = 0;
	modelData = nullptr;
	cachedModel = nullptr;
	cFrame = 0;
}

/*
========================
idRenderModelMDS::~idRenderModelMDS
========================
*/
idRenderModelMDS::~idRenderModelMDS() {
	if(modelData)
	{
		Mem_Free(modelData);
		modelData = nullptr;
	}

	if (cachedModel)
	{
		delete cachedModel;
		cachedModel = nullptr;
	}

	dataSize = 0;
}

/*
========================
idRenderModelMDS::InitFromFile
========================
*/
void idRenderModelMDS::InitFromFile(const char *fileName) {
	name = fileName;
	LoadModel();
}

/*
========================
idRenderModelMDS::LoadModel
========================
*/
void idRenderModelMDS::LoadModel(void) {
	void *mdsMeshBuffer;
	int mdsMeshLen;

	if (!purged) {
		PurgeModel();
	}
	purged = false;

	materials.Clear();

	// Try and load the file off disk.
	mdsMeshLen = fileSystem->ReadFile(name, &mdsMeshBuffer);
	if (mdsMeshLen <= 0 || mdsMeshBuffer == nullptr) {
		common->Warning("Failed to load MDS mesh %s\n", name.c_str());
		MakeDefaultModel();
		return;
	}

	// Parse the MDS file.
	if (!LoadMDSFile(mdsMeshBuffer, mdsMeshLen)) {
		MakeDefaultModel();
		fileSystem->FreeFile(mdsMeshBuffer);
		return;
	}

	// Convert the anims to md5 if it doesn't exist
	WriteAnimToDisk();

	// If we don't have any frames, then render as a static mesh.
	//if (modelData->numFrames == 1)
	{
		viewDef_t view;
		RenderFramesToModel(this, &view, 1, 0, NULL);
		
	}

	// Free the memory from reading in the ifle.
	fileSystem->FreeFile(mdsMeshBuffer);
}

/*
========================
idRenderModelMDS::LoadMDSFile
========================
*/
bool idRenderModelMDS::LoadMDSFile(void *buffer, int filesize) {
	int					i, j, k;
	mdsHeader_t			*pinmodel, *mds;
	mdsFrame_t			*frame;
	mdsSurface_t		*surf;
	mdsTriangle_t		*tri;
	mdsVertex_t			*v;
	mdsBoneInfo_t*		bi;
	mdsTag_t			*tag;
	int					version;
	int					size;
	const idMaterial	*sh;
	int					frameSize;
	int* collapseMap, * boneref;

	const char *mod_name = name.c_str();

	//idStr shaderPrefix = name;
	//shaderPrefix.StripFileExtension();

	bounds.Clear();

	pinmodel = (mdsHeader_t *)buffer;

	//pinmodel->version = LittleLong2(pinmodel->version);
	version = LittleLong2(pinmodel->version);
	if (version != MDS_VERSION)
	{
		common->Warning("R_LoadMDS: %s has wrong version (%i should be %i)\n", mod_name, pinmodel->version, MDS_VERSION);
		return false;
	}

	size = LittleLong2(pinmodel->ofsEnd);
	//mds = mod->mds = ri.Hunk_Alloc(size, h_low);

	//memcpy(mds, buffer, LittleLong2(pinmodel->ofsEnd));

	if (size > filesize)
	{
		common->Warning("R_LoadMDS: Header of %s is broken. Wrong filesize declared!\n", mod_name);
		return false;
	}

	//Fix all this, you can just memcpy and then fix it after
	LL(pinmodel->numFrames);
	LL(pinmodel->numBones);
	LL(pinmodel->numSurfaces);
	LL(pinmodel->ofsFrames);
	LL(pinmodel->ofsBones);

	dataSize += size;
	modelData = mds = (mdsHeader_t *)Mem_Alloc(size);

	memcpy(mds, buffer, LittleLong2(pinmodel->ofsEnd));

	// Copy all the values over from the file and fix endian issues in the process, if necessary.

	mds->ident = LittleLong2(pinmodel->ident);
	mds->version = pinmodel->version;	// Don't need to swap byte order on this one, we already did above.
	strcpy(mds->name, pinmodel->name);
	mds->numFrames = pinmodel->numFrames;
	mds->numBones = pinmodel->numBones;
	mds->numTags = LittleLong2(pinmodel->numTags);
	mds->numSurfaces = pinmodel->numSurfaces;
	mds->ofsFrames = pinmodel->ofsFrames;
	mds->ofsBones = pinmodel->ofsBones;
	mds->ofsTags = LittleLong2(pinmodel->ofsTags);
	mds->ofsEnd = LittleLong2(pinmodel->ofsEnd);
	mds->ofsSurfaces = LittleLong2(pinmodel->ofsSurfaces);
	
	// We don't care about the other offset values, we'll generate them ourselves while loading.

	if (mds->numFrames < 1)
	{
		common->Warning("R_LoadMDS: %s has no frames\n", mod_name);
		return false;
	}

	if (LittleLong2(1) != 1) {
		// swap all the frames
		//frameSize = (int)( &((mdsFrame_t *)0)->bones[ mds->numBones ] );
		frameSize = (int)(sizeof(mdsFrame_t) - sizeof(mdsBoneFrameCompressed_t) + mds->numBones * sizeof(mdsBoneFrameCompressed_t));
		for (i = 0; i < mds->numFrames; i++, frame++) {
			frame = (mdsFrame_t*)((byte*)mds + mds->ofsFrames + i * frameSize);
			frame->radius = LittleFloat(frame->radius);
			for (j = 0; j < 3; j++) {
				frame->bounds[0][j] = LittleFloat(frame->bounds[0][j]);
				frame->bounds[1][j] = LittleFloat(frame->bounds[1][j]);
				frame->localOrigin[j] = LittleFloat(frame->localOrigin[j]);
				frame->parentOffset[j] = LittleFloat(frame->parentOffset[j]);
			}
			for (j = 0; j < mds->numBones * sizeof(mdsBoneFrameCompressed_t) / sizeof(short); j++) {
				((short*)frame->bones)[j] = LittleShort(((short*)frame->bones)[j]);
			}
		}

		// swap all the tags
		tag = (mdsTag_t*)((byte*)mds + mds->ofsTags);
		for (i = 0; i < mds->numTags; i++, tag++) {
			LL(tag->boneIndex);
			tag->torsoWeight = LittleFloat(tag->torsoWeight);
		}

		// swap all the bones
		for (i = 0; i < mds->numBones; i++, bi++) {
			bi = (mdsBoneInfo_t*)((byte*)mds + mds->ofsBones + i * sizeof(mdsBoneInfo_t));
			LL(bi->parent);
			bi->torsoWeight = LittleFloat(bi->torsoWeight);
			bi->parentDist = LittleFloat(bi->parentDist);
			LL(bi->flags);
		}
	}



	// swap all the surfaces
	surf = (mdsSurface_t*)((byte*)mds + mds->ofsSurfaces);
	for (i = 0; i < mds->numSurfaces; i++) {
		if (LittleLong2(1) != 1) {
			LL(surf->ident);
			LL(surf->shaderIndex);
			LL(surf->minLod);
			LL(surf->ofsHeader);
			LL(surf->ofsCollapseMap);
			LL(surf->numTriangles);
			LL(surf->ofsTriangles);
			LL(surf->numVerts);
			LL(surf->ofsVerts);
			LL(surf->numBoneReferences);
			LL(surf->ofsBoneReferences);
			LL(surf->ofsEnd);
		}

		/*
		if (surf->numVerts > SHADER_MAX_VERTEXES) {
			ri.Error(ERR_DROP, "R_LoadMDS: %s has more than %i verts on a surface (%i)",
				mod_name, SHADER_MAX_VERTEXES, surf->numVerts);
		}
		if (surf->numTriangles * 3 > SHADER_MAX_INDEXES) {
			ri.Error(ERR_DROP, "R_LoadMDS: %s has more than %i triangles on a surface (%i)",
				mod_name, SHADER_MAX_INDEXES / 3, surf->numTriangles);
		}
		*/
		// register the shaders
		const char* materialName = va("%s%s", "m", surf->shader);
		if (surf->shader[0]) {
			//sh = R_FindShader(surf->shader, LIGHTMAP_NONE, qtrue);
			sh = declManager->FindMaterial(materialName, true); //R_FindShader(surf->shader, LIGHTMAP_NONE, qtrue);
			if (sh == nullptr) {
				surf->shaderIndex = 0;
			} else {
				surf->shaderIndex = sh->Index();
			}
		} else { surf->shaderIndex = 0;
			common->Warning("Failed to find material %s\n", surf->shader);
		}
		materials.Append(sh);

		if (LittleLong2(1) != 1) {
			// swap all the triangles
			tri = (mdsTriangle_t*)((byte*)surf + surf->ofsTriangles);
			for (j = 0; j < surf->numTriangles; j++, tri++) {
				LL(tri->indexes[0]);
				LL(tri->indexes[1]);
				LL(tri->indexes[2]);
			}

			// swap all the vertexes
			v = (mdsVertex_t*)((byte*)surf + surf->ofsVerts);
			for (j = 0; j < surf->numVerts; j++) {
				v->normal[0] = LittleFloat(v->normal[0]);
				v->normal[1] = LittleFloat(v->normal[1]);
				v->normal[2] = LittleFloat(v->normal[2]);

				v->texCoords[0] = LittleFloat(v->texCoords[0]);
				v->texCoords[1] = LittleFloat(v->texCoords[1]);

				v->numWeights = LittleLong2(v->numWeights);

				for (k = 0; k < v->numWeights; k++) {
					v->weights[k].boneIndex = LittleLong2(v->weights[k].boneIndex);
					v->weights[k].boneWeight = LittleFloat(v->weights[k].boneWeight);
					v->weights[k].offset[0] = LittleFloat(v->weights[k].offset[0]);
					v->weights[k].offset[1] = LittleFloat(v->weights[k].offset[1]);
					v->weights[k].offset[2] = LittleFloat(v->weights[k].offset[2]);
				}

				v = (mdsVertex_t*)&v->weights[v->numWeights];
			}

			// swap the collapse map
			collapseMap = (int*)((byte*)surf + surf->ofsCollapseMap);
			for (j = 0; j < surf->numVerts; j++, collapseMap++) {
				*collapseMap = LittleLong2(*collapseMap);
			}

			// swap the bone references
			boneref = (int*)((byte*)surf + surf->ofsBoneReferences);
			for (j = 0; j < surf->numBoneReferences; j++, boneref++) {
				*boneref = LittleLong2(*boneref);
			}
		}

		// find the next surface
		surf = (mdsSurface_t*)((byte*)surf + surf->ofsEnd);
	}
	/*
	// Store the pointers for each frame, so we have it for faster lookup.
	for ( int frameId = 0; frameId < mds->numFrames; frameId++ ) {
		mdsHeader_t *header;
		mdsFrame_t *frame;
	
		header = (mdsHeader_t *)modelData;
		int frameSize = (size_t)(&((mdsFrame_t *)0)->bones[header->numBones]);
	
		frame = (mdsFrame_t *)((byte *)header + header->ofsFrames + frameId * frameSize);
		frames.Append(frame);
	}
	*/
	
	// Copy the joints over so Doom 3 can interact with it
	mdsHeader_t *header = (mdsHeader_t*)modelData;
	joints.SetGranularity(1);
	joints.SetNum(header->numBones);
	defaultPose.SetGranularity(1);
	defaultPose.SetNum(header->numBones);

	mdsBoneInfo_t* boneInfo, * thisBoneInfo, * parentBoneInfo;
	boneInfo = (mdsBoneInfo_t*)((byte*)header + header->ofsBones);

	for (int i = 0; i < header->numBones; i++) {
		thisBoneInfo = &boneInfo[i];
		if (i == 0) {
			joints[i].parent = NULL; 
			joints[i].name = "origin";
		} else {
		joints[i].parent = &joints[thisBoneInfo->parent];
		joints[i].name = thisBoneInfo->name;
		}
	}
	
	//Defaultpose here for now - Dynamix

	return true;
}

/*
=================
idRenderModelMDS::IsDynamicModel
=================
*/
dynamicModel_t idRenderModelMDS::IsDynamicModel() const {
	return DM_CACHED;
}

/*
=================
idRenderModelMDS::TouchData
=================
*/
void idRenderModelMDS::TouchData() {

}

/*
=================
idRenderModelMDS::NumJoints
=================
*/
int	idRenderModelMDS::NumJoints(void) const {
	return modelData->numBones;
}

/*
=================
idRenderModelMDS::GetJoints
=================
*/
const idMD5Joint *idRenderModelMDS::GetJoints(void) const {
	if (!joints.Num()) {
		common->Error("Model has no Joints");
	}
	return &joints[0];
}

/*
=================
idRenderModelMDS::GetJointHandle
=================
*/
jointHandle_t idRenderModelMDS::GetJointHandle(const char *name) const {
	const idMD5Joint* joint;
	int	i;

	joint = joints.Ptr();
	for (i = 0; i < joints.Num(); i++, joint++) {
		if (idStr::Icmp(joint->name.c_str(), name) == 0) {
			return (jointHandle_t)i;
		}
	}

	return INVALID_JOINT;
}

/*
=================
idRenderModelMDS::Bounds
=================
*/
idBounds idRenderModelMDS::Bounds(const class idRenderEntity*ent) const {
	return bounds;
}

/*
=================
idRenderModelMDS::Memory
=================
*/
int	idRenderModelMDS::Memory() const {
	return dataSize;
}

/*
=================
idRenderModelMDS::RenderFramesToModel
=================
*/
void idRenderModelMDS::RenderFramesToModel(idRenderModelStatic *staticModel, const struct viewDef_s *view, int scale, int frameNum, const renderEntity_t* ent) {
	int				i, j, k, frameSize, currentFrame;
	float			frontlerp, backlerp;
	int				*triangles;
	int				indexes;
	int				baseIndex, baseVertex;
	int				numVerts;
	mdsVertex_t		*v;
	mdsHeader_t		*header;
	mdsFrame_t		*frame;
	mdsFrame_t		*oldFrame;
	mdsBoneFrame_t	bones[MDS_MAX_BONES], *bonePtr, *bone;
	mdsBoneInfo_t	*boneInfo, *thisBoneInfo, *parentBoneInfo;
	
	header = modelData;

	mdsSurface_t *surface = (mdsSurface_t*)((byte*)header + header->ofsSurfaces);

	baseIndex = 0;
	baseVertex = 0;

	for (int i = 0; i < header->numSurfaces; i++) {
		srfTriangles_t *tri = R_AllocStaticTriSurf();
		R_AllocStaticTriSurfVerts(tri, surface->numVerts);
		R_AllocStaticTriSurfIndexes(tri, surface->numTriangles * 3);
		tri->bounds.Clear();

		modelSurface_t	surf;

		surf.id = i;
		surf.geometry = tri;

		surf.shader = materials[i];

		// Set up all triangles.
		triangles = (int *)((byte *)surface + surface->ofsTriangles);
		indexes = surface->numTriangles * 3;
		for (j = 0; j < indexes; j++) {
			tri->indexes[baseIndex + j] = baseVertex + triangles[j];
		}

		tri->numIndexes += indexes;

		// Dynamix - investigate shadows, I think normals are fine now
		//
		// deform the vertexes by the lerped bones
		//
		if (ent) {
			idJointMat bone;

			numVerts = surface->numVerts;
			v = (mdsVertex_t *)((byte *)surface + surface->ofsVerts);

			for (j = 0; j < numVerts; j++) {
				idVec3	tempVert, tempNormal;
				mdsWeight_t	*w;

				tempVert.Zero();
				tempNormal.Zero();
				

				w = v->weights;
				for (k = 0; k < v->numWeights; k++, w++) {
					idMat3 rotMatrix;

					bone = ent->joints[w->boneIndex];

					//for (int r = 0; r < 3; r++) {
					//	for (int c = 0; c < 3; c++) {
					//		rotMatrix[r][c] = bone2.ToMat3()[c][r];
					//	}
					//}

					rotMatrix = bone.ToMat3().Transpose();

					tempVert[0] += w->boneWeight * (DotProduct(rotMatrix[0], w->offset) + bone.ToVec3().x);
					tempVert[1] += w->boneWeight * (DotProduct(rotMatrix[1], w->offset) + bone.ToVec3().y);
					tempVert[2] += w->boneWeight * (DotProduct(rotMatrix[2], w->offset) + bone.ToVec3().z);

					tempNormal[0] += w->boneWeight * DotProduct(rotMatrix[0], v->normal);
					tempNormal[1] += w->boneWeight * DotProduct(rotMatrix[1], v->normal);
					tempNormal[2] += w->boneWeight * DotProduct(rotMatrix[2], v->normal);
				}
				
				tri->verts[baseVertex + j].xyz[0] = tempVert[0];
				tri->verts[baseVertex + j].xyz[1] = tempVert[1];
				tri->verts[baseVertex + j].xyz[2] = tempVert[2];

				tri->verts[baseVertex + j].normal[0] = tempNormal[0];
				tri->verts[baseVertex + j].normal[1] = tempNormal[1];
				tri->verts[baseVertex + j].normal[2] = tempNormal[2];

				tri->verts[baseVertex + j].st[0] = v->texCoords[0];
				tri->verts[baseVertex + j].st[1] = v->texCoords[1];

				tri->generateNormals = false;

				v = (mdsVertex_t *)&v->weights[v->numWeights];
			}

			tri->numVerts += surface->numVerts;

			R_BoundTriSurf(tri);

			// Workaround, RTCW/ET use mesh based skin overrides and don't fill in a 
			// default texture
			//common->Printf("Surf name %s", surface->name);
			//surf.shader = declManager->FindMaterial("models/players/Bj/bj_body1.tga");
			surf.shader = declManager->FindMaterial(surface->name);

			staticModel->AddSurface(surf);
			
			staticModel->bounds.AddPoint(surf.geometry->bounds[0]);
			staticModel->bounds.AddPoint(surf.geometry->bounds[1]);
			
			surface = (mdsSurface_t *)((byte *)surface + surface->ofsEnd);
		}
	}
}

/*
====================
idRenderModelMDS::DrawJoints
====================
*/
void idRenderModelMDS::DrawJoints(const renderEntity_t* ent, const struct viewDef_s* view) const {
	int					i;
	int					num;
	idVec3				pos;
	const idJointMat* joint;
	const idMD5Joint* md5Joint;
	int					parentNum;

	num = ent->numJoints;
	joint = ent->joints;
	md5Joint = joints.Ptr();
	for (i = 0; i < num; i++, joint++, md5Joint++) {
		pos = ent->origin + joint->ToVec3() * ent->axis;
		if (md5Joint->parent) {
			parentNum = md5Joint->parent - joints.Ptr();
			session->rw->DebugLine(colorWhite, ent->origin + ent->joints[parentNum].ToVec3() * ent->axis, pos);
		}

		session->rw->DebugLine(colorRed, pos, pos + joint->ToMat3()[0] * 2.0f * ent->axis);
		session->rw->DebugLine(colorGreen, pos, pos + joint->ToMat3()[1] * 2.0f * ent->axis);
		session->rw->DebugLine(colorBlue, pos, pos + joint->ToMat3()[2] * 2.0f * ent->axis);
	}

	idBounds bounds;

	bounds.FromTransformedBounds(ent->bounds, vec3_zero, ent->axis);
	session->rw->DebugBounds(colorMagenta, bounds, ent->origin);

	if ((r_jointNameScale.GetFloat() != 0.0f) && (bounds.Expand(128.0f).ContainsPoint(view->renderView.vieworg - ent->origin))) {
		idVec3	offset(0, 0, r_jointNameOffset.GetFloat());
		float	scale;

		scale = r_jointNameScale.GetFloat();
		joint = ent->joints;
		num = ent->numJoints;
		for (i = 0; i < num; i++, joint++) {
			pos = ent->origin + joint->ToVec3() * ent->axis;
			session->rw->DrawText(joints[i].name, pos + offset, scale, colorWhite, view->renderView.viewaxis, 1);
		}
	}
}

/*
=================
idRenderModelMDS::InstantiateDynamicModel
=================
*/
idRenderModel *idRenderModelMDS::InstantiateDynamicModel(const struct renderEntity_s* ent, const struct viewDef_s* view, idRenderModel* cachedModel) {
	idRenderModelStatic	*staticModel;
	//class idRenderEntityLocal* renderEntity = (class idRenderEntityLocal*)ent;
	static const char* MDS_SnapshotName = "_MDS_Snapshot_";
	// Dynamix - bring the md5 stuff over for this
	if ( cachedModel ) {
		delete cachedModel;
		cachedModel = NULL;
	}

	if (purged) {
		common->DWarning("model %s instantiated while purged", Name());
		LoadModel();
	}

	if (!ent->joints) {
		common->Printf("idRenderModelMDS::InstantiateDynamicModel: NULL joints on renderEntity for '%s'\n", Name());
		delete cachedModel;
		return NULL;
	} else if (ent->numJoints != joints.Num()) {
		common->Printf("idRenderModelMDS::InstantiateDynamicModel: renderEntity has different number of joints than model for '%s'\n", Name());
		delete cachedModel;
		return NULL;
	}

	if (cachedModel) {
		assert(dynamic_cast<idRenderModelStatic*>(cachedModel) != NULL);
		assert(idStr::Icmp(cachedModel->Name(), MDS_SnapshotName) == 0);
		staticModel = static_cast<idRenderModelStatic*>(cachedModel);
	} else {
		staticModel = new idRenderModelStatic;
		staticModel->InitEmpty(MDS_SnapshotName);
	}

	staticModel->bounds.Clear();

	// Dynamix - unused and I don't know if EF does any scaling, look into it
	int scale = 1; // ent->scale;
	//if (scale == 0)
	//	scale = 1;

	// Dynamix - clean up the parms and/or just merge this back into here
	// Dynamix ent is never null for some reason even if there's no anim, no idea how that happens, 
	// Was fine in the MDR version too, very odd
	if (ent != NULL) {
		// Dynamix - I was using this to manually animate stuff for testing
		//RenderFramesToModel(staticModel, NULL, view, material, scale, ent->shaderParms[SHADERPARM_MD3_FRAME]);
		RenderFramesToModel(staticModel, view, scale, 0, ent);
	} else {
		RenderFramesToModel(staticModel, view, scale, 1, NULL);
	}

	if (r_showSkel.GetInteger()) {
		if ((view != NULL) && (!r_skipSuppress.GetBool() || !ent->suppressSurfaceInViewID || (ent->suppressSurfaceInViewID != view->renderView.viewID))) {
			// only draw the skeleton
			DrawJoints(ent, view);
		}
	}

	return staticModel;
}


/*
=================
idRenderModelMDS::GetJointName
=================
*/
const char * idRenderModelMDS::GetJointName(jointHandle_t handle) const {
	if ((handle < 0) || (handle >= joints.Num())) {
		return "<invalid joint>";
	}

	return joints[handle].name;
}

/*
=================
idRenderModelMDS::GetDefaultPose
=================
*/
const idJointQuat *	idRenderModelMDS::GetDefaultPose(void) const {
	return defaultPose.Ptr();
}
/*
=================
idRenderModelMDS::NearestJoint
=================
*/
int	idRenderModelMDS::NearestJoint(int surfaceNum, int a, int b, int c) const {
	return 0;
}

/*
=================
idRenderModelMDS::WriteAnimToDisk
=================
*/
bool idRenderModelMDS::WriteAnimToDisk() {
	idFile* fp;
	int frameSize, numFrames, numAnims, numBones;
	mdsHeader_t* header;
	mdsFrame_t* frame;
	mdsBoneInfo_t* boneInfo, * thisBoneInfo;
	//mdsBoneFrameCompressed_t* compBone;
	//float compNum = (360.0f / 65536.0f);


	idList <animInfo_t> animInfo;

	animInfo.SetGranularity(1);
	animInfo.SetNum(0);
	numAnims = 0;

	ParseAnims(animInfo, numAnims);
	common->Printf("Numanims = %d\n", numAnims);

	//idStr animName = name;
	//animName.StripFileExtension();
	//animName.Append(".md5anim");

	//if ( findFile_t test = fileSystem->FindFile(animName, 0) ) {
	//common->DPrintf("Found anim file %s for MDS model\n", animName.c_str());
	//return true;
	//}

	header = (mdsHeader_t*)modelData;
	numBones = header->numBones;
	numFrames = header->numFrames;
	frameSize = (int)(sizeof(mdsFrame_t) - sizeof(mdsBoneFrameCompressed_t) + numBones * sizeof(mdsBoneFrameCompressed_t));

	frame = (mdsFrame_t*)((byte*)header + header->ofsFrames + 0 * frameSize);

	boneInfo = (mdsBoneInfo_t*)((byte*)header + header->ofsBones);

	idList <idJointMat> boneData;
	boneData.SetGranularity(1);
	boneData.SetNum(numBones);

	CalcBones(boneData, 0);

	/*
	//Uncompress the bones into boneData
	for (int i = 0; i < numBones; i++) {
		thisBoneInfo = &boneInfo[i];
		compBone = &frame->bones[i];
		int parentNum = thisBoneInfo->parent;

		common->Printf("Compbone %s:\n", thisBoneInfo->name);

		idMat3 boneRotMat = idAngles(compBone->angles[0]*compNum, compBone->angles[1] 
			* compNum, compBone->angles[2] * compNum).ToMat3();

		idQuat boneRotQuat = idAngles(compBone->angles[0] * compNum, compBone->angles[1]
			* compNum, compBone->angles[2] * compNum).ToQuat();

		idMat3 offsetDir = idAngles(compBone->ofsAngles[0] * compNum, compBone->ofsAngles[1]
			* compNum, 0.0f).ToMat3();

		idVec3 offsetVec =  idVec3(thisBoneInfo->parentDist, 0, 0) * offsetDir;

		//common->Printf("Degrees %f %f %f:\n", compBone->angles[0] * compNum, compBone->angles[1]
		//	* compNum, compBone->angles[2] * compNum);

		//common->Printf("Offsetvec %f %f %f:\n", offsetVec.x, offsetVec.y, offsetVec.z);

		//common->Printf("Quats %f %f %f:\n", boneRotQuat.x, boneRotQuat.y, boneRotQuat.z);

		if (i == 0) {
			boneData[i].SetRotation(boneRotMat.Transpose());
			boneData[i].SetTranslation(frame->parentOffset);
			common->Printf("MD5 data:\n");

			idJointQuat JointQuat = boneData[i].ToJointQuat();
			idCQuat cQuat = JointQuat.q.ToCQuat();
			common->Printf("( %f %f %f ) ( %f %f %f )\n", JointQuat.t.x, JointQuat.t.y, JointQuat.t.z, cQuat.x, cQuat.y, cQuat.z);
		} else {
			//idMat3 globalMat = boneData[parentNum].ToMat3() * boneRotMat.Transpose();
			idMat3 globalMat = boneRotMat.Transpose();
			idVec3 globalPos = boneData[parentNum].ToVec3() + offsetVec; // * boneData[parentNum].ToMat3()

			boneData[i].SetRotation(globalMat);
			boneData[i].SetTranslation(globalPos);

			idJointQuat JointQuat = boneData[i].ToJointQuat();
			idCQuat cQuat = JointQuat.q.ToCQuat();

			common->Printf("Abs transform data transform data:\n");
			common->Printf("( %f %f %f ) ( %f %f %f )\n", JointQuat.t.x, JointQuat.t.y, JointQuat.t.z, cQuat.x, cQuat.y, cQuat.z);
		}
	}
	
	// Only fine if parent is always earlier in the joint layout
	// It's what Doom 3 does though so who cares
	//Untransform the joints to .md5anim format
	for (int i = numBones - 1; i > 0; i--) {
		thisBoneInfo = &boneInfo[i];

		boneData[i] /= boneData[thisBoneInfo->parent];
	}
	*/
	/*
	for (int i = 0; i < numBones; i++) {

		//common->Printf("Parent num %d\n", thisBoneInfo->parent);
		idJointQuat JointQuat = boneData[i].ToJointQuat();
		idCQuat cQuat = JointQuat.q.ToCQuat();

		common->Printf("md5anim data bone #%d:\n", i);
		common->Printf("( %f %f %f ) ( %f %f %f )\n", JointQuat.t.x, JointQuat.t.y, JointQuat.t.z, cQuat.x, cQuat.y, cQuat.z);
	}
	*/

	// FIXME Add checks for 0/null animations and then just run through once as the super anim

	for (int c = 0; c < numAnims; c++) {
		idStr animName;
		int startFrame = animInfo[c].animInfo[0];
		int numFrames = animInfo[c].animInfo[1];


		name.ExtractFilePath(animName);
		animName.Append(animInfo[c].animName);
		animName.Append(".md5anim");

		common->Printf("writing %s\n", animName.c_str());
		fp = fileSystem->OpenFileWrite(animName, "fs_devpath");
	
	if (!fp) {
		common->Warning("WriteAnims: Error opening file %s\n", name.c_str());
		return false;
	}

	fp->WriteFloatString("MD5Version 10\n\ncommandline \"\"\n");

	fp->WriteFloatString("numFrames %d\n", numFrames);
	fp->WriteFloatString("numJoints %d\n", numBones);
	fp->WriteFloatString("frameRate %d\n", animInfo[c].animInfo[3]);
	fp->WriteFloatString("numAnimatedComponents %d\n\n", numBones * 6);

	fp->WriteFloatString("hierarchy {\n");

	for (int i = 0; i < numBones; i++) {
		thisBoneInfo = &boneInfo[i];
		if (i == 0) {
			fp->WriteFloatString("\t\"origin\" -1 63 0\n");
		} else {
			fp->WriteFloatString("\t\"%s\" %d 63 %d\n", thisBoneInfo->name, thisBoneInfo->parent, i * 6);
		}
	}

	fp->WriteFloatString("}\n\n");
	fp->WriteFloatString("bounds {\n");
	for (int i = 0, j = startFrame; i < numFrames; i++, j++) {
		frame = (mdsFrame_t*)((byte*)header + header->ofsFrames + j * frameSize);

		fp->WriteFloatString("( %f %f %f ) ( %f %f %f )\n",frame->bounds[0].x, frame->bounds[0].y, frame->bounds[0].z, frame->bounds[1].x, frame->bounds[1].y, frame->bounds[1].z);
	}
	fp->WriteFloatString("}\n\n");
	CalcBones(boneData, 0);
	fp->WriteFloatString("baseframe {\n");
	for (int i = 0; i < numBones; i++) {
		idJointQuat JointQuat = boneData[i].ToJointQuat();
		idCQuat cQuat = JointQuat.q.ToCQuat();
		fp->WriteFloatString("( %f %f %f ) ( %f %f %f )\n", JointQuat.t.x, JointQuat.t.y, JointQuat.t.z, cQuat.x, cQuat.y, cQuat.z);
	}
	fp->WriteFloatString("}\n\n");

	for (int i = 0, j = startFrame; i < numFrames; i++, j++) {
		//frame = (mdsFrame_t*)((byte*)header + header->ofsFrames + j * frameSize);
		CalcBones(boneData, j);
		fp->WriteFloatString("frame %d {\n", i);
			for (int k = 0; k < numBones; k++) {
			idJointQuat JointQuat = boneData[k].ToJointQuat();
			idCQuat cQuat = JointQuat.q.ToCQuat();

			int newlineCounter = 0;
			fp->WriteFloatString("%f %f %f %f %f %f\n", JointQuat.t.x, JointQuat.t.y, JointQuat.t.z, cQuat.x, cQuat.y, cQuat.z);
			}
		fp->WriteFloatString("}\n\n");
	}
	
	fileSystem->CloseFile(fp);
	}
	return false;
}

/*
========================
idRenderModelMDS::CalcBones
========================
*/
void idRenderModelMDS::CalcBones(idList<idJointMat>& boneData, int currentFrame) {
	int frameSize, numFrames, numBones;
	mdsHeader_t* header;
	mdsFrame_t* frame;
	mdsBoneInfo_t* boneInfo, * thisBoneInfo;
	mdsBoneFrameCompressed_t* compBone;
	float compNum = (360.0f / 65536.0f);

	header = (mdsHeader_t*)modelData;
	numBones = header->numBones;
	numFrames = header->numFrames;
	frameSize = (int)(sizeof(mdsFrame_t) - sizeof(mdsBoneFrameCompressed_t) + numBones * sizeof(mdsBoneFrameCompressed_t));

	frame = (mdsFrame_t*)((byte*)header + header->ofsFrames + currentFrame * frameSize);

	boneInfo = (mdsBoneInfo_t*)((byte*)header + header->ofsBones);

	//Uncompress the bones into boneData
	for (int i = 0; i < numBones; i++) {
		thisBoneInfo = &boneInfo[i];
		compBone = &frame->bones[i];
		int parentNum = thisBoneInfo->parent;

		//common->Printf("Compbone %s:\n", thisBoneInfo->name);

		idMat3 boneRotMat = idAngles(compBone->angles[0] * compNum, compBone->angles[1]
			* compNum, compBone->angles[2] * compNum).ToMat3();

		idQuat boneRotQuat = idAngles(compBone->angles[0] * compNum, compBone->angles[1]
			* compNum, compBone->angles[2] * compNum).ToQuat();

		idMat3 offsetDir = idAngles(compBone->ofsAngles[0] * compNum, compBone->ofsAngles[1]
			* compNum, 0.0f).ToMat3();

		idVec3 offsetVec = idVec3(thisBoneInfo->parentDist, 0, 0) * offsetDir;

		//common->Printf("Degrees %f %f %f:\n", compBone->angles[0] * compNum, compBone->angles[1]
		//	* compNum, compBone->angles[2] * compNum);

		//common->Printf("Offsetvec %f %f %f:\n", offsetVec.x, offsetVec.y, offsetVec.z);

		//common->Printf("Quats %f %f %f:\n", boneRotQuat.x, boneRotQuat.y, boneRotQuat.z);

		if (i == 0) {
			boneData[i].SetRotation(boneRotMat.Transpose());
			boneData[i].SetTranslation(frame->parentOffset);
			//common->Printf("Abs transform data:\n");

			//idJointQuat JointQuat = boneData[i].ToJointQuat();
			//idCQuat cQuat = JointQuat.q.ToCQuat();
			//common->Printf("( %f %f %f ) ( %f %f %f )\n", JointQuat.t.x, JointQuat.t.y, JointQuat.t.z, cQuat.x, cQuat.y, cQuat.z);
		}
		else {
			//idMat3 globalMat = boneData[parentNum].ToMat3() * boneRotMat.Transpose();
			idMat3 globalMat = boneRotMat.Transpose();
			idVec3 globalPos = boneData[parentNum].ToVec3() + offsetVec; // * boneData[parentNum].ToMat3()

			boneData[i].SetRotation(globalMat);
			boneData[i].SetTranslation(globalPos);

			//idJointQuat JointQuat = boneData[i].ToJointQuat();
			//idCQuat cQuat = JointQuat.q.ToCQuat();

			//common->Printf("Abs transform data transform data:\n");
			//common->Printf("( %f %f %f ) ( %f %f %f )\n", JointQuat.t.x, JointQuat.t.y, JointQuat.t.z, cQuat.x, cQuat.y, cQuat.z);
		}
	}

	// Only fine if parent is always earlier in the joint layout
	// It's what Doom 3 does though so who cares
	// Untransform the joints to .md5anim format
	for (int i = numBones - 1; i > 0; i--) {
		thisBoneInfo = &boneInfo[i];

		boneData[i] /= boneData[thisBoneInfo->parent];
	}
}

/*
========================
idRenderModelMDS::ParseAnims
========================
*/
void idRenderModelMDS::ParseAnims(idList <animInfo_t>& animInfo, int &numAnims) {
	idFile* fp;
	idStr animName;
	idLexer parser(LEXFL_ALLOWPATHNAMES | LEXFL_NOSTRINGESCAPECHARS | LEXFL_NOSTRINGCONCAT);
	idToken		token;
	animInfo_t* temp;

	name.ExtractFilePath(animName);
	animName.Append("wolfanim.cfg");

	if (findFile_t test = fileSystem->FindFile(animName, 0)) {
		common->DPrintf("Found anim file %s for MDS model\n", animName.c_str());
	}

	parser.LoadFile(animName);

	parser.SkipUntilString("STARTANIMS");
	int i = 0;
	while (1) {
	parser.ReadToken(&token);
	animInfo.SetNum(i + 1);
	animInfo[i].animName = token;
	if (token == "ENDANIMS") {
		numAnims = i;
		return;
	}
	//Skipuntilstring skips string and token is the next valid string,  comment lines are skipped entirely
	animInfo[i].animInfo[0] = parser.ParseInt();
	animInfo[i].animInfo[1] = parser.ParseInt();
	animInfo[i].animInfo[2] = parser.ParseInt();
	animInfo[i].animInfo[3] = parser.ParseInt();
	animInfo[i].animInfo[4] = parser.ParseInt();

	if (parser.PeekTokenType(TT_NUMBER, TT_INTEGER, &token)) {
		animInfo[i].animInfo[5] = parser.ParseInt();
	} else {
		animInfo[i].animInfo[5] = -2;
	}

	common->Printf("%s %d %d %d %d %d %d\n", animInfo[i].animName.c_str(), animInfo[i].animInfo[0], animInfo[i].animInfo[1],
		animInfo[i].animInfo[2], animInfo[i].animInfo[3],
		animInfo[i].animInfo[4], animInfo[i].animInfo[5]);
	i++;
	}

	return;
}


////////
//// MDM
////////
/*
========================
idRenderModelMDM::idRenderModelMDM
========================
*/
idRenderModelMDM::idRenderModelMDM() {
	dataSize = 0;
	modelData = nullptr;
	cachedModel = nullptr;
}

/*
========================
idRenderModelMDM::~idRenderModelMDM
========================
*/
idRenderModelMDM::~idRenderModelMDM() {
	if (modelData) {
		Mem_Free(modelData);
		modelData = nullptr;
	}

	if (cachedModel) {
		delete cachedModel;
		cachedModel = nullptr;
	}

	dataSize = 0;
}

/*
========================
idRenderModelMDM::InitFromFile
========================
*/
void idRenderModelMDM::InitFromFile(const char* fileName) {
	name = fileName;
	LoadModel();
}

/*
========================
idRenderModelMDM::LoadModel
========================
*/
void idRenderModelMDM::LoadModel(void) {
	void* mdsMeshBuffer;
	int mdsMeshLen;

	if (!purged) {
		PurgeModel();
	}
	purged = false;

	materials.Clear();

	// Try and load the file off disk.
	mdsMeshLen = fileSystem->ReadFile(name, &mdsMeshBuffer);
	if (mdsMeshLen <= 0 || mdsMeshBuffer == nullptr)
	{
		common->Warning("Failed to load MDM mesh %s\n", name.c_str());
		MakeDefaultModel();
		return;
	}

	// Parse the MDM file.

	if (!LoadMDMFile(mdsMeshBuffer, mdsMeshLen))
	{
		MakeDefaultModel();
		fileSystem->FreeFile(mdsMeshBuffer);
		return;
	}

	// If we don't have any frames, then render as a static mesh.
	//if (modelData->numFrames == 1)
	//{
		//viewDef_t view;
		//RenderFramesToModel(this, &view, 1, 0, NULL);

	//}

	// Free the memory from reading in the ifle.
	fileSystem->FreeFile(mdsMeshBuffer);
}

/*
========================
idRenderModelMDM::LoadMDMFile
========================
*/
bool idRenderModelMDM::LoadMDMFile(void* buffer, int filesize) {
	int					i, j, k;
	mdmHeader_t			*pinmodel, * mdm;
	mdmSurface_t*		surf;
	mdmTriangle_t*		tri;
	mdmVertex_t*		v;
	mdmTag_t*			tag;
	int					version;
	int					size;
	const idMaterial*	sh;
	int					frameSize;
	int* collapseMap, * boneref;

	const char* mod_name = name.c_str();
	bounds.Clear();

	pinmodel = (mdmHeader_t *)buffer;

	//pinmodel->version = LittleLong2(pinmodel->version);
	version = LittleLong2(pinmodel->version);
	if (version != MDM_VERSION)
	{
		common->Warning("R_LoadMDM: %s has wrong version (%i should be %i)\n", mod_name, pinmodel->version, MDM_VERSION);
		return false;
	}

	size = LittleLong2(pinmodel->ofsEnd);
	//mds = mod->mds = ri.Hunk_Alloc(size, h_low);

	//memcpy(mds, buffer, LittleLong2(pinmodel->ofsEnd));

	if (size > filesize)
	{
		common->Warning("R_LoadMDM: Header of %s is broken. Wrong filesize declared!\n", mod_name);
		return false;
	}

	//Fix all this, you can just memcpy and then fix it after
	LL(pinmodel->numSurfaces);

	dataSize += size;
	modelData = mdm = (mdmHeader_t*)Mem_Alloc(size);

	memcpy(mdm, buffer, LittleLong2(pinmodel->ofsEnd));

	// Copy all the values over from the file and fix endian issues in the process, if necessary.

	mdm->ident = LittleLong2(pinmodel->ident);
	mdm->version = pinmodel->version;	// Don't need to swap byte order on this one, we already did above.
	strcpy(mdm->name, pinmodel->name);
	mdm->numTags = LittleLong2(pinmodel->numTags);
	mdm->numSurfaces = pinmodel->numSurfaces;
	mdm->ofsTags = LittleLong2(pinmodel->ofsTags);
	mdm->ofsEnd = LittleLong2(pinmodel->ofsEnd);
	mdm->ofsSurfaces = LittleLong2(pinmodel->ofsSurfaces);

	// We don't care about the other offset values, we'll generate them ourselves while loading.

	if (LittleLong2(1) != 1) {

		// swap all the tags
		tag = (mdmTag_t*)((byte*)mdm + mdm->ofsTags);
		for (i = 0; i < mdm->numTags; i++) {
			int ii;
			for (ii = 0; ii < 3; ii++)
			{
				tag->axis[ii][0] = LittleFloat(tag->axis[ii][0]);
				tag->axis[ii][1] = LittleFloat(tag->axis[ii][1]);
				tag->axis[ii][2] = LittleFloat(tag->axis[ii][2]);
			}

			LL(tag->boneIndex);
			//tag->torsoWeight = LittleFloat( tag->torsoWeight );
			tag->offset[0] = LittleFloat(tag->offset[0]);
			tag->offset[1] = LittleFloat(tag->offset[1]);
			tag->offset[2] = LittleFloat(tag->offset[2]);

			LL(tag->numBoneReferences);
			LL(tag->ofsBoneReferences);
			LL(tag->ofsEnd);

			// swap the bone references
			boneref = (int*)((byte*)tag + tag->ofsBoneReferences);
			for (j = 0; j < tag->numBoneReferences; j++, boneref++) {
				*boneref = LittleLong2(*boneref);
			}

			// find the next tag
			tag = (mdmTag_t*)((byte*)tag + tag->ofsEnd);
		}
	}

	// swap all the surfaces
	surf = (mdmSurface_t*)((byte*)mdm + mdm->ofsSurfaces);
	for (i = 0; i < mdm->numSurfaces; i++) {
		if (LittleLong2(1) != 1) {
			LL(surf->ident);
			LL(surf->shaderIndex);
			LL(surf->minLod);
			LL(surf->ofsHeader);
			LL(surf->ofsCollapseMap);
			LL(surf->numTriangles);
			LL(surf->ofsTriangles);
			LL(surf->numVerts);
			LL(surf->ofsVerts);
			LL(surf->numBoneReferences);
			LL(surf->ofsBoneReferences);
			LL(surf->ofsEnd);
		}

		// change to surface identifier
		surf->ident = 10;

		/*
		if (surf->numVerts > SHADER_MAX_VERTEXES) {
			ri.Error(ERR_DROP, "R_LoadMDS: %s has more than %i verts on a surface (%i)",
				mod_name, SHADER_MAX_VERTEXES, surf->numVerts);
		}
		if (surf->numTriangles * 3 > SHADER_MAX_INDEXES) {
			ri.Error(ERR_DROP, "R_LoadMDS: %s has more than %i triangles on a surface (%i)",
				mod_name, SHADER_MAX_INDEXES / 3, surf->numTriangles);
		}
		*/

		// register the shaders
		//const char* materialName = va("%s%s", "m", surf->shader);
		const char* materialName = va("%s", surf->name);
		common->Printf("Material name %s\n", materialName);
		if (surf->shader[0]) {
			//sh = R_FindShader(surf->shader, LIGHTMAP_NONE, qtrue);
			sh = declManager->FindMaterial(materialName, true); //R_FindShader(surf->shader, LIGHTMAP_NONE, qtrue);
			if (sh == nullptr) {
				surf->shaderIndex = 0;
			} else {
				surf->shaderIndex = sh->Index();
			}
		} else {
			surf->shaderIndex = 0;
			common->Warning("Failed to find material %s\n", surf->shader);
		}
		materials.Append(sh);

		if (LittleLong2(1) != 1) {
			// swap all the triangles
			tri = (mdmTriangle_t*)((byte*)surf + surf->ofsTriangles);
			for (j = 0; j < surf->numTriangles; j++, tri++) {
				LL(tri->indexes[0]);
				LL(tri->indexes[1]);
				LL(tri->indexes[2]);
			}

			// swap all the vertexes
			v = (mdmVertex_t*)((byte*)surf + surf->ofsVerts);
			for (j = 0; j < surf->numVerts; j++) {
				v->normal[0] = LittleFloat(v->normal[0]);
				v->normal[1] = LittleFloat(v->normal[1]);
				v->normal[2] = LittleFloat(v->normal[2]);

				v->texCoords[0] = LittleFloat(v->texCoords[0]);
				v->texCoords[1] = LittleFloat(v->texCoords[1]);

				v->numWeights = LittleLong2(v->numWeights);

				for (k = 0; k < v->numWeights; k++) {
					v->weights[k].boneIndex = LittleLong2(v->weights[k].boneIndex);
					v->weights[k].boneWeight = LittleFloat(v->weights[k].boneWeight);
					v->weights[k].offset[0] = LittleFloat(v->weights[k].offset[0]);
					v->weights[k].offset[1] = LittleFloat(v->weights[k].offset[1]);
					v->weights[k].offset[2] = LittleFloat(v->weights[k].offset[2]);
				}

				v = (mdmVertex_t*)&v->weights[v->numWeights];
			}

			// swap the collapse map
			collapseMap = (int*)((byte*)surf + surf->ofsCollapseMap);
			for (j = 0; j < surf->numVerts; j++, collapseMap++) {
				*collapseMap = LittleLong2(*collapseMap);
			}

			// swap the bone references
			boneref = (int*)((byte*)surf + surf->ofsBoneReferences);
			for (j = 0; j < surf->numBoneReferences; j++, boneref++) {
				*boneref = LittleLong2(*boneref);
			}
		}

		// find the next surface
		surf = (mdmSurface_t*)((byte*)surf + surf->ofsEnd);
	}

	joints.SetGranularity(1);
	joints.SetNum(53);
	defaultPose.SetGranularity(1);
	defaultPose.SetNum(53);

	for (int i = 0; i < 53; i++) {
		if (i == 0) {
			joints[i].name = "origin";
			joints[i].parent = NULL;
		} else {
			joints[i].name = va("bone%d", i);
			joints[i].parent = &joints[0];
		}
	}

	//Defaultpose here for now - Dynamix

	return true;
}

/*
=================
idRenderModelMDM::IsDynamicModel
=================
*/
dynamicModel_t idRenderModelMDM::IsDynamicModel() const {
	return DM_CACHED;
}

/*
=================
idRenderModelMDM::TouchData
=================
*/
void idRenderModelMDM::TouchData() {

}

/*
=================
idRenderModelMDM::NumJoints
=================
*/
int	idRenderModelMDM::NumJoints(void) const {
	return joints.Num();
	//return modelData->numBones;
}

/*
=================
idRenderModelMDM::GetJoints
=================
*/
const idMD5Joint* idRenderModelMDM::GetJoints(void) const {
	if (!joints.Num()) {
		common->Error("Model has no Joints");
	}
	return &joints[0];
}

/*
=================
idRenderModelMDM::SetJoint
=================
*/
void idRenderModelMDM::SetJoint(int index, int parentNum, idStr name) {
	if (!joints.Num()) {
		common->Error("Model has no Joints");
	}
	if (index == 0) {
		joints[index].name = "origin";
		joints[index].parent = NULL;
	} else {
		joints[index].name = name;
		joints[index].parent = &joints[parentNum];
	}

	return;
}

/*
=================
idRenderModelMDM::GetJointHandle
=================
*/
jointHandle_t idRenderModelMDM::GetJointHandle(const char* name) const {
	const idMD5Joint* joint;
	int	i;

	joint = joints.Ptr();
	for (i = 0; i < joints.Num(); i++, joint++) {
		if (idStr::Icmp(joint->name.c_str(), name) == 0) {
			return (jointHandle_t)i;
		}
	}

	return INVALID_JOINT;
}

/*
=================
idRenderModelMDM::Bounds
=================
*/
idBounds idRenderModelMDM::Bounds(const class idRenderEntity* ent) const {
	return bounds;
}

/*
=================
idRenderModelMDM::Memory
=================
*/
int	idRenderModelMDM::Memory() const {
	return dataSize;
}

/*
=================
idRenderModelMDM::RenderFramesToModel
=================
*/
void idRenderModelMDM::RenderFramesToModel(idRenderModelStatic* staticModel, const struct viewDef_s* view, int scale, int frameNum, const renderEntity_t* ent) {
	int				i, j, k;
	float			frontlerp, backlerp;
	int*			triangles;
	int				indexes;
	int				baseIndex, baseVertex;
	int				numVerts;
	mdmVertex_t* v;
	mdmHeader_t* header;
	header = modelData;
	mdmSurface_t* surface = (mdmSurface_t*)((byte*)header + header->ofsSurfaces);

	baseIndex = 0;
	baseVertex = 0;

	for (int i = 0; i < header->numSurfaces; i++) {
		srfTriangles_t* tri = R_AllocStaticTriSurf();
		R_AllocStaticTriSurfVerts(tri, surface->numVerts);
		R_AllocStaticTriSurfIndexes(tri, surface->numTriangles * 3);
		tri->bounds.Clear();

		modelSurface_t	surf;

		surf.id = i;
		surf.geometry = tri;

		surf.shader = materials[i];

		// Set up all triangles.
		triangles = (int*)((byte*)surface + surface->ofsTriangles);
		indexes = surface->numTriangles * 3;
		for (j = 0; j < indexes; j++) {
			tri->indexes[baseIndex + j] = baseVertex + triangles[j];
		}

		tri->numIndexes += indexes;

		// Dynamix - investigate normals and shadows

		if (ent) {
			idJointMat bone2;
			//joint = ent2->joints;
			//common->Printf("Verts %d \n", surface->numVerts);
			numVerts = surface->numVerts;
			v = (mdmVertex_t*)((byte*)surface + surface->ofsVerts);
			for (j = 0; j < numVerts; j++) {
				idVec3	tempVert, tempNormal;
				mdmWeight_t* w;

				tempVert.Zero();
				tempNormal.Zero();
				w = v->weights;

				for (k = 0; k < v->numWeights; k++, w++) {
					idMat3 rotMatrix;


					bone2 = ent->joints[w->boneIndex];

					//for (int r = 0; r < 3; r++) {
					//	for (int c = 0; c < 3; c++) {
					//		rotMatrix[r][c] = bone2.ToMat3()[c][r];
					//	}
					//}

					rotMatrix = bone2.ToMat3().Transpose();

					//tempVert[0] += w->boneWeight * (DotProduct(joints2->ToMat3()[0], w->offset) + joints2->ToVec3().x);
					//tempVert[1] += w->boneWeight * (DotProduct(joints2->ToMat3()[1], w->offset) + joints2->ToVec3().y);
					//tempVert[2] += w->boneWeight * (DotProduct(joints2->ToMat3()[2], w->offset) + joints2->ToVec3().z);

					//Looks better for torso, think this might be correct
					tempVert[0] += w->boneWeight * (DotProduct(rotMatrix[0], w->offset) + bone2.ToVec3().x);
					tempVert[1] += w->boneWeight * (DotProduct(rotMatrix[1], w->offset) + bone2.ToVec3().y);
					tempVert[2] += w->boneWeight * (DotProduct(rotMatrix[2], w->offset) + bone2.ToVec3().z);

					tempNormal[0] += w->boneWeight * DotProduct(rotMatrix[0], v->normal);
					tempNormal[1] += w->boneWeight * DotProduct(rotMatrix[1], v->normal);
					tempNormal[2] += w->boneWeight * DotProduct(rotMatrix[2], v->normal);
				}

				tri->verts[baseVertex + j].xyz[0] = tempVert[0];
				tri->verts[baseVertex + j].xyz[1] = tempVert[1];
				tri->verts[baseVertex + j].xyz[2] = tempVert[2];

				tri->verts[baseVertex + j].normal[0] = tempNormal[0];
				tri->verts[baseVertex + j].normal[1] = tempNormal[1];
				tri->verts[baseVertex + j].normal[2] = tempNormal[2];

				//R_VaoPackNormal(tess.normal[baseVertex + j], tempNormal);

				tri->verts[baseVertex + j].st[0] = v->texCoords[0];
				tri->verts[baseVertex + j].st[1] = v->texCoords[1];

				tri->generateNormals = false;

				v = (mdmVertex_t*)&v->weights[v->numWeights];

			}

			//tess.numVertexes += surface->numVerts;
			tri->numVerts += surface->numVerts;

			//baseVertex += surface->numVerts;
			//baseIndex += surface->numTriangles * 3;

			R_BoundTriSurf(tri);
			R_DeriveTangents(tri);

			// Dynamix - model skins are different between Q3/Doom 3
			// Generally Q3 derivates use mesh based skinning vs Doom 3
			// texture based overrides and RTCW/ET don't provide a 
			// default value for the shader
			// Long term I will do something mesh based directly, for now
			// I'll just fill in a material name per surface based on surface name 
			// or index that I can override
			surf.shader = declManager->FindMaterial(surface->name);

			staticModel->AddSurface(surf);

			staticModel->bounds.AddPoint(surf.geometry->bounds[0]);
			staticModel->bounds.AddPoint(surf.geometry->bounds[1]);

			surface = (mdmSurface_t*)((byte*)surface + surface->ofsEnd);
		}
	}
}

/*
====================
idRenderModelMDM::DrawJoints
====================
*/
void idRenderModelMDM::DrawJoints(const renderEntity_t* ent, const struct viewDef_s* view) const {
	int					i;
	int					num;
	idVec3				pos;
	const idJointMat* joint;
	const idMD5Joint* md5Joint;
	int					parentNum;

	num = ent->numJoints;
	joint = ent->joints;
	md5Joint = joints.Ptr();
	for (i = 0; i < num; i++, joint++, md5Joint++) {
		pos = ent->origin + joint->ToVec3() * ent->axis;
		if (md5Joint->parent) {
			parentNum = md5Joint->parent - joints.Ptr();
			session->rw->DebugLine(colorWhite, ent->origin + ent->joints[parentNum].ToVec3() * ent->axis, pos);
		}

		session->rw->DebugLine(colorRed, pos, pos + joint->ToMat3()[0] * 2.0f * ent->axis);
		session->rw->DebugLine(colorGreen, pos, pos + joint->ToMat3()[1] * 2.0f * ent->axis);
		session->rw->DebugLine(colorBlue, pos, pos + joint->ToMat3()[2] * 2.0f * ent->axis);
	}

	idBounds bounds;

	bounds.FromTransformedBounds(ent->bounds, vec3_zero, ent->axis);
	session->rw->DebugBounds(colorMagenta, bounds, ent->origin);

	if ((r_jointNameScale.GetFloat() != 0.0f) && (bounds.Expand(128.0f).ContainsPoint(view->renderView.vieworg - ent->origin))) {
		idVec3	offset(0, 0, r_jointNameOffset.GetFloat());
		float	scale;

		scale = r_jointNameScale.GetFloat();
		joint = ent->joints;
		num = ent->numJoints;
		for (i = 0; i < num; i++, joint++) {
			pos = ent->origin + joint->ToVec3() * ent->axis;
			session->rw->DrawText(joints[i].name, pos + offset, scale, colorWhite, view->renderView.viewaxis, 1);
		}
	}
}

/*
=================
idRenderModelMDM::InstantiateDynamicModel
=================
*/
idRenderModel* idRenderModelMDM::InstantiateDynamicModel(const struct renderEntity_s* ent, const struct viewDef_s* view, idRenderModel* cachedModel) {
	idRenderModelStatic* staticModel;
	if (cachedModel) {
		delete cachedModel;
		cachedModel = NULL;
	}

	if (purged) {
		common->DWarning("model %s instantiated while purged", Name());
		LoadModel();
	}

	if (!ent->joints) {
		common->Printf("idRenderModelMDM::InstantiateDynamicModel: NULL joints on renderEntity for '%s'\n", Name());
		delete cachedModel;
		return NULL;
	} else if (ent->numJoints != joints.Num()) {
		common->Printf("idRenderModelMDM::InstantiateDynamicModel: renderEntity has different number of joints than model for '%s'\n", Name());
		delete cachedModel;
		return NULL;
	}

	int scale = 1;

	staticModel = new idRenderModelStatic;
	staticModel->bounds.Clear();

	// Dynamix - clean up the parms and/or just merge this back into here
	// Dynamix ent is never null for some reason even if there's no anim, no idea how that happens, 
	// Was fine in the MDR version too, very odd
	
	if (ent->joints) {
		// Dynamix - I was using this to manually animate stuff for testing
		//RenderFramesToModel(staticModel, NULL, view, material, scale, ent->shaderParms[SHADERPARM_MD3_FRAME]);
		RenderFramesToModel(staticModel, view, scale, 0, ent);
	} else {
		RenderFramesToModel(staticModel, view, scale, 1, NULL);
	}
	
	if (r_showSkel.GetInteger()) {
		if ((view != NULL) && (!r_skipSuppress.GetBool() || !ent->suppressSurfaceInViewID || (ent->suppressSurfaceInViewID != view->renderView.viewID))) {
			// only draw the skeleton
			DrawJoints(ent, view);
		}
	}

	return staticModel;
}


/*
=================
idRenderModelMDM::GetJointName
=================
*/
const char* idRenderModelMDM::GetJointName(jointHandle_t handle) const {
	if ((handle < 0) || (handle >= joints.Num())) {
		return "<invalid joint>";
	}

	return joints[handle].name;
}

/*
=================
idRenderModelMDM::GetDefaultPose
=================
*/
const idJointQuat* idRenderModelMDM::GetDefaultPose(void) const {
	return defaultPose.Ptr();
}
/*
=================
idRenderModelMDM::NearestJoint
=================
*/
int	idRenderModelMDM::NearestJoint(int surfaceNum, int a, int b, int c) const {
	return 0;
}