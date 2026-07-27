#pragma once

#include <vector>

#include <willpower/application/StateFactory.h>
#include <willpower/application/resourcesystem/ResourceManager.h>

#include "Platform.h"
#include "State.h"


namespace applib
{

	class APPLIB_API StateController : public State
	{
		std::string mNextState;

		std::string mGameResourceName;

	private:

		virtual std::string getNextStateName(std::string const& prevStateName, StateTransitionData* transitionData) = 0;

		virtual void updateTransitionData(std::string const& prevStateName, std::string const& nextStateName, StateTransitionData* transitionData) = 0;

	protected:

		void transferTransitionMapData(MapTransitionData::MapData* from, MapTransitionData::MapData* to);

		void setTransitionNextMap(MapTransitionData* data, std::string const& name, wp::application::resourcesystem::ResourcePtr resource);

		void setup(wp::application::resourcesystem::ResourceManager* resourceMgr, mpp::RenderSystem* renderSystem, mpp::ResourceManager* renderResourceMgr, void* args = nullptr) override;

		void teardown() override;

		void suspendImpl(void* args = nullptr);

		void resumeImpl(void* args) override;

		void exitImpl();

		void updateImpl(float frameTime);

		void renderImpl(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr);

	public:

		StateController(std::string const& initialState);

		~StateController();
	};

}