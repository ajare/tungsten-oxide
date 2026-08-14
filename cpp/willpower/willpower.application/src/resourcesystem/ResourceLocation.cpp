#include <algorithm>
#include <memory>
#include <thread>

#include "utils/YamlReader.h"

#include "willpower/common/DataNode.h"
#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/ResourceExceptions.h"
#include "willpower/application/resourcesystem/ResourceLocation.h"
#include "willpower/application/resourcesystem/DataStream.h"
#include "willpower/application/resourcesystem/Resource.h"

namespace {
StructuredData importStructuredData(utils::StructuredData const& source) {
  if (source.isValue()) {
    return StructuredData(source.getName(), source.getValue());
  }

  StructuredData result(source.getName());
  for (auto const& entry : source) {
    result.addEntry(entry.first, importStructuredData(entry.second));
  }
  return result;
}
}  // namespace

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
using namespace std;

ResourceLocation::ResourceLocation(Logger* logger, string const& name, string const& type, string const& definitionFile)
    : mwLogger(logger), mName(name), mType(type), mDefinitionFile(definitionFile), mScanDirty(true) {
}

string const& ResourceLocation::getName() const {
  return mName;
}

string const& ResourceLocation::getType() const {
  return mType;
}

string const& ResourceLocation::getDefinitionFile() const {
  return mDefinitionFile;
}

void ResourceLocation::scan() {
  if (!mScanDirty) {
    return;
  }

  // Clear all records
  mNamespaces.clear();

  // Read and parse definition file
  uint32_t fileSize;
  uint8_t* fileData = readData(mDefinitionFile, &fileSize);
  DataStreamPtr dataPtr(new DataStream(fileData, fileSize));

  string const document((char*)dataPtr->getData(), dataPtr->getSize());
  if (!mDefinitionFile.ends_with(".yaml") && !mDefinitionFile.ends_with(".yml")) {
    throw ResourceSystemException("Resource definition file '" + mDefinitionFile + "' must use YAML.");
  }
  unique_ptr<utils::YamlReader> reader(utils::YamlReader::fromString(document));
  StructuredData rootData = importStructuredData(reader->readTree());
  DataNode root(rootData);

  // Iterate over namespaces
  auto rootNode = &root;
  auto namespaceNode = rootNode->getOptionalChild("Namespace");
  if (namespaceNode) {
    do {
      string namesp = namespaceNode->getProperty("name");
      scanResourceElement(namespaceNode, namesp);
    } while (namespaceNode->next());
  }

  // Iterate over non-namespaced resources
  scanResourceElement(rootNode);
  mScanDirty = false;
}

void ResourceLocation::rescan() {
  mScanDirty = true;
  scan();
}

map<string, ResourceLocation::NamespaceRecord> const& ResourceLocation::getNamespaceRecords() const {
  return mNamespaces;
}

ResourceRecordBaseData ResourceLocation::parseResource(DataNode* element, string const& namesp, string const& file) {
  WP_UNUSED(file);

  ResourceRecordBaseData baseData;

  baseData.locationFound = element->getOptionalProperty("location", baseData.location);
  baseData.typeFound = element->getOptionalProperty("type", baseData.type);
  baseData.nameFound = element->getOptionalProperty("name", baseData.name);

  // Check for 'Option' subelements.
  auto optionElem = element->getOptionalChild("Option");
  if (optionElem) {
    do {
      string optionName = optionElem->getProperty("name");
      string optionValue = optionElem->getValue();

      if (baseData.tags.find(optionName) != baseData.tags.end()) {
        string errMsg = "Resource '" + baseData.name + "' in namespace '" + namesp + "' has a duplicate option '" + optionName + "' which will be ignored.";
        mwLogger->error(errMsg);
      } else {
        baseData.tags[optionName] = optionValue;
      }
    } while (optionElem->next());
  }

  return baseData;
}

