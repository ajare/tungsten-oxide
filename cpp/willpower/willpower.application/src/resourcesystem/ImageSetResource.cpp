#include "willpower/common/XmlReader.h"
#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/ImageSetResource.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			using namespace std;

			ImageSetResource::ImageSetResource(string const& name, string const& namesp, string const& source, map<string, string> const& tags, ResourceLocation* location)
				: Resource(name, namesp, "ImageSet", source, tags, location)
			{
			}

			ResourcePtr ImageSetResource::getImage()
			{
				return getDependentResource("Image");
			}

			bool ImageSetResource::hasImageDefinition(string const& name) const
			{
				return mImageDefinitions.find(name) != mImageDefinitions.end();
			}

			bool ImageSetResource::hasImageDefinitionSet(string const& name) const
			{
				return mImageDefinitionSets.find(name) != mImageDefinitionSets.end();
			}

			ImageSetResource::ImageDefinition const& ImageSetResource::getImageDefinition(string const& name) const
			{
				auto it = mImageDefinitions.find(name);
				if (it != mImageDefinitions.end())
				{
					return it->second;
				}
				else
				{
					throw ResourceException(this, "could not find image definition '" + name + "'.");
				}
			}

			map<string, ImageSetResource::ImageDefinition> const& ImageSetResource::getImageDefinitions() const
			{
				return mImageDefinitions;
			}

			vector<ImageSetResource::ImageDefinition> const& ImageSetResource::getImagesetDefinition(string const& name) const
			{
				auto it = mImageDefinitionSets.find(name);
				if (it != mImageDefinitionSets.end())
				{
					return it->second;
				}
				else
				{
					throw ResourceException(this, "could not find imageset definition '" + name + "'.");
				}
			}

			map<string, vector<ImageSetResource::ImageDefinition>> const& ImageSetResource::getImageDefinitionSets() const
			{
				return mImageDefinitionSets;
			}

			void ImageSetResource::destroy()
			{
				mImageDefinitions.clear();
				mImageDefinitionSets.clear();
			}

			void ImageSetResource::getMaximumDimensions(uint32_t& width, uint32_t& height) const
			{
				uint32_t maxWidth = 0, maxHeight = 0;

				// Individual
				for (auto item : mImageDefinitions)
				{
					auto const&[name, def] = item;
					
					maxWidth = max(maxWidth, def.width);
					maxHeight = max(maxHeight, def.height);
				}

				// For sets just check first one, as they are all the same size
				for (auto item : mImageDefinitionSets)
				{
					auto const&[name, defs] = item;

					maxWidth = max(maxWidth, defs[0].width);
					maxHeight = max(maxHeight, defs[0].height);
				}

				width = maxWidth;
				height = maxHeight;
			}

		} // resourcesystem
	} // application
} // WP_NAMESPACE