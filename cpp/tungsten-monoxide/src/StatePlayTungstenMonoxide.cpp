#define NOMINMAX

#include <algorithm>

#include <mpp/ProgrammaticMaterialStream.h>
#include <mpp/ProgrammaticModelStream.h>

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


mpp::mesh::MeshSpecification createTorusMeshSpecification() {
  mpp::mesh::MeshSpecification meshSpec(mpp::mesh::Primitive::Type::Triangles);

  mpp::mesh::VertexBufferAttributeLayout* attribLayout = meshSpec.createVertexBufferAttributeLayout(false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::Position3, mpp::mesh::Vertex::DataType::Float, false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::Normal3, mpp::mesh::Vertex::DataType::Float, false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::TexCoord2, mpp::mesh::Vertex::DataType::Float, false);
  attribLayout->createAttribute(mpp::mesh::Vertex::Component::Colour4, mpp::mesh::Vertex::DataType::Float, true);

  meshSpec.setStorageType(mpp::mesh::VertexBufferStorageType::Static);
  meshSpec.setIndexedVertices(true);

  return meshSpec;
}

void createTorusMaterial(mpp::mesh::MeshSpecification const& meshSpec, mpp::ResourceManager* resourceMgr, TmResourceWrangler* wrangler) {

  auto materialStream = new mpp::ProgrammaticMaterialStream(resourceMgr);
  materialStream->setProgram2d(false);
  materialStream->setMeshSpecification(meshSpec);
  materialStream->setTexture("TEX1", "__mpp_tex_none__");

  auto res = resourceMgr->declareResource("Torus.Material", mpp::ResourceStreamPtr(materialStream)).first;
  res->acquire(wrangler);
  res->load();
}

mpp::ResourcePtr createTorusModel(mpp::ResourceManager* resourceMgr, TmResourceWrangler* wrangler) {
  auto torusMeshSpec = createTorusMeshSpecification();
  createTorusMaterial(torusMeshSpec, resourceMgr, wrangler);

  auto torusStream = new mpp::ProgrammaticModelStream(resourceMgr);
  auto torusMeshId = torusStream->createMesh("Torus", torusMeshSpec, "Torus.Material", 32);

  // Torus has 64 rings of 16 vertices each
  size_t ringSize{16};
  size_t numRings{64};
  size_t radius{48};
  size_t thickness{12};

  mpp::mesh::VertexData torusData(torusMeshSpec, ringSize * numRings);

  float dp = 2 * 3.14159f / ringSize;
  float dt = 2 * 3.14159f / numRings;

  for (size_t i = 0; i < numRings; ++i) {
    float theta = dt * i;

    for (size_t j = 0; j < ringSize; ++j) {
      float phi = dp * j;

      float nx = cosf(theta);
      float ny = sinf(phi);
      float nz = sinf(theta);

      float x = nx * (radius + cosf(phi) * thickness);
      float y = ny * thickness;
      float z = nz * (radius + cosf(phi) * thickness);

      // Hypertrochoid
      // float x = pow(cosf(theta), 3) * (radius + cosf(phi) * thickness);
      // float z = pow(sinf(theta), 3) * (radius + cosf(phi) * thickness);

      torusData.f32(x, y, z);                                                   // Position
      torusData.f32(nx, ny, nz);                                                // Normal
      torusData.f32(i / ((float)numRings - 1) * 8, j / ((float)ringSize - 1));  // UV coord
      torusData.f32(1.0f, 1.0f, 1.0f, 1.0f);                                    // Colour
    }
  }

  torusStream->addVertexData(torusMeshId, torusData);

  for (size_t i = 0; i < numRings; ++i) {
    for (size_t j = 0; j < ringSize; ++j) {
      auto i0 = i * ringSize + j;
      auto i1 = i * ringSize + ((j + 1) % ringSize);
      auto i2 = ((i + 1) % numRings) * ringSize + ((j + 1) % ringSize);
      auto i3 = ((i + 1) % numRings) * ringSize + j;

      torusStream->addTriangle(torusMeshId, (uint32_t)i0, (uint32_t)i1, (uint32_t)i2);
      torusStream->addTriangle(torusMeshId, (uint32_t)i2, (uint32_t)i3, (uint32_t)i0);
    }
  }

  auto torus = resourceMgr->declareResource("Model.Torus", mpp::ResourceStreamPtr(torusStream)).first;
  torus->acquire(wrangler);
  torus->load();

  return torus;
}


