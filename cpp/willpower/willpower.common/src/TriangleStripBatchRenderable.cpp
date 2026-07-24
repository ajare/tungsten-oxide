#include <algorithm>

#include "willpower/common/TriangleStripBatchRenderable.h"

using namespace std;

namespace WP_NAMESPACE
{

	TriangleStripBatchRenderable::TriangleStripBatchRenderable(int triangleCount, int indexWidth, int posStride, int texStride, int colourOffset)
		: IndexedTriangleBatchRenderable(triangleCount, indexWidth, triangleCount + 2, posStride, texStride, colourOffset)
	{
	}

} // WP_NAMESPACE
