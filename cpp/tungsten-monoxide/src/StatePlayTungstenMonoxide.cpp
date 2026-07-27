#define NOMINMAX

#include <algorithm>

#include <mpp/helper/FreeCamera.h>

#include <willpower/application/StateExceptions.h>

#include <applib/ModelInstance.h>

#include "imgui/imgui.h"
#include "imgui/implot.h"

#include "StatePlayTungstenMonoxide.h"
#include "TungstenMonoxideModel.h"
#include "EntityHandlerTungstenMonoxide.h"
#include "EntityType.h"
#include "Map.h"
#include "ReactiveCamera.h"
#include "GameException.h"

#define CLIPPING_RECORD_COUNT_MAX		10
#define DISPLAY_MESSAGE_COUNT_MAX		128
#define DISPLAY_MESSAGE_TIME			10

using namespace std;
using namespace wp;

DisplayMessage::Level gDisplayMessageLevel = DisplayMessage::Level::Debug;

// ImGui colours go here so they don't clutter up the header file
const ImColor gImGui_MapBackgroundColour{ 0.2f, 0.2f, 0.8f };
const ImColor gImGui_TriangulationLineColour{ 0.8f, 0.8f, 0.2f };
const ImColor gImGui_MapBorderColour{ 1.0f, 1.0f, 0.7f };
const ImColor gImGui_PrimitiveColour{ 0.8f, 0.2f, 0.2f };
const ImColor gImGui_PrimitiveInSourceSetColour{ 0.8f, 0.8f, 0.2f, 0.5f };
const ImColor gImGui_PrimitiveInViewColour{ 0.8f, 0.2f, 0.2f, 0.5f };
const ImColor gImGui_PrimitiveStaticBoundsColour{ 0.2f, 0.8f, 0.2f };
const ImColor gImGui_PrimitiveAnimatedBoundsColour{ 0.2f, 0.2f, 0.8f };
const ImColor gImGui_CollisionLineSolidColour{ 0.8f, 0.8f, 0.2f };
const ImColor gImGui_CollisionLine2WayColour{ 0.6f, 0.6f, 0.0f };
const ImColor gImGui_ViewAreaColour{ 0.5f, 0.5f, 0.5f };

StatePlayTungstenMonoxide::StatePlayTungstenMonoxide()
	: StatePlay()
	, mGlobalTime(0.0)
	, mExitScheduled(false) {
}

StatePlayTungstenMonoxide::~StatePlayTungstenMonoxide()
{
}

Map* StatePlayTungstenMonoxide::getMap()
{
	return static_cast<Map*>(mMap.get());
}

Map const* StatePlayTungstenMonoxide::getMap() const
{
	return static_cast<Map const*>(mMap.get());
}

void StatePlayTungstenMonoxide::createCamera()
{
	float aspectRatio = mwRenderSystem->getWindowWidth() / (float)mwRenderSystem->getWindowHeight();

	auto camera = new ReactiveCamera(glm::vec3(0, 20, 150), 180.0f, 0.0f, 90, aspectRatio);
	camera->setClipDistances(0.1f, 250 + 10);

	mCamera3d = shared_ptr<mpp::Camera>(camera);
}

void StatePlayTungstenMonoxide::registerInput()
{
	using namespace application;

	//											Keys pressed/released/down		// Buttons P/R/D	Wheel U/D,		modifiers	gui-disabled
	registerInputState("Exit",					{ Key::Escape }, {}, {},		{}, {}, {},			false, false,	0,			false);
	registerInputState("Up",					{}, {}, { Key::UpArrow },		{},	{},	{},			false, false,	0,			true);
	registerInputState("Down",					{}, {}, { Key::DownArrow },		{},	{},	{},			false, false,	0,			true);
	registerInputState("Left",					{}, {}, { Key::LeftArrow },		{},	{},	{},			false, false,	0,			true);
	registerInputState("Right",					{},	{},	{ Key::RightArrow },	{},	{},	{},			false, false,	0,			true);
	registerInputState("GenClip",				{ Key::P },  {}, {},			{}, {}, {},			false, false,	0,			true);
	registerInputState("Debug.Minimap",			{ Key::F2 }, {}, {},			{}, {}, {},			false, false,	0,			false);
	registerInputState("Debug.CollisionSim",	{ Key::F3 }, {}, {},			{}, {}, {},			false, false,	0,			false);
	registerInputState("Debug.ClipGen",			{ Key::F4 }, {}, {},			{}, {}, {},			false, false,	0,			false);
	registerInputState("ToggleAllLayers",		{ Key::F9 }, {}, {},			{}, {}, {},			false, false,	0,			true);
}

