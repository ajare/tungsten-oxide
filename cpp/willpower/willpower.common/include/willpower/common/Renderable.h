#pragma once

#include "willpower/common/Platform.h"

namespace WP_NAMESPACE
{

	class WP_COMMON_API Renderable
	{
		mutable bool mRenderableModified;

	protected:

		void rebuildRenderable();

	public:

		Renderable();

		virtual ~Renderable() = default;

		void _setRenderableRebuilt() const;

		bool isRenderableModified() const;
	};

} // WP_NAMESPACE
