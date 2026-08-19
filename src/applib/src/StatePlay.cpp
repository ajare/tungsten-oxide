#include <mpp/helper/OrthoCamera.h>

#include <willpower/application/StateExceptions.h>
#include <willpower/application/ServiceLocator.h>
#include <willpower/application/ApplicationSettings.h>
#include <willpower/application/resourcesystem/ImageSetResource.h>

#include "StatePlay.h"
#include "Game.h"
#include "ModelInstance.h"
#include "Exceptions.h"

namespace applib
{

	using namespace std;
	using namespace wp;

	StatePlay::StatePlay()
		: State("Play")
		, mInputStateMgr(nullptr)
		, mEntityMgr(nullptr)
		, mScreenFxMgr(nullptr)
		, mMouseDeltaX(0.0f)
		, mMouseDeltaY(0.0f)
	{
	}

	Entity const& StatePlay::getPlayerEntity() const
	{
		return mEntityMgr->getPlayerEntity();
	}

	wp::Vector2 StatePlay::getMouseScreenPosition() const
	{
		float mouseX, mouseY;
		mInputStateMgr->mousePosition(&mouseX, &mouseY);

		return { mouseX, mouseY };
	}

	wp::Vector2 StatePlay::getMouseScreenDelta() const
	{
		return { mMouseDeltaX, mMouseDeltaY };
	}

	void StatePlay::createEntity(int type, Vector2 const& position, float angle, bool addImmediately)
	{
		mEntityMgr->createEntity(type, position, angle, addImmediately);
	}

	void StatePlay::createInput()
	{
		destroyInput();
		mInputStateMgr = new application::InputStateManager();
	}

	void StatePlay::destroyInput()
	{
		delete mInputStateMgr;
		mInputStateMgr = nullptr;
	}

	void StatePlay::updateInput(float frameTime)
	{
		WP_UNUSED(frameTime);

		mActiveInputStates = mInputStateMgr->process(_imGuiActive(), & mMouseDeltaX, &mMouseDeltaY);

		auto mouseScreenPos = getMouseScreenPosition();

		ModelInstance::entityHandler()->setActiveInputStates(mActiveInputStates, mouseScreenPos.x, mouseScreenPos.y, mMouseDeltaX, mMouseDeltaY);

		updateActions(mActiveInputStates, frameTime);
	}

	void StatePlay::createScreenFxManagement()
	{
		mScreenFxMgr = new ScreenFxManager(mwRenderResourceMgr);
	}

	void StatePlay::destroyScreenFxManagement()
	{
		delete mScreenFxMgr;
		mScreenFxMgr = nullptr;
	}

	void StatePlay::updateScreenFxManagement(float frameTime)
	{
		mScreenFxMgr->update(frameTime);
	}

	void StatePlay::createEntityManagement()
	{
		Entity::_resetIdGenerator();
		
		mEntityMgr = new EntityManager(mwRenderSystem, mwRenderResourceMgr);

		setupEntityFacades();
		setupEntities();
	}

	void StatePlay::destroyEntityManagement()
	{
		delete mEntityMgr;
		mEntityMgr = nullptr;
	}

	void StatePlay::updateEntityManagement(float frameTime)
	{
		mEntityMgr->updateEntities(true, frameTime);
	}

	void StatePlay::setupEntityFacades()
	{
		// Designed to be implemented by subclasses
	}

	void StatePlay::setupEntities()
	{
		// Designed to be implemented by subclasses
	}

	void StatePlay::createEntityFacade(
		string const& facadeType,
		vector<int> const& types,
		size_t initialSize)
	{
		mEntityMgr->createFacade(
			facadeType,
			mScene,
			types,
			0,
			initialSize);
	}

	void StatePlay::updatePreInput(float frameTime)
	{
		// Designed to be implemented by subclasses
	}

	void StatePlay::updatePreEntities(float frameTime)
	{
		// Designed to be implemented by subclasses
	}

	void StatePlay::updatePostEntities(float frameTime)
	{
		// Designed to be implemented by subclasses
	}

	void StatePlay::updatePreRenderers(float frameTime)
	{
		// Designed to be implemented by subclasses
	}

	void StatePlay::createRenderers(mpp::ResourceManager* renderResourceMgr, StateTransitionData* transitionData)
	{
	}

	void StatePlay::destroyRenderers()
	{
	}

	void StatePlay::updateRenderers(float frameTime)
	{
		// Entities
		mEntityMgr->updateRenderers(frameTime);
	}

	void StatePlay::updateCamera(float frameTime)
	{
	}

	void StatePlay::setupScene()
	{
		mScene->setClearColour(mpp::Colour::Black);
	}

	void StatePlay::createGameObjects(application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args)
	{
		// Designed to be implemented by subclasses
	}

