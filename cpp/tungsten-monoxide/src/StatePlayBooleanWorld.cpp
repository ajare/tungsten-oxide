#define NOMINMAX

#include <algorithm>

#include <mpp/helper/FreeCamera.h>

#include <willpower/application/StateExceptions.h>

#include <willpower/collide/ColliderCircle.h>
#include <willpower/collide/ColliderAABB.h>

#include <willpower/geometry/MeshQuery.h>

#include <willpower/viz/DynamicLineRenderer.h>
#include <willpower/viz/CollisionSimulationRenderer.h>
#include <willpower/viz/FirepowerMeshRenderer.h>

#include <applib/ModelInstance.h>
#include <applib/VisualSpriteEntityFacade.h>

#include <core/Utils.h>
#include <core/DynamicWorldDataGenerator.h>

#include <common/GameDefines.h>

#include "imgui/imgui.h"
#include "imgui/implot.h"

#include "StatePlayBooleanWorld.h"
#include "BooleanWorldModel.h"
#include "EntityHandlerBooleanWorld.h"
#include "EntityType.h"
#include "TriMeshDataProvider.h"
#include "TriMeshEntityFacadeFactory.h"
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

StatePlayBooleanWorld::StatePlayBooleanWorld()
	: StatePlay()
	, mGlobalTime(0.0)
	, mWorldCollisionSim(nullptr)
	, mPlayerCollider(nullptr)
	, mCurrentLayer(0)
	, mPlayerPolygonIndex(-1)
	, mPlayerBorderIntersectIndex(-1)
	, mCollisionsProcessed(0)
	, mwRenderer(nullptr)
	, mPlayerPrevAngle(0)
	, mPlayerPrevPitch(0)
	, mExitScheduled(false)
{
}

StatePlayBooleanWorld::~StatePlayBooleanWorld()
{
	delete mWorldCollisionSim;
}

Map* StatePlayBooleanWorld::getMap()
{
	return static_cast<Map*>(mMap.get());
}

Map const* StatePlayBooleanWorld::getMap() const
{
	return static_cast<Map const*>(mMap.get());
}

applib::PhysicalStats& StatePlayBooleanWorld::getPlayerPhysicalStats()
{
	auto model = static_cast<BooleanWorldModel*>(applib::ModelInstance::get());
	return model->entityHandler->getEntityComponent<applib::PhysicalStats>(mEntityMgr->getPlayerEntity());
}

applib::PhysicalStats const& StatePlayBooleanWorld::getPlayerPhysicalStats() const
{
	auto model = static_cast<BooleanWorldModel*>(applib::ModelInstance::get());
	return model->entityHandler->getEntityComponent<applib::PhysicalStats>(mEntityMgr->getPlayerEntity());
}

void StatePlayBooleanWorld::createCamera()
{
	float aspectRatio = mwRenderSystem->getWindowWidth() / (float)mwRenderSystem->getWindowHeight();

	auto camera = new ReactiveCamera(glm::vec3(0, BW_PLAYER_HEIGHT, 150), 180.0f, 0.0f, BW_PLAYER_FOV, aspectRatio);
	camera->setClipDistances(0.1f, BW_PLAYER_VIEW_DISTANCE + 10);

	mCamera3d = shared_ptr<mpp::Camera>(camera);
}

void StatePlayBooleanWorld::setupMapRenderer(applib::StateTransitionData* transitionData)
{
	mwRenderer = static_cast<WorldRenderer*>(transitionData->userData);
	mwRenderer->create(mScene, getMap()->getWorld(), mwRenderSystem, mwRenderResourceMgr);
}

map<string, tuple<wp::viz::Renderer*, int, bool>> StatePlayBooleanWorld::createAdditionalRenderers(mpp::ResourceManager* renderResourceMgr)
{
	VAR_UNUSED(renderResourceMgr);


	return {};
}

void StatePlayBooleanWorld::registerInput()
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

