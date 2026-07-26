#include <algorithm>
#include <sstream>
#include <numeric>
#include <regex>
#include <filesystem>
#include <fstream>

#include "willpower/common/FileSystem.h"
#include "willpower/common/StringUtils.h"


namespace WP_NAMESPACE
{

	using namespace std;

	//
	// FileInfo
	//
	FileSystem::FileInfo::FileInfo(string const& filepath)
		: mFilepath(filepath)
	{
	}

	string FileSystem::FileInfo::getFilePath() const
	{
		return standardisePath(mFilepath.string());
	}

	string FileSystem::FileInfo::getPath() const
	{
		return standardisePath(mFilepath.relative_path().string());
	}

	string FileSystem::FileInfo::getFileName() const
	{
		return mFilepath.filename().string();
	}

	string FileSystem::FileInfo::getFileNameWithoutExtension() const
	{
		return mFilepath.stem().string();
	}

	string FileSystem::FileInfo::getExtension() const
	{
		return mFilepath.extension().string();
	}

	bool FileSystem::FileInfo::isPathRelative() const
	{
		return mFilepath.is_relative();
	}
	
	//
	// DirectoryInfo
	//
	FileSystem::DirectoryInfo::DirectoryInfo(string const& path)
		: mPath(path)
	{
	}

	string FileSystem::DirectoryInfo::getDirectoryPath() const
	{
		return standardisePath(mPath.string());
	}

	FileSystem::FileInfo FileSystem::DirectoryInfo::createFile(string const& filename)
	{
		filesystem::path filepath = mPath;
		filepath += filename;

		return FileSystem::createFile(filepath);
	}

	FileSystem::DirectoryInfo FileSystem::DirectoryInfo::createSubDirectory(string const& subdir)
	{
		filesystem::path dir = mPath;
		dir += subdir;

		return FileSystem::createDirectory(dir);
	}

	bool FileSystem::DirectoryInfo::isPathRelative() const
	{
		return mPath.is_relative();
	}

	//
	// FileSystem
	//
	void FileSystem::standardisePath(string& path)
	{
		replace(path.begin(), path.end(), '\\', '/');

		// Replace duplicate '/'s
		struct both_slashes
		{
			bool operator()(char a, char b) const
			{
				return a == '/' && b == '/';
			}
		};

		path.erase(unique(path.begin(), path.end(), both_slashes()), path.end());
	}

	string FileSystem::standardisePath(string const& path)
	{
		string ret = path;

		standardisePath(ret);
		return ret;
	}

	string FileSystem::concatPaths(string const& path1, string const& path2)
	{
		if (path1 != "")
		{
			return standardisePath(path1) + "/" + standardisePath(path2);
		}
		else
		{
			return standardisePath(path2);
		}
	}

	string FileSystem::baseDirectory(string const& path)
	{
		auto standardised = standardisePath(path);
		auto slashPos = standardised.find_last_of('/');

		if (slashPos == string::npos)
		{
			return "";
		}
		else
		{
			return standardised.substr(0, slashPos);
		}
	}

	string FileSystem::baseName(string const& path)
	{
		auto standardised = standardisePath(path);
		auto slashPos = standardised.find_last_of('/');

		if (slashPos == string::npos)
		{
			return standardised;
		}
		else
		{
			return standardised.substr(slashPos + 1);
		}
	}

	bool FileSystem::matchesFilePattern(char const* input, char const* pattern)
	{
		int star;
		new_segment:

			star = 0;
			if (*pattern == '*') 
			{
				star = 1;
				do 
				{ 
					pattern++; 
				} while (*pattern == '*');
			}

		test_match:

			int i;
			for (i = 0; pattern[i] && (pattern[i] != '*'); i++) 
			{
				if (toupper(input[i]) != toupper(pattern[i])) 
				{
					if (!input[i]) return 0;
					if ((pattern[i] == '?') && (input[i] != '.')) continue;
					if (!star) return 0;
					input++;
					goto test_match;
				}
			}
			if (pattern[i] == '*') 
			{
				input += i;
				pattern += i;
				goto new_segment;
			}
			if (!input[i]) return 1;
			if (i && pattern[i - 1] == '*') return 1;
			if (!star) return 0;
			input++;
			goto test_match;
	}

	FileSystem::FileInfo FileSystem::createFile(string const& filepath)
	{
		ofstream fp;
			
		fp.open(filepath.operator std::basic_string_view<char, std::char_traits<char>>(), ios_base::trunc);

		if (!fp.is_open())
		{
			throw FileException("Could not create file '" + filepath + "'.");
		}
			
		fp.close();
		return FileInfo(standardisePath(filepath));
	}

	FileSystem::FileInfo FileSystem::createFile(filesystem::path const& filepath)
	{
		return createFile(filepath.string());
	}