void ResourceLocation::scanResourceElement(DataNode* parent, string namesp) {
  // Get namespace record
  auto it = mNamespaces.find(namesp);
  if (it == mNamespaces.end()) {
    it = mNamespaces.insert(make_pair(namesp, NamespaceRecord())).first;
  }

  auto& namespaceRecord = it->second;
  namespaceRecord.name = namesp;

  auto resourceElem = parent->getOptionalChild("Resource");
  if (resourceElem) {
    do {
      // If there are any dependent, then this is a composite resource. If this
      // is the case, then we only care about the 'name' attribute of this resource.
      bool isComposite = false;
      auto depResourcesElem = resourceElem->getOptionalChild("DependentResources");
      if (depResourcesElem && depResourcesElem->getOptionalChild("DependentResource")) {
        isComposite = true;
      }

      ResourceRecord r(namesp, this, isComposite);

      // Get base data
      ResourceRecordBaseData baseData = parseResource(resourceElem, namesp, mDefinitionFile);

      // Get definition(s)
      auto definitionsElem = resourceElem->getOptionalChild("Definitions");
      if (definitionsElem) {
        auto definitionElem = definitionsElem->getOptionalChild("Definition");
        if (definitionElem) {
          do {
            string factory = "";
            definitionElem->getOptionalProperty("factory", factory);

            r.definitions.push_back(make_pair(factory, definitionElem->getData()));
          } while (definitionElem->next());
        }
      }

      // If there isn't a default definition, add one.
      bool foundDefaultDef = false;
      for (auto const& def : r.definitions) {
        if (def.first == "") {
          foundDefaultDef = true;
          break;
        }
      }

      if (!foundDefaultDef) {
        r.definitions.push_back(make_pair("", StructuredData("Definition")));
      }

      // Validate
      if (!isComposite) {
        validateResourceRecordBaseData(baseData);
      } else {
        if (!baseData.nameFound) {
          throw Exception("Composite resource with no name found in '" + mName + "'.");
        }

        // The sub-resources may not be declared yet, but add the record now and
        // then validate later.
        depResourcesElem = resourceElem->getOptionalChild("DependentResources");
        if (depResourcesElem) {
          auto depResourceElem = depResourcesElem->getOptionalChild("DependentResource");
          if (depResourceElem) {
            do {
              DependentResourceRecord drr;

              drr.owner = baseData.name;

              string id;
              if (depResourceElem->getOptionalProperty("id", id)) {
                if (id == "") {
                  throw Exception("Dependent resource specified with an empty 'id' attribute in '" + mName + "'.");
                }

                drr.id = id;
              }

              if (!depResourceElem->getOptionalProperty("ref", drr.ref)) {
                // Resource is declared 'inline', so parse and create, before adding.
                ResourceRecord rr(namesp, this, false);

                rr.baseData = parseResource(depResourceElem, namesp, mDefinitionFile);
                validateResourceRecordBaseData(rr.baseData);

                namespaceRecord.resourceRecords[rr.baseData.name] = rr;

                drr.ref = rr.baseData.name;
              }

              r.dependentResources.push_back(drr);
            } while (depResourceElem->next());
          }
        }
      }

      r.baseData = baseData;
      namespaceRecord.resourceRecords[r.baseData.name] = r;
    } while (resourceElem->next());
  }
}

ResourceRecord const& ResourceLocation::getResourceRecord(string const& resource, string namesp) const {
  auto namespIt = mNamespaces.find(namesp);

  if (namespIt == mNamespaces.end()) {
    throw ResourceSystemException("Namespace '" + namesp + "' has not been defined.");
  }

  auto it = namespIt->second.resourceRecords.find(resource);

  if (it == namespIt->second.resourceRecords.end()) {
    throw ResourceSystemException("Resource '" + resource + "' was not found in location '" + mName + "'.");
  }

  return it->second;
}