void StatePlayBooleanWorld::setupPlayerCollision()
{
	mWorldCollisionSim = new WorldCollisionSim(this);

	auto const& physicalStats = getPlayerPhysicalStats();
	mPlayerCollider = new wp::collide::ColliderCircle(physicalStats.position, BW_PLAYER_RADIUS);

	auto cb = [](wp::collide::SweepResult* result, wp::collide::StaticLine const& line, float t, void* user) -> bool
	{
		auto edgeIndex = (uint32_t)line.getUserData();
		auto state = static_cast<StatePlayBooleanWorld*>(user);
		auto const& graph = state->mWorldData.getGraph();

		state->mCollisionsProcessed++;

		// See if it's two-sided
		auto const& edge = graph.edges[edgeIndex];
		bool twoSided = edge.p[0] != ~0u && edge.p[1] != ~0u;

		if (twoSided)
		{
			auto curPrimitiveIndex = state->getPlayerPrimitive();
			auto newPrimitiveIndex = edge.p[0] == curPrimitiveIndex ? edge.p[1] : edge.p[0];

			auto world = state->getMap()->getWorld();

			auto const& curPrimitiveProps = world->getPrimitive(curPrimitiveIndex)->getProperties();
			auto const& newPrimitiveProps = world->getPrimitive(newPrimitiveIndex)->getProperties();

			auto curFloorZ = curPrimitiveProps.floorZ;
			auto curCeilingZ = curPrimitiveProps.ceilingZ;

			auto newFloorZ = newPrimitiveProps.floorZ;
			auto newCeilingZ = newPrimitiveProps.ceilingZ;

			auto stepLowEnough = newFloorZ - curFloorZ <= BW_PLAYER_STEP_HEIGHT;
			auto ceilingHighEnough = (newCeilingZ >= (curFloorZ + BW_PLAYER_HEIGHT) &&
				(newCeilingZ - newFloorZ) >= BW_PLAYER_HEIGHT);
		
			if (stepLowEnough && ceilingHighEnough)
			{
				return false;
			}
		}

		// For two-sided lines, we can't rely on the normal, as we don't know which direction we're
		// approaching from.  So, flip the normal based on the angle of approach
		auto normal = line.getNormal();
		Winding angleDir;
		auto minAngle = result->movementDesired.minimumAngleTo(normal, &angleDir);

		if (minAngle < 90)
		{
			normal = -normal;
			angleDir = angleDir == Winding::Clockwise ? Winding::Anticlockwise : Winding::Clockwise;
		}

		// Push the result position away from the line a small amount
		result->newPosition = result->oldPosition + result->movementDesired * t + normal * 0.001f; // * MathsUtils::Epsilon;
		result->movementDone = result->newPosition - result->oldPosition;
		result->distanceMoved = result->movementDone.length();

		// Calculate edge normal to slide along
		float angle = 180 - minAngle;

		Vector2 newDirection = angleDir ==
			Winding::Clockwise ? normal.perpendicular() : -normal.perpendicular();

		// Get remaining movement
		result->movementLeft = newDirection * result->movementDesired.distanceTo(result->movementDone) * sin(WP_DEGTORAD(angle));
		return true;
	};

	mPlayerCollider->setHitLineCallback(cb);
	
	mWorldCollisionSim->addCollider(mPlayerCollider);

	applib::ModelInstance::entityHandler()->setupCollisions(mWorldCollisionSim, mPlayerCollider);
}

bool StatePlayBooleanWorld::playerInWorld() const
{
	return mPlayerPolygonIndex >= 0;
}

bool StatePlayBooleanWorld::playerIntersectsWorldBorders() const
{
	return mPlayerBorderIntersectIndex >= 0;
}

void StatePlayBooleanWorld::getWorldInput(wp::Vector2* curPosition, wp::Vector2* newPosition, float* curAngle, float* newAngle, float frameTime) const
{
	auto const& player = mEntityMgr->getPlayerEntity();
	auto entityHandler = static_cast<EntityHandlerBooleanWorld*>(applib::ModelInstance::get()->entityHandler.get());

	wp::Vector2 velocity;
	float curPitch, newPitch;
	entityHandler->peekInput(player, curPosition, newPosition, curAngle, newAngle, &curPitch, &newPitch, &velocity, frameTime);
}

void StatePlayBooleanWorld::createWorldCollisions()
{
	mWorldCollisionSim->clearLines();

	if (playerInWorld())
	{
		auto const& playerPosition = getPlayerPosition();

		auto const& graph = mWorldData.getGraph();
		auto numEdges = (uint32_t)graph.edges.size();
		
		for (uint32_t i = 0; i < numEdges; ++i)
		{
			auto const& edge = graph.edges[i];
			auto const& gv0 = graph.vertices[edge.v[0]];
			auto const& gv1 = graph.vertices[edge.v[1]];
			wp::Vector2 v0{ gv0.x, gv0.y };
			wp::Vector2 v1{ gv1.x, gv1.y };
			vector<wp::Vector2> bboxVerts{ v0, v1 };

			// We don't want to add every line to the collision sim.  At the same time,
			// using a lookup for the graph is probably overkill.  For now, at least.
			// Bear in mind that, because we are inflating the line by max player speed in
			// 1 second, if the framerate drops below 1 FPS then lines which the player can
			// collide with may not be added, because the player will be moving at more than
			// the max player speed for a single update.
			wp::BoundingBox lineBB(bboxVerts);
			lineBB.inflate(BW_PLAYER_SPEED + BW_PLAYER_RADIUS);

			if (lineBB.pointInside(playerPosition))
			{
				mWorldCollisionSim->addLine(v0, v1, edge.is2Sided(), i);
			}
		}
	}
}

void StatePlayBooleanWorld::createGameObjects(application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args)
{
	VAR_UNUSED(resourceMgr);
	VAR_UNUSED(renderSystem);
	VAR_UNUSED(renderResourceMgr);
	VAR_UNUSED(args);

	setupPlayerCollision();
}

void StatePlayBooleanWorld::destroyGameObjects()
{
	delete mWorldCollisionSim;
	mWorldCollisionSim = nullptr;
}

void StatePlayBooleanWorld::setupEntityFacades()
{
	applib::VisualSpriteEntityFacadeRenderOptions options;

	options.rotationType = wp::viz::RotationOptions::None;

	// Create Quad EntityFacade
	auto quadsResource = mwResourceMgr->getResource("EntityAnimations");
	auto animDatabase = applib::ModelInstance::animationDatabase();
	mEntityMgr->registerFacadeFactory("Sprites", new applib::VisualSpriteEntityFacadeFactory(quadsResource, animDatabase));

	createEntityFacade(
		"Sprites",
		{ (int)EntityType::Player },
		options,
		64);

	// Create triangles EntityFacade
	auto trisProviderFactory = [](auto facade) {
		return make_shared<TriMeshDataProvider>(facade);
	};

	mEntityMgr->registerFacadeFactory("TriMesh", new TriMeshEntityFacadeFactory(trisProviderFactory, nullptr));
}

