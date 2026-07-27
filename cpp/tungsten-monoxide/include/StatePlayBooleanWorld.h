#pragma once

#include <vector>
#include <deque>
#include <mutex>

#include <mpp/Camera.h>

#include <willpower/application/StateFactory.h>

#include <willpower/collide/Collider.h>

#include <willpower/common/AccelerationGrid.h>

#include <willpower/viz/DynamicTriangleRenderer.h>

#include <applib/StatePlay.h>

#include <core/DynamicWorldDataGenerator.h>

#include "imgui/imgui.h"

#include "Platform.h"
#include "WorldCollisionSim.h"
#include "WorldRenderer.h"
#include "Map.h"
#include "DisplayMessage.h"
#include "ClippingRecord.h"


class APPLICATION_API StatePlayBooleanWorld : public applib::StatePlay
{
	struct DebugDisplay
	{
		bool minimap{ false };
		bool collisionSim{ false };
		bool clipGeneration{ false };

		bool _renderTriangulationLines{ false };

		bool active() const
		{
			return minimap || collisionSim || clipGeneration;
		}
	};

private:

	typedef wp::viz::DynamicTriangleRenderer<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeFloat> WorldTriangleRenderer;

private:

	double mGlobalTime;

	mpp::CameraPtr mCamera3d;

	bw::core::WorldData mWorldData;

	WorldCollisionSim* mWorldCollisionSim;

	wp::collide::Collider* mPlayerCollider;

	uint8_t mCurrentLayer;

	int32_t mPlayerPolygonIndex;

	int32_t mPlayerBorderIntersectIndex;

	uint32_t mCollisionsProcessed;

	float mPlayerPrevAngle, mPlayerPrevPitch;

	bool mExitScheduled;

	// Created/managed in load states
	WorldRenderer* mwRenderer;

	// ImGui view
	DebugDisplay mDebugDisplay;

	std::mutex mClippingRecordsMutex;

	std::deque<ClippingRecord> mClippingRecords;

	std::deque<DisplayMessage> mDisplayMessages;

private:

	void createCamera();

	void setupMapRenderer(applib::StateTransitionData* transitionData) override;

	std::map<std::string, std::tuple<wp::viz::Renderer*, int, bool>> createAdditionalRenderers(mpp::ResourceManager* renderResourceMgr) override;
	
	void registerInput() override;

	void createGameObjects(wp::application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args) override;

	void destroyGameObjects() override;

	void setupEntityFacades() override;

	void setupEntities() override;

	void updatePreInput(float frameTime) override;

	void updatePreEntities(float frameTime) override;

	void updatePostEntities(float frameTime) override;

	void updateAudio(float frameTime);

	Map* getMap();

	Map const* getMap() const;

	applib::PhysicalStats& getPlayerPhysicalStats();

	applib::PhysicalStats const& getPlayerPhysicalStats() const;

	wp::Vector2 getPlayerPosition() const;

	float getPlayerAngle() const;

	bool playerInWorld() const;

	bool playerIntersectsWorldBorders() const;

	void getWorldInput(wp::Vector2* curPosition, wp::Vector2* newPosition, float* curAngle, float* newAngle, float frameTime) const;

	void setupPlayerCollision();

	void createWorldCollisions();

	void exit();

	void handleClippingUpdate(bw::core::DynamicWorldDataGenerator::GenerationDetails const& details);
	
	bw::core::DynamicWorldDataGenerator* getWDG();

	uint32_t getPrimitiveAtPosition(wp::Vector2 const& pos) const;

	uint32_t getPlayerPrimitive() const;

	float getFloorHeightAt(wp::Vector2 const& pos) const;

	float getCeilingHeightAt(wp::Vector2 const& pos) const;

	float getPlayerFloorHeight() const;

	float getPlayerCeilingHeight() const;

	void addDisplayMessage(DisplayMessage::Level level, std::string const& message);

	// Debug / UI
	void debug_renderMinimap(wp::Vector2 const& viewSize, wp::Vector2 const& viewOffset, wp::Vector2 const& viewScale, wp::BoundingBox const& viewBounds, ImDrawList* drawList);

	void debug_renderCollisionSim(wp::Vector2 const& viewSize, wp::Vector2 const& viewOffset, wp::Vector2 const& viewScale, wp::BoundingBox const& viewBounds, ImDrawList* drawList);

	void debug_renderClipGenerationInfo(ImDrawList* drawList);

	void ImGui_renderTriangulation(bw::core::Triangulation const& triangulation, wp::BoundingBox const& viewBounds, wp::Vector2 const& viewOffset, wp::Vector2 const& viewSize, wp::Vector2 const& viewScale, ImDrawList* drawList);

	void ImGui_renderBorder(std::vector<bw::core::ClippedPolygon> const& clippedPolygons, wp::BoundingBox const& viewBounds, wp::Vector2 const& viewOffset, wp::Vector2 const& viewSize, wp::Vector2 const& viewScale, ImDrawList* drawList);

	void ImGui_renderPrimitives(std::vector<wp::Vector2> const& viewVertices, std::vector<bw::core::Primitive*> const& primitives, wp::BoundingBox const& viewBounds, wp::Vector2 const& viewOffset, wp::Vector2 const& viewSize, wp::Vector2 const& viewScale, ImDrawList* drawList);

	void ImGui_renderView(std::vector<wp::Vector2> const& viewVertices, wp::BoundingBox const& viewBounds, wp::Vector2 const& viewOffset, wp::Vector2 const& viewSize, wp::Vector2 const& viewScale, ImDrawList* drawList);

	static ImVec2 wpVecToImVec2(wp::Vector2 const& v, wp::Vector2 const& offset, wp::Vector2 const& size, wp::Vector2 const& scale);

protected:

	void updateActions(std::vector<std::string> const& activeStates, float frameTime) override;

	void updatePreRenderers(float frameTime) override;

	void setup(wp::application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args) override;

	void suspendImpl(void* args = nullptr) override;

	void resumeImpl(void* args) override;

	void updateImpl(float frameTime) override;

	void renderImpl(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) override;

public:

	StatePlayBooleanWorld();

	~StatePlayBooleanWorld();

	std::vector<std::string> getDebuggingText() const override;

	bool _imGuiActive() const override;

	void _renderImGui(float frameTime, void* imGuiCtx, void* imPlotCtx, void* allocFunc, void* freeFunc, void* userData) override;
};

class StatePlayBooleanWorldFactory : public wp::application::StateFactory
{
	wp::Logger* mLogger;

public:

	explicit StatePlayBooleanWorldFactory(wp::Logger* logger)
		: wp::application::StateFactory("Play")
		, mLogger(logger)
	{
	}

	wp::application::State* createState()
	{
		auto state = new StatePlayBooleanWorld();
		state->setLogger(mLogger);
		return state;
	}
};
