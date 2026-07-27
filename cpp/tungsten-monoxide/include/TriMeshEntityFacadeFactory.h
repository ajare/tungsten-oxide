#pragma once

#include <applib/EntityFacadeFactory.h>
#include <applib/VisualTriMeshEntityFacade.h>

#include "Platform.h"
#include "TriMeshDataProvider.h"


class TriMeshEntityFacadeFactory : public applib::EntityFacadeFactory
{
	applib::VisualTriMeshEntityFacade::DataProviderFactory mProviderFactory;

	wp::application::resourcesystem::ResourcePtr mTexture;

public:

	TriMeshEntityFacadeFactory(applib::VisualTriMeshEntityFacade::DataProviderFactory providerFactory, wp::application::resourcesystem::ResourcePtr texture)
		: EntityFacadeFactory()
		, mProviderFactory(providerFactory)
		, mTexture(texture)
	{
	}

	applib::EntityFacade* create(size_t initialSize) override
	{
		return new applib::VisualTriMeshEntityFacade(mProviderFactory, mTexture, initialSize);
	}
};
