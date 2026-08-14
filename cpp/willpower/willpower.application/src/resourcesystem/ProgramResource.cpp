#include <mpp/ProgrammaticProgramStream.h>
#include <mpp/program/Parser.h>

#include "willpower/common/XmlReader.h"
#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/ProgramResource.h"
#include "willpower/application/resourcesystem/ShaderResource.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			using namespace std;

			ProgramResource::ProgramResource(string const& name, string const& namesp, string const& source, map<string, string> const& tags, ResourceLocation* location)
				: Resource(name, namesp, "Program", source, tags, location)
				, mNumTextures(0)
				, mUseDiffuse(false)
				, mUseColours(false)
				, mUseAtlas(false)
				, mUseRotation(false)
			{
			}

			bool ProgramResource::load(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
			{
				WP_UNUSED(renderSystem);

				auto vertShader = static_cast<ShaderResource*>(getDependentResource("Vertex").get());
				auto fragShader = static_cast<ShaderResource*>(getDependentResource("Fragment").get());

				auto parser = make_shared<mpp::program::Parser>();

				parser->setMeshSpecification(mSpecification);
				parser->setVertexSource(vertShader->getText());
				parser->setFragmentSource(fragShader->getText());

				auto ps = make_shared<mpp::ProgrammaticProgramStream>(resourceMgr);
				ps->setParser(parser);
				
				// Set attribs
				set<string> attribs;

				if (mNumTextures > 0)
				{
					attribs.insert("Texture");
				}

				if (mUseDiffuse)
				{
					attribs.insert("Diffuse");
				}

				if (mUseColours)
				{
					attribs.insert("Colours");
				}

				if (mUseAtlas)
				{
					attribs.insert("Atlas");
				}

				if (mUseRotation)
				{
					attribs.insert("Rotation");
				}

				switch (mSpecification.getPrimitiveType())
				{
				case mpp::mesh::Primitive::Type::Points:
					attribs.insert("Points");
					break;

				case mpp::mesh::Primitive::Type::Lines:
					attribs.insert("Lines");
					break;

				case mpp::mesh::Primitive::Type::Triangles:
					attribs.insert("Triangles");
					break;
				}

				ps->setAttribs(attribs);

				mMppResource = resourceMgr->declareResource(getQualifiedName(), ps).first;
				mMppResource->acquire(this);
				return true;
			}

			bool ProgramResource::unload(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
			{
				WP_UNUSED(renderSystem);
				WP_UNUSED(resourceMgr);

				mMppResource->release(this);
				return true;
			}

		} // resourcesystem
	} // application
} // WP_NAMESPACE