#pragma once

#include <string>
#include <vector>

#include "Platform.h"

#include "StringUtils.h"
#include "StructuredData.h"

namespace WP_NAMESPACE
{

	class WP_COMMON_API XmlNode
	{
		friend class XmlReader;

	private:

		void* mNode;

		std::string mXpath;

		std::string mName;

		std::string mFilepath;

		mutable std::vector<XmlNode*> mChildren;

	private:

		XmlNode(void* node, std::string const& xpath, std::string const& name, std::string const& filepath);

	public:

		~XmlNode();

		bool next();

		std::string getValue() const;

		std::string const& getPath() const;

		XmlNode* getChild(std::string const& child) const;

		XmlNode* getOptionalChild(std::string const& child) const;

		std::string getAttribute(std::string const& attrib) const;

		bool getOptionalAttribute(std::string const& attrib, std::string& value) const;

		std::string getAsText() const;
	};


	class WP_COMMON_API XmlReader
	{
		void* mDocument;

		std::string mFilepath;

		void* mRootNode;

		std::string mRootNodeName;

		std::vector<XmlNode*> mNodes;

	private:

		XmlReader(void* doc, std::string const& filepath);

		static std::string getErrorMessage(int errorCode, bool loadNotParse, std::string const& filepath);

		StructuredData _readTree(void* node);

	public:

		~XmlReader();

		static XmlReader* fromFile(std::string const& filepath);

		static XmlReader* fromString(std::string const& string);

		XmlNode* getNode(std::string const& path);

		XmlNode* getOptionalNode(std::string const& path);

		StructuredData readTree();
	};

}  // namespace WP_NAMESPACE