void StatePlayTungstenMonoxide::createGameObjects(application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args)
{
	VAR_UNUSED(resourceMgr);
	VAR_UNUSED(renderSystem);
	VAR_UNUSED(renderResourceMgr);
	VAR_UNUSED(args);
}

void StatePlayTungstenMonoxide::destroyGameObjects()
{
}

void StatePlayTungstenMonoxide::setupEntityFacades()
{
}

vector<string> StatePlayTungstenMonoxide::getDebuggingText() const
{
	auto mouseScreen = getMouseScreenPosition();
	
	return {
		STR_FORMAT("Mouse screen: {:.0f},{:.0f}", mouseScreen.x, mouseScreen.y),
	};
}

void StatePlayTungstenMonoxide::setupEntities()
{
}

void StatePlayTungstenMonoxide::setup(application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args)
{
	WP_UNUSED(resourceMgr);

	auto transitionData = static_cast<applib::StateTransitionData*>(args);

	// Set up objects to pass to next state
	mTransitionData.mapData.prevMap.map = transitionData->mapData.nextMap.map;
	mTransitionData.userData = transitionData->userData;

	mMap = transitionData->mapData.nextMap.map;

	createInput();
	createScreenFxManagement();
	createEntityManagement();

	createCamera();
	createRenderers(renderResourceMgr, transitionData);

	// We want to turn off the default entity rendering from AppLib here, as it is for 2d entities,
	// and these should only be visible in the minimap
	mEntityMgr->setRenderersVisible(false);

	setupScene();
	loadAllReferencedResources();

	// Set up input
	registerInput();

	// For subclasses
	createGameObjects(resourceMgr, renderSystem, renderResourceMgr, args);

	// Start audio events
	if (mwAudioSystem)
	{
		for (int i = 0; i < 1; ++i)
		{
			//auto event = mwAudioSystem->startEvent();
		}
	}

	// Finish move of transition data
	transitionData->userData = nullptr;
}

void StatePlayTungstenMonoxide::updatePreInput(float frameTime)
{
	VAR_UNUSED(frameTime);
}

void StatePlayTungstenMonoxide::updatePreEntities(float frameTime)
{
}

void StatePlayTungstenMonoxide::updateAudio(float frameTime)
{
}

void StatePlayTungstenMonoxide::updatePostEntities(float frameTime)
{
	VAR_UNUSED(frameTime);

	if (mwAudioSystem)
	{
		updateAudio(frameTime);
	}
}

void StatePlayTungstenMonoxide::exit()
{
	throw wp::application::ReturnFromStateException(&mTransitionData);
}

void StatePlayTungstenMonoxide::updateActions(vector<string> const& activeStates, float frameTime)
{
	VAR_UNUSED(activeStates);
	VAR_UNUSED(frameTime);

	for (auto const& state : activeStates)
	{
		if (state == "Exit")
		{
			exit();
		}
	}

	mEntityMgr->setRenderersVisible(false);
}

void StatePlayTungstenMonoxide::updatePreRenderers(float frameTime)
{
}

void StatePlayTungstenMonoxide::suspendImpl(void* args)
{
	VAR_UNUSED(args);
}

void StatePlayTungstenMonoxide::resumeImpl(void* args)
{
	if (args)
	{
		bool const* shouldExit = static_cast<bool const*>(args);
		mExitScheduled = *shouldExit;
	}
}

void StatePlayTungstenMonoxide::updateImpl(float frameTime)
{
	mGlobalTime += frameTime;

	if (mExitScheduled)
	{
		exit();
	}

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

void StatePlayTungstenMonoxide::renderImpl(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
{
	WP_UNUSED(resourceMgr);

	// Screen FX setup
	//mScreenFxMgr->preRender(getViewCentreWorldPosition());

	renderSystem->renderScene(mScene, mCamera3d, { 0.0f, 0.0f }, getName());

	// Render post-effects
	//mScreenFxMgr->postRender(renderSystem);
}
