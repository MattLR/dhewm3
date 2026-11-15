// Model_mdr.cpp
//

#include "sys/platform.h"
#include "framework/Session.h"
#include "renderer/tr_local.h"
#include "renderer/Model_local.h"
#include "renderer/Model_mdr.h"

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
	if ( modelData ) {
		Mem_Free(modelData);
		modelData = nullptr;
	}

	if ( cachedModel ) {
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
void rvmRenderModelMDR::InitFromFile( const char *fileName ) {
	name = fileName;
	LoadModel();
}

/*
========================
rvmRenderModelMDR::LoadModel
========================
*/
void rvmRenderModelMDR::LoadModel( void ) {
	void *mdrMeshBuffer;
	int mdrMeshLen;

	if ( !purged ) {
		PurgeModel();
	}
	purged = false;

	materials.Clear();

	// Try and load the file off disk.
	mdrMeshLen = fileSystem->ReadFile(name, &mdrMeshBuffer);
	if ( mdrMeshLen <= 0 || mdrMeshBuffer == nullptr ) {
		common->Warning("Failed to load MDR mesh %s\n", name.c_str());
		MakeDefaultModel();
		return;
	}

	// Parse the MDR file.
	if ( !LoadMDRFile(mdrMeshBuffer, mdrMeshLen) ) {
		MakeDefaultModel();
		fileSystem->FreeFile(mdrMeshBuffer);
		return;
	}

	// Convert the anims to md5 if it doesn't exist
	WriteAnimToDisk();

	// If we don't have any frames, then render as a static mesh.
	// FIXME Dynamix
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
bool rvmRenderModelMDR::LoadMDRFile( void *buffer, int filesize ) {
	int				i, j, k, l;
	mdrHeader_t		*pinmodel, *mdr;
	mdrFrame_t		*frame;
	mdrLOD_t		*lod, *curlod;
	mdrSurface_t	*surf, *cursurf;
	mdrTriangle_t	*tri, *curtri;
	mdrVertex_t		*v, *curv;
	mdrWeight_t		*weight, *curweight;
	mdrTag_t		*tag, *curtag;
	int				size;
	const idMaterial *sh;

	const char *mod_name = name.c_str();

	idStr shaderPrefix = name;
	shaderPrefix.StripFileExtension();

	bounds.Clear();

	pinmodel = (mdrHeader_t *)buffer;

	//pinmodel->version = LittleLong(pinmodel->version);
	if ( pinmodel->version != MDR_VERSION ) {
		common->Warning("R_LoadMDR: %s has wrong version (%i should be %i)\n", mod_name, pinmodel->version, MDR_VERSION);
		return false;
	}

	//size = LittleLong(pinmodel->ofsEnd);
	size = LittleLong(pinmodel->ofsEnd);

	if ( size > filesize ) {
		common->Warning("R_LoadMDR: Header of %s is broken. Wrong filesize declared!\n", mod_name);
		return false;
	}

	LL(pinmodel->numFrames);
	LL(pinmodel->numBones);
	LL(pinmodel->ofsFrames);

	// This is a model that uses some type of compressed Bones. We don't want to uncompress every bone for each rendered frame
	// over and over again, we'll uncompress it in this function already, so we must adjust the size of the target mdr.
	if ( pinmodel->ofsFrames < 0 ) {
		// mdrFrame_t is larger than mdrCompFrame_t:
		size += pinmodel->numFrames * sizeof(frame->name);
		// now add enough space for the uncompressed bones.
		size += pinmodel->numFrames * pinmodel->numBones * ((sizeof(mdrBone_t) - sizeof(mdrCompBone_t)));
	}

	// simple bounds check
	if ( pinmodel->numBones < 0 ||
		sizeof(*mdr) + pinmodel->numFrames * (sizeof(*frame) + (pinmodel->numBones - 1) * sizeof(*frame->bones)) > size )
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

	if ( mdr->numFrames < 1 ) {
		common->Warning("R_LoadMDR: %s has no frames\n", mod_name);
		return false;
	}

	/* The first frame will be put into the first free space after the header */
	frame = (mdrFrame_t *)(mdr + 1);
	mdr->ofsFrames = (int)((byte *)frame - (byte *)mdr);

	if  (pinmodel->ofsFrames < 0 ) {
		mdrCompFrame_t *cframe;

		// compressed model...				
		cframe = (mdrCompFrame_t *)((byte *)pinmodel - pinmodel->ofsFrames);

		for ( i = 0; i < mdr->numFrames; i++ ) {
			for ( j = 0; j < 3; j++ ) {
				frame->bounds[0][j] = LittleFloat(cframe->bounds[0][j]);
				frame->bounds[1][j] = LittleFloat(cframe->bounds[1][j]);
				frame->localOrigin[j] = LittleFloat(cframe->localOrigin[j]);
			}

			if ( i == 0 ) {
				bounds[0] = frame->bounds[0];
				bounds[1] = frame->bounds[1];
			}

			frame->radius = LittleFloat(cframe->radius);
			frame->name[0] = '\0';	// No name supplied in the compressed version.

			for ( j = 0; j < mdr->numBones; j++ ) {
				for ( k = 0; k < (sizeof(cframe->bones[j].Comp) / 2); k++ ) {
					// Do swapping for the uncompressing functions. They seem to use shorts
					// values only, so I assume this will work. Never tested it on other
					// platforms, though.

					((unsigned short *)(cframe->bones[j].Comp))[k] =
						LittleShort(((unsigned short*)(cframe->bones[j].Comp))[k]);
				}
				/* Now do the actual uncompressing */
				MC_UnCompress(frame->bones[j].matrix, cframe->bones[j].Comp);
			}

			// Next Frame...
			cframe = (mdrCompFrame_t *)&cframe->bones[j];
			frame = (mdrFrame_t *)&frame->bones[j];
		}
	} else {
		mdrFrame_t *curframe;

		// uncompressed model...

		curframe = (mdrFrame_t*)((byte*)pinmodel + pinmodel->ofsFrames);

		// swap all the frames
		for ( i = 0; i < mdr->numFrames; i++ ) {
			for ( j = 0; j < 3; j++ ) {
				frame->bounds[0][j] = LittleFloat(curframe->bounds[0][j]);
				frame->bounds[1][j] = LittleFloat(curframe->bounds[1][j]);
				frame->localOrigin[j] = LittleFloat(curframe->localOrigin[j]);
			}

			if ( i == 0 ) {
				bounds[0] = frame->bounds[0];
				bounds[1] = frame->bounds[1];
			}

			frame->radius = LittleFloat(curframe->radius);
			strcpy(frame->name, curframe->name);

			for ( j = 0; j < (int)(mdr->numBones * sizeof(mdrBone_t) / 4); j++ ) {
				((float *)frame->bones)[j] = LittleFloat(((float *)curframe->bones)[j]);
			}

			curframe = (mdrFrame_t *)&curframe->bones[mdr->numBones];
			frame = (mdrFrame_t *)&frame->bones[mdr->numBones];
		}
	}

	// frame should now point to the first free address after all frames.
	lod = (mdrLOD_t *)frame;
	mdr->ofsLODs = (int)((byte *)lod - (byte*)mdr);

	curlod = (mdrLOD_t *)((byte *)pinmodel + LittleLong(pinmodel->ofsLODs));

	// swap all the LOD's
	for ( l = 0; l < mdr->numLODs; l++ ) {
		// simple bounds check
		if ((byte *)(lod + 1) > (byte *)mdr + size) {
			common->Warning("R_LoadMDR: %s has broken structure.\n", mod_name);
			return false;
		}

		lod->numSurfaces = LittleLong(curlod->numSurfaces);

		// swap all the surfaces
		surf = (mdrSurface_t*)(lod + 1);
		lod->ofsSurfaces = (int)((byte*)surf - (byte*)lod);
		cursurf = (mdrSurface_t*)((byte*)curlod + LittleLong(curlod->ofsSurfaces));

		for ( i = 0; i < lod->numSurfaces; i++ ) {
			// simple bounds check
			if ( (byte*)(surf + 1) > (byte*)mdr + size ) {
				common->Warning("R_LoadMDR: %s has broken structure.\n", mod_name);
				return false;
			}

			char* shaderPtr = (char*)cursurf + 69;
			// first do some copying stuff
			strcpy(surf->name, cursurf->name);
			//strcpy(surf->shader, cursurf->shader);
			strcpy(surf->shader, shaderPtr);

			surf->ofsHeader = (byte*)mdr - (byte*)surf;

			surf->numVerts = LittleLong(cursurf->numVerts);
			surf->numTriangles = LittleLong(cursurf->numTriangles);
			// numBoneReferences and BoneReferences generally seem to be unused

			// lowercase the surface name so skin compares are faster
			strlwr(surf->name);

			// Raven models are missing the first letter for some reason - I'm sure I'm not doing this efficiently but it works
			// In mdr the string is just one character over and missing the first char,
			// in glm it's in the right place but has a null first character I think

			// register the shaders
			const char* materialName = va("%s%s", "m", surf->shader);
			sh = declManager->FindMaterial(materialName, true); //R_FindShader(surf->shader, LIGHTMAP_NONE, qtrue);
			if ( sh != nullptr ) {
				surf->shaderIndex = sh->Index();
			} else {
				surf->shaderIndex = 0;
				common->Warning("Failed to find material %s\n", materialName);
			}

			materials.Append(sh);

			// now copy the vertexes.
			v = (mdrVertex_t *)(surf + 1);
			surf->ofsVerts = (int)((byte *)v - (byte*)surf);
			curv = (mdrVertex_t *)((byte *)cursurf + LittleLong(cursurf->ofsVerts));

			for ( j = 0; j < surf->numVerts; j++ ) {
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
				for ( k = 0; k < v->numWeights; k++ ) {
					weight->boneIndex = LittleLong(curweight->boneIndex);
					weight->boneWeight = LittleFloat(curweight->boneWeight);

					weight->offset[0] = LittleFloat(curweight->offset[0]);
					weight->offset[1] = LittleFloat(curweight->offset[1]);
					weight->offset[2] = LittleFloat(curweight->offset[2]);

					weight++;
					curweight++;
				}

				v = (mdrVertex_t *)weight;
				curv = (mdrVertex_t *)curweight;
			}

			// we know the offset to the triangles now:
			tri = (mdrTriangle_t *)v;
			surf->ofsTriangles = (int)((byte *)tri - (byte*)surf);
			curtri = (mdrTriangle_t *)((byte *)cursurf + LittleLong(cursurf->ofsTriangles));

			// simple bounds check
			if ( surf->numTriangles < 0 || (byte*)(tri + surf->numTriangles) >(byte*) mdr + size ) {
				common->Warning("R_LoadMDR: %s has broken structure.\n", mod_name);
				return false;
			}

			for ( j = 0; j < surf->numTriangles; j++ ) {
				tri->indexes[0] = LittleLong(curtri->indexes[0]);
				tri->indexes[1] = LittleLong(curtri->indexes[1]);
				tri->indexes[2] = LittleLong(curtri->indexes[2]);

				tri++;
				curtri++;
			}

			// tri now points to the end of the surface.
			surf->ofsEnd = (byte *)tri - (byte *)surf;
			surf = (mdrSurface_t *)tri;

			// find the next surface.
			cursurf = (mdrSurface_t *)((byte *)cursurf + LittleLong(cursurf->ofsEnd));
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
	curtag = (mdrTag_t *)((byte*)pinmodel + LittleLong(pinmodel->ofsTags));

	// simple bounds check
	if ( mdr->numTags < 0 || (byte *)(tag + mdr->numTags) >(byte *) mdr + size ) {
		common->Warning("R_LoadMDR: %s has broken structure.\n", mod_name);
		return false;
	}

	for ( i = 0; i < mdr->numTags; i++ ) {
		tag->boneIndex = LittleLong(curtag->boneIndex);
		strcpy(tag->name, curtag->name);

		tag++;
		curtag++;
	}

	// And finally we know the real offset to the end.
	mdr->ofsEnd = (int)((byte *)tag - (byte *)mdr);

	// Store the pointers for each frame, so we have it for faster lookup.
	for ( int frameId = 0; frameId < mdr->numFrames; frameId++ ) {
		mdrHeader_t *header;
		mdrFrame_t *frame;

		header = (mdrHeader_t *)modelData;
		int frameSize = (size_t)(&((mdrFrame_t *)0)->bones[header->numBones]);

		frame = (mdrFrame_t *)((byte *)header + header->ofsFrames + frameId * frameSize);
		frames.Append(frame);
	}

	// Update joints here for now - Dynamix
	mdrHeader_t *header = (mdrHeader_t*)modelData;
	joints.SetGranularity(1);
	joints.SetNum(header->numBones);
	defaultPose.SetGranularity(1);
	defaultPose.SetNum(header->numBones);


	for ( int i = 0; i < header->numBones; i++ ) {
		if (i == 0) {
			joints[i].name = "origin";
			joints[i].parent = NULL;
		} else {
			joints[i].name = va("bone%d", i);
			//common->Printf("Bone name %s\n", joints[i].name.c_str());
			joints[i].parent = &joints[0];
		}
	}

	//common->DPrintf("Numtags %d\n", header->numTags);
	/*
	tag = (mdrTag_t*)((byte*)header + header->ofsTags);
	for (i = 0; i < header->numTags; i++) {
		common->Printf("Numtags %d, %s\n", tag->boneIndex, tag->name);
		tag++;
	}
	*/

	//Defaultpose here for now - Dynamix

	// Default pose  = calcbones 0?
	// Default pose is transformed idjointquats I think
	// Just make it a sep

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
const idMD5Joint *rvmRenderModelMDR::GetJoints( void ) const {
	if ( !joints.Num() ) {
		common->Error("Model has no Joints");
	}
	return &joints[0];
}

/*
=================
idRenderModelMDR::GetJointHandle
=================
*/
jointHandle_t rvmRenderModelMDR::GetJointHandle( const char *name ) const {
	const idMD5Joint *joint;
	int	i;

	joint = joints.Ptr();

	// Tags are just bones for MDR
	if (memcmp(name, "tag", 3) == 0) {

		mdrTag_t *tag = (mdrTag_t *)((byte *)modelData + modelData->ofsTags);
		for ( i = 0; i < modelData->numTags; i++ ) {
			if ( idStr::Icmp(tag->name, name) == 0 ) {
				common->DPrintf("Tag %s returns bone %d\n", tag->name, tag->boneIndex);
				return (jointHandle_t)tag->boneIndex;
			}
			tag++;
		}
	}

	for ( i = 0; i < joints.Num(); i++, joint++ ) {
		if ( idStr::Icmp(joint->name.c_str(), name) == 0 ) {
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
idBounds rvmRenderModelMDR::Bounds( const class idRenderEntity* ent ) const {
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
void rvmRenderModelMDR::RenderFramesToModel( idRenderModelStatic *staticModel, const struct idRenderEntityParms *ent, const struct viewDef_s *view, const idMaterial *overrideMaterial, int scale, int frameNum, const renderEntity_t *ent2 ) {
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

	int				frameSize;

	// Dynamix - clean this up, it's vaguely useful for frame by frame stuff/debugging and maybe
	// if it's used as a static and you want a specific frame?
	int currentFrame;
	if ( ent2 ) {
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

		for ( counter = 0; counter < num2; counter++ ) {
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
			if ( counter == 0 ) {
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
	if ( ent2 ) {
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
	for ( int i = 0; i < lod->numSurfaces; i++ ) {
		srfTriangles_t* tri = R_AllocStaticTriSurf();
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
		for ( j = 0; j < indexes; j++ ) {
			tri->indexes[baseIndex + j] = baseVertex + triangles[j];
		}
		tri->numIndexes += indexes;

		//
		// lerp all the needed bones
		//
		if ( !backlerp ) {
			// no lerping needed
			bonePtr = frame->bones;
		} else {
			bonePtr = bones;

			for ( i = 0; i < header->numBones * 12; i++ )
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

		for ( j = 0; j < numVerts; j++ ) {
			idVec3	tempVert, tempNormal;
			mdrWeight_t *w;

			tempVert.Zero();
			tempNormal.Zero();
			w = v->weights;
			for ( k = 0; k < v->numWeights; k++, w++ ) {
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
void rvmRenderModelMDR::DrawJoints( const renderEntity_t *ent, const struct viewDef_s *view ) const {
	int					i;
	int					num;
	idVec3				pos;
	const idJointMat	*joint;
	const idMD5Joint	*md5Joint;
	int					parentNum;

	num = ent->numJoints;
	joint = ent->joints;
	md5Joint = joints.Ptr();
	for ( i = 0; i < num; i++, joint++, md5Joint++ ) {
		pos = ent->origin + joint->ToVec3() * ent->axis;
		if ( md5Joint->parent ) {
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

	if ( (r_jointNameScale.GetFloat() != 0.0f) && (bounds.Expand(128.0f).ContainsPoint(view->renderView.vieworg - ent->origin)) ) {
		idVec3	offset(0, 0, r_jointNameOffset.GetFloat());
		float	scale;

		scale = r_jointNameScale.GetFloat();
		joint = ent->joints;
		num = ent->numJoints;
		for ( i = 0; i < num; i++, joint++ ) {
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
idRenderModel *rvmRenderModelMDR::InstantiateDynamicModel( const struct renderEntity_s *ent, const struct viewDef_s *view, idRenderModel *cachedModel ) {
	idRenderModelStatic *staticModel;
	class idRenderEntityLocal *renderEntity = (class idRenderEntityLocal *)ent;

	// Dynamix - bring the md5 stuff over for this
	if ( cachedModel ) {
		delete cachedModel;
		cachedModel = NULL;
	}

	if ( purged ) {
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
	if ( ent != NULL ) {
		//const idMaterial* material = renderEntity->GetCustomShader();
		const idMaterial* material = NULL;
		//idRenderEntityParms *test = renderEntity->parms;
		// Dynamix - I was using this to manually animate stuff for testing
		//RenderFramesToModel(staticModel, NULL, view, material, scale, ent->shaderParms[SHADERPARM_MD3_FRAME]);
		RenderFramesToModel(staticModel, NULL, view, material, scale, 0, ent);
	} else {
		RenderFramesToModel(staticModel, NULL, view, NULL, scale, 1, NULL);
	}

	if ( r_showSkel.GetInteger() ) {
		if ((view != NULL) && (!r_skipSuppress.GetBool() || !ent->suppressSurfaceInViewID || (ent->suppressSurfaceInViewID != view->renderView.viewID))) {
			// only draw the skeleton
			DrawJoints(ent, view);
		}
	}

	return staticModel;
}


/*
=================
idRenderModelMDR::GetJointName
=================
*/
const char *rvmRenderModelMDR::GetJointName( jointHandle_t handle ) const {
	if ( (handle < 0) || (handle >= joints.Num()) ) {
		return "<invalid joint>";
	}

	return joints[handle].name;
}

/*
=================
idRenderModelMDR::GetDefaultPose
=================
*/
const idJointQuat *rvmRenderModelMDR::GetDefaultPose( void ) const {
	return defaultPose.Ptr();
}
/*
=================
idRenderModelMDR::NearestJoint
=================
*/
int	rvmRenderModelMDR::NearestJoint( int surfaceNum, int a, int b, int c ) const {
	// FIXME Dynamix
	return 0;
}

/*
=================
idRenderModelMDR::WriteAnimToDisk
=================
*/
bool rvmRenderModelMDR::WriteAnimToDisk() {
	//return true;
	idFile *fp;
	int frameSize, numFrames, numAnims, numBones;
	mdrHeader_t *header;
	mdrFrame_t *frame;

	idList <animInfo_t> animInfo;

	animInfo.SetGranularity(1);
	animInfo.SetNum(0);
	numAnims = 0;

	ParseAnims( animInfo, numAnims );
	//common->DPrintf("Numanims = %d\n", numAnims);

	//idStr animName = name;
	//animName.StripFileExtension();
	//animName.Append(".md5anim");

	//if ( findFile_t test = fileSystem->FindFile(animName, 0) ) {
	//common->DPrintf("Found anim file %s for MDR model\n", animName.c_str());
	//return true;
	//}

	header = (mdrHeader_t *)modelData;
	numBones = header->numBones;
	numFrames = header->numFrames;

	frame = frames[0];

	// FIXME - hack to make sure we're not reading the wrong set of anims right now
	for ( int i = numAnims - 1; i > 0; i-- ) {
		//int startFrame = animInfo[c].animInfo[0];
		//int numFrames = animInfo[c].animInfo[1];
		int x = animInfo[i].animInfo[0] + animInfo[i].animInfo[1];
		if ( x > numFrames ) {
			animInfo.RemoveIndex(i);
		}
	}

	numAnims = animInfo.Num();

	idList <idJointMat> boneData;
	boneData.SetGranularity(1);
	boneData.SetNum(numBones);

	CalcBones(boneData, 0);

	// FIXME  - Add checks for 0/null animations and if so then just make one anim file called main or all or whatever
	// Checking if file exists to see if we need to generate anim for now, may move to one file check or something in future
	for ( int c = 0; c < numAnims; c++ ) {
		idStr animName;
		int startFrame = animInfo[c].animInfo[0];
		int numFrames = animInfo[c].animInfo[1];
		idStr uplow = name;

		// Eliteforce uses the same animcfg for both upper and lower models
		// Adding a folder so the anims don't overwrite each other
		uplow.StripPath();
		uplow.StripFileExtension();

		name.ExtractFilePath(animName);
		animName.Append(va("%s/", uplow.c_str()));
		animName.Append(animInfo[c].animName);
		animName.Append(".md5anim");

		if ( findFile_t test = fileSystem->FindFile(animName, 0) ) {
			common->DPrintf("Found file for anim #%d for MDR model, skipping file generation\n", c);
			//common->DPrintf("Found file for anim #%s for MDR model, skipping file generation\n", animName.c_str());
			continue;
		}

		common->DPrintf("Writing %s\n", animName.c_str());
		fp = fileSystem->OpenFileWrite(animName, "fs_devpath");

		if ( !fp ) {
			common->Warning("WriteAnims: Error opening file %s\n", name.c_str());
			return false;
		}

		fp->WriteFloatString("MD5Version 10\n\ncommandline \"\"\n");

		fp->WriteFloatString("numFrames %d\n", numFrames);
		fp->WriteFloatString("numJoints %d\n", numBones);
		fp->WriteFloatString("frameRate %d\n", animInfo[c].animInfo[3]);
		fp->WriteFloatString("numAnimatedComponents %d\n\n", numBones * 6);

		fp->WriteFloatString("hierarchy {\n");

		for ( int i = 0; i < numBones; i++ ) {
			if ( i == 0 ) {
				fp->WriteFloatString("\t\"origin\" -1 63 0\n");
			} else {
				fp->WriteFloatString("\t\"%s\" 0 63 %d\n", joints[i].name.c_str(), i * 6);
			}
		}

		fp->WriteFloatString("}\n\n");
		fp->WriteFloatString("bounds {\n");
		for ( int i = 0, j = startFrame; i < numFrames; i++, j++ ) {
			frame = frames[j];
			fp->WriteFloatString("( %f %f %f ) ( %f %f %f )\n", frame->bounds[0].x, frame->bounds[0].y, frame->bounds[0].z, frame->bounds[1].x, frame->bounds[1].y, frame->bounds[1].z);
		}
		fp->WriteFloatString("}\n\n");
		CalcBones(boneData, 0);
		fp->WriteFloatString("baseframe {\n");
		for ( int i = 0; i < numBones; i++ ) {
			idJointQuat JointQuat = boneData[i].ToJointQuat();
			idCQuat cQuat = JointQuat.q.ToCQuat();
			fp->WriteFloatString("( %f %f %f ) ( %f %f %f )\n", JointQuat.t.x, JointQuat.t.y, JointQuat.t.z, cQuat.x, cQuat.y, cQuat.z);
		}
		fp->WriteFloatString("}\n\n");

		for ( int i = 0, j = startFrame; i < numFrames; i++, j++ ) {
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
idRenderModelMDR::CalcBones
========================
*/
void rvmRenderModelMDR::CalcBones( idList<idJointMat>& boneData, int currentFrame ) {
	int numBones;
	mdrHeader_t *header;
	mdrFrame_t *frame;

	header = (mdrHeader_t *)modelData;
	numBones = header->numBones;

	frame = frames[currentFrame];

	for ( int i = 0; i < numBones; i++ ) {
		idMat3 rotMatrix;

		for ( int r = 0; r < 3; r++ ) {
			for ( int c = 0; c < 3; c++ ) {
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
	for ( int i = numBones - 1; i > 0; i-- ) {
		boneData[i] /= boneData[0];
	}
}

/*
========================
idRenderModelMDR::ParseAnims
========================
*/
void rvmRenderModelMDR::ParseAnims( idList <animInfo_t> &animInfo, int &numAnims ) {
	idFile *fp;
	idStr animName;
	idLexer parser(LEXFL_ALLOWPATHNAMES | LEXFL_NOSTRINGESCAPECHARS | LEXFL_NOSTRINGCONCAT);
	idToken		token;
	animInfo_t *temp;

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

	if ( findFile_t test = fileSystem->FindFile(animName, 0) ) {
		common->DPrintf("Found anim file %s for MDR model\n", animName.c_str());
	}

	parser.LoadFile(animName);

	// EF has two different formats for cfg files, in /player2/ it starts with sex m/f 
	// and has the anim names commented out after the animinfo ints
	// In /player/ it starts with commented lines and then goes straight into the anim data 
	// the anim name is uncommented at the start of the line
	// Both just end after the final animation, usually with a blank newline

	// FIXME - I need to check the anims are for the upper not the legs - hacked around it in writeanims for now
	// I'll learn to deal with commented names later, for now we'll just give them an index as a name
	int type = 0;
	if ( parser.CheckTokenString("sex") ) {
		parser.SkipRestOfLine();
		type = 1;
	}

	int i = 0;
	if ( type == 0 ) {
		while (1) {
			if (!parser.ReadToken(&token)) {
				numAnims = i;
				return;
			}
			animInfo.SetNum(i + 1);
			animInfo[i].animName = token;

			// Skipuntilstring skips string and token is the next valid string,  comment lines are skipped entirely
			animInfo[i].animInfo[0] = parser.ParseInt();
			animInfo[i].animInfo[1] = parser.ParseInt();
			animInfo[i].animInfo[2] = parser.ParseInt();
			animInfo[i].animInfo[3] = parser.ParseInt();

			// I think this never has more than 4, double check
			animInfo[i].animInfo[4] = -2;
			animInfo[i].animInfo[5] = -2;

			/*
			common->DPrintf("%s %d %d %d %d %d %d\n", animInfo[i].animName.c_str(), animInfo[i].animInfo[0], animInfo[i].animInfo[1],
				animInfo[i].animInfo[2], animInfo[i].animInfo[3],
				animInfo[i].animInfo[4], animInfo[i].animInfo[5]);
			*/
			i++;
			if ( parser.EndOfFile() ) {
				numAnims = i;
				return;
			}
		}
	} else {
		while (1) {
			if ( !parser.ReadToken(&token) ) {
				numAnims = i;
				return;
			}
			parser.UnreadToken(&token);
			animInfo.SetNum(i + 1);
			animInfo[i].animName = va("anim%d", i + 1);

			//Skipuntilstring skips string and token is the next valid string,  comment lines are skipped entirely
			animInfo[i].animInfo[0] = parser.ParseInt();
			animInfo[i].animInfo[1] = parser.ParseInt();
			animInfo[i].animInfo[2] = parser.ParseInt();
			animInfo[i].animInfo[3] = parser.ParseInt();

			// I think this never has more than 4, double check
			animInfo[i].animInfo[4] = -2;
			animInfo[i].animInfo[5] = -2;

			/*
			common->DPrintf("%s %d %d %d %d %d %d\n", animInfo[i].animName.c_str(), animInfo[i].animInfo[0], animInfo[i].animInfo[1],
				animInfo[i].animInfo[2], animInfo[i].animInfo[3],
				animInfo[i].animInfo[4], animInfo[i].animInfo[5]);
			*/
			i++;
			if ( parser.EndOfFile() ) {
				numAnims = i;
				return;
			}
		}
	}

	return;
}

////////
// 
//// MDX (Ghoul 2)
// 
////////
/*
========================
idRenderModelMDX::idRenderModelMDX
========================
*/
idRenderModelMDX::idRenderModelMDX() {
	dataSize = 0;
	modelData = nullptr;
	animData = nullptr;
	cachedModel = nullptr;
}

/*
========================
idRenderModelMDX::~idRenderModelMDX
========================
*/
idRenderModelMDX::~idRenderModelMDX() {
	if (animData) {
		Mem_Free(animData);
		animData = nullptr;
	}

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
idRenderModelMDX::InitFromFile
========================
*/
void idRenderModelMDX::InitFromFile(const char* fileName) {
	name = fileName;
	LoadModel();
}

/*
========================
idRenderModelMDX::LoadModel
========================
*/
void idRenderModelMDX::LoadModel(void) {
	void* mdxMeshBuffer;
	int mdxMeshLen;

	if (!purged) {
		PurgeModel();
	}
	if (animData) {
		Mem_Free(animData);
		animData = nullptr;
	}
	purged = false;

	materials.Clear();

	// Try and load the file off disk.
	mdxMeshLen = fileSystem->ReadFile(name, &mdxMeshBuffer);
	if (mdxMeshLen <= 0 || mdxMeshBuffer == nullptr)
	{
		common->Warning("Failed to load MDM mesh %s\n", name.c_str());
		MakeDefaultModel();
		return;
	}

	// Parse the MDX file.

	if (!LoadMDXFile(mdxMeshBuffer, mdxMeshLen))
	{
		MakeDefaultModel();
		fileSystem->FreeFile(mdxMeshBuffer);
		return;
	}

	// If this is static generate the surfaces
	if (isStatic) {
		CreateSurfaces();
	}

	// Free the memory from reading in the ifle.
	fileSystem->FreeFile(mdxMeshBuffer);
}

// Read the hierarchy and baseframe from the Noesis MD5Anim paired with this
// GLM.  Animation frame blocks remain the responsibility of idAnimator; this
// loader provides the skeleton/default pose needed by the GLM skinning path.
static bool MDX_LoadMD5AnimSkeleton(
	const char* animBaseName,
	int expectedJoints,
	idList<idMD5Joint>& outJoints,
	idList<idJointQuat>& outDefaultPose,
	idStr& outLoadedName)
{
	idStr baseName = animBaseName;
	baseName.StripFileExtension();

	idStr candidates[5];
	// Prefer the supplied Academy1 conversion: the GLM's animName is the
	// generic _humanoid alias, while this asset's actual GLA/MD5Anim pair is
	// _humanoid_academy1.
	candidates[0] = "models/players/_humanoid_academy1/_humanoid_academy1_noesis.md5anim";
	candidates[1] = "_humanoid_academy1_noesis.md5anim";
	candidates[2] = baseName;
	candidates[2] += "_noesis.md5anim";
	candidates[3] = baseName;
	candidates[3] += ".md5anim";
	candidates[4] = baseName;
	candidates[4] += "_converted.md5anim";

	idStr loadedName;
	idLexer* parser = NULL;
	for (int candidateNum = 0; candidateNum < 5; ++candidateNum) {
		idLexer* testParser = new idLexer(LEXFL_ALLOWPATHNAMES | LEXFL_NOSTRINGESCAPECHARS);
		if (testParser->LoadFile(candidates[candidateNum])) {
			parser = testParser;
			loadedName = candidates[candidateNum];
			break;
		}
		delete testParser;
	}

	if (!parser) {
		common->Warning("GLM: couldn't find MD5Anim '%s_noesis.md5anim' or '%s.md5anim'\n",
			baseName.c_str(), baseName.c_str());
		return false;
	}

	idToken token;
	parser->ExpectTokenString("MD5Version");
	const int version = parser->ParseInt();
	if (version != 10) {
		common->Warning("GLM: unsupported MD5Anim version %d in %s\n",
			version, loadedName.c_str());
		delete parser;
		return false;
	}

	parser->ExpectTokenString("commandline");
	parser->ReadToken(&token);
	parser->ExpectTokenString("numFrames");
	const int numFrames = parser->ParseInt();
	parser->ExpectTokenString("numJoints");
	const int numJoints = parser->ParseInt();
	parser->ExpectTokenString("frameRate");
	parser->ParseInt();
	parser->ExpectTokenString("numAnimatedComponents");
	parser->ParseInt();

	if (numJoints != expectedJoints || numJoints <= 0 || numFrames <= 0) {
		common->Warning("GLM: MD5Anim %s has %d joints; GLM expects %d\n",
			loadedName.c_str(), numJoints, expectedJoints);
		delete parser;
		return false;
	}

	idList<int> parentIndices;
	parentIndices.SetGranularity(1);
	parentIndices.SetNum(numJoints);
	outJoints.SetGranularity(1);
	outJoints.SetNum(numJoints);
	outDefaultPose.SetGranularity(1);
	outDefaultPose.SetNum(numJoints);

	parser->ExpectTokenString("hierarchy");
	parser->ExpectTokenString("{");
	for (int i = 0; i < numJoints; ++i) {
		parser->ReadToken(&token);
		outJoints[i].name = token;
		parentIndices[i] = parser->ParseInt();
		parser->ParseInt(); // channel flags; consumed by idAnimator's MD5Anim reader
		parser->ParseInt(); // first component index
		if (parentIndices[i] < -1 || parentIndices[i] >= numJoints || parentIndices[i] == i) {
			common->Warning("GLM: invalid MD5Anim parent %d for joint %d in %s\n",
				parentIndices[i], i, loadedName.c_str());
			delete parser;
			return false;
		}

		// Doom 3 requires parents to precede children.  The Noesis/Ghoul2
		// hierarchy places face at 52 and its children at 16..23.  Reparent
		// those children to cranium (15); the supplied corrected MD5Anim
		// has the face-local transform baked into their animation channels.
		if (i >= 16 && i <= 23 && parentIndices[i] == 52) {
			parentIndices[i] = 15;
		}
	}
	parser->ExpectTokenString("}");

	// Bounds are per-frame and are not needed to construct the bind pose.
	parser->ExpectTokenString("bounds");
	parser->ExpectTokenString("{");
	float boundsValues[3];
	for (int frame = 0; frame < numFrames; ++frame) {
		parser->Parse1DMatrix(3, boundsValues);
		parser->Parse1DMatrix(3, boundsValues);
	}
	parser->ExpectTokenString("}");

	parser->ExpectTokenString("baseframe");
	parser->ExpectTokenString("{");
	for (int i = 0; i < numJoints; ++i) {
		parser->Parse1DMatrix(3, outDefaultPose[i].t.ToFloatPtr());
		parser->Parse1DMatrix(3, outDefaultPose[i].q.ToFloatPtr());
		outDefaultPose[i].q.w = outDefaultPose[i].q.CalcW();
	}
	parser->ExpectTokenString("}");

	// Set parent pointers only after the idList has its final storage.
	for (int i = 0; i < numJoints; ++i) {
		outJoints[i].parent = (parentIndices[i] < 0)
			? NULL : &outJoints[parentIndices[i]];
	}

	outLoadedName = loadedName;
	delete parser;
	common->DPrintf("GLM: loaded MD5Anim skeleton %s (%d joints, %d frames)\n",
		loadedName.c_str(), numJoints, numFrames);
	return true;
}

/*
========================
idRenderModelMDX::LoadMDXFile
========================
*/
bool idRenderModelMDX::LoadMDXFile(void* buffer, int filesize) {
	int					i, j, k, l;
	mdxmHeader_t* pinmodel, * mdxm;
	mdxmSurfHierarchy_t* surfHeader;
	mdxmLOD_t* lod;
	mdxmSurface_t* surf;
	mdxmTriangle_t* tri;
	mdxmVertex_t* v;
	int					version;
	int					size;
	const idMaterial* sh;
	int					frameSize;
	int* collapseMap, * boneref;

	const char* mod_name = name.c_str();
	bounds.Clear();

	pinmodel = (mdxmHeader_t*)buffer;

	version = LittleLong(pinmodel->version);
	if (version != MDXM_VERSION)
	{
		common->Warning("R_LoadMDXM: %s has wrong version (%i should be %i)\n", mod_name, pinmodel->version, MDXM_VERSION);
		return false;
	}

	size = LittleLong(pinmodel->ofsEnd);
	//mds = mod->mds = ri.Hunk_Alloc(size, h_low);

	//memcpy(mds, buffer, LittleLong2(pinmodel->ofsEnd));

	if (size > filesize)
	{
		common->Warning("R_LoadMDXM: Header of %s is broken. Wrong filesize declared!\n", mod_name);
		return false;
	}

	//Fix all this, you can just memcpy and then fix it after
	LL(pinmodel->numSurfaces);

	dataSize += size;
	modelData = mdxm = (mdxmHeader_t*)Mem_Alloc(size);

	memcpy(mdxm, buffer, LittleLong(pinmodel->ofsEnd));

	// Copy all the values over from the file and fix endian issues in the process, if necessary.

	mdxm->ident = LittleLong(pinmodel->ident);
	mdxm->version = pinmodel->version;	// Don't need to swap byte order on this one, we already did above.
	strcpy(mdxm->name, pinmodel->name);
	strcpy(mdxm->animName, pinmodel->animName);
	mdxm->numSurfaces = pinmodel->numSurfaces;
	mdxm->ofsEnd = LittleLong(pinmodel->ofsEnd);
	mdxm->ofsSurfHierarchy = LittleLong(pinmodel->ofsSurfHierarchy);

	if (idStr::Icmp(mdxm->animName, sDEFAULT_GLA_NAME) == 0) {
		common->DPrintf("Static model\n");
		isStatic = true;
	}

	common->DPrintf("Version %d, name %s, animName %s, # bones %d, # surfaces %d\n", mdxm->version, mdxm->name, mdxm->animName, mdxm->numBones, mdxm->numSurfaces);

	surfHeader = (mdxmSurfHierarchy_t*)((byte*)mdxm + mdxm->ofsSurfHierarchy);
	for (i = 0; i < mdxm->numSurfaces; i++) {
		LL(surfHeader->flags);
		LL(surfHeader->numChildren);
		LL(surfHeader->parentIndex);

		//common->Printf("surfHier name %s, # children %d\n", surfHeader->name, surfHeader->numChildren);

		// Raven models are missing the first letter for some reason - I'm sure I'm not doing this efficiently but it works
		char* test = surfHeader->shader;
		if (test[0] == '\0') {
			// Check following bytes manually
			if (memcmp(test + 1, "odel", 4) == 0)
			{
				test[0] = 'm';
			}
			else {
				//test[0] = '[';
				test = "textures/common/nodraw";
			}
		}
		strcpy(surfHeader->shader, test);

		// register the shaders
		//const char* materialName = va("%s%s", "m", surf->shader);
		const char* materialName = va("%s", surfHeader->shader);
		common->DPrintf("Material name %s\n", surfHeader->shader);
		if (surfHeader->shader[0]) {
			//sh = R_FindShader(surf->shader, LIGHTMAP_NONE, qtrue);
			sh = declManager->FindMaterial(materialName, true); //R_FindShader(surf->shader, LIGHTMAP_NONE, qtrue);
			if (sh == nullptr) {
				surfHeader->shaderIndex = 0;
			}
			else {
				surfHeader->shaderIndex = sh->Index();
			}
		}
		else {
			surfHeader->shaderIndex = 0;
			common->Warning("Failed to find material %s\n", surfHeader->shader);
		}
		materials.Append(sh);

		// Advance to next variable-sized surface
		surfHeader = (mdxmSurfHierarchy_t*)((byte*)surfHeader
			+ offsetof(mdxmSurfHierarchy_t, childIndexes)
			+ surfHeader->numChildren * sizeof(int)
			);
	}

	lod = (mdxmLOD_t*)((byte*)pinmodel + LittleLong(pinmodel->ofsLODs));

	// swap all the LOD's
	for (l = 0; l < mdxm->numLODs; l++) {
		// simple bounds check
		if (0) {
			//if ((byte*)(lod + 1) > (byte*)mdxm + size) {
				// This didn't work on a static model? FIXME Dynamix
			common->Warning("R_LoadMDX: %s has broken structure.\n", mod_name);
			return false;
		}

		// swap all the surfaces
		//surf = (mdxmSurface_t*)((byte*)mdxm + mdxm->ofsSurfHierarchy);
		//surf = (mdxmSurface_t*)((byte*)mdxm + mdxm->ofsLODs + sizeof(mdxmLOD_t));
		surf = (mdxmSurface_t*)((byte*)lod + sizeof(mdxmLOD_t) + (mdxm->numSurfaces * sizeof(mdxmLODSurfOffset_t)));
		for (i = 0; i < mdxm->numSurfaces; i++) {
			if (LittleLong(1) != 1) {
				LL(surf->ident);
				LL(surf->thisSurfaceIndex);
				LL(surf->ofsHeader);
				LL(surf->numVerts);
				LL(surf->ofsVerts);
				LL(surf->numTriangles);
				LL(surf->ofsTriangles);
				LL(surf->numBoneReferences);
				LL(surf->ofsBoneReferences);
				LL(surf->ofsEnd);
			}

			//common->Printf("Surf index %d, numverts %d\n", surf->thisSurfaceIndex, surf->numVerts);

			// change to surface identifier
			surf->ident = 8;

			if (LittleLong(1) != 1) {
				// swap all the triangles
				tri = (mdxmTriangle_t*)((byte*)surf + surf->ofsTriangles);
				for (j = 0; j < surf->numTriangles; j++, tri++) {
					LL(tri->indexes[0]);
					LL(tri->indexes[1]);
					LL(tri->indexes[2]);
				}

				// swap all the vertexes
				v = (mdxmVertex_t*)((byte*)surf + surf->ofsVerts);
				for (j = 0; j < surf->numVerts; j++) {
					v->normal[0] = LittleFloat(v->normal[0]);
					v->normal[1] = LittleFloat(v->normal[1]);
					v->normal[2] = LittleFloat(v->normal[2]);

					v->normal[0] = LittleFloat(v->normal[0]);
					v->normal[1] = LittleFloat(v->normal[1]);
					v->normal[2] = LittleFloat(v->normal[2]);

					LL(v->uiNmWeightsAndBoneIndexes);
					v++;
				}

				// swap the bone references
				boneref = (int*)((byte*)surf + surf->ofsBoneReferences);
				for (j = 0; j < surf->numBoneReferences; j++, boneref++) {
					*boneref = LittleLong(*boneref);
				}
			}

			// find the next surface
			surf = (mdxmSurface_t*)((byte*)surf + surf->ofsEnd);
		}
		lod = (mdxmLOD_t*)((byte*)lod + LittleLong(lod->ofsEnd));
	}

	joints.SetGranularity(1);
	joints.SetNum(mdxm->numBones);
	defaultPose.SetGranularity(1);
	defaultPose.SetNum(mdxm->numBones);

	if (!isStatic && mdxm->numBones > 0) {
		// The renderer now consumes the Noesis/Doom 3 MD5Anim skeleton as its
		// animation contract.  Its hierarchy and baseframe define the joint
		// order and inverse-bind pose used by GLM skinning.
		idStr loadedMD5Anim;
		if (!MDX_LoadMD5AnimSkeleton(mdxm->animName, mdxm->numBones,
			joints, defaultPose, loadedMD5Anim)) {
			common->Warning("GLM %s: couldn't load matching MD5Anim for '%s'\n",
				mod_name, mdxm->animName);
			return false;
		}

		// The bind pose belongs to the GLA/mesh, not the MD5Anim baseframe.
		// Pair the GLA with the actual MD5Anim file that was found. Noesis
		// names its animation files *_noesis.md5anim; remove that suffix to
		// find the corresponding *.gla.
		idStr glaBase = loadedMD5Anim;
		glaBase.StripFileExtension();
		if (glaBase.Length() > 7 &&
			idStr::Icmp(glaBase.Right(7), "_noesis") == 0) {
			glaBase = glaBase.Left(glaBase.Length() - 7);
		}
		glaBase += ".gla";

		idStr glaCandidates[2];
		glaCandidates[0] = glaBase;
		glaCandidates[1] = mdxm->animName;
		glaCandidates[1] += ".gla";

		bool loadedGLA = false;
		for (int glaCandidate = 0; glaCandidate < 2; ++glaCandidate) {
			void* glaBuffer = NULL;
			const int glaLength = fileSystem->ReadFile(glaCandidates[glaCandidate], &glaBuffer);
			if (glaLength <= 0 || glaBuffer == NULL) {
				continue;
			}

			if (LoadMDXA(glaBuffer, glaLength)) {
				loadedGLA = true;
				fileSystem->FreeFile(glaBuffer);
				common->DPrintf("GLM: loaded bind pose from %s\n",
					glaCandidates[glaCandidate].c_str());
				break;
			}

			fileSystem->FreeFile(glaBuffer);
		}

		if (!loadedGLA) {
			common->Warning("GLM %s: couldn't find GLA bind pose for %s\n",
				mod_name, loadedMD5Anim.c_str());
			return false;
		}
	}
	else {
		for (int i = 0; i < mdxm->numBones; ++i) {
			joints[i].name = va("bone_%d", i);
			joints[i].parent = (i == 0) ? NULL : &joints[i - 1];
			defaultPose[i].t.Zero();
			//defaultPose[i].q.Identity();
		}
	}

	return true;
}

/*
=================
idRenderModelMDX::IsDynamicModel
=================
*/
dynamicModel_t idRenderModelMDX::IsDynamicModel() const {
	if (isStatic) {
		return DM_STATIC;
	}
	return DM_CACHED;
}

/*
=================
idRenderModelMDX::TouchData
=================
*/
void idRenderModelMDX::TouchData() {

}

/*
=================
idRenderModelMDX::NumJoints
=================
*/
int	idRenderModelMDX::NumJoints(void) const {
	return joints.Num();
	//return modelData->numBones;
}

/*
=================
idRenderModelMDX::GetJoints
=================
*/
const idMD5Joint* idRenderModelMDX::GetJoints(void) const {
	if (!joints.Num()) {
		common->Error("Model has no Joints");
	}
	return &joints[0];
}

/*
=================
idRenderModelMDX::SetJoint
=================
*/
void idRenderModelMDX::SetJoint(int index, int parentNum, idStr name) {
	if (!joints.Num()) {
		common->Error("Model has no Joints");
	}
	if (index == 0) {
		joints[index].name = "origin";
		joints[index].parent = NULL;
	}
	else {
		joints[index].name = name;
		joints[index].parent = &joints[parentNum];
	}

	return;
}

/*
=================
idRenderModelMDX::GetJointHandle
=================
*/
jointHandle_t idRenderModelMDX::GetJointHandle(const char* name) const {
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
idRenderModelMDX::Bounds
=================
*/
idBounds idRenderModelMDX::Bounds(const class idRenderEntity* ent) const {
	return bounds;
}

/*
=================
idRenderModelMDX::Memory
=================
*/
int	idRenderModelMDX::Memory() const {
	return dataSize;
}

/*
=================
idRenderModelMDX::RenderFramesToModel
=================
*/
// Apply only the rotational part of an affine joint matrix (used for normals).
static ID_INLINE idVec3 MDX_TransformVectorByJointMatRaw(
	const idJointMat& joint, const idVec3& v)
{
	const float* m = joint.ToFloatPtr();
	return idVec3(
		m[0] * v.x + m[1] * v.y + m[2] * v.z,
		m[4] * v.x + m[5] * v.y + m[6] * v.z,
		m[8] * v.x + m[9] * v.y + m[10] * v.z);
}

// Convert an absolute Ghoul2 GLA bind-pose matrix into the Noesis/Doom 3
// MD5 coordinate basis.  GLM vertices use C(x,y,z)=(-y,x,z).  The matching
// bone conversion is R_md5 = C * transpose(R_gla) / uniformScale,
// t_md5 = C * t_gla.  Per-bone scale is removed from the basis only;
// translations are already in model units and must NOT be divided by fScale.
static bool MDX_BuildGLABindPose(
	const mdxaHeader_t* gla,
	const idList<idMD5Joint>& joints,
	idList<idJointMat>& absoluteBind)
{
	if (!gla || gla->numBones != joints.Num() || gla->numBones <= 0) {
		return false;
	}

	const byte* fileBase = reinterpret_cast<const byte*>(gla);
	const byte* offsetBase = fileBase + sizeof(mdxaHeader_t);
	if (offsetBase + gla->numBones * sizeof(int) > fileBase + gla->ofsEnd) {
		return false;
	}
	const int* skelOffsets = reinterpret_cast<const int*>(offsetBase);
	if (gla->ofsSkel != (int)(offsetBase - fileBase) + skelOffsets[0]) {
		common->Warning("GLM: unexpected GLA skeleton offset table\n");
		return false;
	}

	absoluteBind.SetGranularity(1);
	absoluteBind.SetNum(gla->numBones);

	for (int i = 0; i < gla->numBones; ++i) {
		const int offset = skelOffsets[i];
		if (offset < 0 || offsetBase + offset + offsetof(mdxaSkel_t, children) > fileBase + gla->ofsEnd) {
			common->Warning("GLM: invalid GLA skeleton offset for bone %d\n", i);
			return false;
		}

		const mdxaSkel_t* skel =
			reinterpret_cast<const mdxaSkel_t*>(offsetBase + offset);

		if (idStr::Icmp(skel->name, joints[i].name.c_str()) != 0) {
			common->Warning("GLM: GLA/MD5Anim joint mismatch at %d: '%s' vs '%s'\n",
				i, skel->name, joints[i].name.c_str());
			return false;
		}

		const float (*m)[4] = skel->BasePoseMat.matrix;
		const float sx = idMath::Sqrt(m[0][0] * m[0][0] +
			m[0][1] * m[0][1] +
			m[0][2] * m[0][2]);
		const float sy = idMath::Sqrt(m[1][0] * m[1][0] +
			m[1][1] * m[1][1] +
			m[1][2] * m[1][2]);
		const float sz = idMath::Sqrt(m[2][0] * m[2][0] +
			m[2][1] * m[2][1] +
			m[2][2] * m[2][2]);
		const float uniformScale = (sx + sy + sz) * (1.0f / 3.0f);

		if (uniformScale < 1e-8f) {
			common->Warning("GLM: singular GLA bind basis for bone %d\n", i);
			return false;
		}

		float* out = absoluteBind[i].ToFloatPtr();

		// C * transpose(M), C = [ 0 -1 0 ; 1 0 0 ; 0 0 1 ].
		out[0] = -m[0][1] / uniformScale;
		out[1] = -m[1][1] / uniformScale;
		out[2] = -m[2][1] / uniformScale;
		out[3] = -m[1][3];

		out[4] = m[0][0] / uniformScale;
		out[5] = m[1][0] / uniformScale;
		out[6] = m[2][0] / uniformScale;
		out[7] = m[0][3];

		out[8] = m[0][2] / uniformScale;
		out[9] = m[1][2] / uniformScale;
		out[10] = m[2][2] / uniformScale;
		out[11] = m[2][3];
	}

	return true;
}

void idRenderModelMDX::RenderFramesToModel(
	idRenderModelStatic* staticModel,
	const struct viewDef_s* view,
	int scale,
	int frameNum,
	const renderEntity_t* ent)
{
	(void)view;
	(void)scale;
	(void)frameNum;

	if (!modelData || joints.Num() <= 0 || defaultPose.Num() != joints.Num()) {
		return;
	}

	const int numBones = joints.Num();
	if (ent && (!ent->joints || ent->numJoints != numBones)) {
		common->Warning("GLM %s: render entity joint count doesn't match MD5Anim skeleton\n", name.c_str());
		return;
	}

	// The MD5Anim baseframe is an animation reference pose, NOT the mesh
	// bind pose.  For this asset model_root is z=-24 in the MD5Anim baseframe
	// but z=0 in the MD5Mesh/GLA bind pose.  Build inverse binds from the
	// actual GLA BasePoseMat records, converted into the MD5 coordinate basis.
	if (!animData || animData->numBones != numBones) {
		common->Warning("GLM %s: matching GLA bind pose is not loaded\n", name.c_str());
		return;
	}

	idList<idJointMat> absoluteBind;
	idList<idJointMat> inverseBind;
	absoluteBind.SetGranularity(1);
	inverseBind.SetGranularity(1);
	absoluteBind.SetNum(numBones);
	inverseBind.SetNum(numBones);

	if (!MDX_BuildGLABindPose(animData, joints, absoluteBind)) {
		common->Warning("GLM %s: failed to convert GLA bind pose\n", name.c_str());
		return;
	}

	for (int i = 0; i < numBones; ++i) {
		if (!InvertJointMatAffine2(absoluteBind[i], inverseBind[i])) {
			common->Warning("GLM %s: singular GLA bind matrix for bone %d\n", name.c_str(), i);
			return;
		}
	}

	const idJointMat* currentJoints = ent ? ent->joints : absoluteBind.Ptr();

	mdxmHeader_t* header = modelData;
	mdxmLOD_t* lod = (mdxmLOD_t*)((byte*)header + header->ofsLODs);
	mdxmSurface_t* surface = (mdxmSurface_t*)((byte*)lod + sizeof(mdxmLOD_t) +
		header->numSurfaces * sizeof(mdxmLODSurfOffset_t));

	for (int surfaceNum = 0; surfaceNum < header->numSurfaces; ++surfaceNum) {
		srfTriangles_t* tri = R_AllocStaticTriSurf();
		R_AllocStaticTriSurfVerts(tri, surface->numVerts);
		R_AllocStaticTriSurfIndexes(tri, surface->numTriangles * 3);
		tri->bounds.Clear();

		modelSurface_t surf;
		surf.id = surfaceNum;
		surf.geometry = tri;
		surf.shader = materials[surfaceNum];

		int* triangles = (int*)((byte*)surface + surface->ofsTriangles);
		const int numIndexes = surface->numTriangles * 3;
		for (int j = 0; j < numIndexes; ++j) {
			tri->indexes[j] = triangles[j];
		}
		tri->numIndexes = numIndexes;

		int* boneRefs = (int*)((byte*)surface + surface->ofsBoneReferences);
		mdxmVertex_t* verts = (mdxmVertex_t*)((byte*)surface + surface->ofsVerts);
		mdxmVertexTexCoord_t* texCoords = (mdxmVertexTexCoord_t*)((byte*)verts +
			surface->numVerts * sizeof(mdxmVertex_t));

		for (int vertNum = 0; vertNum < surface->numVerts; ++vertNum) {
			const mdxmVertex_t* v = &verts[vertNum];
			const mdxmVertexTexCoord_t* tc = &texCoords[vertNum];

			// GLM geometry uses the Ghoul2 axis basis.  Noesis MD5Anim uses
			// Doom/MD5 basis: (x,y,z) -> (-y,x,z).  Apply the same conversion
			// to positions and normals before applying MD5 joint matrices.
			const idVec3 modelPosition(-v->vertCoords.y,
				v->vertCoords.x,
				v->vertCoords.z);
			const idVec3 modelNormal(-v->normal.y,
				v->normal.x,
				v->normal.z);

			idVec3 skinnedPosition(0.0f, 0.0f, 0.0f);
			idVec3 skinnedNormal(0.0f, 0.0f, 0.0f);
			float totalWeight = 0.0f;
			float validWeight = 0.0f;
			const int numWeights = G2_GetVertWeights(v);

			for (int weightNum = 0; weightNum < numWeights; ++weightNum) {
				const int refNum = G2_GetVertBoneIndex(v, weightNum);
				const float weight = G2_GetVertBoneWeight(v, weightNum,
					totalWeight, numWeights);
				if (refNum < 0 || refNum >= surface->numBoneReferences) {
					continue;
				}

				const int boneNum = boneRefs[refNum];
				if (boneNum < 0 || boneNum >= numBones) {
					continue;
				}

				// GLM stores a model-space bind-pose vertex.  Convert it to
				// this joint's bind-local space, then apply the current
				// absolute MD5 joint matrix: exactly the MD5 weighted-joint
				// skinning operation, without mixing local and world space.
				const idVec3 jointLocalPosition =
					TransformPointByJointMatRaw(inverseBind[boneNum], modelPosition);
				const idVec3 animatedPosition =
					TransformPointByJointMatRaw(currentJoints[boneNum], jointLocalPosition);

				const idVec3 jointLocalNormal =
					MDX_TransformVectorByJointMatRaw(inverseBind[boneNum], modelNormal);
				const idVec3 animatedNormal =
					MDX_TransformVectorByJointMatRaw(currentJoints[boneNum], jointLocalNormal);

				skinnedPosition += animatedPosition * weight;
				skinnedNormal += animatedNormal * weight;
				validWeight += weight;
			}

			if (validWeight > 1e-6f) {
				const float invWeight = 1.0f / validWeight;
				skinnedPosition *= invWeight;
				skinnedNormal *= invWeight;
			}
			else {
				// Bad/missing weight references should not collapse a vertex.
				skinnedPosition = modelPosition;
				skinnedNormal = modelNormal;
			}

			if (skinnedNormal.LengthSqr() > 1e-12f) {
				skinnedNormal.Normalize();
			}

			tri->verts[vertNum].xyz[0] = skinnedPosition.x;
			tri->verts[vertNum].xyz[1] = skinnedPosition.y;
			tri->verts[vertNum].xyz[2] = skinnedPosition.z;
			tri->verts[vertNum].normal[0] = skinnedNormal.x;
			tri->verts[vertNum].normal[1] = skinnedNormal.y;
			tri->verts[vertNum].normal[2] = skinnedNormal.z;
			tri->verts[vertNum].st[0] = tc->texCoords[0];
			tri->verts[vertNum].st[1] = tc->texCoords[1];
		}

		tri->numVerts = surface->numVerts;
		R_BoundTriSurf(tri);
		R_DeriveTangents(tri);
		staticModel->AddSurface(surf);
		staticModel->bounds.AddPoint(surf.geometry->bounds[0]);
		staticModel->bounds.AddPoint(surf.geometry->bounds[1]);

		surface = (mdxmSurface_t*)((byte*)surface + surface->ofsEnd);
	}
}

/*
====================
idRenderModelMDX::DrawJoints
====================
*/
void idRenderModelMDX::DrawJoints(const renderEntity_t* ent, const struct viewDef_s* view) const {
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
idRenderModelMDX::InstantiateDynamicModel
=================
*/
idRenderModel* idRenderModelMDX::InstantiateDynamicModel(const struct renderEntity_s* ent, const struct viewDef_s* view, idRenderModel* cachedModel) {
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
		common->DPrintf("idRenderModelMDX::InstantiateDynamicModel: NULL joints on renderEntity for '%s'\n", Name());
		delete cachedModel;
		return NULL;
	}
	else if (ent->numJoints != joints.Num()) {
		common->DPrintf("idRenderModelMDX::InstantiateDynamicModel: renderEntity has different number of joints than model for '%s'\n", Name());
		delete cachedModel;
		return NULL;
	}

	int scale = 1;

	staticModel = new idRenderModelStatic;
	staticModel->bounds.Clear();

	// Dynamix - clean up the parms and/or just merge this back into here
	if (ent->joints) {
		// Dynamix - I was using this to manually animate stuff for testing
		//RenderFramesToModel(staticModel, NULL, view, material, scale, ent->shaderParms[SHADERPARM_MD3_FRAME]);
		RenderFramesToModel(staticModel, view, scale, 0, ent);
	}
	else {
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
idRenderModelMDX::GetJointName
=================
*/
const char* idRenderModelMDX::GetJointName(jointHandle_t handle) const {
	if ((handle < 0) || (handle >= joints.Num())) {
		return "<invalid joint>";
	}

	return joints[handle].name;
}

/*
=================
idRenderModelMDX::GetDefaultPose
=================
*/
const idJointQuat* idRenderModelMDX::GetDefaultPose(void) const {
	return defaultPose.Ptr();
}
/*
=================
idRenderModelMDX::NearestJoint
=================
*/
int	idRenderModelMDX::NearestJoint(int surfaceNum, int a, int b, int c) const {
	return 0;
}

/*
=============
idRenderModelMDX::CreateSurfaces
=============
*/
void idRenderModelMDX::CreateSurfaces() {
	int				i, j, k;
	float			frontlerp, backlerp;
	int* triangles;
	int				indexes;
	int				numVerts;
	mdxmVertex_t* v, * verts;
	mdxmVertexTexCoord_t* tcs, * tc;
	mdxmHeader_t* header;
	header = modelData;
	mdxmSurfHierarchy_t* surfHeader;
	mdxmLOD_t* lod = (mdxmLOD_t*)((byte*)header + header->ofsLODs);
	mdxmSurface_t* surface = (mdxmSurface_t*)((byte*)lod + sizeof(mdxmLOD_t) + (header->numSurfaces * sizeof(mdxmLODSurfOffset_t)));


	this->bounds.Clear();

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
			tri->indexes[j] = triangles[j];
		}

		tri->numIndexes = indexes;

		int* boneRefs = (int*)((byte*)surface + surface->ofsBoneReferences);


		// If it's a static mesh just do this 
		if (1) {
			idVec3	tempVert, tempNormal;

			tempVert.Zero();
			tempNormal.Zero();
			numVerts = surface->numVerts;

			verts = (mdxmVertex_t*)((byte*)surface + surface->ofsVerts);
			tcs = (mdxmVertexTexCoord_t*)((byte*)verts + surface->numVerts * sizeof(mdxmVertex_t));


			for (j = 0; j < numVerts; j++) {
				idVec3	tempVert, tempNormal;
				v = &verts[j];
				tc = &tcs[j];

				//real
				//tempVert[0] = v->vertCoords[0];
				//tempVert[1] = v->vertCoords[1];
				//tempVert[2] = v->vertCoords[2];
				//Rot test

				tempVert[0] = -v->vertCoords[1];
				tempVert[1] = v->vertCoords[0];
				tempVert[2] = v->vertCoords[2];

				tri->verts[j].xyz[0] = tempVert[0];
				tri->verts[j].xyz[1] = tempVert[1];
				tri->verts[j].xyz[2] = tempVert[2];

				//tri->verts[j].normal[0] = tempNormal[0];
				//tri->verts[j].normal[1] = tempNormal[1];
				//tri->verts[j].normal[2] = tempNormal[2];

				//R_VaoPackNormal(tess.normal[baseVertex + j], tempNormal);

				tri->verts[j].st[0] = tc->texCoords[0];
				tri->verts[j].st[1] = tc->texCoords[1];

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
			//surf.shader = declManager->FindMaterial(surfHeader->name);
			//surf.shader = declManager->FindMaterial("models/players/rebel/rebel_torso");

			this->AddSurface(surf);

			this->bounds.AddPoint(surf.geometry->bounds[0]);
			this->bounds.AddPoint(surf.geometry->bounds[1]);
		}
		surface = (mdxmSurface_t*)((byte*)surface + surface->ofsEnd);
	}
}

/*
========================
idRenderModelMDX::LoadAnim
========================
*/
void idRenderModelMDX::LoadAnim(void) {
	if (!modelData) {
		common->Warning("GLM %s: cannot load GLA without MDXM header\n", name.c_str());
		return;
	}

	idStr candidates[3];
	candidates[0] = modelData->animName;
	candidates[0] += ".gla";
	candidates[1] = "models/players/_humanoid_academy1/_humanoid_academy1.gla";
	candidates[2] = "models/players/_humanoid/_humanoid.gla";

	for (int candidate = 0; candidate < 3; ++candidate) {
		void* glaBuffer = NULL;
		const int glaLength = fileSystem->ReadFile(candidates[candidate], &glaBuffer);
		if (glaLength <= 0 || glaBuffer == NULL) {
			continue;
		}

		if (LoadMDXA(glaBuffer, glaLength)) {
			fileSystem->FreeFile(glaBuffer);
			common->DPrintf("GLM: loaded bind pose from %s\n", candidates[candidate].c_str());
			return;
		}

		fileSystem->FreeFile(glaBuffer);
	}

	common->Warning("GLM %s: unable to load GLA bind pose '%s.gla'\n",
		name.c_str(), modelData->animName);
}

bool idRenderModelMDX::LoadMDXA(void* buffer, int filesize) {
	int					i, j, k, l;
	mdxaHeader_t* pinmodel, * mdxa;
	int					version;
	int					size;

	const char* mod_name = name.c_str();

	pinmodel = (mdxaHeader_t*)buffer;

	version = LittleLong(pinmodel->version);
	if (version != MDXA_VERSION)
	{
		common->Warning("R_LoadMDXA: %s has wrong version (%i should be %i)\n", mod_name, pinmodel->version, MDXM_VERSION);
		return false;
	}

	size = LittleLong(pinmodel->ofsEnd);

	if (modelData && pinmodel->numBones != modelData->numBones) {
		common->Warning("R_LoadMDXA: %s has %d bones; GLM requires %d\n",
			mod_name, pinmodel->numBones, modelData->numBones);
		return false;
	}

	if (animData) {
		Mem_Free(animData);
		animData = nullptr;
	}

	//memcpy(mds, buffer, LittleLong2(pinmodel->ofsEnd));

	if (size > filesize)
	{
		common->Warning("R_LoadMDXA: Header of %s is broken. Wrong filesize declared!\n", mod_name);
		return false;
	}

	//Fix all this, you can just memcpy and then fix it after
	LL(pinmodel->numFrames);

	dataSize += size;
	animData = mdxa = (mdxaHeader_t*)Mem_Alloc(size);

	memcpy(mdxa, buffer, LittleLong(pinmodel->ofsEnd));

	// Copy all the values over from the file and fix endian issues in the process, if necessary.

	mdxa->ident = LittleLong(pinmodel->ident);
	mdxa->version = pinmodel->version;	// Don't need to swap byte order on this one, we already did above.
	strcpy(mdxa->name, pinmodel->name);
	mdxa->ofsEnd = LittleLong(pinmodel->ofsEnd);


	common->DPrintf("MDXA Version %d\nanim file %s\nscale %f\nnum frames %d\nnum bones %d\n", mdxa->version, mdxa->name, mdxa->fScale, mdxa->numFrames, mdxa->numBones);

	return true;
}

/*
=================
idRenderModelMDX::WriteAnimToDisk
=================
*/
bool idRenderModelMDX::WriteAnimToDisk() {
	if (!animData || animData->numBones <= 0 || animData->numFrames <= 0) {
		return false;
	}

	const int numBones = animData->numBones;
	const int numFrames = animData->numFrames;

	if (numBones > MDR_MAX_BONES) {
		common->Warning("WriteAnimToDisk: unsupported GLA bone count %d\n", numBones);
		return false;
	}

	mdxaSkelOffsets_t* offsets = (mdxaSkelOffsets_t*)((byte*)animData + sizeof(mdxaHeader_t));
	idList<mdxaSkel_t*> skeleton;
	skeleton.SetGranularity(1);
	skeleton.SetNum(numBones);

	// Use the skeleton actually referenced by this GLA, including its names
	// and parent indices. This is important for the face bone at index 52.
	for (int i = 0; i < numBones; ++i) {
		skeleton[i] = (mdxaSkel_t*)((byte*)animData + sizeof(mdxaHeader_t) + offsets->offsets[i]);
	}

	// Match the channel layout emitted by Noesis for the supplied Academy GLA:
	// root: translation-Y only; face: quaternion only; all other joints: all 6.
	// The MD5 frame component count is therefore 1 + 51*6 + 3 = 310.
	idList<int> channelFlags;
	idList<int> componentStarts;
	channelFlags.SetGranularity(1);
	componentStarts.SetGranularity(1);
	channelFlags.SetNum(numBones);
	componentStarts.SetNum(numBones);

	int numAnimatedComponents = 0;
	for (int i = 0; i < numBones; ++i) {
		int flags = 63;
		if (i == 0) {
			flags = 2;
		}
		else if (!idStr::Icmp(skeleton[i]->name, "face")) {
			flags = 56;
		}

		channelFlags[i] = flags;
		componentStarts[i] = numAnimatedComponents;

		for (int bit = 0; bit < 6; ++bit) {
			if (flags & (1 << bit)) {
				++numAnimatedComponents;
			}
		}
	}

	idList<idJointMat> boneData;
	boneData.SetGranularity(1);
	boneData.SetNum(numBones);

	// Frame zero is the bind/reference pose in the supplied Noesis conversion.
	// CalcBones now reconstructs the parent-local MD5 transforms from the GLA
	// BasePoseMat matrices and the compressed frame matrices.
	CalcBones(boneData, 0);

	idStr animName = animData->name;
	animName.Append(".md5anim");

	common->DPrintf("Writing GLA animation %s (%d frames, %d joints)\n",
		animName.c_str(), numFrames, numBones);

	// Always regenerate: an old experimental MD5 file must not silently
	// override the corrected conversion.
	idFile* fp = fileSystem->OpenFileWrite(animName, "fs_devpath");
	if (!fp) {
		common->Warning("WriteAnimToDisk: Error opening file %s\n", animName.c_str());
		return false;
	}

	fp->WriteFloatString("MD5Version 10\n\ncommandline \"\"\n");
	fp->WriteFloatString("numFrames %d\n", numFrames);
	fp->WriteFloatString("numJoints %d\n", numBones);
	fp->WriteFloatString("frameRate 20\n");
	fp->WriteFloatString("numAnimatedComponents %d\n\n", numAnimatedComponents);

	fp->WriteFloatString("hierarchy {\n");
	for (int i = 0; i < numBones; ++i) {
		fp->WriteFloatString("\t\"%s\" %d %d %d\n",
			skeleton[i]->name,
			skeleton[i]->parent,
			channelFlags[i],
			componentStarts[i]);
	}
	fp->WriteFloatString("}\n\n");

	// Broad bounds avoid culling animated limbs outside the old hard-coded
	// player-sized box. The animation file has no per-frame mesh bounds.
	fp->WriteFloatString("bounds {\n");
	for (int i = 0; i < numFrames; ++i) {
		fp->WriteFloatString("( -512 -512 -512 ) ( 512 512 512 )\n");
	}
	fp->WriteFloatString("}\n\n");

	fp->WriteFloatString("baseframe {\n");
	for (int i = 0; i < numBones; ++i) {
		const idJointQuat joint = boneData[i].ToJointQuat();
		const idCQuat quat = joint.q.ToCQuat();

		fp->WriteFloatString("( %f %f %f ) ( %f %f %f )\n",
			joint.t.x, joint.t.y, joint.t.z,
			quat.x, quat.y, quat.z);
	}
	fp->WriteFloatString("}\n\n");

	for (int frameNum = 0; frameNum < numFrames; ++frameNum) {
		CalcBones(boneData, frameNum);
		fp->WriteFloatString("frame %d {\n", frameNum);

		for (int boneNum = 0; boneNum < numBones; ++boneNum) {
			const idJointQuat joint = boneData[boneNum].ToJointQuat();
			const idCQuat quat = joint.q.ToCQuat();
			const int flags = channelFlags[boneNum];

			// MD5 component order: tx, ty, tz, qx, qy, qz.
			if (flags & 1)  fp->WriteFloatString("%f ", joint.t.x);
			if (flags & 2)  fp->WriteFloatString("%f ", joint.t.y);
			if (flags & 4)  fp->WriteFloatString("%f ", joint.t.z);
			if (flags & 8)  fp->WriteFloatString("%f ", quat.x);
			if (flags & 16) fp->WriteFloatString("%f ", quat.y);
			if (flags & 32) fp->WriteFloatString("%f ", quat.z);
			fp->WriteFloatString("\n");
		}

		fp->WriteFloatString("}\n\n");
	}

	fileSystem->CloseFile(fp);
	return true;
}

/*
========================
idRenderModelMDX::CalcBones
========================
*/

// ---------------------------------------------------------------------------
// GLM/GLA -> Doom 3 MD5 animation conversion helpers.
//
// GLA BasePoseMat is a model-space matrix.  For MD5 we need the parent-local
// transform.  The Ghoul2 frame matrix is applied between the parent's inverse
// bind matrix and this bone's bind matrix:
//
//     local = inverse(parentBase) * decodedFrame * thisBase
//
// The GLA matrices have their 3x3 basis stored transposed relative to the
// column-vector convention used here.  Their translation is already in GLA
// model units; the final parent-local translation is multiplied by fScale.
// ---------------------------------------------------------------------------

static void GLA_MultiplyAffine3x4(const float a[3][4],
	const float b[3][4],
	float out[3][4])
{
	float tmp[3][4];

	for (int r = 0; r < 3; ++r) {
		for (int c = 0; c < 3; ++c) {
			tmp[r][c] = a[r][0] * b[0][c] +
				a[r][1] * b[1][c] +
				a[r][2] * b[2][c];
		}
		tmp[r][3] = a[r][0] * b[0][3] +
			a[r][1] * b[1][3] +
			a[r][2] * b[2][3] + a[r][3];
	}

	memcpy(out, tmp, sizeof(tmp));
}

static bool GLA_InvertAffine3x4(const float in[3][4], float out[3][4])
{
	const float a = in[0][0], b = in[0][1], c = in[0][2];
	const float d = in[1][0], e = in[1][1], f = in[1][2];
	const float g = in[2][0], h = in[2][1], i = in[2][2];

	const float det = a * (e * i - f * h) -
		b * (d * i - f * g) +
		c * (d * h - e * g);

	if (idMath::Fabs(det) < 1e-8f) {
		return false;
	}

	const float invDet = 1.0f / det;

	out[0][0] = (e * i - f * h) * invDet;
	out[0][1] = (c * h - b * i) * invDet;
	out[0][2] = (b * f - c * e) * invDet;

	out[1][0] = (f * g - d * i) * invDet;
	out[1][1] = (a * i - c * g) * invDet;
	out[1][2] = (c * d - a * f) * invDet;

	out[2][0] = (d * h - e * g) * invDet;
	out[2][1] = (b * g - a * h) * invDet;
	out[2][2] = (a * e - b * d) * invDet;

	for (int r = 0; r < 3; ++r) {
		out[r][3] = -(out[r][0] * in[0][3] +
			out[r][1] * in[1][3] +
			out[r][2] * in[2][3]);
	}

	return true;
}

static void GLA_NormalizeAffineBasis(float m[3][4])
{
	// The face attachment bones in this GLA have an additional uniform
	// 1.087 basis scale. Remove basis scale before converting to a quaternion.
	for (int c = 0; c < 3; ++c) {
		const float len = idMath::Sqrt(
			m[0][c] * m[0][c] +
			m[1][c] * m[1][c] +
			m[2][c] * m[2][c]);

		if (len > 1e-8f) {
			const float invLen = 1.0f / len;
			m[0][c] *= invLen;
			m[1][c] *= invLen;
			m[2][c] *= invLen;
		}
	}
}

static void GLA_ReadBasePoseColumnMatrix(const mdxaSkel_t* skel,
	float out[3][4])
{
	for (int r = 0; r < 3; ++r) {
		for (int c = 0; c < 3; ++c) {
			out[r][c] = skel->BasePoseMat.matrix[c][r];
		}
		out[r][3] = skel->BasePoseMat.matrix[r][3];
	}
}

void idRenderModelMDX::CalcBones(idList<idJointMat>& boneData, int currentFrame) {
	const int numBones = animData->numBones;
	mdxaSkelOffsets_t* offsets = (mdxaSkelOffsets_t*)((byte*)animData + sizeof(mdxaHeader_t));

	if (numBones <= 0 || numBones > MDR_MAX_BONES) {
		return;
	}

	idList<mdxaSkel_t*> skeleton;
	idList<mdxaBone_t> basePose;
	skeleton.SetGranularity(1);
	basePose.SetGranularity(1);
	skeleton.SetNum(numBones);
	basePose.SetNum(numBones);

	// Read the actual GLA skeleton. Do not use the hard-coded 53-bone table:
	// parent 52 ("face") is used by bones 16-23, so parent order is not linear.
	for (int i = 0; i < numBones; ++i) {
		skeleton[i] = (mdxaSkel_t*)((byte*)animData + sizeof(mdxaHeader_t) + offsets->offsets[i]);
		GLA_ReadBasePoseColumnMatrix(skeleton[i], basePose[i].matrix);
	}

	const float scale = (animData->fScale != 0.0f) ? animData->fScale : 1.0f;

	for (int boneNum = 0; boneNum < numBones; ++boneNum) {
		float decoded[3][4];
		float local[3][4];

		UnCompressBone(decoded, boneNum, animData, currentFrame);

		const int parent = skeleton[boneNum]->parent;

		if (parent < 0) {
			// Ghoul2's root is rotated 90 degrees into Doom/MD5 space.
			// Its translation is converted by the inverse basis mapping:
			// (x,y,z) -> (-y,x,z). Unlike child translations, root motion
			// is already in final model units and must not be scaled by fScale.
			idMat3 rootRotation;
			for (int c = 0; c < 3; ++c) {
				rootRotation[0][c] = decoded[c][1];
				rootRotation[1][c] = -decoded[c][0];
				rootRotation[2][c] = decoded[c][2];
			}

			boneData[boneNum].SetRotation(rootRotation);
			boneData[boneNum].SetTranslation(idVec3(
				-decoded[1][3],
				decoded[0][3],
				decoded[2][3]));
			continue;
		}

		if (parent >= numBones) {
			common->Warning("GLA bone %d has invalid parent %d\n", boneNum, parent);
			continue;
		}

		float inverseParent[3][4];
		float parentTimesFrame[3][4];

		if (!GLA_InvertAffine3x4(basePose[parent].matrix, inverseParent)) {
			common->Warning("GLA bone %d has a singular parent bind matrix\n", boneNum);
			continue;
		}

		// local = inverse(parent bind) * decoded frame * this bone bind
		GLA_MultiplyAffine3x4(inverseParent, decoded, parentTimesFrame);
		GLA_MultiplyAffine3x4(parentTimesFrame, basePose[boneNum].matrix, local);

		// Strip any residual uniform basis scale (notably the 1.087 scale
		// on the face attachment bones) before converting to a quaternion.
		GLA_NormalizeAffineBasis(local);

		idMat3 rotation;
		for (int r = 0; r < 3; ++r) {
			for (int c = 0; c < 3; ++c) {
				// idJointMat/MD5 quaternion convention is the transpose of
				// the column-vector affine matrix used for the calculation.
				rotation[r][c] = local[c][r];
			}
		}

		boneData[boneNum].SetRotation(rotation);
		boneData[boneNum].SetTranslation(idVec3(
			local[0][3] * scale,
			local[1][3] * scale,
			local[2][3] * scale));
	}
}