void ResourceLocation::validateResourceDefinitions() {
  vector<ResourceRecord> missingFiles;
  vector<DependentResourceRecord> missingReferences;
  vector<string> miscErrors;

  bool errorsFound = false;
  for (auto namespEntry : mNamespaces) {
    auto const& namesp = namespEntry.second;

    for (auto recordEntry : namesp.resourceRecords) {
      auto& record = recordEntry.second;

      // Resources cannot have '/' in their name.
      if (record.baseData.name.find("/") != string::npos) {
        errorsFound = true;
        string errMsg;

        if (record.namesp == "") {
          errMsg = "Resource '" + record.baseData.name + "'  in default namespace has an invalid name.  " +
                   "Resources cannot include '/' in their name.";
        } else {
          errMsg = "Resource '" + record.baseData.name + "' in namespace '" + record.namesp + "' has an invalid name.  " +
                   "Resources cannot include '/' in their name.";
        }

        miscErrors.push_back(errMsg);
      }

      // Check resource exists
      if (record.isComposite) {
        for (auto const& depResource : record.dependentResources) {
          // Get namespace for sub resource
          string depNamesp, depResName;
          Resource::splitName(depResource.ref, record.namesp, &depNamesp, &depResName);

          auto depNamespRecord = mNamespaces.find(depNamesp)->second;

          // Get location for the referenced resource
          auto it = find_if(
              depNamespRecord.resourceRecords.begin(),
              depNamespRecord.resourceRecords.end(),
              [depResName](ResourceRecordEntry const& rre) {
                return depResName == rre.first;
              });

          if (it == depNamespRecord.resourceRecords.end()) {
            missingReferences.push_back(depResource);
            errorsFound = true;
          }
        }
      } else {
        // Check that the 'hard' resource (ie the file the resource uses) exists.
        if (record.baseData.locationFound && !hardResourceExists(record.baseData.location)) {
          missingFiles.push_back(record);
          errorsFound = true;
        }
      }

      // Check definitions
      int defaultFactoryPos{-1};
      for (size_t i = 0; i < record.definitions.size(); ++i) {
        auto const& resDef = record.definitions[i];
        auto const& [factory, definition] = resDef;
        if (factory == "") {
          if (defaultFactoryPos >= 0) {
            string err = "Resource '" + record.baseData.name +
                         "' in namespace '" + record.namesp +
                         "' has multiple default definitions.";

            miscErrors.push_back(err);
            errorsFound = true;
          }

          defaultFactoryPos = (int)i;
        }
      }

      if (defaultFactoryPos != (int)(record.definitions.size() - 1)) {
        // Move the default factory to the end
        auto defaultFactory = record.definitions[defaultFactoryPos];
        for (size_t i = defaultFactoryPos; i < record.definitions.size() - 1; ++i) {
          record.definitions[i] = record.definitions[i + 1];
        }

        record.definitions[record.definitions.size() - 1] = defaultFactory;
      }
    }

    // Print issues to log
    for (auto const& record : missingFiles) {
      string errMsg;

      if (record.namesp == "") {
        errMsg = "Resource '" + record.baseData.name + "'  in default namespace was declared in location '" +
                 mName + "' but file '" + record.baseData.location + "' was not found.";
      } else {
        errMsg = "Resource '" + record.baseData.name + "' in namespace '" + record.namesp + "' was declared in location '" +
                 mName + "' but file '" + record.baseData.location + "' was not found.";
      }

      mwLogger->error(errMsg);
    }

    for (auto const& subRecord : missingReferences) {
      string errMsg;

      if (namespEntry.first == "") {
        errMsg = "Resource '" + subRecord.ref + "' was referenced by composite resource '" +
                 subRecord.owner + "' but has not been declared in '" + mName + "'.";
      } else {
        errMsg = "Resource '" + subRecord.ref + "' was referenced by composite resource '" +
                 subRecord.owner + "' but has not been declared in '" + mName + " namespace '" + namespEntry.first + "'.";
      }

      mwLogger->error(errMsg);
    }

    for (auto const& error : miscErrors) {
      mwLogger->error(error);
    }
  }

  if (errorsFound) {
    throw ResourceSystemException("Errors were encountered while validating resources in location '" + mName + "'.");
  }
}

void ResourceLocation::validateResourceRecordBaseData(ResourceRecordBaseData& baseData) {
  if (!baseData.typeFound) {
    throw ResourceSystemException("Non-composite resource '" + baseData.name + "' with no type found in '" + mName + "'.");
  }

  if (!baseData.nameFound) {
    baseData.name = baseData.location;
  }
}

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE