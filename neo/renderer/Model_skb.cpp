// Model_skb.cpp
//
// Loader for .skb v4 skeletal models, fed either a .skb or a .tik: skinned by the Doom 3 animator,
// .ska animations converted to .md5anim (Model_skb_anim.cpp). See Model_skb.h.
//

#include "sys/platform.h"
#include "framework/Session.h"
#include "renderer/tr_local.h"
#include "renderer/Model_local.h"
#include "renderer/Model_sil.h"

#include "renderer/Model_skb.h"
#include "renderer/Tik_parse.h"


// Model_skb_anim.cpp
//
// .ska -> .md5anim for the .skb models, see the notes in Model_skb.h.
//
// Laid out like the MDS / MDM loaders: CalcBones() works out the pose of every joint for one
// frame in the form .md5anim wants (relative to the parent), WriteAnimToDisk() prints them.
//

static_assert(offsetof(skaHeader_t, ofsFrames4) == 108, "skaHeader_t must match the on-disk size");
static_assert(sizeof(skaHeader_t) == 112, "skaHeader_t must match the on-disk size");
static_assert(sizeof(skaBoneName_t) == 36, "skaBoneName_t must match the on-disk size");
static_assert(sizeof(skaFrame_t) == 40, "skaFrame_t must match the on-disk size");

#define SKA_HEADER_SIZE_V3		108

/*
========================
SKA_InRange

True if [ofs, ofs + count * stride) lies inside a buffer of the given size.
Done in 64 bit so hostile counts can't wrap around.
========================
*/
static bool SKA_InRange(int ofs, int count, int stride, int size) {
	if (ofs < 0 || count < 0) {
		return false;
	}
	return (long long)ofs + (long long)count * (long long)stride <= (long long)size;
}

/*
========================
SKA_SanitizeJointName

Doom 3 joint names are matched by string, so spaces are replaced with underscores. The same rule as
SanitizeJointName in Model_mds.cpp, keep the two in step.
========================
*/
static idStr SKA_SanitizeJointName(const char* boneName) {
	idStr s(boneName);
	s.Replace(" ", "_");
	return s;
}

/*
===============================================================================

	Quaternion maths, in double. x, y, z, w with the usual meaning, v' = R(q) * v.
	Only used on the way to the idJointQuat that CalcBones hands out.

===============================================================================
*/
struct skaQuat_t {
	double	x, y, z, w;
};

static skaQuat_t SKA_QuatMul(const skaQuat_t& a, const skaQuat_t& b) {
	skaQuat_t r;

	r.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
	r.y = a.w * b.y + a.y * b.w + a.z * b.x - a.x * b.z;
	r.z = a.w * b.z + a.z * b.w + a.x * b.y - a.y * b.x;
	r.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
	return r;
}

static skaQuat_t SKA_QuatConj(const skaQuat_t& q) {
	skaQuat_t r = { -q.x, -q.y, -q.z, q.w };
	return r;
}

static skaQuat_t SKA_QuatNormalize(const skaQuat_t& q) {
	const double len = sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);

	if (len < 1e-9) {
		skaQuat_t id = { 0.0, 0.0, 0.0, 1.0 };
		return id;
	}
	skaQuat_t r = { q.x / len, q.y / len, q.z / len, q.w / len };
	return r;
}

static void SKA_QuatRotate(const skaQuat_t& q, const double v[3], double out[3]) {
	const double xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
	const double xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
	const double wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

	out[0] = (1.0 - 2.0 * (yy + zz)) * v[0] + 2.0 * (xy - wz) * v[1] + 2.0 * (xz + wy) * v[2];
	out[1] = 2.0 * (xy + wz) * v[0] + (1.0 - 2.0 * (xx + zz)) * v[1] + 2.0 * (yz - wx) * v[2];
	out[2] = 2.0 * (xz - wy) * v[0] + 2.0 * (yz + wx) * v[1] + (1.0 - 2.0 * (xx + yy)) * v[2];
}

// A bone as stored in the .skb base frame and in the .ska frames. The w component is stored with
// the opposite sign, see skbSource_t::Open in Model_skb.cpp.
static void SKA_BoneToLocal(const skbBone_t& b, skaQuat_t& q, double t[3]) {
	skaQuat_t raw;

	raw.x = LittleShort(b.quat[0]) * (double)SKB_QUAT_SCALE;
	raw.y = LittleShort(b.quat[1]) * (double)SKB_QUAT_SCALE;
	raw.z = LittleShort(b.quat[2]) * (double)SKB_QUAT_SCALE;
	raw.w = -LittleShort(b.quat[3]) * (double)SKB_QUAT_SCALE;
	q = SKA_QuatNormalize(raw);

	t[0] = LittleShort(b.offset[0]) * (double)SKB_OFFSET_SCALE;
	t[1] = LittleShort(b.offset[1]) * (double)SKB_OFFSET_SCALE;
	t[2] = LittleShort(b.offset[2]) * (double)SKB_OFFSET_SCALE;
}

/*
===============================================================================

	idSKBAnim: one opened .ska

===============================================================================
*/

/*
========================
idSKBAnim::idSKBAnim
========================
*/
idSKBAnim::idSKBAnim() {
	buffer = NULL;
	version = 0;
	numFrames = 0;
	numBones = 0;
	frameTime = 0.0f;
	ofsNames = 0;
	ofsFrames = 0;
	frameSize = 0;
}

/*
========================
idSKBAnim::~idSKBAnim
========================
*/
idSKBAnim::~idSKBAnim() {
	if (buffer != NULL) {
		fileSystem->FreeFile(buffer);
	}
}

/*
========================
idSKBAnim::Open

Reads the file and checks it. Every offset and count is checked against the file size, so Open()
returning true means the accessors below can't read outside of the file.
========================
*/
bool idSKBAnim::Open(const char* fileName) {
	skaHeader_t		header;
	int				len, i, j;

	name = fileName;

	len = fileSystem->ReadFile(fileName, &buffer, NULL);
	if (len <= 0 || buffer == NULL) {
		// a zero length file still comes back with a buffer, the destructor releases it
		common->Warning("idSKBAnim: couldn't read %s", fileName);
		return false;
	}

	if (len < SKA_HEADER_SIZE_V3) {
		common->Warning("idSKBAnim: %s is too small to be a ska", fileName);
		return false;
	}

	// version 3 has a shorter header, don't read past it
	memset(&header, 0, sizeof(header));
	memcpy(&header, buffer, len < (int)sizeof(header) ? len : (int)sizeof(header));

	if (memcmp(header.ident, "SKAN", 4) != 0) {
		common->Warning("idSKBAnim: %s is not a ska file", fileName);
		return false;
	}

	version = LittleLong(header.version);
	numFrames = LittleLong(header.numFrames);
	numBones = LittleLong(header.numBones);
	frameTime = LittleFloat(header.frameTime);

	if (version != SKA_VERSION_3 && version != SKA_VERSION_4) {
		common->Warning("idSKBAnim: %s is version %i, only %i and %i are known", fileName, version, SKA_VERSION_3, SKA_VERSION_4);
		return false;
	}

	if (version == SKA_VERSION_4 && len < (int)sizeof(header)) {
		common->Warning("idSKBAnim: %s is too small to be a version %i ska", fileName, version);
		return false;
	}

	if (numFrames < 1 || numFrames > SKA_MAX_FRAMES) {
		common->Warning("idSKBAnim: %s has %i frames (limit is %i)", fileName, numFrames, SKA_MAX_FRAMES);
		return false;
	}

	if (numBones < 1 || numBones > SKB_MAX_BONES) {
		common->Warning("idSKBAnim: %s has %i bones (limit is %i)", fileName, numBones, SKB_MAX_BONES);
		return false;
	}

	// written so that NaN fails too
	if (!(frameTime > 0.0001f && frameTime < 10.0f)) {
		common->Warning("idSKBAnim: %s has a frame time of %f", fileName, frameTime);
		return false;
	}

	frameSize = (int)sizeof(skaFrame_t) + numBones * (int)sizeof(skbBone_t);

	if (version == SKA_VERSION_3) {
		ofsNames = 0;
		ofsFrames = LittleLong(header.ofsFrames);
	}
	else {
		ofsNames = LittleLong(header.ofsFrames);
		ofsFrames = LittleLong(header.ofsFrames4);

		if (ofsNames < (int)sizeof(header) || !SKA_InRange(ofsNames, numBones, sizeof(skaBoneName_t), len) ||
			ofsFrames < ofsNames + numBones * (int)sizeof(skaBoneName_t)) {
			common->Warning("idSKBAnim: %s has a bone name table outside the file", fileName);
			return false;
		}
	}

	if (ofsFrames < SKA_HEADER_SIZE_V3 || !SKA_InRange(ofsFrames, numFrames, frameSize, len)) {
		common->Warning("idSKBAnim: %s has frames outside the file", fileName);
		return false;
	}

	// bounds go straight into the .md5anim
	for (i = 0; i < numFrames; i++) {
		float b[2][3];

		GetBounds(i, b);
		for (j = 0; j < 6; j++) {
			const float v = b[j / 3][j % 3];

			if (!(v > -1.0e9f && v < 1.0e9f)) {
				common->Warning("idSKBAnim: %s has bad bounds in frame %i", fileName, i);
				return false;
			}
		}
	}

	return true;
}

