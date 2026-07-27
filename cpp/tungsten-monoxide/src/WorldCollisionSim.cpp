#include <willpower/common/ExtentsCalculator.h>

#include "WorldCollisionSim.h"

using namespace std;
using namespace wp;


WorldCollisionSim::WorldCollisionSim(void* userObj)
	: collide::Simulation(ExtentsCalculator({ 0.0f, 0.0f }, { 100.0f, 100.f }, 0.0f), 1, 1, userObj)
{
}

set<uint32_t> WorldCollisionSim::getLineIndices(BoundingBox const& bounds) const
{
	set<uint32_t> indices;

	auto numLines = getNumStaticLines();

	for (uint32_t i = 0; i < numLines; ++i)
	{
		indices.insert(i);
	}

	return indices;
}

vector<wp::collide::StaticLine> const& WorldCollisionSim::getLines() const
{
	return mStaticLines;
}

void WorldCollisionSim::clearLines()
{
	mStaticLines.clear();
}

void WorldCollisionSim::addLine(wp::Vector2 const& v0, wp::Vector2 const& v1, bool doubleSided, uint32_t index)
{
	mStaticLines.push_back({ v0, v1, doubleSided, 1.0f, (int32_t)index });
}