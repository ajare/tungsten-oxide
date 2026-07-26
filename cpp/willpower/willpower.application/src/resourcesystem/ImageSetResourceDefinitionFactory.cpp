#include "willpower/common/XmlReader.h"
#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/ImageSetResourceDefinitionFactory.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			using namespace std;

			ImageSetResourceDefinitionFactory::ImageSetResourceDefinitionFactory(string const& factoryType)
				: ResourceDefinitionFactory("ImageSet", factoryType)
			{
			}

			void ImageSetResourceDefinitionFactory::clear(ImageSetResource* resource)
			{
				resource->mImageDefinitionSets.clear();
				resource->mImageDefinitions.clear();
			}

			void ImageSetResourceDefinitionFactory::addImageDefinition(ImageSetResource* resource, string const& name, ImageSetResource::ImageDefinition const& def)
			{
				resource->mImageDefinitions[name] = def;
			}

			void ImageSetResourceDefinitionFactory::addImageDefinitionSet(ImageSetResource* resource, string const& name, vector<ImageSetResource::ImageDefinition> const& defs)
			{
				resource->mImageDefinitionSets[name] = defs;
			}

			void ImageSetResourceDefinitionFactory::validateCoordinates(ImageSetResource* resource, string const& name, int x, int y, int width, int height)
			{
				auto imageRes = static_cast<ImageResource*>(resource->getImage().get());
				int imageWidth = imageRes->getWidth();
				int imageHeight = imageRes->getHeight();

				if (x < 0 || y < 0 || (x + width > imageWidth) || (y + height > imageHeight))
				{
					throw ResourceException(resource, "image definition for '" + name + "' is out of image bounds.");
				}
			}

			void ImageSetResourceDefinitionFactory::calculateUvCoords(ImageSetResource* resource, ImageSetResource::ImageDefinition* imageDef)
			{
				auto imageRes = static_cast<ImageResource*>(resource->getImage().get());
				int imageWidth = imageRes->getWidth();
				int imageHeight = imageRes->getHeight();

				imageDef->u[0] = (float)imageDef->x / imageWidth;
				imageDef->v[0] = 1.0f - (float)(imageDef->y + imageDef->height) / imageHeight;
				imageDef->u[1] = (float)(imageDef->x + imageDef->width) / imageWidth;
				imageDef->v[1] = 1.0f - (float)imageDef->y / imageHeight;
			}

			ImageSetResource::ImageDefinition ImageSetResourceDefinitionFactory::createImageDefinition(ImageSetResource* resource, string const& name, XmlNode* node)
			{
				uint32_t x = StringUtils::parseUInt(node->getAttribute("x"));
				uint32_t y = StringUtils::parseUInt(node->getAttribute("y"));
				uint32_t width = StringUtils::parseUInt(node->getAttribute("width"));
				uint32_t height = StringUtils::parseUInt(node->getAttribute("height"));

				validateCoordinates(resource, name, (int)x, (int)y, (int)width, (int)height);

				ImageSetResource::ImageDefinition imageDef;

				imageDef.x = x;
				imageDef.y = y;
				imageDef.width = width;
				imageDef.height = height;

				calculateUvCoords(resource, &imageDef);

				return imageDef;
			}

		} // resourcesystem
	} // application
} // WP_NAMESPACE