/*
========================
idSKBAnim::BoneName
========================
*/
const char* idSKBAnim::BoneName(int bone, idStr& out) const {
	char	tmp[SKA_BONE_NAME_LEN + 1];

	if (ofsNames == 0 || bone < 0 || bone >= numBones) {
		return NULL;
	}
	memcpy(tmp, (const byte*)buffer + ofsNames + bone * sizeof(skaBoneName_t) + offsetof(skaBoneName_t, name), SKA_BONE_NAME_LEN);
	tmp[SKA_BONE_NAME_LEN] = '\0';
	out = tmp;
	return out.c_str();
}

/*
========================
idSKBAnim::GetBounds
========================
*/
void idSKBAnim::GetBounds(int frame, float bounds[2][3]) const {
	skaFrame_t	f;

	memcpy(&f, (const byte*)buffer + ofsFrames + frame * frameSize, sizeof(f));
	for (int i = 0; i < 6; i++) {
		bounds[i / 3][i % 3] = LittleFloat(f.bounds[i / 3][i % 3]);
	}
}

/*
========================
idSKBAnim::GetBone
========================
*/
void idSKBAnim::GetBone(int frame, int bone, skbBone_t& out) const {
	memcpy(&out, (const byte*)buffer + ofsFrames + frame * frameSize + sizeof(skaFrame_t) + bone * sizeof(skbBone_t), sizeof(out));
}

/*
===============================================================================

	idSKBSkeleton

===============================================================================
*/

/*
========================
idSKBSkeleton::idSKBSkeleton
========================
*/
idSKBSkeleton::idSKBSkeleton() {
	numBones = 0;
	addedOrigin = false;
}

/*
========================
idSKBSkeleton::Load

Reads the bones and the base frame of the .skb and works out the joint list, see Model_skb.h.
========================
*/
bool idSKBSkeleton::Load(const char* skbName) {
	void* buffer = NULL;
	skbHeader_t		header;
	idList<idStr>	rawName;
	int				len, i, j;

	joints.Clear();
	jointOfBone.Clear();
	boneParent.Clear();
	baseFrame.Clear();
	numBones = 0;
	addedOrigin = false;

	len = fileSystem->ReadFile(skbName, &buffer, NULL);
	if (len <= 0 || buffer == NULL) {
		if (buffer != NULL) {
			fileSystem->FreeFile(buffer);
		}
		common->Warning("idSKBSkeleton: couldn't read %s", skbName);
		return false;
	}

	if (len < (int)sizeof(header)) {
		fileSystem->FreeFile(buffer);
		common->Warning("idSKBSkeleton: %s is too small to be a skb", skbName);
		return false;
	}

	memcpy(&header, buffer, sizeof(header));
	header.version = LittleLong(header.version);
	header.numBones = LittleLong(header.numBones);
	header.ofsBones = LittleLong(header.ofsBones);
	header.ofsBoneBaseFrame = LittleLong(header.ofsBoneBaseFrame);
	header.ofsEnd = LittleLong(header.ofsEnd);

	if (memcmp(header.ident, "SKL ", 4) != 0 || header.version != SKB_VERSION || header.ofsEnd > len ||
		header.numBones < 1 || header.numBones > SKB_MAX_BONES ||
		!SKA_InRange(header.ofsBones, header.numBones, sizeof(skbBoneInfo_t), len) ||
		!SKA_InRange(header.ofsBoneBaseFrame, header.numBones, sizeof(skbBone_t), len)) {
		fileSystem->FreeFile(buffer);
		common->Warning("idSKBSkeleton: %s is not a usable version %i skb", skbName, SKB_VERSION);
		return false;
	}

	numBones = header.numBones;

	const byte* data = (const byte*)buffer;

	for (i = 0; i < numBones; i++) {
		skbBoneInfo_t	info;
		skbBone_t		base;
		char			tmp[SKB_NAME_LEN + 1];

		memcpy(&info, data + header.ofsBones + i * sizeof(info), sizeof(info));
		memcpy(&base, data + header.ofsBoneBaseFrame + i * sizeof(base), sizeof(base));

		memcpy(tmp, info.name, SKB_NAME_LEN);
		tmp[SKB_NAME_LEN] = '\0';

		rawName.Append(idStr(tmp));
		boneParent.Append(LittleLong(info.parent));
		baseFrame.Append(base);

		if (boneParent[i] < -1 || boneParent[i] >= i) {
			fileSystem->FreeFile(buffer);
			common->Warning("idSKBSkeleton: %s bone %i has parent %i, which doesn't come before it", skbName, i, boneParent[i]);
			return false;
		}
	}
	fileSystem->FreeFile(buffer);

	// ---- the new parent of every bone. Bone numbers, plus one more "bone" (numBones) for an
	// origin that has to be added.
	int origin = -1;

	for (i = 0; i < numBones; i++) {
		if (rawName[i].Icmp("origin") == 0) {
			if (origin < 0) {
				origin = i;
			}
			else {
				common->Warning("idSKBSkeleton: %s has more than one bone called origin, using the first", skbName);
			}
		}
	}

	const int			numNodes = numBones + (origin < 0 ? 1 : 0);
	idList<int>			newParent;
	idList<bool>		onChain;

	for (i = 0; i < numNodes; i++) {
		newParent.Append(i < numBones ? boneParent[i] : -1);
		onChain.Append(false);
	}

	if (origin >= 0) {
		// turn the path from origin up to the old root around: origin becomes the root, and every
		// bone on the way becomes the child of the one it used to be the parent of
		int prev = -1;
		int c = origin;

		while (c >= 0) {
			const int oldParent = boneParent[c];

			newParent[c] = prev;
			onChain[c] = true;
			prev = c;
			c = oldParent;
		}
		// any other bone without a parent hangs off origin
		for (i = 0; i < numBones; i++) {
			if (!onChain[i] && boneParent[i] < 0) {
				newParent[i] = origin;
			}
		}
	}
	else {
		addedOrigin = true;
		for (i = 0; i < numBones; i++) {
			if (boneParent[i] < 0) {
				newParent[i] = numBones;
			}
		}
	}

	// ---- order: origin first, then parents before children, otherwise the .skb order
	const int			rootNode = (origin >= 0) ? origin : numBones;
	idList<int>			order;
	idList<bool>		emitted;

	for (i = 0; i < numNodes; i++) {
		emitted.Append(false);
	}
	order.Append(rootNode);
	emitted[rootNode] = true;

	for (bool added = true; added && order.Num() < numNodes; ) {
		added = false;
		for (i = 0; i < numNodes; i++) {
			if (!emitted[i] && newParent[i] >= 0 && emitted[newParent[i]]) {
				order.Append(i);
				emitted[i] = true;
				added = true;
			}
		}
	}

	if (order.Num() != numNodes) {
		common->Warning("idSKBSkeleton: %s doesn't make a single tree", skbName);
		return false;
	}

	// ---- the joints
	idList<int>		jointOfNode;

	for (i = 0; i < numNodes; i++) {
		jointOfNode.Append(-1);
	}
	for (i = 0; i < numNodes; i++) {
		jointOfNode[order[i]] = i;
	}

	for (i = 0; i < numNodes; i++) {
		const int	node = order[i];
		joint_t		jt;

		jt.bone = (node < numBones) ? node : -1;
		jt.parent = (newParent[node] >= 0) ? jointOfNode[newParent[node]] : -1;

		if (node == rootNode) {
			jt.name = "origin";
		}
		else if (rawName[node].Length() == 0) {
			jt.name = va("bone%i", node);
		}
		else {
			jt.name = SKA_SanitizeJointName(rawName[node].c_str());
		}
		joints.Append(jt);
	}

	for (i = 0; i < numBones; i++) {
		jointOfBone.Append(jointOfNode[i]);
	}

	// joints are found by name in Doom 3, a repeated name would silently use the first
	for (i = 0; i < joints.Num(); i++) {
		for (j = 0; j < i; j++) {
			if (joints[i].name.Icmp(joints[j].name.c_str()) == 0) {
				common->Warning("idSKBSkeleton: %s has two joints called %s", skbName, joints[i].name.c_str());
				joints.Clear();
				return false;
			}
		}
	}

	return true;
}

/*
========================
idSKBSkeleton::JointOfBone
========================
*/
int idSKBSkeleton::JointOfBone(int bone) const {
	if (bone < 0 || bone >= jointOfBone.Num()) {
		return -1;
	}
	return jointOfBone[bone];
}

