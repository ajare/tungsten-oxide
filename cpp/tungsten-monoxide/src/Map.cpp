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
// Position3(float32, 12B) + Normal3(float32, 12B) + TexCoord2(float32, 8B) +
// Colour4(normalised UnsignedByte, 4B) = a tightly-packed 36-byte interleaved vertex. This MUST
// agree byte-for-byte with the exporter, because the .mppmodel binary does not record its own
// attribute layout (MPPMODEL_EXPORT_SPEC.md 2.2) -- ProgrammaticModelStream derives both the
// vertex count (dataSize / stride) and every attribute's byte offset purely from this spec, so
// any disagreement silently decodes garbage rather than failing.
//
// Colour4 is UnsignedByte, NOT Float: this is deliberately different from
// StatePlayTungstenMonoxide.cpp's createTorusMeshSpecification(), whose torus is built in memory
// with f32 colours and never round-trips through a .mppmodel. Declaring Float here would make
// getVertexStrideInBytes() report 48 bytes against 36-byte-packed file data. It matches
// Resources.xml's TrackProgram MeshSpecification (`<data>colour4</data><type>uint8</type>`).
//
// Vertices are NOT indexed: every batch tox emits is already a triangle soup (TrackBake.cpp's
// Builder::tri()/TrackMesh.cpp's addTriangle() give every triangle three brand-new vertices, so
// vertexCount == 3 * triangleCount), which is why the exporter writes no index buffer at all.
// With this false, ProgrammaticModelStream derives primitiveCount as vertexCount / 3 instead of
// from index data. Also matches Resources.xml's `<indexed>false</indexed>`.
mpp::mesh::MeshSpecification trackMeshSpecification()
{
	mpp::mesh::MeshSpecification meshSpec(mpp::mesh::Primitive::Type::Triangles);

	auto attribLayout = meshSpec.createVertexBufferAttributeLayout(false);
	attribLayout->createAttribute(mpp::mesh::Vertex::Component::Position3, mpp::mesh::Vertex::DataType::Float, false);
	attribLayout->createAttribute(mpp::mesh::Vertex::Component::Normal3, mpp::mesh::Vertex::DataType::Float, false);
	attribLayout->createAttribute(mpp::mesh::Vertex::Component::TexCoord2, mpp::mesh::Vertex::DataType::Float, false);
	attribLayout->createAttribute(mpp::mesh::Vertex::Component::Colour4, mpp::mesh::Vertex::DataType::UnsignedByte, true);

	meshSpec.setStorageType(mpp::mesh::VertexBufferStorageType::Static);
	meshSpec.setIndexedVertices(false);

	return meshSpec;
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
		// exports every tox::Track geometry batch, including a PathSurface batch that falls back
		// to the legacy "road" materialKey literal (TrackBake.cpp) when a path has no authored
		// TrackMaterial, which buildTrackResourceXml deliberately doesn't list (there's no
		// rendering material defined for it). Skip those rather than fail the whole track load
		// over geometry nothing renders yet.
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

		// indexWidth 16 is inert here (createMesh only validates/stores it for INDEXED meshes, and
		// trackMeshSpecification() declares non-indexed), but must still be a legal 16 or 32 --
		// createMesh throws on anything else. Deliberately not ser.getIndexWidth(i): a
		// non-indexed export writes no index streams at all for that to read.
		auto meshId = modelStream->createMesh(ser.getName(i), meshSpec, materialMppName, 16);

		size_t vertexCount, vertexStride;
		shared_ptr<const int8_t> vertexData;
		ser.getVertexStream(i, 0, &vertexCount, &vertexStride, &vertexData);

		if (vertexStride != meshSpec.getVertexStrideInBytes())
		{
			// The file's own recorded stride is the one piece of layout the binary DOES carry
			// (MPPMODEL_EXPORT_SPEC.md 2.2), so it's worth cross-checking against the layout above
			// -- a mismatch means the two have drifted and every vertex past the first would
			// decode as garbage, which is far easier to diagnose here than on screen.
			throw application::resourcesystem::ResourceException(this, "mesh '" + ser.getName(i) + "' has vertex stride " + to_string(vertexStride) + " bytes, expected " + to_string(meshSpec.getVertexStrideInBytes()) + " (see trackMeshSpecification()).");
		}

		vector<int8_t> vertexBytes(vertexData.get(), vertexData.get() + vertexCount * vertexStride);
		modelStream->addVertexData(meshId, vertexBytes);

		// No addTriangle()/index data: the mesh is non-indexed (see trackMeshSpecification()), so
		// ProgrammaticModelStream draws the vertex buffer straight through as a triangle soup and
		// derives primitiveCount as vertexCount / 3 itself. Any index buffer an older, indexed
		// .mppmodel still carries is ignored -- harmlessly, since tox only ever emitted the
		// identity permutation (indices[k] == k), which draws identically either way.
	}

	mMppResource = resourceMgr->declareResource(getQualifiedName(), mpp::ResourceStreamPtr(modelStream)).first;
	mMppResource->acquire(this);
	// declareResource()+acquire() only registers/ref-counts the resource -- nothing in the
	// Scene::add3dModel()/SceneModel3d chain that later consumes getMppResource() ever calls
	// load() on it (SceneModel3d's constructor only acquire()s), so without this the
	// ProgrammaticModelStream's createMeshDataStreams()/GPU buffer upload never runs and the mesh
	// silently has no vertex data. Mirrors StatePlayTungstenMonoxide.cpp's createTorusModel(),
	// which calls ->load() explicitly right after acquire() for the same reason.
	mMppResource->load();

	return true;
}

bool Map::unload(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
{
	WP_UNUSED(renderSystem);
	WP_UNUSED(resourceMgr);

	mMppResource->release(this);
	return true;
}
