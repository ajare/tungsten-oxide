#pragma once

#include <string>

#include <willpower/application/resourcesystem/Resource.h>
#include <willpower/application/resourcesystem/ResourceFactory.h>

#include "applib/Game.h"

#include "Platform.h"

class Game : public applib::Game {
  friend class GameDefinitionFactory;

  std::string mShipModelFile;
  std::string mShipMaterialBinding;

public:
  Game(std::string const& name,
       std::string const& namesp,
       std::string const& source,
       std::map<std::string, std::string> const& tags,
       wp::application::resourcesystem::ResourceLocation* location);

  std::string const& getShipModelFile() const { return mShipModelFile; }
  std::string const& getShipMaterialBinding() const { return mShipMaterialBinding; }
  std::string getShipModelPath() const;
};

class GameResourceFactory : public wp::application::resourcesystem::ResourceFactory {
public:
  explicit GameResourceFactory()
      : wp::application::resourcesystem::ResourceFactory("Game") {
  }

  wp::application::resourcesystem::Resource* createResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, wp::application::resourcesystem::ResourceLocation* location) override {
    return new Game(name, namesp, source, tags, location);
  }
};
