#pragma once

#include <string>

#include "willpower/common/XmlReader.h"

#include "willpower/application/Platform.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			class Resource;
			class ResourceManager;

			/**	\class ResourceFactory
			*   \brief Base class for creating resource definitions
			*/
			class WP_APPLICATION_API ResourceDefinitionFactory
			{
				std::string mResType;

				std::string mFactoryType;

			public:

				/**	\brief Constructor.
				*
				*	\param type State type.
				*/
				ResourceDefinitionFactory(std::string const& resType, std::string const& factoryType);

				/*
				 * Destructor.
				 *
				 */
				virtual ~ResourceDefinitionFactory() = default;

				/**	\brief Returns ResourceDefinition type.
				*
				*	\return the Resource type.
				*/
				std::string const& getResourceType() const;

				/**	\brief Returns Factory type.
				*
				*	\return the factory type.
				*/
				std::string const& getFactoryType() const;

				/**	\brief Returns a new Resource instance.
				*
				*	\return the Resource instance.
				*/
				virtual void create(Resource* resource, ResourceManager* resourceMgr, wp::XmlNode* node) = 0;
			};
		}
	} // application
} // WP_NAMESPACE
