#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>

#include "willpower/common/DataNode.h"
#include "willpower/common/Logger.h"

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/ResourceRecord.h"
#include "willpower/application/resourcesystem/DataStream.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
class WP_APPLICATION_API ResourceLocation {
protected:
  typedef std::function<void(DataStreamPtr)> DataStreamFetchedCallback;

  typedef std::function<void(float)> DataStreamFetchProgressCallback;

public:
  struct NamespaceRecord {
    std::string name;
    std::map<std::string, ResourceRecord> resourceRecords;
  };

protected:
  typedef std::pair<std::string, ResourceRecord> ResourceRecordEntry;

private:
  Logger* mwLogger;

  std::string mName;

  std::string mType;

  std::map<std::string, NamespaceRecord> mNamespaces;

  bool mScanDirty;

protected:
  std::string mDefinitionFile;

private:
  ResourceRecordBaseData parseResource(wp::DataNode* element, std::string const& namesp, std::string const& file);

  void scanResourceElement(wp::DataNode* parent, std::string namesp = "");

  ResourceRecord const& getResourceRecord(std::string const& resource, std::string namesp = "") const;

  /*
  DataStreamPtr getResourceDataStreamProgressive(DataStreamFetchProgressCallback progress, std::string const& resource, std::string namesp = "") const;

  virtual DataStreamPtr getHardResourceDataStream(std::string const& file, std::string const& namesp) const = 0;

  virtual DataStreamPtr getHardResourceDataStreamProgressive(std::string const& file, std::string const& namesp, DataStreamFetchProgressCallback progress) const = 0;
  */

  virtual bool hardResourceExists(std::string const& file) const = 0;

  void validateResourceRecordBaseData(ResourceRecordBaseData& baseData);

public:
  ResourceLocation(Logger* logger, std::string const& name, std::string const& type, std::string const& definitionFile);

  virtual ~ResourceLocation() = default;

  std::string const& getName() const;

  std::string const& getType() const;

  virtual std::string const& getDefinitionFile() const;

  void scan();

  void rescan();

  void validateResourceDefinitions();

  virtual uint8_t* readData(std::string const& source, uint32_t* dataSize) = 0;

  std::map<std::string, NamespaceRecord> const& getNamespaceRecords() const;

  /*
  DataStreamPtr getResourceDataStream(std::string const& resource, std::string namesp = "") const;

  void getResourceDataStreamAsync(DataStreamFetchedCallback fetched, DataStreamFetchProgressCallback progress, std::string const& resource, std::string namesp = "");
  */
};

typedef std::function<ResourceLocation*(std::string const&, std::string const&)> ResourceLocationFactory;

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE
