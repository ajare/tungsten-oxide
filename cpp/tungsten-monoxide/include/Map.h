#pragma once

#include <string>
#include <vector>

#include <willpower/application/resourcesystem/Resource.h>
#include <willpower/application/resourcesystem/ResourceFactory.h>

#include <willpower/common/Logger.h>

#include <applib/Map.h>

#include "Ship.hpp"

class Map : public applib::Map
{
	friend class MapTungstenMonoxideDefinitionFactory;

	wp::Logger* mwLogger;

	// The .mppmodel filename, set by MapTungstenMonoxideDefinitionFactory::create() from this
	// resource's <Definition factory="Track"><File>...</File></Definition> -- NOT this resource's
	// own `location`/getSource(): ResourceManager::instantiateResource() forces `source` to "" for
	// any composite resource (one with <DependentResources>), which Track now always is (it lists
	// its TrackMaterial dependents), so a `location=` attribute on the Track element itself is
	// silently discarded. The Definition-carried filename is the only channel left for it.
	std::string mModelFileName;

	// The settled starting-grid poses (position/forward/surface-up per slot), set by
	// MapTungstenMonoxideDefinitionFactory::create() from this resource's <Definition
	// factory="Track"><StartGrid>...</StartGrid></Definition> -- see
	// cpp/editor/src/MppModelExport.cpp's buildTrackResourceXml, which computes them via
	// tox::StartGrid::startingGridPoses(). Empty for a Track resource exported before this field
	// existed; callers must not assume a non-empty vector.
	std::vector<tox::Pose> mStartGridPoses;

private:

	// Loads mModelFileName's geometry via mpp::ModelSerializer directly (NOT mpp::MppModelStream --
	// that class resolves each mesh's material only against the .mppmodel file's own embedded
	// Materials section, which cpp/editor's MppModelExport.cpp deliberately leaves empty; see
	// Map.cpp's comment on resolveMaterialMppName). Instead this builds an
	// mpp::ProgrammaticModelStream mesh-by-mesh, resolving each mesh's material string against this
	// resource's own already-loaded TrackMaterial/Material dependents (see Resources.xml's
	// DependentResources on the Track resource). A mesh whose material has no matching dependent
	// (e.g. PathShell's "shell", ZoneSurface's "zone-<effect>" -- auxiliary geometry
	// buildTrackResourceXml doesn't currently declare a material dependent for) is skipped with a
	// logged warning, not a load failure. Requires a DirectoryResourceLocation (a real filesystem
	// path): mpp::ModelSerializer opens the file directly via ifstream, bypassing willpower's
	// ResourceLocation/DataStream abstraction entirely, so a Track resource loaded from a
	// ZipResourceLocation is not currently supported and throws ResourceException.
	bool load(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) override;

	bool unload(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) override;

public:

	Map(std::string const& name,
		std::string const& namesp,
		std::string const& source,
		std::map<std::string, std::string> const& tags,
		wp::application::resourcesystem::ResourceLocation* location,
		wp::Logger* logger
	);

	~Map();

	std::vector<tox::Pose> const& getStartGridPoses() const { return mStartGridPoses; }
};

class MapResourceFactory : public wp::application::resourcesystem::ResourceFactory
{
	wp::Logger* mwLogger;

public:

	explicit MapResourceFactory(wp::Logger* logger)
		: wp::application::resourcesystem::ResourceFactory("Track")
		, mwLogger(logger)
	{
	}

	wp::application::resourcesystem::Resource* createResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, wp::application::resourcesystem::ResourceLocation* location) override
	{
		return new Map(name, namesp, source, tags, location, mwLogger);
	}
};