vector<string> StatePlayBooleanWorld::getDebuggingText() const
{
	auto mouseScreen = getMouseScreenPosition();
	auto mouseWorld = getMouseWorldPosition();
	
	auto const& physicalStats = getPlayerPhysicalStats();
	auto const& worldStats = mWorldData.getStats();
	auto playerPrimIndex = getPlayerPrimitive();
	auto floorHeight = getPlayerFloorHeight();
	auto ceilingHeight = getPlayerCeilingHeight();

	return {
		STR_FORMAT("Mouse screen: {:.0f},{:.0f}", mouseScreen.x, mouseScreen.y),
		STR_FORMAT("Mouse world: {:.2f},{:.2f}", mouseWorld.x, mouseWorld.y),
		STR_FORMAT("Player world: {:.2f},{:.2f}", physicalStats.position.x, physicalStats.position.y),
		STR_FORMAT("Player floor/ceil: {:.2f},{:.2f}", floorHeight, ceilingHeight),
		STR_FORMAT("Player angle: {:.2f}", physicalStats.angle),
		STR_FORMAT("Player poly: {}", mPlayerPolygonIndex),
		STR_FORMAT("Player prim: {}", playerPrimIndex),
		STR_FORMAT("Collision count: {}", mCollisionsProcessed)

	};
}

uint32_t StatePlayBooleanWorld::getPrimitiveAtPosition(wp::Vector2 const& pos) const
{
	return mWorldData.getContainingTrianglePrimitiveIndex(pos);
}

uint32_t StatePlayBooleanWorld::getPlayerPrimitive() const
{
	auto const& physicalStats = getPlayerPhysicalStats();

	return getPrimitiveAtPosition(physicalStats.position);
}

wp::Vector2 StatePlayBooleanWorld::getPlayerPosition() const
{
	return getPlayerPhysicalStats().position;
}

float StatePlayBooleanWorld::getPlayerAngle() const
{
	return getPlayerPhysicalStats().angle;
}

float StatePlayBooleanWorld::getFloorHeightAt(wp::Vector2 const& pos) const
{
	auto index = getPrimitiveAtPosition(pos);

	if (index == ~0u)
	{
		return 0.0f;
	}
	else
	{
		auto prim = getMap()->getWorld()->getPrimitive(index);
		auto const& primProps = prim->getProperties();

		return primProps.floorZ;
	}
}

float StatePlayBooleanWorld::getCeilingHeightAt(wp::Vector2 const& pos) const
{
	auto index = getPrimitiveAtPosition(pos);

	if (index == ~0u)
	{
		return 0.0f;
	}
	else
	{
		auto prim = getMap()->getWorld()->getPrimitive(index);
		auto const& primProps = prim->getProperties();

		return primProps.ceilingZ;
	}
}

float StatePlayBooleanWorld::getPlayerFloorHeight() const
{
	auto const& playerStats = getPlayerPhysicalStats();

	return getFloorHeightAt(playerStats.position);
}

float StatePlayBooleanWorld::getPlayerCeilingHeight() const
{
	auto const& playerStats = getPlayerPhysicalStats();

	return getCeilingHeightAt(playerStats.position);
}

bw::core::DynamicWorldDataGenerator* StatePlayBooleanWorld::getWDG()
{
	auto world = getMap()->getWorld();

	return dynamic_cast<bw::core::DynamicWorldDataGenerator*>(world->getWorldDataGenerator());
}

void StatePlayBooleanWorld::addDisplayMessage(DisplayMessage::Level level, string const& message)
{
	mDisplayMessages.push_back({
		mGlobalTime,
		level,
		message
	});

	while (mDisplayMessages.size() >= DISPLAY_MESSAGE_COUNT_MAX)
	{
		mDisplayMessages.pop_front();
	}
}

void StatePlayBooleanWorld::setupEntities()
{
	// if player is not fully in the world, bail
	auto world = getMap()->getWorld();

	auto playerPos = world->getPlayerStartPosition();
	auto playerAngle = world->getPlayerStartAngle() - 180;

	createEntity((int)EntityType::Player, playerPos, playerAngle, true);

	world->update(0, {
		playerPos,
		0,
		BW_PLAYER_RADIUS,
		BW_PLAYER_FOV,
		BW_PLAYER_VIEW_DISTANCE,
		false,
		false,
		0
	}, { 0, 0 });

	mWorldData = world->getWorldData(playerPos, playerAngle);
	mWorldData.triangulate(world);

	if (mWorldData.pointInPolygon(playerPos) < 0 || mWorldData.circleIntersectsBorder(playerPos, BW_PLAYER_RADIUS) >= 0)
	{
		throw GameException("Player is starting outside the world geometry");
	}
}

void StatePlayBooleanWorld::setup(application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args)
{
	WP_UNUSED(resourceMgr);

	mPlayerPrevAngle = 0;
	mPlayerPrevPitch = 0;

	auto transitionData = static_cast<applib::StateTransitionData*>(args);

	// Set up objects to pass to next state
	mTransitionData.mapData.prevMap.map = transitionData->mapData.nextMap.map;
	mTransitionData.userData = transitionData->userData; // WorldRenderer

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

	// Start scheduled world clipping
	auto dataGenerator = getWDG();

	dataGenerator->registerGenerationCallback(bind(&StatePlayBooleanWorld::handleClippingUpdate, this, std::placeholders::_1));
	//dataGenerator->startGenerationSchedule(5.0f);

	// Finish move of transition data
	transitionData->userData = nullptr;
}

