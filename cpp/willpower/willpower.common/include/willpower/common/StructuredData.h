#pragma once

#include <string>
#include <vector>

#include "Platform.h"

class WP_COMMON_API StructuredData
{
public:

	typedef std::pair<std::string, StructuredData> Entry;

private:

	std::string mName;

	bool mIsValue;

	std::string mValue;

	std::vector<Entry> mEntries;

public:

	explicit StructuredData(std::string const& name);

	StructuredData(std::string const& name, std::string const& value);

	std::string const& getName() const;

	bool isValue() const;

	void setValue(std::string const& value);

	std::string const& getValue() const;

	void addEntry(std::string const& key, std::string const& value);

	void addEntry(std::string const& key, StructuredData const& value);

	StructuredData const& getEntry(std::string const& key) const;

	bool hasEntry(std::string const& key) const;

	std::vector<Entry>::const_iterator begin() const;

	std::vector<Entry>::const_iterator end() const;
};
