#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/ShaderResource.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			using namespace std;

			ShaderResource::ShaderResource(string const& name, string const& namesp, string const& source, map<string, string> const& tags, ResourceLocation* location)
				: Resource(name, namesp, "Shader", source, tags, location)
			{
			}

			void ShaderResource::parseData(DataStreamPtr dataPtr)
			{
				mText = string((char const*)dataPtr->getData(), dataPtr->getSize());
			}

			void ShaderResource::destroy()
			{
				mText.clear();
			}

			string const& ShaderResource::getText() const
			{
				return mText;
			}

		} // resourcesystem
	} // application
} // WP_NAMESPACE