void StatePlayBooleanWorld::updatePreInput(float frameTime)
{
	VAR_UNUSED(frameTime);

	auto const& physicalStats = getPlayerPhysicalStats();
	
	mPlayerPrevAngle = physicalStats.angle;
	mPlayerPrevPitch = physicalStats.pitch;
}

void StatePlayBooleanWorld::updatePreEntities(float frameTime)
{
	// Get input
	wp::Vector2 curPosition, newPosition;
	float curAngle, newAngle;

	getWorldInput(&curPosition, &newPosition, &curAngle, &newAngle, frameTime);

	bool playerMoved = newPosition != curPosition;
	bool playerTurned = newAngle != curAngle;

	// Apply to world
	auto world = getMap()->getWorld();

	wp::Vector2 playerPosition;
	float playerAngle;

	playerPosition = newPosition;
	playerAngle = 360 - newAngle;

	world->update(frameTime, { 
		playerPosition, 
		playerAngle, 
		BW_PLAYER_RADIUS,
		BW_PLAYER_FOV, 
		BW_PLAYER_VIEW_DISTANCE, 
		playerMoved, 
		playerTurned, 
		mCurrentLayer
	}, { 0, 0 });

	mWorldData = world->getWorldData(playerPosition, playerAngle);
	mWorldData.triangulate(world);

	mPlayerPolygonIndex = mWorldData.pointInPolygon(curPosition);
	mPlayerBorderIntersectIndex = mWorldData.circleIntersectsBorder(curPosition, BW_PLAYER_RADIUS);

	if (!playerInWorld() || playerIntersectsWorldBorders())
	{
		// TODO
		// ...
	}

	// Do collision detection.
	createWorldCollisions();
}

void StatePlayBooleanWorld::updateAudio(float frameTime)
{
	BW_UNUSED(frameTime);
	
	bw::core::Triangulation::Triangle const* tri{ nullptr };
	auto const& physicalStats = getPlayerPhysicalStats();

	if (mWorldData.getContainingTriangle(physicalStats.position, &tri))
	{
		float u, v, w;
		tri->getBarycentricCoords(physicalStats.position, u, v, w);

		auto i0 = (uint32_t)BW_VERTEX_Z_UNPACK_VERTEX_INDEX(tri->v[0].z);
		auto i1 = (uint32_t)BW_VERTEX_Z_UNPACK_VERTEX_INDEX(tri->v[1].z);
		auto i2 = (uint32_t)BW_VERTEX_Z_UNPACK_VERTEX_INDEX(tri->v[2].z);

		auto const& vd0 = mWorldData.getVertexData(i0);
		auto const& vd1 = mWorldData.getVertexData(i1);
		auto const& vd2 = mWorldData.getVertexData(i2);

		// Set volume
		for (int i = 0; i < 1; ++i)
		{
			//mwAudioSystem->setEventVolume(event, volume);
		}
	}
}

void StatePlayBooleanWorld::updatePostEntities(float frameTime)
{
	VAR_UNUSED(frameTime);

	if (mwAudioSystem)
	{
		updateAudio(frameTime);
	}
}

void StatePlayBooleanWorld::exit()
{
	mTransitionData.mapData.prevMap.mapRenderer = mMapRenderer ? move(mMapRenderer) : nullptr;
	mTransitionData.mapData.prevMap.mapCollisionSim = mMapCollisionSim ? move(mMapCollisionSim) : nullptr;
	mTransitionData.mapData.prevMap.meshCollisionMgr = mMeshCollisionMgr ? move(mMeshCollisionMgr) : nullptr;

	applib::ModelInstance::get()->collisionSim = nullptr;
	applib::ModelInstance::get()->collisionMgr = nullptr;

	throw wp::application::ReturnFromStateException(&mTransitionData);
}

void StatePlayBooleanWorld::updateActions(vector<string> const& activeStates, float frameTime)
{
	VAR_UNUSED(activeStates);
	VAR_UNUSED(frameTime);

	for (auto const& state : activeStates)
	{
		if (state == "Exit")
		{
			exit();
		}
		else if (state == "GenClip")
		{
			getMap()->getWorld()->generateClipping(bw::core::WorldDataGenerator::NarrowPhaseCulling::None, true);
		}
		else if (state == "Debug.Minimap")
		{
			mDebugDisplay.minimap = !mDebugDisplay.minimap;
		}
		else if (state == "Debug.CollisionSim")
		{
			mDebugDisplay.collisionSim = !mDebugDisplay.collisionSim;
		}
		else if (state == "Debug.ClipGen")
		{
			mDebugDisplay.clipGeneration = !mDebugDisplay.clipGeneration;
		}
		else if (state == "ToggleAllLayers")
		{
			mCurrentLayer = mCurrentLayer == 0 ? BW_LAYER_ALL : 0;
		}
	}

	mEntityMgr->setRenderersVisible(false);
}

