#include "willpower/common/Renderable.h"

namespace WP_NAMESPACE
{

	Renderable::Renderable()
		: mRenderableModified(true)
	{
	}

	void Renderable::_setRenderableRebuilt() const
	{
		mRenderableModified = false;
	}

	bool Renderable::isRenderableModified() const
	{
		return mRenderableModified;
	}

	void Renderable::rebuildRenderable()
	{
		mRenderableModified = true;
	}

} // WP_NAMESPACE
