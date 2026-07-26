#include "utils/FileSystem.h"

#include "miniz.c"
#include "ZipResourceLocation.h"

using namespace std;

using namespace wp::application;

ZipResourceLocation::ZipResourceLocation(wp::Logger* logger, string const& file, string const& definitionFile)
	: resourcesystem::ResourceLocation(logger, file, "ZipFile", definitionFile)
	, mRootPath(file)
	, mDefinitionPath(file + ":" + definitionFile)
{
	mArchive = new mz_zip_archive();
	auto archive = static_cast<mz_zip_archive*>(mArchive);

	if (!mz_zip_reader_init_file(archive, mRootPath.c_str(), 0))
	{
		delete archive;

		string errMsg = "Could not open zipfile '" + mRootPath + "'.";
		throw exception(errMsg.c_str());
	}

	for (int i = 0; i < (int)mz_zip_reader_get_num_files(archive); ++i)
	{
		mz_zip_archive_file_stat fileStat;

		if (!mz_zip_reader_file_stat(archive, i, &fileStat))
		{
			mz_zip_reader_end(archive);
			delete archive;

			string errMsg = "Could not read zipfile entry for '" + mRootPath + "'.";
			throw exception(errMsg.c_str());
		}

		FileEntry fe;

		fe.filename = utils::FileSystem::standardisePath(fileStat.m_filename);
		fe.index = i;
		fe.compressedSize = (size_t)fileStat.m_comp_size;
		fe.uncompressedSize = (size_t)fileStat.m_uncomp_size;

		mFileEntries[fe.filename] = fe;
	}

	mz_zip_reader_end(archive);
}

ZipResourceLocation::~ZipResourceLocation()
{
	delete static_cast<mz_zip_archive*>(mArchive);
}

string const& ZipResourceLocation::getRootPath() const
{
	return mRootPath;
}
/*
resourcesystem::DataStreamPtr ZipResourceLocation::getHardResourceDataStream(string const& file, string const& namesp) const
{
	auto it = mFileEntries.find(utils::FileSystem::standardisePath(file));
	FileEntry const& e = it->second;

	size_t fileSize;
	char* buffer = (char*)mz_zip_reader_extract_to_heap(static_cast<mz_zip_archive*>(mArchive), e.index, &fileSize, 0);

	return resourcesystem::DataStreamPtr(new resourcesystem::DataStream(buffer, fileSize, namesp));
}

resourcesystem::DataStreamPtr ZipResourceLocation::getHardResourceDataStreamProgressive(string const& file, string const& namesp, DataStreamFetchProgressCallback progress) const
{
	throw exception("ZipResourceLocation::getHardResourceDataStreamProgressive() not yet implemented.");
}
*/
bool ZipResourceLocation::hardResourceExists(string const& file) const
{
	string filepath = utils::FileSystem::concatPaths(mRootPath, file);
	return utils::FileSystem::fileExists(filepath);
}

uint8_t* ZipResourceLocation::readData(string const& source, uint32_t* dataSize)
{
	auto it = mFileEntries.find(utils::FileSystem::standardisePath(source));
	FileEntry const& e = it->second;

	size_t ds = (size_t)*dataSize;
	auto res = (uint8_t*)mz_zip_reader_extract_to_heap(static_cast<mz_zip_archive*>(mArchive), (mz_uint)e.index, &ds, 0);
	*dataSize = (uint32_t)ds;
	return res;
}

string const& ZipResourceLocation::getDefinitionFile() const
{
	return mDefinitionPath;
}