void StatePlayBooleanWorld::updatePreRenderers(float frameTime)
{
	auto viewBounds = getViewBounds();

	// Set camera position
	auto const& physicalStats = getPlayerPhysicalStats();

	// Camera position is player world Y offset plus player eye height
	auto playerViewHeight = getPlayerFloorHeight() + BW_PLAYER_EYE_HEIGHT;

	static_cast<ReactiveCamera*>(mCamera3d.get())->setPosition({ physicalStats.position.x, playerViewHeight, physicalStats.position.y });
	static_cast<ReactiveCamera*>(mCamera3d.get())->yaw(physicalStats.angle - mPlayerPrevAngle);
	static_cast<ReactiveCamera*>(mCamera3d.get())->pitch(physicalStats.pitch - mPlayerPrevPitch);

	// World 3d
	mwRenderer->update(getMap()->getWorld(), mWorldData, frameTime);
}

void StatePlayBooleanWorld::suspendImpl(void* args)
{
	VAR_UNUSED(args);
}

void StatePlayBooleanWorld::resumeImpl(void* args)
{
	if (args)
	{
		bool const* shouldExit = static_cast<bool const*>(args);
		mExitScheduled = *shouldExit;
	}
}

void StatePlayBooleanWorld::handleClippingUpdate(bw::core::DynamicWorldDataGenerator::GenerationDetails const& details)
{
	const int MaxRecords = 10;

	lock_guard<mutex> lock(mClippingRecordsMutex);

	// If state is Generating, insert
	switch (details.state)
	{
	case bw::core::DynamicWorldDataGenerator::GenerationState::Generating:
		mClippingRecords.push_back({
			details.clippingId,
			mGlobalTime,
			0.0f,
			0.0f,
			details.genTimeNs,
			details.stats
		});
		break;

	case bw::core::DynamicWorldDataGenerator::GenerationState::Committed:
		mwRenderer->setWorldChanged();
		addDisplayMessage(DisplayMessage::Level::Debug, format("Clipping committed: {} prims, {} polys ", 
			details.stats.clip.primitivesProcessed,
			details.stats.clip.polygonsGenerated)
		);
		[[ fallthrough ]];
	case bw::core::DynamicWorldDataGenerator::GenerationState::Generated:
		// Find the record with the matching ID and update
		for (auto& record : mClippingRecords)
		{
			if (record.clippingId == details.clippingId)
			{
				if (details.state == bw::core::DynamicWorldDataGenerator::GenerationState::Generated)
				{
					record.generationCompleteTime = mGlobalTime;
					record.generationTimeNs = details.genTimeNs;
					record.stats = details.stats;
				}
				else
				{
					record.commitedTime = mGlobalTime;
				}

				break;
			}
		}
		break;

	default:
		break;
	}

	while (mClippingRecords.size() >= CLIPPING_RECORD_COUNT_MAX)
	{
		mClippingRecords.pop_front();
	}
}

void StatePlayBooleanWorld::updateImpl(float frameTime)
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

