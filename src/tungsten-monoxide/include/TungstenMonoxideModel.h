#pragma once

#include <filesystem>
#include <memory>

#include <applib/Model.h>

#include "Platform.h"
#include "TungstenPbrPackage.h"

namespace wp
{
	class Logger;
}

struct TungstenMonoxideModel : public applib::Model
{
	std::unique_ptr<TungstenPbrPackage> pbrPackage;

	TungstenMonoxideModel(applib::EntityHandlerFactoryFunction handlerFactory,
	                      wp::application::resourcesystem::ResourceManager* resourceMgr,
	                      std::filesystem::path pbrPackagePath,
	                      wp::Logger* logger)
		: applib::Model(handlerFactory, resourceMgr)
		, pbrPackage(std::make_unique<TungstenPbrPackage>(std::move(pbrPackagePath), logger))
	{
	}
};