/*
========================
idSKBSkeleton::MapAnim

Version 4 animations name their bones, and find them in the skeleton by that name. Version 3 has
no names, its bones have to be the skeleton's bones in the same order.
========================
*/
bool idSKBSkeleton::MapAnim(const idSKBAnim& anim, idList<int>& boneOfAnimBone) const {
	int		i, j;

	boneOfAnimBone.Clear();

	if (joints.Num() == 0) {
		return false;
	}

	if (anim.Version() == SKA_VERSION_3) {
		if (anim.NumBones() != numBones) {
			common->Warning("idSKBSkeleton: a version %i animation has %i bones, the skeleton has %i", SKA_VERSION_3, anim.NumBones(), numBones);
			return false;
		}
		for (i = 0; i < numBones; i++) {
			boneOfAnimBone.Append(i);
		}
		return true;
	}

	// the names of the skeleton's bones, as they were in the .skb
	for (i = 0; i < anim.NumBones(); i++) {
		idStr	animBone;
		int		found = -1;

		anim.BoneName(i, animBone);
		for (j = 0; j < joints.Num(); j++) {
			if (joints[j].bone >= 0 && SKA_SanitizeJointName(animBone.c_str()).Icmp(joints[j].name.c_str()) == 0) {
				found = joints[j].bone;
				break;
			}
		}
		if (found < 0) {
			common->Warning("idSKBSkeleton: animation bone %s is not in the skeleton", animBone.c_str());
			boneOfAnimBone.Clear();
			return false;
		}
		for (j = 0; j < boneOfAnimBone.Num(); j++) {
			if (boneOfAnimBone[j] == found) {
				common->Warning("idSKBSkeleton: animation drives bone %s twice", animBone.c_str());
				boneOfAnimBone.Clear();
				return false;
			}
		}
		boneOfAnimBone.Append(found);
	}
	return true;
}

/*
========================
idSKBSkeleton::BuildWorld

Steps 1 and 2 of CalcBones: the model space pose of every .skb bone (plus the added "origin", last,
if there is one), as quaternions and translations in double.
========================
*/
void idSKBSkeleton::BuildWorld(const idSKBAnim* anim, const idList<int>* boneOfAnimBone, int frame, idList<skaQuat_t>& worldQ, idList<double>& worldT) const {
	const int		numNodes = numBones + (addedOrigin ? 1 : 0);
	idList<skaQuat_t>	localQ;
	idList<double>	localT;			// 3 per bone
	int				i, k;

	worldQ.Clear();
	worldT.Clear();

	for (i = 0; i < numBones; i++) {
		skaQuat_t	q;
		double		t[3];

		SKA_BoneToLocal(baseFrame[i], q, t);
		localQ.Append(q);
		localT.Append(t[0]);
		localT.Append(t[1]);
		localT.Append(t[2]);
	}

	if (anim != NULL && boneOfAnimBone != NULL) {
		if (frame < 0) {
			frame = 0;
		}
		if (frame >= anim->NumFrames()) {
			frame = anim->NumFrames() - 1;
		}
		for (k = 0; k < anim->NumBones() && k < boneOfAnimBone->Num(); k++) {
			const int	b = (*boneOfAnimBone)[k];
			skbBone_t	sb;
			skaQuat_t	q;
			double		t[3];

			if (b < 0 || b >= numBones) {
				continue;
			}
			anim->GetBone(frame, k, sb);
			SKA_BoneToLocal(sb, q, t);
			localQ[b] = q;
			localT[b * 3 + 0] = t[0];
			localT[b * 3 + 1] = t[1];
			localT[b * 3 + 2] = t[2];
		}
	}

	// model space, parents come first in the .skb
	for (i = 0; i < numNodes; i++) {
		skaQuat_t	wq = { 0.0, 0.0, 0.0, 1.0 };
		double		wt[3] = { 0.0, 0.0, 0.0 };

		if (i < numBones) {
			const int p = boneParent[i];

			if (p < 0) {
				wq = localQ[i];
				wt[0] = localT[i * 3 + 0];
				wt[1] = localT[i * 3 + 1];
				wt[2] = localT[i * 3 + 2];
			}
			else {
				double lt[3] = { localT[i * 3 + 0], localT[i * 3 + 1], localT[i * 3 + 2] };
				double rt[3];

				SKA_QuatRotate(worldQ[p], lt, rt);
				wq = SKA_QuatNormalize(SKA_QuatMul(worldQ[p], localQ[i]));
				wt[0] = worldT[p * 3 + 0] + rt[0];
				wt[1] = worldT[p * 3 + 1] + rt[1];
				wt[2] = worldT[p * 3 + 2] + rt[2];
			}
		}
		worldQ.Append(wq);
		worldT.Append(wt[0]);
		worldT.Append(wt[1]);
		worldT.Append(wt[2]);
	}
}

/*
========================
idSKBSkeleton::JointOfName
========================
*/
int idSKBSkeleton::JointOfName(const char* boneName) const {
	const idStr	wanted = SKA_SanitizeJointName(boneName);

	for (int i = 0; i < joints.Num(); i++) {
		if (joints[i].name.Icmp(wanted.c_str()) == 0) {
			return i;
		}
	}
	return -1;
}

/*
========================
idSKBSkeleton::CalcWorld
========================
*/
void idSKBSkeleton::CalcWorld(const idSKBAnim* anim, const idList<int>* boneOfAnimBone, int frame, idList<float>& r, idList<float>& t) const {
	idList<skaQuat_t>	worldQ;
	idList<double>	worldT;

	BuildWorld(anim, boneOfAnimBone, frame, worldQ, worldT);

	r.SetNum(joints.Num() * 9);
	t.SetNum(joints.Num() * 3);
	for (int i = 0; i < joints.Num(); i++) {
		const int			node = (joints[i].bone >= 0) ? joints[i].bone : numBones;
		const skaQuat_t& q = worldQ[node];
		const double		xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
		const double		xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
		const double		wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

		r[i * 9 + 0] = (float)(1.0 - 2.0 * (yy + zz));
		r[i * 9 + 1] = (float)(2.0 * (xy - wz));
		r[i * 9 + 2] = (float)(2.0 * (xz + wy));
		r[i * 9 + 3] = (float)(2.0 * (xy + wz));
		r[i * 9 + 4] = (float)(1.0 - 2.0 * (xx + zz));
		r[i * 9 + 5] = (float)(2.0 * (yz - wx));
		r[i * 9 + 6] = (float)(2.0 * (xz - wy));
		r[i * 9 + 7] = (float)(2.0 * (yz + wx));
		r[i * 9 + 8] = (float)(1.0 - 2.0 * (xx + yy));
		for (int k = 0; k < 3; k++) {
			t[i * 3 + k] = (float)worldT[node * 3 + k];
		}
	}
}

/*
========================
idSKBSkeleton::CalcBones

1. The local pose of every .skb bone: from the animation frame for the bones it drives, from the
   base frame for the rest.
2. Those are turned into model space poses, bone after bone down the .skb's own tree.
3. Each joint is then expressed relative to its parent in the joint tree, which is where the
   changes around "origin" happen. The "origin" itself stays in model space.

Doom 3 stores the quaternion of a joint as the conjugate of the usual one (and only keeps x, y, z,
w always comes out positive), so the quaternion is conjugated on the way out. ToCQuat() on the result
then does the rest, the same as in the other loaders.
========================
*/
void idSKBSkeleton::CalcBones(const idSKBAnim* anim, const idList<int>* boneOfAnimBone, int frame, idList<idJointQuat>& pose) const {
	idList<skaQuat_t>	worldQ;
	idList<double>	worldT;			// 3 per node
	int				i;

	BuildWorld(anim, boneOfAnimBone, frame, worldQ, worldT);

	// relative to the parent in the joint tree
	pose.SetNum(joints.Num());
	for (i = 0; i < joints.Num(); i++) {
		const int	node = (joints[i].bone >= 0) ? joints[i].bone : numBones;
		skaQuat_t	q = worldQ[node];
		double		t[3] = { worldT[node * 3 + 0], worldT[node * 3 + 1], worldT[node * 3 + 2] };

		if (joints[i].parent >= 0) {
			const int	pn = (joints[joints[i].parent].bone >= 0) ? joints[joints[i].parent].bone : numBones;
			const skaQuat_t	pc = SKA_QuatConj(worldQ[pn]);
			double		d[3] = { t[0] - worldT[pn * 3 + 0], t[1] - worldT[pn * 3 + 1], t[2] - worldT[pn * 3 + 2] };

			q = SKA_QuatNormalize(SKA_QuatMul(pc, worldQ[node]));
			SKA_QuatRotate(pc, d, t);
		}

		pose[i].q.Set((float)-q.x, (float)-q.y, (float)-q.z, (float)q.w);
		pose[i].t.Set((float)t[0], (float)t[1], (float)t[2]);
	}
}

/*
========================
SKA_Line

Nine decimals, not %f's six: the quaternion is stored as three components and w is rebuilt as
sqrt( 1 - x*x - y*y - z*z ), which magnifies rounding by 1/w for rotations near 180 degrees.
With %f that cost about 1e-3 on idle1.
========================
*/
static const char* SKA_Line(char* dest, int size, const idVec3& t, const idCQuat& q, bool brackets = false) {
	if (brackets) {
		idStr::snPrintf(dest, size, "( %.9f %.9f %.9f ) ( %.9f %.9f %.9f )", t.x, t.y, t.z, q.x, q.y, q.z);
	}
	else {
		idStr::snPrintf(dest, size, "%.9f %.9f %.9f %.9f %.9f %.9f", t.x, t.y, t.z, q.x, q.y, q.z);
	}
	return dest;
}

