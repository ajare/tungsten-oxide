#include <fstream>

#include "utils/FileSystem.h"

#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/DataStream.h"
#include "willpower/application/resourcesystem/DirectoryResourceLocation.h"

using namespace std;

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{

			DirectoryResourceLocation::DirectoryResourceLocation(Logger* logger, string const& directory, string const& definitionFile)
				: ResourceLocation(logger, directory, "Directory", definitionFile)
				, mRootPath(directory)
				, mDefinitionPath(utils::FileSystem::concatPaths(mRootPath, mDefinitionFile))
			{
			}

			string const& DirectoryResourceLocation::getRootPath() const
			{
				return mRootPath;
			}

			bool DirectoryResourceLocation::hardResourceExists(string const& file) const
			{
				string filepath = utils::FileSystem::concatPaths(mRootPath, file);
				return utils::FileSystem::fileExists(filepath);
			}

			uint8_t* DirectoryResourceLocation::readData(string const& source, uint32_t* dataSize)
			{
				ifstream fp;

				string filePath = utils::FileSystem::concatPaths(mRootPath, source);
				fp.open(filePath, ios::in | ios::binary | ios::ate);

				if (!fp.is_open())
				{
					throw wp::Exception("Could not read in data stream from '" + filePath + "'.");
				}

				// Get data size
				fp.seekg(0, ios::end);
				uint32_t size = (uint32_t)fp.tellg();
				*dataSize = size;

				// Read in data
				fp.seekg(0, ios::beg);
				uint8_t* data = new uint8_t[size];

				fp.read((char*)data, size);
				fp.close();

				return data;
			}

			string const& DirectoryResourceLocation::getDefinitionFile() const
			{
				return mDefinitionPath;
			}

		} // resourcesystem
	} // application
} // WP_NAMESPACE
