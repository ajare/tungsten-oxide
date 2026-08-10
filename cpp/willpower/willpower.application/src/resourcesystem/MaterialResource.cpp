#include "willpower/common/XmlReader.h"
#include <mpp/ProgrammaticBasicMaterialStream.h>

#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/MaterialResource.h"
#include "willpower/application/resourcesystem/ImageResource.h"
#include "willpower/application/resourcesystem/ProgramResource.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			using namespace std;

			MaterialResource::MaterialResource(string const& name, string const& namesp, string const& source, map<string, string> const& tags, ResourceLocation* location)
				: Resource(name, namesp, "Material", source, tags, location)
			{
			}

			bool MaterialResource::load(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
			{
				WP_UNUSED(renderSystem);

				auto program = static_cast<ProgramResource*>(getDependentResource("Program").get());

				auto materialStream = new mpp::ProgrammaticBasicMaterialStream(resourceMgr);

				// Program
				materialStream->setProgram(program->getQualifiedName());

				// Textures
				for (uint32_t i = 0; i < getNumTextureDefinitions(); ++i)
				{
					auto const& textureDef = getTextureDefinition(i);

					if (textureDef.isResource)
					{
						auto texture = static_cast<ImageResource*>(getDependentResource(textureDef.resourceName).get());
						materialStream->setTexture(textureDef.samplerName, texture->getQualifiedName());
					}
					else
					{
						materialStream->setDefaultTexture(textureDef.samplerName);
					}
				}

				mMppResource = resourceMgr->declareResource(getQualifiedName(), mpp::ResourceStreamPtr(materialStream)).first;
				mMppResource->acquire(this);

				return true;
			}

			bool MaterialResource::unload(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
			{
				WP_UNUSED(renderSystem);
				WP_UNUSED(resourceMgr);

				mMppResource->release(this);
				return true;
			}

			uint32_t MaterialResource::getNumTextureDefinitions() const
			{
				return (uint32_t)mTextureDefinitions.size();
			}

			MaterialResource::TextureDefinition const& MaterialResource::getTextureDefinition(uint32_t index) const
			{
				assert(index < getNumTextureDefinitions() && "MaterialResource::getTextureDefinition() index out of range.");

				return mTextureDefinitions[index];
			}

			uint32_t MaterialResource::getNumTextures() const
			{
				auto res = static_cast<mpp::Material*>(getMppResource().get());

				return res->getNumTextures();
			}

			uint32_t MaterialResource::getTextureWidth(uint32_t index) const
			{
				auto res = static_cast<mpp::Material*>(getMppResource().get());
				auto tex = static_cast<mpp::Texture*>(res->getTexture(index).get());

				return (uint32_t)tex->getWidth();
			}

			uint32_t MaterialResource::getTextureHeight(uint32_t index) const
			{
				auto res = static_cast<mpp::Material*>(getMppResource().get());
				auto tex = static_cast<mpp::Texture*>(res->getTexture(index).get());

				return (uint32_t)tex->getHeight();
			}

		} // resourcesystem
	} // application
} // WP_NAMESPACE