/*
========================
idSKBSkeleton::WriteAnimToDisk

Same layout as idRenderModelMDS::WriteAnimToDisk: every joint fully animated, baseframe = frame 0.
Skipped if the file already exists, delete it to regenerate.
========================
*/
bool idSKBSkeleton::WriteAnimToDisk(const char* skaName) const {
	idStr			animName = skaName;
	idFile* fp;
	idSKBAnim		anim;
	idList<int>		boneMap;
	idList<idJointQuat> pose;
	char			line[256];
	int				i, f;

	if (joints.Num() == 0) {
		return false;
	}

	animName.SetFileExtension("md5anim");

	if (fileSystem->FindFile(animName, false) != FIND_NO) {
		common->DPrintf("Found %s for SKB model, skipping file generation\n", animName.c_str());
		return true;
	}

	if (!anim.Open(skaName) || !MapAnim(anim, boneMap)) {
		common->Warning("idSKBSkeleton: %s not converted", skaName);
		return false;
	}

	const float	rate = 1.0f / anim.FrameTime();
	int			fps = (int)floorf(rate + 0.5f);

	if (fps < 1) {
		fps = 1;
	}
	if (fabsf(rate - (float)fps) > 0.01f) {
		common->Warning("idSKBSkeleton: %s runs at %f frames per second, md5anim stores %i", skaName, rate, fps);
	}

	common->Printf("Writing %s\n", animName.c_str());
	fp = fileSystem->OpenFileWrite(animName, "fs_devpath");

	if (!fp) {
		common->Warning("WriteAnims: Error opening file %s\n", animName.c_str());
		return false;
	}

	fp->WriteFloatString("MD5Version 10\n\ncommandline \"\"\n");

	fp->WriteFloatString("numFrames %d\n", anim.NumFrames());
	fp->WriteFloatString("numJoints %d\n", joints.Num());
	fp->WriteFloatString("frameRate %d\n", fps);
	fp->WriteFloatString("numAnimatedComponents %d\n\n", joints.Num() * 6);

	fp->WriteFloatString("hierarchy {\n");
	for (i = 0; i < joints.Num(); i++) {
		fp->WriteFloatString("\t\"%s\" %d 63 %d\n", joints[i].name.c_str(), joints[i].parent, i * 6);
	}
	fp->WriteFloatString("}\n\n");

	fp->WriteFloatString("bounds {\n");
	for (f = 0; f < anim.NumFrames(); f++) {
		float b[2][3];

		anim.GetBounds(f, b);
		fp->WriteFloatString("( %f %f %f ) ( %f %f %f )\n", b[0][0], b[0][1], b[0][2], b[1][0], b[1][1], b[1][2]);
	}
	fp->WriteFloatString("}\n\n");

	// Baseframe, frame 0
	CalcBones(&anim, &boneMap, 0, pose);
	fp->WriteFloatString("baseframe {\n");
	for (i = 0; i < joints.Num(); i++) {
		idCQuat cQuat = pose[i].q.ToCQuat();

		fp->WriteFloatString("%s\n", SKA_Line(line, sizeof(line), pose[i].t, cQuat, true));
	}
	fp->WriteFloatString("}\n\n");

	for (f = 0; f < anim.NumFrames(); f++) {
		CalcBones(&anim, &boneMap, f, pose);
		fp->WriteFloatString("frame %d {\n", f);
		for (i = 0; i < joints.Num(); i++) {
			idCQuat cQuat = pose[i].q.ToCQuat();

			fp->WriteFloatString("%s\n", SKA_Line(line, sizeof(line), pose[i].t, cQuat));
		}
		fp->WriteFloatString("}\n\n");
	}

	fileSystem->CloseFile(fp);
	return true;
}

/*
===============================================================================

	idRenderModelSKB

===============================================================================
*/

/*
========================
idRenderModelSKB::WriteAnimsToDisk

Converts every .ska the .tik lists that doesn't have a .md5anim yet, with the skeleton the model was
built from, so the joints of the .md5anim and of the model are the same by construction. Returns false
if anything couldn't be written, which doesn't stop the model from loading.
========================
*/
bool idRenderModelSKB::WriteAnimsToDisk(const idTikFile& tik) const {
	idStrList	files;
	idStr		ext;
	int			i;
	bool		ok = true;

	if (skeleton == NULL) {
		return false;
	}

	tik.GetAnimFiles(files);

	// "loadAllAnims": every .ska in the path directory counts, named or not
	if (tik.loadAllAnims && tik.path.Length() > 0) {
		idStr dir = tik.path;

		dir.StripTrailing('/');
		idFileList* list = fileSystem->ListFiles(dir.c_str(), ".ska", false, true);

		for (i = 0; i < list->GetNumFiles(); i++) {
			bool seen = false;

			for (int j = 0; j < files.Num(); j++) {
				if (files[j].Icmp(list->GetFile(i)) == 0) {
					seen = true;
					break;
				}
			}
			if (!seen) {
				files.Append(list->GetFile(i));
			}
		}
		fileSystem->FreeFileList(list);
	}

	for (i = 0; i < files.Num(); i++) {
		idStr animName = files[i];

		files[i].ExtractFileExtension(ext);
		if (ext.Icmp("ska") != 0) {
			common->DPrintf("idRenderModelSKB: %s is not a ska, not converted\n", files[i].c_str());
			continue;
		}

		// WriteAnimToDisk skips the ones that exist already
		if (!skeleton->WriteAnimToDisk(files[i].c_str())) {
			ok = false;
		}
	}
	return ok;
}



// the snapshot model handed out by InstantiateDynamicModel, same idea as MDS_SNAPSHOT_NAME (Model_sil.h)
#ifndef SKB_SNAPSHOT_NAME
#define SKB_SNAPSHOT_NAME "_SKB_Snapshot_"
#endif

// Catch a layout mistake at compile time rather than as garbage geometry.
static_assert(sizeof(skbHeader_t) == 96, "skbHeader_t must match the on-disk size");
static_assert(sizeof(skbBoneInfo_t) == 72, "skbBoneInfo_t must match the on-disk size");
static_assert(sizeof(skbBone_t) == 16, "skbBone_t must match the on-disk size");
static_assert(sizeof(skbSurface_t) == 96, "skbSurface_t must match the on-disk size");
static_assert(sizeof(skbTriangle_t) == 12, "skbTriangle_t must match the on-disk size");
static_assert(sizeof(skbVertex_t) == 24, "skbVertex_t must match the on-disk size");
static_assert(sizeof(skbWeight_t) == 20, "skbWeight_t must match the on-disk size");

/*
========================
SKB_InRange

True if [ofs, ofs + count * stride) lies inside a buffer of the given size.
Done in 64 bit so hostile counts can't wrap around.
========================
*/
static bool SKB_InRange(int ofs, int count, int stride, int size) {
	if (ofs < 0 || count < 0) {
		return false;
	}
	return (long long)ofs + (long long)count * (long long)stride <= (long long)size;
}

/*
========================
SKB_StripExtension

Copies a name without its extension. Returns true if it had one. Only a '.' after the last '/'
counts, so "models/foo.bar/head" has none.
========================
*/
static bool SKB_StripExtension(const char* in, idStr& out) {
	const char* dot = NULL;

	for (const char* p = in; *p; p++) {
		if (*p == '/' || *p == '\\') {
			dot = NULL;
		}
		else if (*p == '.') {
			dot = p;
		}
	}

	out = in;
	if (dot == NULL) {
		return false;
	}

	char	tmp[1024];
	int		len = (int)(dot - in);

	if (len >= (int)sizeof(tmp)) {
		len = (int)sizeof(tmp) - 1;
	}
	memcpy(tmp, in, len);
	tmp[len] = '\0';
	out = tmp;
	return true;
}

/*
===============================================================================

	skbSource_t: one opened .skb, with its default pose worked out

===============================================================================
*/
class skbSource_t {
public:
	idStr			name;
	void* buffer;
	ID_TIME_T		timeStamp;
	skbHeader_t		header;
	int				numBones;

	skbSource_t() : buffer(NULL), timeStamp(0), numBones(0) {}
	~skbSource_t() {
		if (buffer != NULL) {
			fileSystem->FreeFile(buffer);
		}
	}

	bool			Open(const char* fileName);
	const byte* Data() const { return (const byte*)buffer; }

private:
	skbSource_t(const skbSource_t&);				// not copyable, it owns the file buffer
	skbSource_t& operator=(const skbSource_t&);
};

