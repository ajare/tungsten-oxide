#include <willpower/application/resourcesystem/TextFileResource.h>
#include <willpower/application/resourcesystem/ResourceExceptions.h>

#include <core/DynamicWorldDataGenerator.h>
#include <core/YamlSerializer.h>

#include "Map.h"

using namespace std;
using namespace wp;
using namespace wp::geometry;


Map::Map(string const& name,
	string const& namesp,
	string const& source,
	map<string, string> const& tags,
	application::resourcesystem::ResourceLocation* location,
	wp::Logger* logger)
	: applib::Map(name, namesp, source, tags, location, 512)
	, mWorld(nullptr)
	, mwLogger(logger)
{
}

Map::~Map()
{
	delete mWorld;
}

bw::core::World* Map::getWorld()
{
	return mWorld;
}

bw::core::World const* Map::getWorld() const
{
	return mWorld;
}

void Map::loadWorldFromYaml(wp::application::resourcesystem::ResourcePtr resource)
{
	delete mWorld;

	auto res = static_cast<wp::application::resourcesystem::TextFileResource*>(resource.get());
	string text = res->getText();

	auto ser = shared_ptr<bw::core::YamlSerializer>(bw::core::YamlSerializer::fromString(text));

	ser->deserialize();

	mWorld = new bw::core::World(1.0f, -1.0f);

	// Create grid with cell size 512
	auto workData = bw::core::SerializationWorkData{ 512.0f };

	if (mWorld->deserialize(ser, workData))
	{
		auto const& warnings = mWorld->getDeserializationWarnings();

		if (!warnings.empty())
		{
			for (auto const& warning : warnings)
			{
				mwLogger->warn(warning);
			}
		}

		auto genFn = [this](wp::Vector2 offset, int dimX, int dimY, float cellSize) {
			BW_UNUSED(offset);
			BW_UNUSED(dimX);
			BW_UNUSED(dimY);
			BW_UNUSED(cellSize);
			auto wdg = new bw::core::DynamicWorldDataGenerator(mWorld);

			wdg->setBroadPhaseCulling(bw::core::WorldDataGenerator::BroadPhaseCulling::Circle);
			wdg->setNarrowPhaseCulling(bw::core::WorldDataGenerator::NarrowPhaseCulling::None);

			return wdg;
		};

		mWorld->setWorldDataGeneratorFactory(genFn);
	}
	else
	{
		auto const& errors = mWorld->getDeserializationErrors();

		if (!errors.empty())
		{
			for (auto const& error : errors)
			{
				mwLogger->error(error);
			}
		}

		throw wp::application::resourcesystem::ResourceException(resource.get(), "Could not load World from YAML.");
	}
}
