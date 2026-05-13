// Model_mdr.cpp
//

#include "sys/platform.h"
#include "framework/Session.h"
#include "renderer/tr_local.h"
#include "renderer/Model_local.h"

int		LittleLong(int l) { return l; }
/*
========================
rvmRenderModelMDR::rvmRenderModelMDR
========================
*/
rvmRenderModelMDR::rvmRenderModelMDR() {
	dataSize = 0;
	modelData = nullptr;
	cachedModel = nullptr;
	cFrame = 0;
}

/*
========================
rvmRenderModelMDR::~rvmRenderModelMDR
========================
*/
rvmRenderModelMDR::~rvmRenderModelMDR() {
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
rvmRenderModelMDR::InitFromFile
========================
*/
void rvmRenderModelMDR::InitFromFile(const char *fileName) {
	name = fileName;
	LoadModel();
}

/*
========================
rvmRenderModelMDR::LoadModel
========================
*/
void rvmRenderModelMDR::LoadModel(void) {
	void *mdrMeshBuffer;
	int mdrMeshLen;

	if (!purged) {
		PurgeModel();
	}
	purged = false;

	materials.Clear();

	// Try and load the file off disk.
	mdrMeshLen = fileSystem->ReadFile(name, &mdrMeshBuffer);
	if (mdrMeshLen <= 0 || mdrMeshBuffer == nullptr)
	{
		common->Warning("Failed to load MDR mesh %s\n", name.c_str());
		MakeDefaultModel();
		return;
	}

	// Parse the MDR file.
	if (!LoadMDRFile(mdrMeshBuffer, mdrMeshLen))
	{
		MakeDefaultModel();
		fileSystem->FreeFile(mdrMeshBuffer);
		return;
	}

	// Convert the anims to md5 if it doesn't exist
	WriteAnimToDisk();

	// If we don't have any frames, then render as a static mesh.
	//if (modelData->numFrames == 1)
	{
		viewDef_t view;
		RenderFramesToModel(this, NULL, &view, NULL, 1, 0, NULL);
		
	}

	// Free the memory from reading in the ifle.
	fileSystem->FreeFile(mdrMeshBuffer);
}

/*
========================
rvmRenderModelMDR::LoadMDRFile
========================
*/
bool rvmRenderModelMDR::LoadMDRFile(void *buffer, int filesize) {
	int					i, j, k, l;
	mdrHeader_t			*pinmodel, *mdr;
	mdrFrame_t			*frame;
	mdrLOD_t			*lod, *curlod;
	mdrSurface_t			*surf, *cursurf;
	mdrTriangle_t			*tri, *curtri;
	mdrVertex_t			*v, *curv;
	mdrWeight_t			*weight, *curweight;
	mdrTag_t			*tag, *curtag;
	int					size;
	const idMaterial	*sh;

	const char *mod_name = name.c_str();

	idStr shaderPrefix = name;
	shaderPrefix.StripFileExtension();

	bounds.Clear();

	pinmodel = (mdrHeader_t *)buffer;

	//pinmodel->version = LittleLong(pinmodel->version);
	if (pinmodel->version != MDR_VERSION)
	{
		common->Warning("R_LoadMDR: %s has wrong version (%i should be %i)\n", mod_name, pinmodel->version, MDR_VERSION);
		return false;
	}

	//size = LittleLong(pinmodel->ofsEnd);
	size = LittleLong(pinmodel->ofsEnd);

	if (size > filesize)
	{
		common->Warning("R_LoadMDR: Header of %s is broken. Wrong filesize declared!\n", mod_name);
		return false;
	}

	LL(pinmodel->numFrames);
	LL(pinmodel->numBones);
	LL(pinmodel->ofsFrames);

	// This is a model that uses some type of compressed Bones. We don't want to uncompress every bone for each rendered frame
	// over and over again, we'll uncompress it in this function already, so we must adjust the size of the target mdr.
	if (pinmodel->ofsFrames < 0)
	{
		// mdrFrame_t is larger than mdrCompFrame_t:
		size += pinmodel->numFrames * sizeof(frame->name);
		// now add enough space for the uncompressed bones.
		size += pinmodel->numFrames * pinmodel->numBones * ((sizeof(mdrBone_t) - sizeof(mdrCompBone_t)));
	}

	// simple bounds check
	if (pinmodel->numBones < 0 ||
		sizeof(*mdr) + pinmodel->numFrames * (sizeof(*frame) + (pinmodel->numBones - 1) * sizeof(*frame->bones)) > size)
	{
		common->Warning("R_LoadMDR: %s has broken structure.\n", mod_name);
		return false;
	}

	dataSize += size;
	modelData = mdr = (mdrHeader_t *)Mem_Alloc(size);

	// Copy all the values over from the file and fix endian issues in the process, if necessary.

	mdr->ident = LittleLong(pinmodel->ident);
	mdr->version = pinmodel->version;	// Don't need to swap byte order on this one, we already did above.
	strcpy(mdr->name, pinmodel->name);
	mdr->numFrames = pinmodel->numFrames;
	mdr->numBones = pinmodel->numBones;
	mdr->numLODs = LittleLong(pinmodel->numLODs);
	mdr->numTags = LittleLong(pinmodel->numTags);
	// We don't care about the other offset values, we'll generate them ourselves while loading.

	numLods = mdr->numLODs;

	if (mdr->numFrames < 1)
	{
		common->Warning("R_LoadMDR: %s has no frames\n", mod_name);
		return false;
	}

	/* The first frame will be put into the first free space after the header */
	frame = (mdrFrame_t *)(mdr + 1);
	mdr->ofsFrames = (int)((byte *)frame - (byte *)mdr);

	if (pinmodel->ofsFrames < 0)
	{
		mdrCompFrame_t *cframe;

		// compressed model...				
		cframe = (mdrCompFrame_t *)((byte *)pinmodel - pinmodel->ofsFrames);

		for (i = 0; i < mdr->numFrames; i++)
		{
			for (j = 0; j < 3; j++)
			{
				frame->bounds[0][j] = LittleFloat(cframe->bounds[0][j]);
				frame->bounds[1][j] = LittleFloat(cframe->bounds[1][j]);
				frame->localOrigin[j] = LittleFloat(cframe->localOrigin[j]);
			}

			if (i == 0) {
				bounds[0] = frame->bounds[0];
				bounds[1] = frame->bounds[1];
			}

			frame->radius = LittleFloat(cframe->radius);
			frame->name[0] = '\0';	// No name supplied in the compressed version.

			for (j = 0; j < mdr->numBones; j++)
			{
				for (k = 0; k < (sizeof(cframe->bones[j].Comp) / 2); k++)
				{
					// Do swapping for the uncompressing functions. They seem to use shorts
					// values only, so I assume this will work. Never tested it on other
					// platforms, though.

					((unsigned short *)(cframe->bones[j].Comp))[k] =
						LittleShort(((unsigned short *)(cframe->bones[j].Comp))[k]);
				}

				/* Now do the actual uncompressing */
				MC_UnCompress(frame->bones[j].matrix, cframe->bones[j].Comp);
			}

			// Next Frame...
			cframe = (mdrCompFrame_t *)&cframe->bones[j];
			frame = (mdrFrame_t *)&frame->bones[j];
		}
	}
	else
	{
		mdrFrame_t *curframe;

		// uncompressed model...
		//

		curframe = (mdrFrame_t *)((byte *)pinmodel + pinmodel->ofsFrames);

		// swap all the frames
		for (i = 0; i < mdr->numFrames; i++)
		{
			for (j = 0; j < 3; j++)
			{
				frame->bounds[0][j] = LittleFloat(curframe->bounds[0][j]);
				frame->bounds[1][j] = LittleFloat(curframe->bounds[1][j]);
				frame->localOrigin[j] = LittleFloat(curframe->localOrigin[j]);
			}

			if(i == 0) {
				bounds[0] = frame->bounds[0];
				bounds[1] = frame->bounds[1];
			}

			frame->radius = LittleFloat(curframe->radius);
			strcpy(frame->name, curframe->name);

			for (j = 0; j < (int)(mdr->numBones * sizeof(mdrBone_t) / 4); j++)
			{
				((float *)frame->bones)[j] = LittleFloat(((float *)curframe->bones)[j]);
			}

			curframe = (mdrFrame_t *)&curframe->bones[mdr->numBones];
			frame = (mdrFrame_t *)&frame->bones[mdr->numBones];
		}
	}

	// frame should now point to the first free address after all frames.
	lod = (mdrLOD_t *)frame;
	mdr->ofsLODs = (int)((byte *)lod - (byte *)mdr);

	curlod = (mdrLOD_t*)((byte*)pinmodel + LittleLong(pinmodel->ofsLODs));

	// swap all the LOD's
	for (l = 0; l < mdr->numLODs; l++)
	{
		// simple bounds check
		if ((byte*)(lod + 1) > (byte*)mdr + size)
		{
			common->Warning("R_LoadMDR: %s has broken structure.\n", mod_name);
			return false;
		}

		lod->numSurfaces = LittleLong(curlod->numSurfaces);

		// swap all the surfaces
		surf = (mdrSurface_t*)(lod + 1);
		lod->ofsSurfaces = (int)((byte*)surf - (byte*)lod);
		cursurf = (mdrSurface_t*)((byte*)curlod + LittleLong(curlod->ofsSurfaces));

		for (i = 0; i < lod->numSurfaces; i++)
		{
			// simple bounds check
			if ((byte*)(surf + 1) > (byte*)mdr + size)
			{
				common->Warning("R_LoadMDR: %s has broken structure.\n", mod_name);
				return false;
			}

			//Hack to get shader working while I work out what's going on Dynamix
			char* shaderPtr = (char*)cursurf + 69;
			// first do some copying stuff
			strcpy(surf->name, cursurf->name);
			strcpy(surf->shader, shaderPtr);

			surf->ofsHeader = (byte*)mdr - (byte*)surf;

			surf->numVerts = LittleLong(cursurf->numVerts);
			surf->numTriangles = LittleLong(cursurf->numTriangles);
			// numBoneReferences and BoneReferences generally seem to be unused

			// lowercase the surface name so skin compares are faster
			strlwr(surf->name);

			// register the shaders
			//const char* materialName = va("%s/%s", shaderPrefix.c_str(), surf->shader);
			//Hack for this missing m thing Dynamix
			const char* materialName = va("%s%s", "m", surf->shader);
			sh = declManager->FindMaterial(materialName, true); //R_FindShader(surf->shader, LIGHTMAP_NONE, qtrue);
			if (sh != nullptr)
			{
				surf->shaderIndex = sh->Index();
			}
			else
			{
				surf->shaderIndex = 0;
				common->Warning("Failed to find material %s\n", materialName);
			}

			materials.Append(sh);

			// now copy the vertexes.
			v = (mdrVertex_t*)(surf + 1);
			surf->ofsVerts = (int)((byte*)v - (byte*)surf);
			curv = (mdrVertex_t*)((byte*)cursurf + LittleLong(cursurf->ofsVerts));

			for (j = 0; j < surf->numVerts; j++)
			{
				LL(curv->numWeights);

				// simple bounds check
				if (curv->numWeights < 0 || (byte*)(v + 1) + (curv->numWeights - 1) * sizeof(*weight) >(byte*) mdr + size)
				{
					common->Warning("R_LoadMDR: %s has broken structure.\n", mod_name);
					return false;
				}

				v->normal[0] = LittleFloat(curv->normal[0]);
				v->normal[1] = LittleFloat(curv->normal[1]);
				v->normal[2] = LittleFloat(curv->normal[2]);

				v->texCoords[0] = LittleFloat(curv->texCoords[0]);
				v->texCoords[1] = LittleFloat(curv->texCoords[1]);

				v->numWeights = curv->numWeights;
				weight = &v->weights[0];
				curweight = &curv->weights[0];

				// Now copy all the weights
				for (k = 0; k < v->numWeights; k++)
				{
					weight->boneIndex = LittleLong(curweight->boneIndex);
					weight->boneWeight = LittleFloat(curweight->boneWeight);

					weight->offset[0] = LittleFloat(curweight->offset[0]);
					weight->offset[1] = LittleFloat(curweight->offset[1]);
					weight->offset[2] = LittleFloat(curweight->offset[2]);

					weight++;
					curweight++;
				}

				v = (mdrVertex_t*)weight;
				curv = (mdrVertex_t*)curweight;
			}

			// we know the offset to the triangles now:
			tri = (mdrTriangle_t*)v;
			surf->ofsTriangles = (int)((byte*)tri - (byte*)surf);
			curtri = (mdrTriangle_t*)((byte*)cursurf + LittleLong(cursurf->ofsTriangles));

			// simple bounds check
			if (surf->numTriangles < 0 || (byte*)(tri + surf->numTriangles) >(byte*) mdr + size)
			{
				common->Warning("R_LoadMDR: %s has broken structure.\n", mod_name);
				return false;
			}

			for (j = 0; j < surf->numTriangles; j++)
			{
				tri->indexes[0] = LittleLong(curtri->indexes[0]);
				tri->indexes[1] = LittleLong(curtri->indexes[1]);
				tri->indexes[2] = LittleLong(curtri->indexes[2]);

				tri++;
				curtri++;
			}

			// tri now points to the end of the surface.
			surf->ofsEnd = (byte*)tri - (byte*)surf;
			surf = (mdrSurface_t*)tri;

			// find the next surface.
			cursurf = (mdrSurface_t*)((byte*)cursurf + LittleLong(cursurf->ofsEnd));
		}

		// surf points to the next lod now.
		lod->ofsEnd = (int)((byte *)surf - (byte *)lod);
		lod = (mdrLOD_t *)surf;

		// find the next LOD.
		curlod = (mdrLOD_t *)((byte *)curlod + LittleLong(curlod->ofsEnd));
	}

	// lod points to the first tag now, so update the offset too.
	tag = (mdrTag_t *)lod;
	mdr->ofsTags = (int)((byte *)tag - (byte *)mdr);
	curtag = (mdrTag_t *)((byte *)pinmodel + LittleLong(pinmodel->ofsTags));

	// simple bounds check
	if (mdr->numTags < 0 || (byte *)(tag + mdr->numTags) >(byte *) mdr + size)
	{
		common->Warning("R_LoadMDR: %s has broken structure.\n", mod_name);
		return false;
	}

	for (i = 0; i < mdr->numTags; i++)
	{
		tag->boneIndex = LittleLong(curtag->boneIndex);
		strcpy(tag->name, curtag->name);

		tag++;
		curtag++;
	}

	// And finally we know the real offset to the end.
	mdr->ofsEnd = (int)((byte *)tag - (byte *)mdr);

	// Store the pointers for each frame, so we have it for faster lookup.
	for(int frameId = 0; frameId < mdr->numFrames; frameId++)
	{
		mdrHeader_t *header;
		mdrFrame_t *frame;
	
		header = (mdrHeader_t *)modelData;
		int frameSize = (size_t)(&((mdrFrame_t *)0)->bones[header->numBones]);
	
		frame = (mdrFrame_t *)((byte *)header + header->ofsFrames + frameId * frameSize);
		frames.Append(frame);
	}

	//Update joints here for now - Dynamix
	mdrHeader_t *header = (mdrHeader_t*)modelData;
	joints.SetGranularity(1);
	joints.SetNum(header->numBones);
	defaultPose.SetGranularity(1);
	defaultPose.SetNum(header->numBones);

	
	for (int i = 0; i < header->numBones; i++) {
		if (i == 0) {
			joints[i].name = "origin";
			joints[i].parent = NULL;
		} else {
			joints[i].name = va("bone%d", i);
			//common->Printf("Bone name %s\n", joints[i].name.c_str());
			joints[i].parent = &joints[0];
		}
	}

	common->Printf("Numtags %d", header->numTags);
	tag = (mdrTag_t*)((byte*)header + header->ofsTags);
	for (i = 0; i < header->numTags; i++) {
		common->Printf("Numtags %d, %s\n", tag->boneIndex, tag->name);
		tag++;
	}
	
	//Defaultpose here for now - Dynamix

	return true;
}

/*
=================
idRenderModelMDR::IsDynamicModel
=================
*/
dynamicModel_t rvmRenderModelMDR::IsDynamicModel() const {
	return DM_CACHED;
}

/*
=================
idRenderModelMDR::TouchData
=================
*/
void rvmRenderModelMDR::TouchData() {

}

/*
=================
idRenderModelMDR::NumJoints
=================
*/
int	rvmRenderModelMDR::NumJoints(void) const {
	return modelData->numBones;
}

/*
=================
idRenderModelMDR::GetJoints
=================
*/
const idMD5Joint *rvmRenderModelMDR::GetJoints(void) const {
	if (!joints.Num()) {
		common->Error("Model has no Joints");
	}
	return &joints[0];
}

/*
=================
idRenderModelMDR::GetJointHandle
=================
*/
jointHandle_t rvmRenderModelMDR::GetJointHandle(const char *name) const {
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
idRenderModelMDR::Bounds
=================
*/
idBounds rvmRenderModelMDR::Bounds(const class idRenderEntity*ent) const {
	return bounds;
}

/*
=================
idRenderModelMDR::Memory
=================
*/
int	rvmRenderModelMDR::Memory() const {
	return dataSize;
}

/*
=================
rvmRenderModelMDR::RenderFramesToModel
=================
*/
void rvmRenderModelMDR::RenderFramesToModel(idRenderModelStatic *staticModel, const struct idRenderEntityParms *ent, const struct viewDef_s *view, const idMaterial *overrideMaterial, int scale, int frameNum, const renderEntity_t* ent2) {
	int				i, j, k;
	float			frontlerp, backlerp;
	int				*triangles;
	int				indexes;
	int				baseIndex, baseVertex;
	int				numVerts;
	mdrVertex_t		*v;
	mdrHeader_t		*header;
	mdrFrame_t		*frame;
	mdrFrame_t		*oldFrame;
	mdrBone_t		bones[MDR_MAX_BONES], *bonePtr, *bone;

	int			frameSize;

	// Dynamix - clean this up, it's vaguely useful for frame by frame stuff/debugging and maybe
	// if it's used as a static and you want a specific frame?
	int currentFrame;
	if (ent2) {
		currentFrame = 0;
	} else {
		currentFrame = frameNum;
	}
	int oldFrameNum = currentFrame - 1;
	// Dynamix - check if it's worth keeping this at all and just rip out if not
	// lerping is handled by bones and I don't think EF uses mdr for non-char models
	// don't lerp if lerping off, or this is the only frame, or the last frame...
	//
	//if (backEnd.currentEntity->e.oldframe == backEnd.currentEntity->e.frame)
	//{
		backlerp = 0;	// if backlerp is 0, lerping is off and frontlerp is never used
		frontlerp = 1;
	//}
	//else
	//{
	//	backlerp = backEnd.currentEntity->e.backlerp;
	//	frontlerp = 1.0f - backlerp;
	//}

	header = modelData;

	frameSize = (size_t)(&((mdrFrame_t *)0)->bones[header->numBones]);
	frame = frames[currentFrame];
	//frame = (mdrFrame_t *)((byte *)header + header->ofsFrames + currentFrame * frameSize);
	oldFrame = (mdrFrame_t *)((byte *)header + header->ofsFrames + oldFrameNum * frameSize);

	// Dynamix
	// Now correctly prints the md5anim version of joints
	// TODO add boiler plate anim file lines then move out to own function and actually write the file
	if (0) {
		int num2 = header->numBones;
		idJointMat originBone;
		int counter;

		for (counter = 0; counter < num2; counter++) {
			idJointMat testJointMat;
			idMat3 rotMatrix;

			for (int r = 0; r < 3; r++) {
				for (int c = 0; c < 3; c++) {
					rotMatrix[r][c] = frame->bones[counter].matrix[c][r];
				}
			}

			idVec3 translationVec(frame->bones[counter].matrix[0][3], frame->bones[counter].matrix[1][3], frame->bones[counter].matrix[2][3]);

			testJointMat.SetRotation(rotMatrix);
			testJointMat.SetTranslation(translationVec);
			if (counter == 0 ) {
				originBone = testJointMat;
			}
			else {
				testJointMat /= originBone;
			}

			idJointQuat JointQuat = testJointMat.ToJointQuat();
			idCQuat cQuat = JointQuat.q.ToCQuat();
			common->DPrintf("( %f %f %f ) ( %f %f %f )\n", JointQuat.t.x, JointQuat.t.y, JointQuat.t.z, cQuat.x, cQuat.y, cQuat.z);
		}

	}
	
	// Dynamix
	// Essentially draw joints for internal mdr bones
	// Keep but move to function later, was useful for debugging
	/*
	if (ent2 && r_showSkel.GetInteger()) {
	int num = header->numBones;
	idVec3	pos, pos0;
	const mdrBone_t* joint = frame->bones;
	//const idMD5Joint* md5Joint = joints.Ptr();
	pos0.x = joint[0].matrix[0][3];
	pos0.y = joint[0].matrix[1][3];
	pos0.z = joint[0].matrix[2][3];
	//pos0 = pos0;
	//ent->origin + ent->joints[parentNum].ToVec3() * ent->axis
	pos0 = ent2->origin + pos0 * ent2->axis;
	float scale;
	scale = r_jointNameScale.GetFloat();
		for (i = 0; i < num; i++) {
			//pos = ent2->origin + joint->ToVec3() * ent2->axis;
			pos.x = joint[i].matrix[0][3];
			pos.y = joint[i].matrix[1][3];
			pos.z = joint[i].matrix[2][3];
			pos = ent2->origin + pos * ent2->axis;
			//pos = pos;
			//if (md5Joint->parent) {
			//session->rw->DebugLine(colorWhite, ent2->origin + ent2->joints[parentNum].ToVec3() * ent->axis, pos);
			session->rw->DebugLine(colorWhite, pos0 , pos);
			session->rw->DrawText(joints[i].name, pos, scale, colorWhite, view->renderView.viewaxis, 1);
			//}
		}
	}
	*/

	mdrLOD_t *lod = (mdrLOD_t *)((byte *)header + header->ofsLODs);
	mdrSurface_t *surface = (mdrSurface_t *)((byte *)lod + lod->ofsSurfaces);

	// If we have a render entity copy the joints over to the mdr model for animation
	if (ent2) {

		const idJointMat* joint;
		joint = ent2->joints;
		int num = ent2->numJoints;
		int l;

		for (l = 0; l < num; l++) {

			//Long form for reference
			// Row 0
			//frame->bones[l].matrix[0][0] = m[0];
			//frame->bones[l].matrix[0][1] = m[1];
			//frame->bones[l].matrix[0][2] = m[2];
			//frame->bones[l].matrix[0][3] = m[3];
			//frame->bones[l].matrix[0][3] = pos.x;

			// Row 1
			//frame->bones[l].matrix[1][0] = m[4];
			//frame->bones[l].matrix[1][1] = m[5];
			//frame->bones[l].matrix[1][2] = m[6];
			//frame->bones[l].matrix[1][3] = m[7];
			//frame->bones[l].matrix[1][3] = pos.y;

			// Row 2
			//frame->bones[l].matrix[2][0] = m[8];
			//frame->bones[l].matrix[2][1] = m[9];
			//frame->bones[l].matrix[2][2] = m[10];
			//frame->bones[l].matrix[2][3] = m[11];
			//frame->bones[l].matrix[2][3] = pos.z;
			
			memcpy(frame->bones, ent2->joints[0].ToFloatPtr(), ent2->numJoints * sizeof(mdrBone_t));
		}
	}


	baseIndex = 0;
	baseVertex = 0;
	for (int i = 0; i < lod->numSurfaces; i++)
	{
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
		for (j = 0; j < indexes; j++)
		{
			tri->indexes[baseIndex + j] = baseVertex + triangles[j];
		}
		tri->numIndexes += indexes;

		//
		// lerp all the needed bones
		//
		if (!backlerp) {
			// no lerping needed
			bonePtr = frame->bones;
		} else {
			bonePtr = bones;

			for (i = 0; i < header->numBones * 12; i++)
			{
				((float *)bonePtr)[i] = frontlerp * ((float *)frame->bones)[i] + backlerp * ((float *)oldFrame->bones)[i];
			}
		}

		// Dynamix - investigate normals and shadows
		//
		// deform the vertexes by the lerped bones
		//
		numVerts = surface->numVerts;
		v = (mdrVertex_t *)((byte *)surface + surface->ofsVerts);

		for (j = 0; j < numVerts; j++) {
			idVec3	tempVert, tempNormal;
			mdrWeight_t	*w;

			tempVert.Zero();
			tempNormal.Zero();
			w = v->weights;
			for (k = 0; k < v->numWeights; k++, w++) {
				bone = bonePtr + w->boneIndex;

				tempVert[0] += w->boneWeight * (DotProduct(bone->matrix[0], w->offset) + bone->matrix[0][3]);
				tempVert[1] += w->boneWeight * (DotProduct(bone->matrix[1], w->offset) + bone->matrix[1][3]);
				tempVert[2] += w->boneWeight * (DotProduct(bone->matrix[2], w->offset) + bone->matrix[2][3]);

				tempNormal[0] += w->boneWeight * DotProduct(bone->matrix[0], v->normal);
				tempNormal[1] += w->boneWeight * DotProduct(bone->matrix[1], v->normal);
				tempNormal[2] += w->boneWeight * DotProduct(bone->matrix[2], v->normal);
			}

			tri->verts[baseVertex + j].xyz[0] = tempVert[0];
			tri->verts[baseVertex + j].xyz[1] = tempVert[1];
			tri->verts[baseVertex + j].xyz[2] = tempVert[2];

			//R_VaoPackNormal(tess.normal[baseVertex + j], tempNormal);

			tri->verts[baseVertex + j].st[0] = v->texCoords[0];
			tri->verts[baseVertex + j].st[1] = v->texCoords[1];

			v = (mdrVertex_t *)&v->weights[v->numWeights];
		}

		//tess.numVertexes += surface->numVerts;
		tri->numVerts += surface->numVerts;

		//baseVertex += surface->numVerts;
		//baseIndex += surface->numTriangles * 3;

		R_BoundTriSurf(tri);

		staticModel->AddSurface(surf);
		staticModel->bounds.AddPoint(surf.geometry->bounds[0]);
		staticModel->bounds.AddPoint(surf.geometry->bounds[1]);

		surface = (mdrSurface_t *)((byte *)surface + surface->ofsEnd);
	}
}

/*
====================
rvmRenderModelMDR::DrawJoints
====================
*/
void rvmRenderModelMDR::DrawJoints(const renderEntity_t* ent, const struct viewDef_s* view) const {
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
rvmRenderModelMDR::InstantiateDynamicModel
=================
*/
idRenderModel *rvmRenderModelMDR::InstantiateDynamicModel(const struct renderEntity_s* ent, const struct viewDef_s* view, idRenderModel* cachedModel) {
	idRenderModelStatic	*staticModel;
	class idRenderEntityLocal* renderEntity = (class idRenderEntityLocal*)ent;

	// Dynamix - bring the md5 stuff over for this
	if (cachedModel) {
		delete cachedModel;
		cachedModel = NULL;
	}

	if (purged) {
		common->DWarning("model %s instantiated while purged", Name());
		LoadModel();
	}

	staticModel = new idRenderModelStatic;
	staticModel->bounds.Clear();

	// Dynamix - unused and I don't know if EF does any scaling, look into it
	int scale = 1; // ent->scale;
	//if (scale == 0)
	//	scale = 1;

	// Dynamix - clean up the parms and/or just merge this back into here 
	if (ent != NULL) {
		//const idMaterial* material = renderEntity->GetCustomShader();
		const idMaterial* material = NULL;
		//idRenderEntityParms *test = renderEntity->parms;
		// Dynamix - I was using this to manually animate stuff for testing
		//RenderFramesToModel(staticModel, NULL, view, material, scale, ent->shaderParms[SHADERPARM_MD3_FRAME]);
		RenderFramesToModel(staticModel, NULL, view, material, scale, 0, ent);
	} else {
		RenderFramesToModel(staticModel, NULL, view, NULL, scale, 1, NULL);
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
idRenderModelMDR::GetJointHandle
=================
*/
const char *		rvmRenderModelMDR::GetJointName(jointHandle_t handle) const {
	return "not_implemented";
}

/*
=================
idRenderModelMDR::GetDefaultPose
=================
*/
const idJointQuat *	rvmRenderModelMDR::GetDefaultPose(void) const {
	return defaultPose.Ptr();
}
/*
=================
idRenderModelMDR::NearestJoint
=================
*/
int	rvmRenderModelMDR::NearestJoint(int surfaceNum, int a, int b, int c) const {
	return 0;
}

/*
=================
idRenderModelMDS::WriteAnimToDisk
=================
*/
bool rvmRenderModelMDR::WriteAnimToDisk() {
	idFile* fp;
	int frameSize, numFrames, numAnims, numBones;
	mdrHeader_t* header;
	mdrFrame_t* frame;

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

	header = (mdrHeader_t*)modelData;
	numBones = header->numBones;
	numFrames = header->numFrames;

	frame = frames[0];

	// FIXME - hack to make sure we're not reading the wrong set of anims right now
	for (int i = numAnims - 1; i > 0 ; i--) {
		//int startFrame = animInfo[c].animInfo[0];
		//int numFrames = animInfo[c].animInfo[1];
		int x = animInfo[i].animInfo[0] + animInfo[i].animInfo[1];
		if (x > numFrames) {
			animInfo.RemoveIndex(i);
		}
	}

	numAnims = animInfo.Num();

	idList <idJointMat> boneData;
	boneData.SetGranularity(1);
	boneData.SetNum(numBones);

	CalcBones(boneData, 0);

	// FIXME  - Add checks for 0/null animations and if so then just make one anim file called main or all or whatever
	for (int c = 0; c < numAnims; c++) {
		idStr animName;
		int startFrame = animInfo[c].animInfo[0];
		int numFrames = animInfo[c].animInfo[1];


		name.ExtractFilePath(animName);
		animName.Append(animInfo[c].animName);
		animName.Append(".md5anim");

		common->Printf("Writing %s\n", animName.c_str());
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
			if (i == 0) {
				fp->WriteFloatString("\t\"origin\" -1 63 0\n");
			}
			else {
				fp->WriteFloatString("\t\"%s\" 0 63 %d\n", joints[i].name.c_str(), i * 6);
			}
		}

		fp->WriteFloatString("}\n\n");
		fp->WriteFloatString("bounds {\n");
		for (int i = 0, j = startFrame; i < numFrames; i++, j++) {
			frame = frames[j];

			fp->WriteFloatString("( %f %f %f ) ( %f %f %f )\n", frame->bounds[0].x, frame->bounds[0].y, frame->bounds[0].z, frame->bounds[1].x, frame->bounds[1].y, frame->bounds[1].z);
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
void rvmRenderModelMDR::CalcBones(idList<idJointMat>& boneData, int currentFrame) {
	int numBones;
	mdrHeader_t* header;
	mdrFrame_t* frame;

	header = (mdrHeader_t*)modelData;
	numBones = header->numBones;

	frame = frames[currentFrame];

	for (int i = 0; i < numBones; i++) {
		idMat3 rotMatrix;

		for (int r = 0; r < 3; r++) {
			for (int c = 0; c < 3; c++) {
				rotMatrix[r][c] = frame->bones[i].matrix[c][r];
			}
		}

		idVec3 translationVec(frame->bones[i].matrix[0][3], frame->bones[i].matrix[1][3], frame->bones[i].matrix[2][3]);

		boneData[i].SetRotation(rotMatrix);
		boneData[i].SetTranslation(translationVec);
	}

	// In Elite Force they just parent everything to the origin bone, but may as well do this anyway 
	// just in case somebody wants to use it or custom models don't do that or something
	// Only fine if parent is always earlier in the joint layout
	// which Doom 3 expects anyway
	// Untransform the joints to .md5anim format
	for (int i = numBones - 1; i > 0; i--) {
		boneData[i] /= boneData[0];
	}
}

/*
========================
idRenderModelMDS::ParseAnims
========================
*/
void rvmRenderModelMDR::ParseAnims(idList <animInfo_t>& animInfo, int& numAnims) {
	idFile* fp;
	idStr animName;
	idLexer parser(LEXFL_ALLOWPATHNAMES | LEXFL_NOSTRINGESCAPECHARS | LEXFL_NOSTRINGCONCAT);
	idToken		token;
	animInfo_t* temp;

	/*
	animInfo.SetNum(1);
	animInfo[0].animName = "test";
	animInfo[0].animInfo[0] = 0;
	animInfo[0].animInfo[1] = 6;
	animInfo[0].animInfo[2] = 0;
	animInfo[0].animInfo[3] = 10;
	animInfo[0].animInfo[4] = 0;
	animInfo[0].animInfo[5] = -2;
	numAnims = 1;
	return;
	*/

	name.ExtractFilePath(animName);
	animName.Append("animation.cfg");

	if (findFile_t test = fileSystem->FindFile(animName, 0)) {
		common->DPrintf("Found anim file %s for MDR model\n", animName.c_str());
	}

	parser.LoadFile(animName);

	// EF has two different formats for cfg files, in /player2/ it starts with sex m/f 
	// and has the anim names commented out after the animinfo ints
	// In /player/ it starts with commented lines and then goes straight into the anim data 
	// the anim name is uncommented at the start of the line
	// Both just end after the final animation, usually with a blank newline

	//FIXME - I need to check the anims are for the upper not the legs - hacked around it in writeanims for now
	//I'll learn to deal with commented names later, for now we'll just give them an index as a name
	int type = 0;
	if (parser.CheckTokenString("sex") ) {
		parser.SkipRestOfLine();
		type = 1;
	 }

	int i = 0;
	if (type == 0) {
		while (1) {
			if ( !parser.ReadToken(&token) ) {
				numAnims = i;
				return;
			}
			animInfo.SetNum(i + 1);
			animInfo[i].animName = token;

			//Skipuntilstring skips string and token is the next valid string,  comment lines are skipped entirely
			animInfo[i].animInfo[0] = parser.ParseInt();
			animInfo[i].animInfo[1] = parser.ParseInt();
			animInfo[i].animInfo[2] = parser.ParseInt();
			animInfo[i].animInfo[3] = parser.ParseInt();

			// I think this never has more than 4, double check
			animInfo[i].animInfo[4] = -2;
			animInfo[i].animInfo[5] = -2;

			common->Printf("%s %d %d %d %d %d %d\n", animInfo[i].animName.c_str(), animInfo[i].animInfo[0], animInfo[i].animInfo[1],
				animInfo[i].animInfo[2], animInfo[i].animInfo[3],
				animInfo[i].animInfo[4], animInfo[i].animInfo[5]);
			i++;
			if ( parser.EndOfFile() ) {
				numAnims = i;
				return;
			}
		}
	} else {
		while (1) {
			if (!parser.ReadToken(&token)) {
				numAnims = i;
				return;
			}
			parser.UnreadToken(&token);
			animInfo.SetNum(i + 1);
			animInfo[i].animName = va("anim%d", i+1);

			//Skipuntilstring skips string and token is the next valid string,  comment lines are skipped entirely
			animInfo[i].animInfo[0] = parser.ParseInt();
			animInfo[i].animInfo[1] = parser.ParseInt();
			animInfo[i].animInfo[2] = parser.ParseInt();
			animInfo[i].animInfo[3] = parser.ParseInt();

			// I think this never has more than 4, double check
			animInfo[i].animInfo[4] = -2;
			animInfo[i].animInfo[5] = -2;

			common->Printf("%s %d %d %d %d %d %d\n", animInfo[i].animName.c_str(), animInfo[i].animInfo[0], animInfo[i].animInfo[1],
				animInfo[i].animInfo[2], animInfo[i].animInfo[3],
				animInfo[i].animInfo[4], animInfo[i].animInfo[5]);
			i++;
			if (parser.EndOfFile()) {
				numAnims = i;
				return;
			}
		}
	}

	return;
}