/*
========================
skbSource_t::Open

Reads the file and checks the header and the bone list.
Every offset and count read from the file is checked against the file size before it is used.
========================
*/
bool skbSource_t::Open(const char* fileName) {
	int		len, i;

	name = fileName;

	len = fileSystem->ReadFile(fileName, &buffer, &timeStamp);
	if (len <= 0 || buffer == NULL) {
		// a zero length file still comes back with a buffer, the destructor releases it
		common->Warning("idRenderModelSKB: couldn't read %s", fileName);
		return false;
	}

	if (len < (int)sizeof(header)) {
		common->Warning("idRenderModelSKB: %s is too small to be a skb", fileName);
		return false;
	}

	memcpy(&header, buffer, sizeof(header));
	header.version = LittleLong(header.version);
	header.numSurfaces = LittleLong(header.numSurfaces);
	header.numBones = LittleLong(header.numBones);
	header.ofsBones = LittleLong(header.ofsBones);
	header.ofsSurfaces = LittleLong(header.ofsSurfaces);
	header.ofsBoneBaseFrame = LittleLong(header.ofsBoneBaseFrame);
	header.ofsEnd = LittleLong(header.ofsEnd);

	if (memcmp(header.ident, "SKL ", 4) != 0) {
		common->Warning("idRenderModelSKB: %s is not a skb file", fileName);
		return false;
	}

	if (header.version != SKB_VERSION) {
		// version 3 has no base frame in the header, so there is no default pose to read
		common->Warning("idRenderModelSKB: %s is version %i, only version %i has a default pose", fileName, header.version, SKB_VERSION);
		return false;
	}

	if (header.ofsEnd > len) {
		common->Warning("idRenderModelSKB: header of %s is broken, declared size %i but file is %i", fileName, header.ofsEnd, len);
		return false;
	}

	numBones = header.numBones;
	if (numBones < 1 || numBones > SKB_MAX_BONES) {
		common->Warning("idRenderModelSKB: %s has %i bones (limit is %i)", fileName, numBones, SKB_MAX_BONES);
		return false;
	}

	if (header.numSurfaces < 1) {
		common->Warning("idRenderModelSKB: %s has no surfaces", fileName);
		return false;
	}

	if (!SKB_InRange(header.ofsBones, numBones, sizeof(skbBoneInfo_t), len) ||
		!SKB_InRange(header.ofsBoneBaseFrame, numBones, sizeof(skbBone_t), len)) {
		common->Warning("idRenderModelSKB: %s has bone data outside the file", fileName);
		return false;
	}

	const byte* data = Data();

	// the skeleton needs the parent of a bone to come before it (they do in every file seen so far)
	for (i = 0; i < numBones; i++) {
		skbBoneInfo_t	info;

		memcpy(&info, data + header.ofsBones + i * sizeof(info), sizeof(info));

		const int parent = LittleLong(info.parent);
		if (parent < -1 || parent >= i) {
			common->Warning("idRenderModelSKB: %s bone %i has parent %i, which doesn't come before it", fileName, i, parent);
			return false;
		}
	}

	return true;
}

/*
===============================================================================

	Surfaces

===============================================================================
*/

struct skbSurfaceRef_t {
	skbSurface_t	surf;					// byte swapped
	char			name[SKB_NAME_LEN + 1];
	int				ofs;					// of the surface header in the file
	int				end;					// where the next surface starts
};

/*
========================
SKB_ReadSurface

Reads and checks the header of the surface at surfOfs.
========================
*/
static bool SKB_ReadSurface(const skbSource_t& src, int index, int surfOfs, skbSurfaceRef_t& ref) {
	const char* modName = src.name.c_str();

	if (!SKB_InRange(surfOfs, 1, sizeof(skbSurface_t), src.header.ofsEnd)) {
		common->Warning("idRenderModelSKB: %s surface %i header is outside the file", modName, index);
		return false;
	}

	memcpy(&ref.surf, src.Data() + surfOfs, sizeof(ref.surf));
	ref.surf.numTriangles = LittleLong(ref.surf.numTriangles);
	ref.surf.numVerts = LittleLong(ref.surf.numVerts);
	ref.surf.ofsTriangles = LittleLong(ref.surf.ofsTriangles);
	ref.surf.ofsVerts = LittleLong(ref.surf.ofsVerts);
	ref.surf.ofsEnd = LittleLong(ref.surf.ofsEnd);

	memcpy(ref.name, ref.surf.name, SKB_NAME_LEN);
	ref.name[SKB_NAME_LEN] = '\0';

	if (ref.surf.ofsEnd < (int)sizeof(ref.surf) || !SKB_InRange(surfOfs, ref.surf.ofsEnd, 1, src.header.ofsEnd)) {
		common->Warning("idRenderModelSKB: %s surface %i (%s) has a bad size", modName, index, ref.name);
		return false;
	}

	if (ref.surf.numVerts < 0 || ref.surf.numVerts > SKB_MAX_VERTS || ref.surf.numTriangles < 0 || ref.surf.numTriangles > SKB_MAX_VERTS * 2) {
		common->Warning("idRenderModelSKB: %s surface %i (%s) has %i verts and %i triangles", modName, index, ref.name, ref.surf.numVerts, ref.surf.numTriangles);
		return false;
	}

	ref.ofs = surfOfs;
	ref.end = surfOfs + ref.surf.ofsEnd;
	return true;
}

/*
========================
SKB_FindSurface

Looks a surface up by name. False if there isn't one (or the surface chain is damaged).
========================
*/
static bool SKB_FindSurface(const skbSource_t& src, const char* surfName, skbSurfaceRef_t& ref) {
	int surfOfs = src.header.ofsSurfaces;

	for (int i = 0; i < src.header.numSurfaces; i++) {
		skbSurfaceRef_t r;

		if (!SKB_ReadSurface(src, i, surfOfs, r)) {
			return false;
		}
		if (idStr::Icmp(r.name, surfName) == 0) {
			ref = r;
			return true;
		}
		surfOfs = r.end;
	}
	return false;
}

/*
========================
SKB_BuildJointMap

For each bone of src, the joint of the skeleton it is skinned to. Joints are found by name, which for
the .skb the skeleton was made from gives the same answer as JointOfBone(). -1 for a bone the
skeleton doesn't have.
========================
*/
static void SKB_BuildJointMap(const skbSource_t& src, const idSKBSkeleton& skeleton, idList<int>& map) {
	map.Clear();
	for (int i = 0; i < src.numBones; i++) {
		skbBoneInfo_t	info;
		char			boneName[SKB_NAME_LEN + 1];

		memcpy(&info, src.Data() + src.header.ofsBones + i * sizeof(info), sizeof(info));
		memcpy(boneName, info.name, SKB_NAME_LEN);
		boneName[SKB_NAME_LEN] = '\0';
		map.Append(skeleton.JointOfName(boneName));
	}
}

static bool SKB_Finite(float v) {
	return v > -1e9f && v < 1e9f;		// false for NaN too
}

