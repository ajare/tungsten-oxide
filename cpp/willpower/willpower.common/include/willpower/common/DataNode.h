#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "Platform.h"
#include "StructuredData.h"

namespace WP_NAMESPACE {
// A lightweight view over StructuredData used by YAML-backed configuration
// and resource-definition readers. Repeated keys are exposed through next(),
// preserving the traversal style used by the definition factories.
class WP_COMMON_API DataNode {
  std::vector<StructuredData const*> mMatches;
  std::size_t mIndex{0};
  mutable std::vector<std::unique_ptr<DataNode>> mChildren;

  StructuredData const& current() const;

public:
  explicit DataNode(StructuredData const& node);
  explicit DataNode(std::vector<StructuredData const*> matches);
  DataNode(DataNode const&) = delete;
  DataNode& operator=(DataNode const&) = delete;
  DataNode(DataNode&&) noexcept = default;
  DataNode& operator=(DataNode&&) noexcept = default;

  bool next();
  std::string getValue() const;
  DataNode* getChild(std::string const& child) const;
  DataNode* getOptionalChild(std::string const& child) const;
  std::string getProperty(std::string const& property) const;
  bool getOptionalProperty(std::string const& property, std::string& value) const;
  StructuredData const& getData() const;
};
}  // namespace WP_NAMESPACE
