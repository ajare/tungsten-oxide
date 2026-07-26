#pragma once

#include <string>

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/Resource.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			/**	\class ResourceFactory
			*   \brief Base class for creating resources
			*/
			class WP_APPLICATION_API ResourceFactory
			{
				std::string mType;

			public:

				/**	\brief Constructor.
				*
				*	\param type State type.
				*/
				explicit ResourceFactory(std::string const& type)
					: mType(type)
				{
				}

				/*
				 * Destructor.
				 *
				 */
				virtual ~ResourceFactory() = default;

				/**	\brief Returns Resource type.
				*
				*	\return the Resource type.
				*/
				std::string const& getType() const
				{
					return mType;
				}

				/**	\brief Returns a new Resource instance.
				*
				*	\return the Resource instance.
				*/
				virtual Resource* createResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, ResourceLocation* location) = 0;
			};
		}
	} // application
} // WP_NAMESPACE
