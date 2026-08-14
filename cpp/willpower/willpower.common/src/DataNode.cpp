#include "willpower/common/DataNode.h"

#include "willpower/common/Exceptions.h"

namespace WP_NAMESPACE {
DataNode::DataNode(StructuredData const& node)
    : mMatches{&node} {
}

DataNode::DataNode(std::vector<StructuredData const*> matches)
    : mMatches(std::move(matches)) {
}

StructuredData const& DataNode::current() const {
  if (mIndex >= mMatches.size()) {
    throw Exception("Attempted to access an invalid structured-data node.");
  }
  return *mMatches[mIndex];
}

bool DataNode::next() {
  if (mIndex + 1 >= mMatches.size()) {
    return false;
  }
  ++mIndex;
  mChildren.clear();
  return true;
}

std::string DataNode::getValue() const {
  if (current().isValue()) return current().getValue();
  if (current().hasEntry("value")) return current().getEntry("value").getValue();
  return {};
}

DataNode* DataNode::getChild(std::string const& child) const {
  auto node = getOptionalChild(child);
  if (!node) {
    throw Exception("Required property '" + child + "' was not found in '" + current().getName() + "'.");
  }
  return node;
}

DataNode* DataNode::getOptionalChild(std::string const& child) const {
  std::vector<StructuredData const*> matches;
  for (auto const& entry : current()) {
    if (entry.first == child) {
      matches.push_back(&entry.second);
    }
  }
  if (matches.empty()) {
    return nullptr;
  }
  mChildren.push_back(std::make_unique<DataNode>(std::move(matches)));
  return mChildren.back().get();
}

std::string DataNode::getProperty(std::string const& property) const {
  return getChild(property)->getValue();
}

bool DataNode::getOptionalProperty(std::string const& property, std::string& value) const {
  auto node = getOptionalChild(property);
  if (!node) {
    return false;
  }
  value = node->getValue();
  return true;
}

StructuredData const& DataNode::getData() const {
  return current();
}
}  // namespace WP_NAMESPACE
