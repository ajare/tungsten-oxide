#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/XmlFileResource.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			using namespace std;

			XmlFileResource::XmlFileResource(string const& name, string const& namesp, string const& source, map<string, string> const& tags, ResourceLocation* location)
				: Resource(name, namesp, "XmlFile", source, tags, location)
				, mReader(nullptr)
			{
			}

			void XmlFileResource::parseData(DataStreamPtr dataPtr)
			{
				assert(!mReader);
				mReader = XmlReader::fromString(string((char*)dataPtr->getData(), dataPtr->getSize()));
			}

			void XmlFileResource::destroy()
			{
				delete mReader;
				mReader = nullptr;
			}

			XmlReader* XmlFileResource::getReader()
			{
				return mReader;
			}

		} // resourcesystem
	} // application
} // WP_NAMESPACE