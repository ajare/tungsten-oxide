#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/TextFileResource.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			using namespace std;

			TextFileResource::TextFileResource(string const& name, string const& namesp, string const& source, map<string, string> const& tags, ResourceLocation* location)
				: Resource(name, namesp, "TextFile", source, tags, location)
			{
			}

			void TextFileResource::parseData(DataStreamPtr dataPtr)
			{
				mText = string((char const*)dataPtr->getData(), dataPtr->getSize());
			}

			void TextFileResource::destroy()
			{
				mText.clear();
			}

			string const& TextFileResource::getText() const
			{
				return mText;
			}

		} // resourcesystem
	} // application
} // WP_NAMESPACE