void StatePlayBooleanWorld::renderImpl(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
{
	WP_UNUSED(resourceMgr);

	// Screen FX setup
	//mScreenFxMgr->preRender(getViewCentreWorldPosition());

	renderSystem->renderScene(mScene, mCamera3d, { 0.0f, 0.0f }, getName());

	// Render post-effects
	//mScreenFxMgr->postRender(renderSystem);

	// Messages
	auto numMessages = (int)mDisplayMessages.size();
	auto messageIndex = max(numMessages - 4, 0);

	int y = 0;
	for (int i = messageIndex; i < numMessages; ++i)
	{
		auto const& message = mDisplayMessages[i];

		if (((message.time + DISPLAY_MESSAGE_TIME) < mGlobalTime) ||
			(int)message.level < (int)gDisplayMessageLevel)
		{
			continue;
		}

		auto colour = message.level == DisplayMessage::Level::Debug ? mpp::Colour::Grey75 : mpp::Colour::White;

		renderSystem->renderText(format("{:.2f}", message.time), 0, y, colour);
		renderSystem->renderText(message.text, 100, y, colour);
		y += 16;
	}
}

bool StatePlayBooleanWorld::_imGuiActive() const
{
	return mDebugDisplay.active();
}

ImVec2 StatePlayBooleanWorld::wpVecToImVec2(wp::Vector2 const& v, wp::Vector2 const& offset, wp::Vector2 const& size, wp::Vector2 const& scale)
{
	return {
		(v.x - offset.x) * scale.x,
		size.y - (v.y - offset.y) * scale.y
	};
}

void StatePlayBooleanWorld::ImGui_renderTriangulation(bw::core::Triangulation const& triangulation, wp::BoundingBox const& viewBounds, wp::Vector2 const& viewOffset, wp::Vector2 const& viewSize, wp::Vector2 const& viewScale, ImDrawList* drawList)
{
	VAR_UNUSED(viewBounds);
	VAR_UNUSED(viewSize);

	auto numWorldTriangles = (uint32_t)triangulation.tris.size();

	if (numWorldTriangles > 0)
	{
		drawList->Flags &= ~ImDrawListFlags_AntiAliasedFill;

		for (uint32_t i = 0; i < numWorldTriangles; ++i)
		{
			auto const& tri = triangulation.tris[i];

			drawList->AddTriangleFilled(
				wpVecToImVec2(tri.v[0].p, viewOffset, viewSize, viewScale),
				wpVecToImVec2(tri.v[1].p, viewOffset, viewSize, viewScale),
				wpVecToImVec2(tri.v[2].p, viewOffset, viewSize, viewScale),
				gImGui_MapBackgroundColour
			);

			if (mDebugDisplay._renderTriangulationLines)
			{
				drawList->AddTriangle(
					wpVecToImVec2(tri.v[0].p, viewOffset, viewSize, viewScale),
					wpVecToImVec2(tri.v[1].p, viewOffset, viewSize, viewScale),
					wpVecToImVec2(tri.v[2].p, viewOffset, viewSize, viewScale),
					gImGui_TriangulationLineColour
				);
			}
		}
	}
}

void StatePlayBooleanWorld::ImGui_renderBorder(vector<bw::core::ClippedPolygon> const& clippedPolygons, wp::BoundingBox const& viewBounds, wp::Vector2 const& viewOffset, wp::Vector2 const& viewSize, wp::Vector2 const& viewScale, ImDrawList* drawList)
{
	VAR_UNUSED(viewBounds);
	VAR_UNUSED(viewSize);

	auto numClippedPolygons = (uint32_t)clippedPolygons.size();

	if (numClippedPolygons > 0)
	{
		drawList->Flags |= ImDrawListFlags_AntiAliasedFill;

		for (auto const& clippedPolygon : clippedPolygons)
		{
			auto numPolyVertices = (int)clippedPolygon.vertices.size();
			vector<ImVec2> imPoints(numPolyVertices);

			for (int i = 0; i < numPolyVertices; ++i)
			{
				imPoints[i] = wpVecToImVec2(clippedPolygon.vertices[i].p, viewOffset, viewSize, viewScale);
			}

			drawList->AddPolyline(imPoints.data(), numPolyVertices, gImGui_MapBorderColour, ImDrawFlags_Closed, 2.0f);
		}
	}
}

void StatePlayBooleanWorld::ImGui_renderPrimitives(vector<wp::Vector2> const& viewVertices, vector<bw::core::Primitive*> const& primitives, wp::BoundingBox const& viewBounds, wp::Vector2 const& viewOffset, wp::Vector2 const& viewSize, wp::Vector2 const& viewScale, ImDrawList* drawList)
{
	VAR_UNUSED(viewBounds);

	auto dataGenerator = getWDG();
	auto clippingPrims = dataGenerator->getSourceClippingPrimitives();
	set<bw::core::Primitive*> clippingPrimsSet(clippingPrims.begin(), clippingPrims.end());

	if (!primitives.empty())
	{
		drawList->Flags |= ImDrawListFlags_AntiAliasedFill;

		for (auto primitive : primitives)
		{
			// Primitive borders
			vector<ImVec2> ghostBorderPoints;

			auto complexPolygons = primitive->getVertices();

			for (auto const& complexPolygon : complexPolygons)
			{
				for (auto const& polygon : complexPolygon)
				{
					auto numVertices = (int)polygon.size();
					vector<ImVec2> imPoints(numVertices);

					for (int i = 0; i < numVertices; ++i)
					{
						imPoints[i] = wpVecToImVec2(polygon[i].p, viewOffset, viewSize, viewScale);
					}

					// Filled in polygon for those directly in view
					if (dataGenerator->primitiveInView(primitive, viewVertices))
					{
						drawList->AddConcavePolyFilled(imPoints.data(), numVertices, gImGui_PrimitiveInViewColour);
					}
					else if (clippingPrimsSet.find(primitive) != clippingPrimsSet.end())
					{
						drawList->AddConcavePolyFilled(imPoints.data(), numVertices, gImGui_PrimitiveInSourceSetColour);
					}

					// Border
					drawList->AddPolyline(imPoints.data(), numVertices, gImGui_PrimitiveColour, ImDrawFlags_Closed, 1.f);
				}
			}

			// Primitive bounds
			if (false)
			{
				bool primStatic = primitive->isStatic();
				auto primitiveBounds = primitive->getBounds();
				auto primitiveBoundsColour = primStatic ? gImGui_PrimitiveStaticBoundsColour : gImGui_PrimitiveAnimatedBoundsColour;

				wp::Vector2 primMinExtent, primMaxExtent;

				primitiveBounds.getExtents(primMinExtent, primMaxExtent);

				drawList->AddRect(
					wpVecToImVec2(primMinExtent, viewOffset, viewSize, viewScale), 
					wpVecToImVec2(primMaxExtent, viewOffset, viewSize, viewScale),
					primitiveBoundsColour
				);
			}
		}
	}
}

void StatePlayBooleanWorld::ImGui_renderView(vector<wp::Vector2> const& viewVertices, wp::BoundingBox const& viewBounds, wp::Vector2 const& viewOffset, wp::Vector2 const& viewSize, wp::Vector2 const& viewScale, ImDrawList* drawList)
{
	VAR_UNUSED(viewBounds);

	// View cone
	auto numVertices = (uint32_t)viewVertices.size();
	for (uint32_t i = 0; i < numVertices; ++i)
	{
		uint32_t j = (i + 1) % numVertices;

		drawList->AddLine(
			wpVecToImVec2(viewVertices[i], viewOffset, viewSize, viewScale),
			wpVecToImVec2(viewVertices[j], viewOffset, viewSize, viewScale),
			gImGui_ViewAreaColour,
			2.0f
		);
	}

	ImVec2 playerPos = wpVecToImVec2(viewVertices[0], viewOffset, viewSize, viewScale);

	// View radius
	drawList->AddCircleFilled(playerPos, BW_PLAYER_VIEW_DISTANCE, ImColor(0.5f, 0.8f, 0.5f, 0.25f));

	// Player circle
	drawList->AddCircleFilled(playerPos, BW_PLAYER_RADIUS, ImColor(0.8f, 0.8f, 0.4f));
}

void StatePlayBooleanWorld::debug_renderMinimap(wp::Vector2 const& viewSize, wp::Vector2 const& viewOffset, wp::Vector2 const& viewScale, wp::BoundingBox const& viewBounds, ImDrawList* drawList)
{
	if (!mDebugDisplay.minimap)
	{
		return;
	}
	
	// Shared objects
	auto world = getMap()->getWorld();
	auto dataGenerator = dynamic_cast<bw::core::DynamicWorldDataGenerator const*>(world->getWorldDataGenerator());
	auto viewVertices = dataGenerator->getViewVertices();

	auto const& triangulation = mWorldData.getTriangulation();
	auto const& clippedPolygons = mWorldData.getArrangementPolygons();
	auto primitives = world->findPrimitives(viewBounds);
	auto cellSize = world->getPrimitiveAccelerationGridSize();

	ImGui_renderTriangulation(triangulation, viewBounds, viewOffset, viewSize, viewScale, drawList);
	ImGui_renderBorder(clippedPolygons, viewBounds, viewOffset, viewSize, viewScale, drawList);
	ImGui_renderPrimitives(viewVertices, primitives, viewBounds, viewOffset, viewSize, viewScale, drawList);
	ImGui_renderView(viewVertices, viewBounds, viewOffset, viewSize, viewScale, drawList);
}

void StatePlayBooleanWorld::debug_renderCollisionSim(wp::Vector2 const& viewSize, wp::Vector2 const& viewOffset, wp::Vector2 const& viewScale, wp::BoundingBox const& viewBounds, ImDrawList* drawList)
{
	if (!mDebugDisplay.collisionSim)
	{
		return;
	}

	auto const& lines = mWorldCollisionSim->getLines();
	auto const& graph = mWorldData.getGraph();

	for (auto const& line : lines)
	{
		auto const& v0 = line.getVertex(0);
		auto const& v1 = line.getVertex(1);
		auto edgeIndex = (uint32_t)line.getUserData();

		// See if it's two-sided
		auto const& edge = graph.edges[edgeIndex];
		ImColor lineColour = gImGui_CollisionLineSolidColour;
		float lineWidth = 2.5f;

		if (edge.is2Sided())
		{
			lineColour = gImGui_CollisionLine2WayColour;
			lineWidth = 1.0f;
		}

		drawList->AddLine(
			wpVecToImVec2(v0, viewOffset, viewSize, viewScale),
			wpVecToImVec2(v1, viewOffset, viewSize, viewScale),
			lineColour,
			lineWidth
		);
	}

	// Player circle
	ImVec2 playerPos = wpVecToImVec2(getPlayerPosition(), viewOffset, viewSize, viewScale);
	drawList->AddCircleFilled(playerPos, BW_PLAYER_RADIUS, ImColor(0.8f, 0.8f, 0.4f));
}

void StatePlayBooleanWorld::debug_renderClipGenerationInfo(ImDrawList* drawList)
{
	if (!mDebugDisplay.clipGeneration)
	{
		return;
	}

	if (ImGui::Begin("Clipping records"))
	{
		lock_guard<mutex> lock(mClippingRecordsMutex);

		ImGuiTableFlags flags =
			ImGuiTableFlags_SizingStretchSame |
			ImGuiTableFlags_Resizable |
			ImGuiTableFlags_BordersOuter |
			ImGuiTableFlags_BordersV |
			ImGuiTableFlags_ContextMenuInBody;

		if (ImGui::BeginTable("Generation", 13, flags))
		{
			ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, 128);
			ImGui::TableSetupColumn("Gen 0", ImGuiTableColumnFlags_WidthFixed, 128);
			ImGui::TableSetupColumn("Gen 1", ImGuiTableColumnFlags_WidthFixed, 128);
			ImGui::TableSetupColumn("Commit", ImGuiTableColumnFlags_WidthFixed, 128);
			ImGui::TableSetupColumn("Lag (s)", ImGuiTableColumnFlags_WidthFixed, 96);
			ImGui::TableSetupColumn("Gen (us)", ImGuiTableColumnFlags_WidthFixed, 96);
			ImGui::TableSetupColumn("< p");
			ImGui::TableSetupColumn("< p:vis");
			ImGui::TableSetupColumn("< p:upd");
			ImGui::TableSetupColumn("< pv");
			ImGui::TableSetupColumn("> P");
			ImGui::TableSetupColumn("> Pv");
			ImGui::TableSetupColumn("> Pv:lerp");
			ImGui::TableHeadersRow();

			auto numRecords = (int)mClippingRecords.size();
			for (uint32_t i = 0; i < min(10, numRecords); ++i)
			{
				auto const& record = mClippingRecords[numRecords - 1 - i];

				ImGui::TableNextRow();

				// Id
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%d", record.clippingId);

				// Gen started
				ImGui::TableSetColumnIndex(1);

				if (record.generationStartedTime >= 0.0)
				{
					ImGui::Text("%5.4f", record.generationStartedTime);
				}
				else
				{
					ImGui::Text("-");
				}

				// Gen complete
				ImGui::TableSetColumnIndex(2);

				if (record.generationCompleteTime >= 0.0)
				{
					ImGui::Text("%5.4f", record.generationCompleteTime);
				}
				else
				{
					ImGui::Text("-");
				}

				// Commit complete
				ImGui::TableSetColumnIndex(3);

				if (record.commitedTime >= 0.0)
				{
					ImGui::Text("%5.4f", record.commitedTime);
				}
				else
				{
					ImGui::Text("-");
				}

				// Commit lag
				ImGui::TableSetColumnIndex(4);

				if (record.commitedTime >= 0.0)
				{
					ImGui::Text("%5.4f", record.commitedTime - record.generationCompleteTime);
				}
				else
				{
					ImGui::Text("-");
				}

				// Gen time
				ImGui::TableSetColumnIndex(5);

				if (record.generationCompleteTime >= 0.0)
				{
					ImGui::Text("%d", record.generationTimeNs / 1000);
				}
				else
				{
					ImGui::Text("-");
				}

				// Input Primitives
				ImGui::TableSetColumnIndex(6);

				if (record.generationCompleteTime >= 0.0)
				{
					ImGui::Text("%d", record.stats.prim.candidateCount);
				}
				else
				{
					ImGui::Text("-");
				}

				// Visible Primitives
				ImGui::TableSetColumnIndex(7);

				if (record.generationCompleteTime >= 0.0)
				{
					ImGui::Text("%d", record.stats.prim.visibleCount);
				}
				else
				{
					ImGui::Text("-");
				}

				// Visible Primitives
				ImGui::TableSetColumnIndex(8);

				if (record.generationCompleteTime >= 0.0)
				{
					ImGui::Text("%d", record.stats.prim.updateVertexCount);
				}
				else
				{
					ImGui::Text("-");
				}

				// Primitive vertex count
				ImGui::TableSetColumnIndex(9);

				if (record.generationCompleteTime >= 0.0)
				{
					ImGui::Text("%d", record.stats.clip.primVerticesProcessed);
				}
				else
				{
					ImGui::Text("-");
				}

				// Polygons created
				ImGui::TableSetColumnIndex(10);

				if (record.generationCompleteTime >= 0.0)
				{
					ImGui::Text("%d", record.stats.clip.polygonsGenerated);
				}
				else
				{
					ImGui::Text("-");
				}

				// Vertices created
				ImGui::TableSetColumnIndex(11);

				if (record.generationCompleteTime >= 0.0)
				{
					ImGui::Text("%d", record.stats.clip.verticesGenerated);
				}
				else
				{
					ImGui::Text("-");
				}

				// Vertices interpolated
				ImGui::TableSetColumnIndex(12);

				if (record.generationCompleteTime >= 0.0)
				{
					ImGui::Text("%d", record.stats.clip.interpolatedVertices);
				}
				else
				{
					ImGui::Text("-");
				}
			}

			ImGui::EndTable();
		}
	}

	ImGui::End();
}