	FileSystem::DirectoryInfo FileSystem::createDirectory(string const& dirpath)
	{
		if (!directoryExists(dirpath))
		{
			if (!filesystem::create_directory(filesystem::path(dirpath)))
			{
				throw FileException("Could not create directory '" + dirpath + "'.");
			}
		}

		return DirectoryInfo(dirpath);
	}

	FileSystem::DirectoryInfo FileSystem::createDirectory(filesystem::path const& dirpath)
	{
		return createDirectory(dirpath.string());
	}

	void FileSystem::deleteFile(string const& filepath)
	{
		if (fileExists(filepath))
		{
			if (!filesystem::remove(filesystem::path(filepath)))
			{
				throw FileException("Could not delete '" + filepath + "'.");
			}
		}
	}

	void FileSystem::deleteFile(FileInfo const& fi)
	{
		deleteFile(fi.getFilePath());
	}

	void FileSystem::deleteDirectory(string const& dirpath)
	{
		if (!filesystem::remove_all(filesystem::path(dirpath)))
		{
			throw FileException("Could not delete '" + dirpath + "'.");
		}
	}

	void FileSystem::deleteDirectory(DirectoryInfo const& di)
	{
		deleteDirectory(di.getDirectoryPath());
	}

	FileSystem::FileInfo FileSystem::getFile(string const& filepath)
	{
		filesystem::path p(filepath);

		if (!filesystem::exists(p))
		{
			throw FileException("File '" + filepath + "' not found.");
		}
		else
		{
			if (!filesystem::is_regular_file(p))
			{
				throw FileException("'" + filepath + "' is not a file.");
			}

			return FileInfo(filepath);
		}
	}

	FileSystem::DirectoryInfo FileSystem::getDirectory(string const& dirpath)
	{
		filesystem::path p(dirpath);

		if (!filesystem::exists(p))
		{
			throw FileException("Directory '" + dirpath + "' not found.");
		}
		else
		{
			if (!filesystem::is_directory(p))
			{
				throw FileException("'" + dirpath + "' is not a directory.");
			}

			return DirectoryInfo(dirpath);
		}
	}

	FileSystem::DirectoryInfo FileSystem::getCurrentDirectory()
	{
		return DirectoryInfo(filesystem::current_path().string());
	}

	vector<FileSystem::FileInfo> FileSystem::getFilesInDirectory(string const& dir, string const& pattern, bool subdirs)
	{
		vector<FileInfo> files;

		filesystem::path dirPath(dir);
		if (!filesystem::is_directory(dirPath))
		{
			throw FileException("Path '" + dir + "' is not a directory.");
		}

		// Split patterns
		auto patterns = StringUtils::split(pattern, "|");

		if (subdirs)
		{
			filesystem::recursive_directory_iterator it(dirPath), end;
			while (it != end)
			{
				auto entryPath = it->path();

				if (filesystem::is_regular_file(entryPath))
				{
					// Ignore anything that doesn't match pattern
					string filename = entryPath.filename().string();

					for (auto const& p: patterns)
					{
						if (matchesFilePattern(filename.c_str(), p.c_str()))
						{
							files.push_back(FileInfo(standardisePath(entryPath.string())));
						}
						break;
					}
				}

				++it;
			}
		}
		else
		{
			filesystem::directory_iterator it(dirPath), end;
			while (it != end)
			{
				auto entryPath = it->path();

				if (filesystem::is_regular_file(entryPath))
				{
					// Ignore anything that doesn't match pattern
					string filename = entryPath.filename().string();
					if (matchesFilePattern(filename.c_str(), pattern.c_str()))
					{
						files.push_back(FileInfo(standardisePath(entryPath.string())));
					}
				}

				++it;
			}
		}

		return files;
	}

	vector<FileSystem::FileInfo> FileSystem::getFilesInDirectory(DirectoryInfo const& di, string const& pattern, bool subdirs)
	{
		return getFilesInDirectory(di.getDirectoryPath(), pattern, subdirs);
	}

	bool FileSystem::fileExists(string const& filepath)
	{
		return filesystem::exists(filesystem::path(filepath));
	}

	bool FileSystem::fileExists(FileInfo const& fi)
	{
		return fileExists(fi.getFilePath());
	}

	bool FileSystem::directoryExists(string const& dirpath)
	{
		return filesystem::exists(filesystem::path(dirpath));
	}

	bool FileSystem::directoryExists(DirectoryInfo const& di)
	{
		return directoryExists(di.getDirectoryPath());
	}

	string FileSystem::readTextFile(std::string const& filepath)
	{
		ifstream fstr(filepath);

		if (!fstr.is_open())
		{
			throw FileException("Could not open file '" + filepath + "'.");
		}

		string str;

		fstr.seekg(0, ios::end);
		str.reserve((size_t)fstr.tellg());
		fstr.seekg(0, ios::beg);

		str.assign((istreambuf_iterator<char>(fstr)), istreambuf_iterator<char>());
		return str;
	}

} // WP_NAMESPACE
