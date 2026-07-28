#include <willpower/application/resourcesystem/ResourceManager.h>
#include <willpower/common/StringUtils.h>

#include "MapTungstenMonoxideDefinitionFactory.h"
#include "Map.h"


MapTungstenMonoxideDefinitionFactory::MapTungstenMonoxideDefinitionFactory()
	: applib::MapResourceDefinitionFactory("Track")
{
}

// Reads one Vec3-shaped attribute triple off `node` using the given attribute-name prefix (e.g.
// "p" for px/py/pz), mirroring buildTrackResourceXml's formatCoord() output.
static tox::Vec3 readVec3Attribs(wp::XmlNode* node, char const* prefix)
{
	tox::Vec3 v;
	v.x = wp::StringUtils::parseFloat(node->getAttribute(std::string(prefix) + "x"));
	v.y = wp::StringUtils::parseFloat(node->getAttribute(std::string(prefix) + "y"));
	v.z = wp::StringUtils::parseFloat(node->getAttribute(std::string(prefix) + "z"));
	return v;
}

// Reads the .mppmodel filename out of <Definition factory="Track"><File>...</File></Definition>
// and stashes it on Map::mModelFileName, for Map::load() to resolve against the resource's
// DirectoryResourceLocation. This can't just be the Resource's own `location`/getSource(): Track
// is a composite resource now (it lists TrackMaterial dependents), and
// ResourceManager::instantiateResource() unconditionally forces a composite resource's `source` to
// "" regardless of any `location=` attribute -- see Map.h's comment on mModelFileName.
void MapTungstenMonoxideDefinitionFactory::create(wp::application::resourcesystem::Resource* resource, wp::application::resourcesystem::ResourceManager* resourceMgr, wp::XmlNode* node)
{
	VAR_UNUSED(resourceMgr);

	auto mapRes = static_cast<Map*>(resource);

	auto fileNode = node->getChild("File");
	mapRes->mModelFileName = fileNode->getValue();

	// <StartGrid><Pose index=".." px=".." py=".." pz=".." fx=".." fy=".." fz=".." nx=".." ny=".."
	// nz=".." />...</StartGrid>, written by cpp/editor's buildTrackResourceXml (MppModelExport.cpp)
	// from tox::StartGrid::startingGridPoses(). Optional: a Track resource exported before this
	// field existed simply has no <StartGrid> child, and mStartGridPoses stays empty rather than
	// throwing (see Map.h's comment on mStartGridPoses).
	auto startGridNode = node->getOptionalChild("StartGrid");
	if (startGridNode)
	{
		auto poseNode = startGridNode->getOptionalChild("Pose");
		if (poseNode)
		{
			do
			{
				tox::Pose pose;
				pose.pos = readVec3Attribs(poseNode, "p");
				pose.forward = readVec3Attribs(poseNode, "f");
				pose.up = readVec3Attribs(poseNode, "n");
				mapRes->mStartGridPoses.push_back(pose);
			} while (poseNode->next());
		}
	}
}