	void StatePlay::setup(application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args)
	{
		WP_UNUSED(resourceMgr);

		auto transitionData = static_cast<StateTransitionData*>(args);

		// Set up objects to pass to next state
		mTransitionData.mapData.prevMap.map = transitionData->mapData.nextMap.map;
		mMap = transitionData->mapData.nextMap.map;

		createInput();
		createScreenFxManagement();
		createEntityManagement();
		createRenderers(renderResourceMgr, transitionData);

		setupScene();
		loadAllReferencedResources();


		// Set up input
		registerInput();

		// For subclasses
		createGameObjects(resourceMgr, renderSystem, renderResourceMgr, args);
	}

	void StatePlay::destroyGameObjects()
	{
		// Designed to be implemented by subclasses
	}

	void StatePlay::teardown()
	{
		// For subclasses
		destroyGameObjects();
	}

	void StatePlay::enterImpl(wp::application::resourcesystem::ResourceManager* resourceMgr, wp::application::AudioSystem* audioSystem, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args)
	{
		State::enterImpl(resourceMgr, audioSystem, renderSystem, renderResourceMgr, args);
	}

	void StatePlay::exitImpl()
	{
		State::exitImpl();

		destroyRenderers();
		destroyEntityManagement();
		destroyScreenFxManagement();
		destroyInput();
	}

	void StatePlay::suspendImpl(void* args)
	{
		WP_UNUSED(args);
	}

	void StatePlay::resumeImpl(void* args)
	{
		WP_UNUSED(args);
	}

	void StatePlay::injectKeyInputImpl(application::KeyEvent evt, application::Key key, application::KeyModifiers modifiers)
	{
		mInputStateMgr->injectKeyInput(evt, key, modifiers);
	}

	void StatePlay::injectMouseButtonInputImpl(application::MouseButtonEvent evt, application::MouseButton mouseButton, application::KeyModifiers modifiers)
	{
		mInputStateMgr->injectMouseInput(evt, mouseButton, modifiers);
	}

	void StatePlay::injectMouseMotionInputImpl(float positionX, float positionY)
	{
		mInputStateMgr->setMousePosition(positionX, positionY);
	}

	mpp::CameraPtr StatePlay::getActiveCamera() const
	{
		return mCamera;
	}

	glm::vec2 StatePlay::getPlayerPosition() const
	{
		auto playerEnt = getPlayerEntity();

		if (ModelInstance::entityHandler()->entityHasComponent<PhysicalStats>(playerEnt))
		{
			auto const& physicalStats = ModelInstance::entityHandler()->getEntityComponent<PhysicalStats>(playerEnt);
			return glm::vec2(physicalStats.position.x, physicalStats.position.y);
		}
		else
		{
			throw Exception("Player entity does not have a position property.");
		}
	}

	void StatePlay::registerInputState(string const& name,
		vector<wp::application::Key> const& keysPressed,
		vector<wp::application::Key> const& keysReleased,
		vector<wp::application::Key> const& keysDown,
		vector<wp::application::MouseButton> const& buttonsPressed,
		vector<wp::application::MouseButton> const& buttonsReleased,
		vector<wp::application::MouseButton> const& buttonsDown,
		bool mouseWheelUp,
		bool mouseWheelDown,
		uint32_t keyModifiers,
		bool disableInGui)
	{
		mInputStateMgr->registerState(name,
			keysPressed,
			keysReleased,
			keysDown,
			buttonsPressed,
			buttonsReleased,
			buttonsDown,
			mouseWheelUp,
			mouseWheelDown,
			keyModifiers,
			disableInGui);
	}

	void StatePlay::unregisterInputState(string const& name)
	{
		mInputStateMgr->unregisterState(name);
	}

	void StatePlay::updateActions(vector<string> const& activeStates, float frameTime)
	{
		// Designed to be implemented by subclasses
	}

	void StatePlay::updateImpl(float frameTime)
	{
		updatePreInput(frameTime);
		updateInput(frameTime);
		updatePreEntities(frameTime);
		updateEntityManagement(frameTime);
		updatePostEntities(frameTime);
		updateCamera(frameTime);
		updatePreRenderers(frameTime);
		updateScreenFxManagement(frameTime);
		updateRenderers(frameTime);
	}

	void StatePlay::renderImpl(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
	{
		WP_UNUSED(resourceMgr);

		// Screen FX setup
        mScreenFxMgr->preRender({ 0, 0 });

		// Render scene
		renderSystem->renderScene(
			mScene,
			getActiveCamera(),
			glm::vec2(0, 0),
			getName());

		// Render post-effects
		mScreenFxMgr->postRender(renderSystem);
	}

} // applib