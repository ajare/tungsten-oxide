#include "EntityManager.h"
#include "Exceptions.h"

namespace applib
{

	using namespace std;
	using namespace wp;

	EntityManager::EntityManager(mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr)
		: mwRenderSystem(renderSystem)
		, mwRenderResourceMgr(renderResourceMgr)
	{
	}

	EntityManager::~EntityManager()
	{
		for (auto facade : mFacades)
		{
			delete facade;
		}

		for (auto const& entry : mFacadeFactories)
		{
			delete entry.second;
		}
	}

	void EntityManager::registerFacadeFactory(string const& type, EntityFacadeFactory* factory)
	{
		if (mFacadeFactories.find(type) != mFacadeFactories.end())
		{
			throw Exception(STR_FORMAT("EntityFacadeFactory type  '{}' already registered.", type));
		}
		else
		{
			mFacadeFactories[type] = factory;
		}
	}

	void EntityManager::createFacade(
		std::string const& facadeType,
		mpp::ScenePtr scene, 
		std::vector<int> const& types,
		int renderOrder,
		size_t initialSize)
	{
		// Check types
		int maxType{ -1 };
		for (auto type : types)
		{
			if (type < 0 || type >= 1024)
			{
				throw Exception("Entity type must be within [0, 1024).");
			}

			maxType = max(maxType, type);
		}

		// Create facade
		auto factoryIt = mFacadeFactories.find(facadeType);
		if (factoryIt == mFacadeFactories.end())
		{
			throw Exception(STR_FORMAT("EntityFacadeFactory type  '{}' not registered.", facadeType));
		}

		auto facade = factoryIt->second->create(initialSize);
		facade->initialise(scene, mwRenderSystem, mwRenderResourceMgr, renderOrder);
		mFacades.push_back(facade);

		// Type mapping
		mTypeFacadeMapping.resize(maxType + 1);

		for (auto type : types)
		{
			mTypeFacadeMapping[type] = facade;
		}
	}
	
	Entity const& EntityManager::getPlayerEntity() const
	{
		for (auto const& facade : mFacades)
		{
			try
			{
				return facade->getEntityById(0);
			}
			catch (Exception& e)
			{
				// Pass...
			}
		}

		throw Exception("Could not get player entity (id 0)");
	}

	void EntityManager::createEntity(int type, wp::Vector2 const& position, float angle, bool addImmediately)
	{
		auto facade = mTypeFacadeMapping[type];
		facade->createEntity(type, position, angle, addImmediately);
	}

	void EntityManager::updateEntities(bool controlActive, float frameTime)
	{
		for (auto facade : mFacades)
		{
			facade->updateEntities(controlActive, frameTime);
		}
	}

	void EntityManager::updateRenderers(float frameTime)
	{
		for (auto facade : mFacades)
		{
			facade->updateRenderer(frameTime);
		}
	}

	void EntityManager::setRenderersVisible(bool visible)
	{
		for (auto facade : mFacades)
		{
			facade->setRenderersVisible(visible);
		}

	}

} // applib