/*
========================
SKB_ReadSurfaceData

Appends one surface to the shared lists, with its weights pointing at joints. Nothing is appended
(and false is returned, with a warning) if the data is damaged. A surface with nothing in it is not
an error: out.numVerts is 0 and nothing is appended.
========================
*/
static bool SKB_ReadSurfaceData(const skbSource_t& src, const skbSurfaceRef_t& ref, int index, const idList<int>& jointMap,
	idList<skbDynVert_t>& verts, idList<skbDynWeight_t>& weights, idList<int>& indexes, skbDynSurface_t& out) {
	const char* modName = src.name.c_str();
	const char* surfName = ref.name;
	const skbSurface_t& surf = ref.surf;
	const byte* buffer = src.Data();
	const int		oldVerts = verts.Num(), oldWeights = weights.Num(), oldIndexes = indexes.Num();
	int				j, k;

	memset(&out, 0, sizeof(out));

	// nothing to draw
	if (surf.numVerts == 0 || surf.numTriangles == 0) {
		return true;
	}

	// check the offsets relative to the surface first, so the additions below can't overflow
	if (surf.ofsTriangles < (int)sizeof(surf) || surf.ofsVerts < (int)sizeof(surf) ||
		!SKB_InRange(surf.ofsTriangles, surf.numTriangles, sizeof(skbTriangle_t), surf.ofsEnd) ||
		surf.ofsVerts > surf.ofsEnd) {
		common->Warning("idRenderModelSKB: %s surface %i (%s) has data outside the surface", modName, index, surfName);
		return false;
	}
	const int triOfs = ref.ofs + surf.ofsTriangles;
	const int vertOfs = ref.ofs + surf.ofsVerts;

	// vertices are variable length (a header followed by numWeights weights), so walk them
	const byte* vp = buffer + vertOfs;
	const byte* vEnd = buffer + ref.end;

	for (j = 0; j < surf.numVerts; j++) {
		skbVertex_t		vert;
		skbDynVert_t	dv;

		if (vEnd - vp < (int)sizeof(vert)) {
			common->Warning("idRenderModelSKB: %s surface %i (%s) vertex %i is outside the surface", modName, index, surfName, j);
			goto fail;
		}

		memcpy(&vert, vp, sizeof(vert));
		vp += sizeof(vert);

		const int numWeights = LittleLong(vert.numWeights);
		if (numWeights < 1 || numWeights > SKB_MAX_WEIGHTS || (vEnd - vp) / (int)sizeof(skbWeight_t) < numWeights) {
			common->Warning("idRenderModelSKB: %s surface %i (%s) vertex %i has %i weights", modName, index, surfName, j, numWeights);
			goto fail;
		}

		for (k = 0; k < 3; k++) {
			dv.normal[k] = LittleFloat(vert.normal[k]);
			if (!SKB_Finite(dv.normal[k])) {
				common->Warning("idRenderModelSKB: %s surface %i (%s) vertex %i has an invalid normal", modName, index, surfName, j);
				goto fail;
			}
		}
		dv.st[0] = LittleFloat(vert.texCoords[0]);
		dv.st[1] = LittleFloat(vert.texCoords[1]);
		dv.firstWeight = weights.Num() - oldWeights;
		dv.numWeights = numWeights;

		for (k = 0; k < numWeights; k++) {
			skbWeight_t		w;
			skbDynWeight_t	dw;

			memcpy(&w, vp, sizeof(w));
			vp += sizeof(w);

			const int boneIndex = LittleLong(w.boneIndex);

			if (boneIndex < 0 || boneIndex >= src.numBones) {
				common->Warning("idRenderModelSKB: %s surface %i (%s) vertex %i uses bone %i", modName, index, surfName, j, boneIndex);
				goto fail;
			}
			if (jointMap[boneIndex] < 0) {
				common->Warning("idRenderModelSKB: %s surface %i (%s) vertex %i uses bone %i, which the model's skeleton doesn't have", modName, index, surfName, j, boneIndex);
				goto fail;
			}

			dw.joint = jointMap[boneIndex];
			dw.weight = LittleFloat(w.boneWeight);
			for (int c = 0; c < 3; c++) {
				dw.offset[c] = LittleFloat(w.offset[c]);
			}
			if (!SKB_Finite(dw.weight) || !SKB_Finite(dw.offset[0]) || !SKB_Finite(dw.offset[1]) || !SKB_Finite(dw.offset[2])) {
				common->Warning("idRenderModelSKB: %s surface %i (%s) vertex %i has an invalid weight", modName, index, surfName, j);
				goto fail;
			}
			weights.Append(dw);
		}
		verts.Append(dv);
	}

	// The stored winding already is the one Doom 3 wants (the normal comes out of
	// (c - a) x (b - a)), so the indexes are copied as they are. The Blender importer
	// swaps two of them because Blender uses the opposite winding, don't copy that.
	for (j = 0; j < surf.numTriangles; j++) {
		skbTriangle_t tr;

		memcpy(&tr, buffer + triOfs + j * sizeof(tr), sizeof(tr));

		for (k = 0; k < 3; k++) {
			const int index3 = LittleLong(tr.indexes[k]);

			if (index3 < 0 || index3 >= surf.numVerts) {
				common->Warning("idRenderModelSKB: %s surface %i (%s) triangle %i uses vertex %i of %i", modName, index, surfName, j, index3, surf.numVerts);
				goto fail;
			}
			indexes.Append(index3);
		}
	}

	out.firstVert = oldVerts;
	out.numVerts = surf.numVerts;
	out.firstIndex = oldIndexes;
	out.numIndexes = surf.numTriangles * 3;
	// firstWeight of each vertex was stored relative to this surface, make it absolute
	for (j = oldVerts; j < verts.Num(); j++) {
		verts[j].firstWeight += oldWeights;
	}
	return true;

fail:
	verts.SetNum(oldVerts);
	weights.SetNum(oldWeights);
	indexes.SetNum(oldIndexes);
	return false;
}

/*
===============================================================================

	skbSourceCache_t: the extra .skb files a .tik pulls surfaces from (replacesurface)

===============================================================================
*/
class skbSourceCache_t {
public:
	~skbSourceCache_t() {
		for (int i = 0; i < sources.Num(); i++) {
			delete sources[i];
		}
	}

	// NULL if the file can't be used. A file that failed once is neither retried nor warned about again.
	const skbSource_t* Get(const char* fileName) {
		for (int i = 0; i < names.Num(); i++) {
			if (names[i].Icmp(fileName) == 0) {
				return sources[i];
			}
		}

		skbSource_t* src = new skbSource_t;
		if (!src->Open(fileName)) {
			delete src;
			src = NULL;
		}
		names.Append(fileName);
		sources.Append(src);
		return src;
	}

private:
	idStrList				names;
	idList<skbSource_t*>	sources;
};

/*
===============================================================================

	idRenderModelSKB

===============================================================================
*/

/*
========================
SKB_JointMats

The joint matrices the Doom 3 animator makes of a pose: every joint from its idJointQuat, then
multiplied by its parent (parents come first). Used for the default pose and to check conventions.
========================
*/
static void SKB_JointMats(const idList<idMD5Joint>& joints, const idList<idJointQuat>& pose, idList<idJointMat>& mats) {
	mats.SetNum(joints.Num());
	for (int i = 0; i < joints.Num(); i++) {
		mats[i].SetRotation(pose[i].q.ToMat3());
		mats[i].SetTranslation(pose[i].t);
		if (joints[i].parent != NULL) {
			mats[i] *= mats[joints[i].parent - joints.Ptr()];
		}
	}
}

/*
========================
idRenderModelSKB::idRenderModelSKB
========================
*/
idRenderModelSKB::idRenderModelSKB() {
	skeleton = NULL;
	skinTranspose = false;
	dataSize = 0;
}

/*
========================
idRenderModelSKB::~idRenderModelSKB
========================
*/
idRenderModelSKB::~idRenderModelSKB() {
	ClearData();
	rvmSilCache_Free(this);
}

/*
========================
idRenderModelSKB::ClearData
========================
*/
void idRenderModelSKB::ClearData() {
	delete skeleton;
	skeleton = NULL;
	joints.Clear();
	defaultPose.Clear();
	dynSurfaces.Clear();
	dynVerts.Clear();
	dynWeights.Clear();
	dynIndexes.Clear();
	skinTranspose = false;
	dataSize = 0;
}

/*
========================
idRenderModelSKB::InitFromFile
========================
*/
void idRenderModelSKB::InitFromFile(const char* fileName) {
	name = fileName;
	LoadModel();
}

/*
========================
idRenderModelSKB::LoadModel
========================
*/
void idRenderModelSKB::LoadModel(void) {
	idStr	ext;
	bool	loaded;

	if (!purged) {
		PurgeModel();
	}
	purged = false;
	defaulted = false;
	rvmSilCache_Free(this);
	ClearData();

	name.ExtractFileExtension(ext);
	if (ext.Icmp("tik") == 0) {
		loaded = LoadTik();
	}
	else {
		loaded = LoadRawSKB();
	}

	if (!loaded) {
		// MakeDefaultModel() throws away any surfaces we already added
		ClearData();
		MakeDefaultModel();
		return;
	}

	// The default pose as ordinary static surfaces (and the bounds), for anything that draws the model
	// without an animator. Animated entities get their own surfaces from InstantiateDynamicModel.
	idList<idJointMat> mats;

	SKB_JointMats(joints, defaultPose, mats);
	bounds.Clear();
	SkinSurfaces(this, mats.Ptr());
}

/*
========================
idRenderModelSKB::LoadRawSKB

A .skb on its own: every surface is drawn, materials are looked up by surface name.
========================
*/
bool idRenderModelSKB::LoadRawSKB() {
	return BuildFromSKB(name.c_str(), NULL);
}

/*
========================
idRenderModelSKB::LoadTik
========================
*/
bool idRenderModelSKB::LoadTik() {
	idTikFile	tik;
	idStr		ext;

	if (!tik.Parse(name.c_str())) {
		common->Warning("idRenderModelSKB: couldn't read %s", name.c_str());
		return false;
	}

	if (tik.skelModels.Num() == 0) {
		common->Warning("idRenderModelSKB: %s doesn't name a model (no skelmodel)", name.c_str());
		return false;
	}

	if (tik.skelModels.Num() > 1) {
		common->Warning("idRenderModelSKB: %s lists %i models, only the first (%s) is loaded", name.c_str(), tik.skelModels.Num(), tik.skelModels[0].c_str());
	}

	tik.skelModels[0].ExtractFileExtension(ext);
	if (ext.Icmp("skb") != 0) {
		common->Warning("idRenderModelSKB: %s uses %s, which is not a skb", name.c_str(), tik.skelModels[0].c_str());
		return false;
	}

	if (!BuildFromSKB(tik.skelModels[0].c_str(), &tik)) {
		return false;
	}

	// reloading is decided by the .tik, not the .skb it points at
	timeStamp = tik.timeStamp;

	// one .md5anim per .ska the .tik lists (existing files are left alone); it warns for itself
	WriteAnimsToDisk(tik);
	return true;
}

