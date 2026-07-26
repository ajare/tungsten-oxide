#pragma once

#include <string>
#include <vector>
#include <exception>
#include <filesystem>

#include "Platform.h"


namespace WP_NAMESPACE 
{

	class FileSystem
	{
	public:

		//
		// Generic exception class
		//
		class FileException : public std::exception
		{
		public:

			explicit FileException(std::string const& message)
				: exception(message.c_str())
			{
			}
		};

		//
		// Class for holding info about a file
		//
		class FileInfo
		{
			std::filesystem::path mFilepath;

		public:

			explicit FileInfo(std::string const& filepath);

			std::string getFilePath() const;

			std::string getPath() const;

			std::string getFileName() const;

			std::string getFileNameWithoutExtension() const;

			std::string getExtension() const;

			bool isPathRelative() const;
		};

		//
		// Class for holding info about a directory
		//
		class DirectoryInfo
		{
			std::filesystem::path mPath;

		public:

			explicit DirectoryInfo(std::string const& path);

			std::string getDirectoryPath() const;

			FileInfo createFile(std::string const& filename);

			DirectoryInfo createSubDirectory(std::string const& subdir);

			bool isPathRelative() const;
		};

	public:

		static bool matchesFilePattern(char const* input, char const* pattern);

		static void standardisePath(std::string& path);

		static std::string standardisePath(std::string const& path);

		static std::string concatPaths(std::string const& path1, std::string const& path2);

		static std::string baseDirectory(std::string const& path);

		static std::string baseName(std::string const& path);

		static FileInfo createFile(std::string const& filepath);

		static FileInfo createFile(std::filesystem::path const& filepath);

		static DirectoryInfo createDirectory(std::string const& dirpath);

		static DirectoryInfo createDirectory(std::filesystem::path const& dirpath);

		static void deleteFile(std::string const& filepath);

		static void deleteFile(FileInfo const& fi);

		static void deleteDirectory(std::string const& dirpath);

		static void deleteDirectory(DirectoryInfo const& di);

		static FileInfo getFile(std::string const& filepath);

		static DirectoryInfo getDirectory(std::string const& dirpath);

		static DirectoryInfo getCurrentDirectory();

		static std::vector<FileInfo> getFilesInDirectory(std::string const& dir, std::string const& pattern, bool subdirs);

		static std::vector<FileInfo> getFilesInDirectory(DirectoryInfo const& di, std::string const& pattern, bool subdirs);

		static bool fileExists(std::string const& filepath);

		static bool fileExists(FileInfo const& fi);

		static bool directoryExists(std::string const& dirpath);

		static bool directoryExists(DirectoryInfo const& di);

		static std::string readTextFile(std::string const& filepath);
	};

} // utils