void StatePlayBooleanWorld::_renderImGui(float frameTime, void* imGuiCtx, void* imPlotCtx, void* allocFunc, void* freeFunc, void* userData)
{
	VAR_UNUSED(frameTime);

	// Set context from caller
	auto thisImGuiCtx = static_cast<ImGuiContext*>(imGuiCtx);
	auto prevImGuiCtx = ImGui::GetCurrentContext();

	auto thisImPlotCtx = static_cast<ImPlotContext*>(imPlotCtx);
	auto prevImPlotCtx = ImPlot::GetCurrentContext();

	ImGui::SetCurrentContext(thisImGuiCtx);
	ImPlot::SetCurrentContext(thisImPlotCtx);

	ImGuiMemAllocFunc prevAllocFunc, imguiAllocFunc = static_cast<ImGuiMemAllocFunc>(allocFunc);
	ImGuiMemFreeFunc prevFreeFunc, imguiFreeFunc = static_cast<ImGuiMemFreeFunc>(freeFunc);
	void* prevUserData;

	ImGui::GetAllocatorFunctions(&prevAllocFunc, &prevFreeFunc, &prevUserData);
	ImGui::SetAllocatorFunctions(imguiAllocFunc, imguiFreeFunc, userData);

	//
	// Render
	//
	ImGuiIO& io = ImGui::GetIO();
	wp::Vector2 viewSize{ (float)mwRenderSystem->getWindowWidth(), (float)mwRenderSystem->getWindowHeight() };
	wp::Vector2 viewOffset = getPlayerPosition() - viewSize * 0.5f;
	wp::Vector2 viewScale{ 1.0f, 1.0f };
	wp::BoundingBox viewBounds{ viewOffset, { io.DisplaySize.x, io.DisplaySize.y } };

	auto drawList = ImGui::GetBackgroundDrawList();

	debug_renderMinimap(viewSize, viewOffset, viewScale, viewBounds, drawList);
	debug_renderCollisionSim(viewSize, viewOffset, viewScale, viewBounds, drawList);
	debug_renderClipGenerationInfo(drawList);

	// Reset.  We don't really need to do this as nothing is done on this DLL's context
	VAR_UNUSED(prevImGuiCtx);
	VAR_UNUSED(prevImPlotCtx);
	//ImGui::SetCurrentContext(prevImGuiCtx);
	//ImPlot::SetCurrentContext(prevImPlotCtx);
	//ImGui::SetAllocatorFunctions(prevAllocFunc, prevFreeFunc, prevUserData);
}