/*
========================
idRenderModelSKB::FindSurfaceMaterial

The material for a surface, in this order:
  1. the first "surface <name> shader <shader>" of the .tik. It is looked up the way the shader
	 is written: with a file extension ("head.tga") it is an image next to the "texturepath" that
	 was in force, without one ("dm/hazardteam/male/normal-red", "munrohairalpha") it is a
	 shader name first and relative to the texturepath second
  2. a material called <surface name> in the directory of the .skb, same as the MDS loader
The first shader is the default skin, the team colour alternatives are ignored.
========================
*/
const idMaterial* idRenderModelSKB::FindSurfaceMaterial(const char* surfaceName, const idTikFile* tik, const char* fallbackDir) const {
	const idTikFile::surface_t* ts = (tik != NULL) ? tik->FindSurface(surfaceName) : NULL;

	if (ts == NULL || ts->shaders.Num() == 0) {
		return declManager->FindMaterial(va("%s%s", fallbackDir, surfaceName), true);
	}

	const idTikFile::shader_t& sh = ts->shaders[0];
	idStr	stripped, relative;
	idStr	candidate[2];
	int		numCandidates = 0;

	const bool hasExtension = SKB_StripExtension(sh.shader.c_str(), stripped);
	stripped.BackSlashesToSlashes();

	relative = sh.texturePath.c_str();
	relative += stripped.c_str();
	relative.BackSlashesToSlashes();

	const bool haveTexturePath = (sh.texturePath.Length() > 0);

	if (hasExtension && haveTexturePath) {
		candidate[numCandidates++] = relative.c_str();
		candidate[numCandidates++] = stripped.c_str();
	}
	else if (haveTexturePath) {
		candidate[numCandidates++] = stripped.c_str();
		candidate[numCandidates++] = relative.c_str();
	}
	else {
		candidate[numCandidates++] = stripped.c_str();
	}

	for (int i = 0; i < numCandidates; i++) {
		const idMaterial* material = declManager->FindMaterial(candidate[i].c_str(), false);

		if (material != NULL) {
			return material;
		}
	}

	common->Warning("idRenderModelSKB: %s: no material for surface %s (shader %s)", name.c_str(), surfaceName, sh.shader.c_str());
	return declManager->FindMaterial(candidate[0].c_str(), true);
}

static float SKB_Max(float a, float b) {
	return (a > b) ? a : b;
}

/*
========================
idRenderModelSKB::BuildJoints

Joints and default pose from the skeleton. The default pose goes through the same conversion as the
.md5anim baseframe (x, y, z only, w rebuilt as positive), so the two always agree.

The animator builds its joint matrices with idQuat::ToMat3() and idJointMat's *=, and this
cannot be settled from the outside which way round a skinning matrix has to be used, so it is
checked here: the default pose is run through exactly that code and compared with the model space
transforms the skeleton works out by itself, once as they are and once transposed. The one that
matches is what SkinSurfaces uses. If neither matches the conversion is wrong somewhere and that
is said.
========================
*/
void idRenderModelSKB::BuildJoints() {
	const int			n = skeleton->NumJoints();
	idList<idJointQuat>	pose;
	idList<idJointMat>	mats;
	idList<float>		R, T;
	int					i, r, c;

	joints.SetGranularity(1);
	joints.SetNum(n);
	for (i = 0; i < n; i++) {
		joints[i].name = skeleton->Joint(i).name;
		joints[i].parent = (skeleton->Joint(i).parent < 0) ? NULL : &joints[skeleton->Joint(i).parent];
	}

	skeleton->CalcBones(NULL, NULL, 0, pose);
	defaultPose.SetGranularity(1);
	defaultPose.SetNum(n);
	for (i = 0; i < n; i++) {
		idJointQuat jointQuat = pose[i];

		jointQuat.q = jointQuat.q.ToCQuat().ToQuat();
		defaultPose[i] = jointQuat;
	}

	SKB_JointMats(joints, defaultPose, mats);
	skeleton->CalcWorld(NULL, NULL, 0, R, T);

	float errAsIs = 0.0f, errTransposed = 0.0f, errPos = 0.0f;
	for (i = 0; i < n; i++) {
		const idMat3	m = mats[i].ToMat3();
		const idVec3	p = mats[i].ToVec3();

		for (r = 0; r < 3; r++) {
			for (c = 0; c < 3; c++) {
				errAsIs = SKB_Max(errAsIs, idMath::Fabs(m[r][c] - R[i * 9 + r * 3 + c]));
				errTransposed = SKB_Max(errTransposed, idMath::Fabs(m[r][c] - R[i * 9 + c * 3 + r]));
			}
			errPos = SKB_Max(errPos, idMath::Fabs(p[r] - T[i * 3 + r]));
		}
	}

	skinTranspose = (errTransposed < errAsIs);
	const float errRot = skinTranspose ? errTransposed : errAsIs;

	if (errRot > 0.01f || errPos > 0.1f) {
		common->Warning("idRenderModelSKB: %s: the default pose built by the animator doesn't match the .skb (rotation %f, position %f), the model will look wrong", name.c_str(), errRot, errPos);
	}
	else {
		common->DPrintf("idRenderModelSKB: %s: skinning with the %s joint matrices (rotation error %g, position error %g)\n", name.c_str(), skinTranspose ? "transposed" : "plain", errRot, errPos);
	}
}

/*
========================
idRenderModelSKB::BuildFromSKB

Reads the skeleton and the surfaces of a .skb. With a .tik, the surfaces it marks nodraw are left out,
and the ones it replaces come from another .skb (their weights are matched to our joints by bone name).
========================
*/
bool idRenderModelSKB::BuildFromSKB(const char* skbName, const idTikFile* tik) {
	skbSource_t			primary;
	skbSourceCache_t	others;
	idList<int>			primaryMap;
	idStr				skbDir;
	int					numAdded = 0, numSkipped = 0, numReplaced = 0;

	if (!primary.Open(skbName)) {
		return false;
	}

	// with a .tik the .tik's timestamp is what counts, LoadTik sets that
	if (tik == NULL) {
		timeStamp = primary.timeStamp;
	}

	skeleton = new idSKBSkeleton;
	if (!skeleton->Load(skbName)) {
		return false;
	}
	BuildJoints();

	primaryMap.SetNum(primary.numBones);
	for (int b = 0; b < primary.numBones; b++) {
		primaryMap[b] = skeleton->JointOfBone(b);
	}

	{
		idStr full = skbName;
		full.ExtractFilePath(skbDir);
	}

	int surfOfs = primary.header.ofsSurfaces;

	for (int i = 0; i < primary.header.numSurfaces; i++) {
		skbSurfaceRef_t	ref;

		if (!SKB_ReadSurface(primary, i, surfOfs, ref)) {
			return false;
		}
		surfOfs = ref.end;

		const idTikFile::surface_t* ts = (tik != NULL) ? tik->FindSurface(ref.name) : NULL;

		if (ts != NULL && (ts->flags & TIK_SURF_NODRAW)) {
			numSkipped++;
			continue;
		}

		// where the geometry comes from: this file, unless the .tik says otherwise
		const skbSource_t* src = &primary;
		skbSurfaceRef_t		use = ref;
		const idList<int>* map = &primaryMap;
		idList<int>			otherMap;

		const idTikFile::replace_t* rep = (tik != NULL) ? tik->FindReplace(ref.name) : NULL;
		if (rep != NULL) {
			const skbSource_t* other = others.Get(rep->file.c_str());	// warns by itself if it can't be used
			skbSurfaceRef_t found;

			if (other != NULL) {
				if (SKB_FindSurface(*other, rep->newSurface.c_str(), found)) {
					src = other;
					use = found;
					SKB_BuildJointMap(*other, *skeleton, otherMap);
					map = &otherMap;
				}
				else {
					common->Warning("idRenderModelSKB: %s: replacesurface %s: %s has no surface %s, keeping the original", name.c_str(), ref.name, rep->file.c_str(), rep->newSurface.c_str());
				}
			}
		}

		skbDynSurface_t	ds;

		if (!SKB_ReadSurfaceData(*src, use, i, *map, dynVerts, dynWeights, dynIndexes, ds)) {
			if (src == &primary) {
				return false;
			}
			// the replacement is damaged (already warned about), fall back on the original
			src = &primary;
			if (!SKB_ReadSurfaceData(primary, ref, i, primaryMap, dynVerts, dynWeights, dynIndexes, ds)) {
				return false;
			}
		}
		else if (src != &primary && ds.numVerts > 0) {
			numReplaced++;
		}

		if (ds.numVerts == 0) {
			continue;
		}

		// the material belongs to the slot in the model ("material1"), not to where the mesh came from
		ds.shader = FindSurfaceMaterial(ref.name, tik, skbDir.c_str());
		dynSurfaces.Append(ds);
		numAdded++;
	}

	if (numAdded == 0) {
		common->Warning("idRenderModelSKB: %s has nothing to draw", skbName);
		return false;
	}

	dataSize = (int)(dynVerts.Num() * sizeof(skbDynVert_t) + dynWeights.Num() * sizeof(skbDynWeight_t) +
		dynIndexes.Num() * sizeof(int) + dynSurfaces.Num() * sizeof(skbDynSurface_t) +
		joints.Num() * (sizeof(idMD5Joint) + sizeof(idJointQuat)));

	common->DPrintf("idRenderModelSKB: %s: %i surfaces (%i not drawn, %i replaced), %i joints, %i verts, %i triangles\n",
		skbName, numAdded, numSkipped, numReplaced, joints.Num(), dynVerts.Num(), dynIndexes.Num() / 3);
	return true;
}

