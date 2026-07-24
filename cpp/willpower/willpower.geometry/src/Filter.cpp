#include "willpower/geometry/Filter.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		using namespace WP_NAMESPACE;
		using namespace std;

		Filter::Filter(Mesh const* mesh)
			: mwMesh(mesh)
		{
		}

		IndexSet const& Filter::getIndices() const
		{
			return mIndices;
		}

		void Filter::setIndices(IndexSet const& indices)
		{
			mIndices = indices;
		}

		void Filter::addIndices(IndexSet const& indices)
		{
			IndexSet u;
			
			set_union(
				mIndices.cbegin(), 
				mIndices.cend(), 
				indices.cbegin(), 
				indices.cend(),
				inserter(u, u.begin()));

			setIndices(u);
		}

		void Filter::removeIndices(IndexSet const& indices)
		{
			IndexSet u;
			
			set_difference(
				mIndices.cbegin(),
				mIndices.cend(),
				indices.cbegin(),
				indices.cend(),
				inserter(u, u.begin()));
				
			setIndices(u);
		}

		void Filter::filter(FilterFunction func)
		{
			IndexSet u;
			
			copy_if(mIndices.begin(), mIndices.end(), inserter(u, u.begin()), func);
			setIndices(u);
		}

		void Filter::minElement(CompareFunction func)
		{
			auto result = *min_element(mIndices.begin(), mIndices.end(), func);
			setIndices({ result });
		}

		void Filter::maxElement(CompareFunction func)
		{
			auto result = *max_element(mIndices.begin(), mIndices.end(), func);
			setIndices({ result });
		}

	} // geometry
} //WP_NAMESPACE