StatePlayTungstenMonoxide::StatePlayTungstenMonoxide()
	: StatePlay()
	, mGlobalTime(0.0)
	, mExitScheduled(false) 
	, mTrackModel(nullptr) 
{
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

vector<string> StatePlayTungstenMonoxide::getDebuggingText() const {
  auto mouseScreen = getMouseScreenPosition();

  return {
      STR_FORMAT("Mouse screen: {:.0f},{:.0f}", mouseScreen.x, mouseScreen.y),
  };
}

void StatePlayTungstenMonoxide::createCamera()
{
	float aspectRatio = mwRenderSystem->getWindowWidth() / (float)mwRenderSystem->getWindowHeight();

	auto camera = new ReactiveCamera(glm::vec3(0, 0, 150), 180.0f, 0.0f, 90, aspectRatio);
	camera->setClipDistances(0.1f, 250 + 10);

	mCamera3d = shared_ptr<mpp::Camera>(camera);
}

void StatePlayTungstenMonoxide::registerInput()
{
	using namespace application;

	//											Keys pressed/released/down		// Buttons P/R/D	Wheel U/D,		modifiers	gui-disabled
	registerInputState("Exit",					{ Key::Escape }, {}, {},		{}, {}, {},			false, false,	0,			false);
	registerInputState("Forward",				{}, {}, { Key::UpArrow },		{},	{},	{},			false, false,	0,			true);
	registerInputState("Down",					{}, {}, { Key::DownArrow },		{},	{},	{},			false, false,	0,			true);
	registerInputState("Back",					{}, {}, { Key::LeftArrow },		{},	{},	{},			false, false,	0,			true);
	registerInputState("Right",					{},	{},	{ Key::RightArrow },	{},	{},	{},			false, false,	0,			true);
}

void StatePlayTungstenMonoxide::createGameObjects(application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args)
{
	VAR_UNUSED(resourceMgr);
	VAR_UNUSED(renderSystem);
	VAR_UNUSED(renderResourceMgr);
	VAR_UNUSED(args);

	auto track = resourceMgr->getResource("NewTrack", "Tracks");
    
	mTrackModel = track->getMppResource();
    mTrackModel->acquire(&mWrangler);

    mScene->add3dModel(mTrackModel);
}

void StatePlayTungstenMonoxide::destroyGameObjects()
{
  mTrackModel->release(&mWrangler);
}

void StatePlayTungstenMonoxide::setupEntityFacades()
{
}

void StatePlayTungstenMonoxide::setupScene() {
	// Place the camera at the first starting-grid slot (Map::getStartGridPoses(), populated from
	// the Track resource's <StartGrid> -- see cpp/editor's buildTrackResourceXml/StartGrid.hpp).
	// Empty for a Track resource exported before that field existed, so the camera is simply left
	// at createCamera()'s hardcoded default in that case rather than indexing an empty vector.
	auto const& startGridPoses = getMap()->getStartGridPoses();
	if (!startGridPoses.empty())
	{
		tox::Pose const& pose = startGridPoses[0];

		auto camera = static_cast<ReactiveCamera*>(mCamera3d.get());
		camera->setPosition(glm::vec3((float)pose.pos.x, (float)pose.pos.y, (float)pose.pos.z));
		camera->setOrientation(glm::vec3((float)pose.forward.x, (float)pose.forward.y, (float)pose.forward.z),
			glm::vec3((float)pose.up.x, (float)pose.up.y, (float)pose.up.z));
	}
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

	// We want to turn off the default entity rendering from AppLib here, as it is for 2d entities,
	// and these should only be visible in the minimap
	mEntityMgr->setRenderersVisible(false);

	loadAllReferencedResources();

	// Set up input
	registerInput();

	// For subclasses
	createGameObjects(resourceMgr, renderSystem, renderResourceMgr, args);

	setupScene();

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

	renderSystem->setAmbientColour(mpp::Colour::Grey25);
    renderSystem->setLightCount(1);
    renderSystem->setLight1Colour(mpp::Colour::White);

	renderSystem->renderScene(mScene, mCamera3d, { 0.0f, 0.0f }, getName());

	// Render post-effects
	//mScreenFxMgr->postRender(renderSystem);
}
