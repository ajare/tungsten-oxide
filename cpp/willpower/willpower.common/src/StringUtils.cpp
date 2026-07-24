#include <algorithm>
#include <sstream>
#include <regex>
#include <cctype>
#include <cstdarg>
#include <iostream>
#include <vector>
#include <string>
#include <cstdarg>
#include <cstring>
#include <fstream>

#include "willpower/common/StringUtils.h"

namespace utils
{

	using namespace std;

	void StringUtils::trim(string& str, bool left, bool right, char const* delims)
	{
		if (right)
		{
			str.erase(str.find_last_not_of(delims) + 1); // trim right
		}
		if (left)
		{
			str.erase(0, str.find_first_not_of(delims)); // trim left
		}
	}

	string StringUtils::trim(string const& str, bool left, bool right, char const* delims)
	{
		string ret = str;
			
		trim(ret, left, right, delims);
		return ret;
	}

	vector<string> StringUtils::split(string const& str, string const& delims, unsigned int maxSplits)
	{
		vector<string> ret;
		ret.reserve(maxSplits ? maxSplits + 1 : 10); // 10 is guessed capacity

		unsigned int numSplits = 0;
		size_t start = 0, pos;

		do
		{
			pos = str.find_first_of(delims, start);
			if (pos == start)
			{
				// Do nothing
				start = pos + 1;
			}
			else if (pos == string::npos || (maxSplits && numSplits == maxSplits))
			{
				// Copy the rest of the string
				ret.push_back(str.substr(start));
				break;
			}
			else
			{
				// Copy up to delimiter
				ret.push_back(str.substr(start, pos - start));
				start = pos + 1;
			}

			// parse up to next real data
			start = str.find_first_not_of(delims, start);
			++numSplits;

		} while (pos != string::npos);

		return ret;
	}

	void StringUtils::toLower(string& str)
	{
		transform(str.begin(), str.end(), str.begin(), [](char c) {return static_cast<char>(std::tolower(c)); });
	}

	string StringUtils::toLower(string const& str)
	{
		string ret = str;

		toLower(ret);
		return ret;
	}

	void StringUtils::toUpper(string& str)
	{
		transform(str.begin(), str.end(), str.begin(), [](char c) {return static_cast<char>(std::toupper(c)); });
	}

	string StringUtils::toUpper(string const& str)
	{
		string ret = str;

		toUpper(ret);
		return ret;
	}

	bool StringUtils::startsWith(string const& str, string const& pattern, bool caseSensitive)
	{
		size_t thisLen = str.length();
		size_t patternLen = pattern.length();
			
		if (thisLen < patternLen || patternLen == 0)
		{
			return false;
		}

		const string startOfThis = str.substr(0, patternLen);
		return caseSensitive ? startOfThis == pattern : toLower(startOfThis) == toLower(pattern);
	}

	bool StringUtils::endsWith(string const& str, string const& pattern, bool caseSensitive)
	{
		size_t thisLen = str.length();
		size_t patternLen = pattern.length();

		if (thisLen < patternLen || patternLen == 0)
		{
			return false;
		}

		const string endOfThis = str.substr(thisLen - patternLen, patternLen);
		return caseSensitive ? endOfThis == pattern : toLower(endOfThis) == toLower(pattern);
	}

	bool StringUtils::contains(string const& str, string const& pattern, bool caseSensitive)
	{
		string fixedPattern = pattern;

		regex re = caseSensitive 
			? regex(fixedPattern)
			: regex(fixedPattern, regex_constants::icase);

		return regex_search(str, re);
	}

	bool StringUtils::isNumber(string const& str)
	{
		istringstream istr(str);
			
		float test;
		istr >> test;

		return !istr.fail() && istr.eof();
	}

	void StringUtils::replaceAll(string& str, string const& toFind, string const& replacement)
	{
		// Get the first occurrence
		size_t pos = str.find(toFind);

		// Repeat till end is reached
		while (pos != std::string::npos)
		{
			str.replace(pos, toFind.size(), replacement);
			int offset = (int)replacement.size() - (int)toFind.size();
			pos = str.find(toFind, pos + offset);
		}
	}

	float StringUtils::parseFloat(string const& value)
	{
		istringstream str(value);

		float ret = 0;
		str >> ret;

		return ret;
	}

	int StringUtils::parseInt(string const& value)
	{
		istringstream str(value);

		int ret = 0;
		str >> ret;

		return ret;
	}

	unsigned int StringUtils::parseUInt(string const& value)
	{
		istringstream str(value);

		unsigned int ret = 0;
		str >> ret;

		return ret;
	}

	bool StringUtils::parseBool(string const& value)
	{
		string tl = toLower(value);
		return tl == "true" || tl == "yes" || value == "1";
	}

} // utils
