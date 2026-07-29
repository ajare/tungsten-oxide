#pragma once

#include <memory>
#include <string>
#include <vector>

#include <willpower/application/resourcesystem/Resource.h>
#include <willpower/application/resourcesystem/ResourceFactory.h>

#include <willpower/common/Logger.h>

#include <applib/Map.h>

#include "Ship.hpp"
#include "Track.hpp"

class Map : public applib::Map {
  friend class MapTungstenMonoxideDefinitionFactory;

  wp::Logger* mwLogger;

  // Definition-carried relative filenames and the exact model mesh ids classified as drivable.
  // Composite resources lose their normal `source`, so these cannot use a location attribute.
  std::string mModelFileName;
  std::string mTrackDataFileName;
  std::vector<std::string> mTrackMeshNames;
  std::shared_ptr<tox::Track> mTrack;

  // Eight poses regenerated from schema-10 metadata and settled onto selected model triangles
  // during load; no duplicate StartGrid payload exists in resource XML.
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
      wp::Logger* logger);

  ~Map();

  std::vector<tox::Pose> const& getStartGridPoses() const { return mStartGridPoses; }
  std::shared_ptr<tox::Track> const& getTrack() const { return mTrack; }
};

class MapResourceFactory : public wp::application::resourcesystem::ResourceFactory {
  wp::Logger* mwLogger;

public:
  explicit MapResourceFactory(wp::Logger* logger)
      : wp::application::resourcesystem::ResourceFactory("Track"), mwLogger(logger) {
  }

  wp::application::resourcesystem::Resource* createResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, wp::application::resourcesystem::ResourceLocation* location) override {
    return new Map(name, namesp, source, tags, location, mwLogger);
  }
};