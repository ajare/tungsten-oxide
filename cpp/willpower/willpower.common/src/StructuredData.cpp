#include <algorithm>

#include "willpower/common/StructuredData.h"


using namespace std;

StructuredData::StructuredData(string const& name)
	: mName(name)
	, mIsValue(false)
{
}

StructuredData::StructuredData(string const& name, string const& value)
	: mName(name)
	, mIsValue(true)
	, mValue(value)
{
}

string const& StructuredData::getName() const
{
	return mName;
}

bool StructuredData::isValue() const
{
	return mIsValue;
}

void StructuredData::setValue(string const& value)
{
	mIsValue = true;
	mValue = value;
}

string const& StructuredData::getValue() const
{
	return mValue;
}

void StructuredData::addEntry(string const& key, string const& value)
{
	auto entry = make_pair(key, StructuredData(key, value));
	mEntries.push_back(entry);
}

void StructuredData::addEntry(string const& key, StructuredData const& value)
{
	auto entry = make_pair(key, value);
	mEntries.push_back(entry);
}

StructuredData const& StructuredData::getEntry(string const& key) const
{
	auto it = find_if(mEntries.begin(), mEntries.end(), [key](auto const& entry) { return entry.first == key; });

	if (it == mEntries.end())
	{
		string errMsg = "Could not find entry '" + key + "'.";
		throw exception(errMsg.c_str());
	}

	return (*it).second;
}

bool StructuredData::hasEntry(string const& key) const
{
	auto it = find_if(mEntries.begin(), mEntries.end(), [key](auto const& entry) { return entry.first == key; });

	return it != mEntries.end();
}

vector<StructuredData::Entry>::const_iterator StructuredData::begin() const
{
	return mEntries.begin();
}

vector<StructuredData::Entry>::const_iterator StructuredData::end() const
{
	return mEntries.end();
}
