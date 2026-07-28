#include <exception>

#include <mpp/ModelSerializer.h>
#include <mpp/ProgrammaticModelStream.h>

#include <utils/FileSystem.h>

#include <applib/TrackMaterial.h>

#include <willpower/application/resourcesystem/DirectoryResourceLocation.h>
#include <willpower/application/resourcesystem/MaterialResource.h>
#include <willpower/application/resourcesystem/ResourceExceptions.h>

#include "Map.h"

using namespace std;
using namespace wp;

namespace
{

// The fixed vertex layout MppModelExport.cpp always packs (MPPMODEL_EXPORT_SPEC.md 4.1):
// Position3(float32) + Normal3(float32) + TexCoord2(float32) + Colour4(normalised float) --
// mirrors StatePlayTungstenMonoxide.cpp's createTorusMeshSpecification() exactly, since it's the
// same TrackProgram-compatible layout declared in Resources.xml's "Program" MeshSpecification.
mpp::mesh::MeshSpecification trackMeshSpecification()
{
	mpp::mesh::MeshSpecification meshSpec(mpp::mesh::Primitive::Type::Triangles);

	auto attribLayout = meshSpec.createVertexBufferAttributeLayout(false);
	attribLayout->createAttribute(mpp::mesh::Vertex::Component::Position3, mpp::mesh::Vertex::DataType::Float, false);
	attribLayout->createAttribute(mpp::mesh::Vertex::Component::Normal3, mpp::mesh::Vertex::DataType::Float, false);
	attribLayout->createAttribute(mpp::mesh::Vertex::Component::TexCoord2, mpp::mesh::Vertex::DataType::Float, false);
	attribLayout->createAttribute(mpp::mesh::Vertex::Component::Colour4, mpp::mesh::Vertex::DataType::Float, true);

	meshSpec.setStorageType(mpp::mesh::VertexBufferStorageType::Static);
	meshSpec.setIndexedVertices(true);

	return meshSpec;
}

// Reads one little-endian index (2 or 4 bytes, matching MppModelExport.cpp's packIndices()) out
// of a raw index buffer.
uint32_t readIndex(uint8_t const* data, size_t indexWidthBits, size_t i)
{
	size_t const bytesPerIndex = indexWidthBits / 8;
	uint8_t const* p = data + i * bytesPerIndex;

	uint32_t value = (uint32_t)p[0] | ((uint32_t)p[1] << 8);
	if (bytesPerIndex == 4)
	{
		value |= ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
	}

	return value;
}

// A mesh's `material` string (a GeometryBatch::materialKey -- see TrackBake.cpp/TrackMesh.cpp) is
// a namespace-qualified TrackMaterial or Material name, e.g. "Tracks/DefaultTrack" or
// "Tracks/DefaultRailMaterial". Resources.xml's Track resource lists exactly these as
// DependentResources with a matching `id` (see MppModelExport.hpp's buildTrackResourceXml), so
// they're already loaded by the time Map::load() runs (dependents load before their parent) --
// this just needs the underlying MaterialResource's own mpp resource name, which is what it
// declared itself under in MaterialResource::load(). TrackMaterial resources are one hop removed
// (via their own "Material" dependent, see applib::TrackMaterial::getMaterial()); a Material
// dependent (the fixed rail/mesh materials) needs none.
string resolveMaterialMppName(Map* map, string const& materialKey)
{
	auto dependent = map->getDependentResource(materialKey);

	if (dependent->getType() == "TrackMaterial")
	{
		return static_cast<applib::TrackMaterial*>(dependent.get())->getMaterial()->getQualifiedName();
	}
	else if (dependent->getType() == "Material")
	{
		return dependent->getQualifiedName();
	}

	throw application::resourcesystem::ResourceException(map, "material '" + materialKey + "' is a '" + dependent->getType() + "' resource, expected TrackMaterial or Material.");
}

} // namespace

Map::Map(string const& name,
	string const& namesp,
	string const& source,
	map<string, string> const& tags,
	application::resourcesystem::ResourceLocation* location,
	wp::Logger* logger)
	: applib::Map(name, namesp, source, tags, location, 512)
	, mwLogger(logger)
{
}

Map::~Map()
{
}

bool Map::load(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
{
	WP_UNUSED(renderSystem);

	// mpp::ModelSerializer opens `filePath` with a raw ifstream, so this needs an actual filesystem
	// path -- not the DataStream-based bytes willpower's generic ResourceLocation::readData()
	// abstraction provides. Only DirectoryResourceLocation can supply one (see the header comment
	// on Map::load's declaration).
	auto directoryLocation = dynamic_cast<application::resourcesystem::DirectoryResourceLocation*>(mwLocation);

	if (directoryLocation == nullptr)
	{
		throw application::resourcesystem::ResourceException(this, "Track resources can only be loaded from a directory-based resource location (mpp::ModelSerializer requires a real filesystem path).");
	}

	if (mModelFileName == "")
	{
		throw application::resourcesystem::ResourceException(this, "no model filename set -- expected <Definition factory=\"Track\"><File>...</File></Definition>.");
	}

	string filePath = utils::FileSystem::concatPaths(directoryLocation->getRootPath(), mModelFileName);

	mpp::ModelSerializer ser(resourceMgr);
	ser.load(filePath);

	auto meshSpec = trackMeshSpecification();
	auto modelStream = new mpp::ProgrammaticModelStream(resourceMgr);

	for (size_t i = 0; i < ser.getMeshCount(); ++i)
	{
		// Not every exported mesh has a material declared as a dependent -- MppModelExport.cpp
		// exports every tox::Track geometry batch, including auxiliary ones (PathShell's "shell",
		// ZoneSurface's "zone-<effect>") that buildTrackResourceXml deliberately doesn't list
		// (there's no rendering material defined for them yet). Skip those rather than fail the
		// whole track load over geometry nothing renders yet.
		string materialMppName;

		try
		{
			materialMppName = resolveMaterialMppName(this, ser.getMaterial(i));
		}
		catch (exception const& error)
		{
			mwLogger->warn("Map '" + getQualifiedName() + "': skipping mesh '" + ser.getName(i) + "' (material '" + ser.getMaterial(i) + "'): " + error.what());
			continue;
		}

		auto meshId = modelStream->createMesh(ser.getName(i), meshSpec, materialMppName, ser.getIndexWidth(i));

		size_t vertexCount, vertexStride;
		shared_ptr<const int8_t> vertexData;
		ser.getVertexStream(i, 0, &vertexCount, &vertexStride, &vertexData);

		vector<int8_t> vertexBytes(vertexData.get(), vertexData.get() + vertexCount * vertexStride);
		modelStream->addVertexData(meshId, vertexBytes);

		auto indexData = ser.getIndexData(i).get();
		int const indexWidthBits = ser.getIndexWidth(i);
		int const primitiveCount = ser.getPrimitiveCount(i);

		for (int t = 0; t < primitiveCount; ++t)
		{
			uint32_t const v0 = readIndex(indexData, indexWidthBits, t * 3 + 0);
			uint32_t const v1 = readIndex(indexData, indexWidthBits, t * 3 + 1);
			uint32_t const v2 = readIndex(indexData, indexWidthBits, t * 3 + 2);

			modelStream->addTriangle(meshId, v0, v1, v2);
		}
	}

	mMppResource = resourceMgr->declareResource(getQualifiedName(), mpp::ResourceStreamPtr(modelStream)).first;
	mMppResource->acquire(this);

	return true;
}

bool Map::unload(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
{
	WP_UNUSED(renderSystem);
	WP_UNUSED(resourceMgr);

	mMppResource->release(this);
	return true;
}