/*
========================
idRenderModelSKB::SkinSurfaces

Skins every surface with the given joint matrices (model space, as the animator hands them out) into
dst, reusing the surfaces dst already has.
========================
*/
void idRenderModelSKB::SkinSurfaces(idRenderModelStatic* dst, const idJointMat* jointMats) {
	idList<idMat3>	rots;
	int				i, j, k;

	rots.SetNum(joints.Num());
	for (i = 0; i < joints.Num(); i++) {
		rots[i] = jointMats[i].ToMat3();
		if (skinTranspose) {
			rots[i] = rots[i].Transpose();
		}
	}

	for (i = 0; i < dynSurfaces.Num(); i++) {
		const skbDynSurface_t& ds = dynSurfaces[i];

		// md3/md5 style: reuse the surface and tri from the cached snapshot, rewrite verts only
		modelSurface_t& surf = *rvmAcquireSurface(dst, i, ds.numVerts);
		srfTriangles_t* tri = surf.geometry;
		surf.shader = ds.shader;

		for (j = 0; j < ds.numVerts; j++) {
			const skbDynVert_t& v = dynVerts[ds.firstVert + j];
			idVec3	pos, nor;

			pos.Zero();
			nor.Zero();

			for (k = 0; k < v.numWeights; k++) {
				const skbDynWeight_t& w = dynWeights[v.firstWeight + k];
				const idMat3& m = rots[w.joint];
				const idVec3	t = jointMats[w.joint].ToVec3();

				pos.x += w.weight * (m[0][0] * w.offset[0] + m[0][1] * w.offset[1] + m[0][2] * w.offset[2] + t.x);
				pos.y += w.weight * (m[1][0] * w.offset[0] + m[1][1] * w.offset[1] + m[1][2] * w.offset[2] + t.y);
				pos.z += w.weight * (m[2][0] * w.offset[0] + m[2][1] * w.offset[1] + m[2][2] * w.offset[2] + t.z);

				nor.x += w.weight * (m[0][0] * v.normal[0] + m[0][1] * v.normal[1] + m[0][2] * v.normal[2]);
				nor.y += w.weight * (m[1][0] * v.normal[0] + m[1][1] * v.normal[1] + m[1][2] * v.normal[2]);
				nor.z += w.weight * (m[2][0] * v.normal[0] + m[2][1] * v.normal[1] + m[2][2] * v.normal[2]);
			}

			const float len = idMath::Sqrt(nor.x * nor.x + nor.y * nor.y + nor.z * nor.z);
			if (len > 1e-6f) {
				nor *= 1.0f / len;
			}
			else {
				nor.Set(0.0f, 0.0f, 1.0f);
			}

			idDrawVert& dv = tri->verts[j];

			dv.xyz = pos;
			dv.normal = nor;
			dv.st[0] = v.st[0];
			dv.st[1] = v.st[1];
		}

		// the file has its own normals, don't let anything throw them away
		tri->generateNormals = false;
		tri->numVerts += ds.numVerts;

		// stencil shadows: silhouette info depends only on topology, so it is built
		// once from the first skinned pose and shared by every frame (see Model_sil.h)
		rvmSilCache_Apply(this, i, tri, &dynIndexes[ds.firstIndex], ds.numIndexes, 0);

		R_BoundTriSurf(tri);
		R_DeriveTangents(tri);

		dst->bounds.AddPoint(surf.geometry->bounds[0]);
		dst->bounds.AddPoint(surf.geometry->bounds[1]);
	}
}

/*
=================
idRenderModelSKB::IsDynamicModel
=================
*/
dynamicModel_t idRenderModelSKB::IsDynamicModel() const {
	return DM_CACHED;
}

/*
=================
idRenderModelSKB::TouchData
=================
*/
void idRenderModelSKB::TouchData() {
}

/*
=================
idRenderModelSKB::Bounds
=================
*/
idBounds idRenderModelSKB::Bounds(const class idRenderEntity* ent) const {
	return bounds;
}

/*
=================
idRenderModelSKB::Memory
=================
*/
int idRenderModelSKB::Memory() const {
	return dataSize;
}

/*
=================
idRenderModelSKB::NumJoints
=================
*/
int idRenderModelSKB::NumJoints(void) const {
	return joints.Num();
}

/*
=================
idRenderModelSKB::GetJoints
=================
*/
const idMD5Joint* idRenderModelSKB::GetJoints(void) const {
	if (!joints.Num()) {
		common->Error("Model has no Joints");
	}
	return &joints[0];
}

/*
=================
idRenderModelSKB::GetJointHandle
=================
*/
jointHandle_t idRenderModelSKB::GetJointHandle(const char* jointName) const {
	const idMD5Joint* joint = joints.Ptr();

	for (int i = 0; i < joints.Num(); i++, joint++) {
		if (idStr::Icmp(joint->name.c_str(), jointName) == 0) {
			return (jointHandle_t)i;
		}
	}
	return INVALID_JOINT;
}

/*
=================
idRenderModelSKB::GetJointName
=================
*/
const char* idRenderModelSKB::GetJointName(jointHandle_t handle) const {
	if ((handle < 0) || (handle >= joints.Num())) {
		return "<invalid joint>";
	}
	return joints[handle].name;
}

/*
=================
idRenderModelSKB::GetDefaultPose
=================
*/
const idJointQuat* idRenderModelSKB::GetDefaultPose(void) const {
	return defaultPose.Ptr();
}

/*
=================
idRenderModelSKB::NearestJoint

The joint with the most weight on the three vertices of the triangle.
=================
*/
int idRenderModelSKB::NearestJoint(int surfaceNum, int a, int b, int c) const {
	if (surfaceNum < 0 || surfaceNum >= dynSurfaces.Num()) {
		return 0;
	}

	const skbDynSurface_t& ds = dynSurfaces[surfaceNum];
	const int				vertNum[3] = { a, b, c };
	idList<float>			total;
	int						i, k, best = 0;

	total.SetNum(joints.Num());
	for (i = 0; i < joints.Num(); i++) {
		total[i] = 0.0f;
	}

	for (i = 0; i < 3; i++) {
		if (vertNum[i] < 0 || vertNum[i] >= ds.numVerts) {
			continue;
		}
		const skbDynVert_t& v = dynVerts[ds.firstVert + vertNum[i]];

		for (k = 0; k < v.numWeights; k++) {
			const skbDynWeight_t& w = dynWeights[v.firstWeight + k];

			total[w.joint] += w.weight;
		}
	}

	for (i = 1; i < joints.Num(); i++) {
		if (total[i] > total[best]) {
			best = i;
		}
	}
	return best;
}

/*
====================
idRenderModelSKB::DrawJoints
====================
*/
void idRenderModelSKB::DrawJoints(const renderEntity_t* ent, const struct viewDef_s* view) const {
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
idRenderModelSKB::InstantiateDynamicModel
=================
*/
idRenderModel* idRenderModelSKB::InstantiateDynamicModel(const struct renderEntity_s* ent, const struct viewDef_s* view, idRenderModel* cachedModel) {
	idRenderModelStatic* staticModel;

	if (cachedModel && idStr::Icmp(cachedModel->Name(), SKB_SNAPSHOT_NAME) != 0) {
		delete cachedModel;
		cachedModel = NULL;
	}

	if (purged) {
		common->DWarning("model %s instantiated while purged", Name());
		if (cachedModel) {
			delete cachedModel;
			cachedModel = NULL;
		}
		LoadModel();
	}

	if (!ent || !ent->joints) {
		common->Printf("idRenderModelSKB::InstantiateDynamicModel: NULL joints on renderEntity for '%s'\n", Name());
		delete cachedModel;
		return NULL;
	}
	else if (ent->numJoints != joints.Num()) {
		common->Printf("idRenderModelSKB::InstantiateDynamicModel: renderEntity has different number of joints than model for '%s'\n", Name());
		delete cachedModel;
		return NULL;
	}

	if (cachedModel) {
		assert(dynamic_cast<idRenderModelStatic*>(cachedModel) != NULL);
		staticModel = static_cast<idRenderModelStatic*>(cachedModel);
	}
	else {
		staticModel = new idRenderModelStatic;
		staticModel->InitEmpty(SKB_SNAPSHOT_NAME);
	}

	staticModel->bounds.Clear();

	SkinSurfaces(staticModel, ent->joints);

	if (r_showSkel.GetInteger()) {
		if ((view != NULL) && (!r_skipSuppress.GetBool() || !ent->suppressSurfaceInViewID || (ent->suppressSurfaceInViewID != view->renderView.viewID))) {
			// only draw the skeleton
			DrawJoints(ent, view);
		}
	}

	return staticModel;
}

