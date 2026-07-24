#pragma once

#include <string>
#include <vector>

#include "Platform.h"

namespace utils
{

	class StringUtils
	{
	public:

		// Trim whitespace
		static void trim(std::string& str, bool left = true, bool right = true, char const* delims = " \t\r");

		static std::string trim(std::string const& str, bool left = true, bool right = true, char const* delims = " \t\r");

		// Split into tokens on delimiter
		static std::vector<std::string> split(std::string const& str, std::string const& delims = "\t\n ", unsigned int maxSplits = 0);

		// Convert to lowercase
		static void toLower(std::string& str);

		static std::string toLower(std::string const& str);

		// Convert to uppercase
		static void toUpper(std::string& str);

		static std::string toUpper(std::string const& str);

		// Does the string start with something?
		static bool startsWith(std::string const& str, std::string const& pattern, bool caseSensitive = true);

		// Does the string end with something?
		static bool endsWith(std::string const& str, std::string const& pattern, bool caseSensitive = true);

		// Does the string contain a substring?
		static bool contains(std::string const& str, std::string const& pattern, bool caseSensitive = true);

		// Is the string a number?
		static bool isNumber(std::string const& str);

		// Replace
		static void replaceAll(std::string& str, std::string const& toFind, std::string const& replacement);

		// Join
		template<typename T>
		static std::string join(T begin, T end, std::string const& j)
		{
			std::string result;

			T it = begin;
			while (it != end)
			{
				result += *it;

				T next = it; ++next;

				if (next != end)
				{
					result += j;
				}

				it = next;
			}

			return result;
		}

		//
		// Conversions
		//
		static float parseFloat(std::string const& value);

		static int parseInt(std::string const& value);

		static unsigned int parseUInt(std::string const& value);

		static bool parseBool(std::string const& value);

	};

} // WP_NAMESPACE


