#pragma once

#include "willpower/geometry/UserAttributes.h"

#include "Platform.h"

namespace applib
{

	struct VertexAttributes
	{
		float r, g, b;

		VertexAttributes lerp(VertexAttributes const& to, float t) const
		{
			return
			{
				r + (to.r - r) * t,
				g + (to.g - g) * t,
				b + (to.b - b) * t
			};
		}
	};

	struct EdgeAttributes
	{
		float r, g, b;
	};

	struct PolygonAttributes
	{
		float r, g, b;
		wp::geometry::UserAttributePolygonColourType colourType;
		std::vector<std::string> textures;
	};

	struct PolygonVertexAttributes
	{
		float u0, v0, u1, v1;
		float w0, w1;
		float r, g, b;

		PolygonVertexAttributes lerp(PolygonVertexAttributes const& to, float t) const
		{
			return
			{
				u0 + (to.u0 - u0) * t,
				v0 + (to.v0 - v0) * t,
				u1 + (to.u1 - u1) * t,
				v1 + (to.v1 - v1) * t,
				w0 + (to.w0 - w0) * t,
				w1 + (to.w1 - w1) * t,
				r + (to.r - r) * t,
				g + (to.g - g) * t,
				b + (to.b - b) * t
			};
		}
	};

	class VertexAttributeHolder : public wp::geometry::UserAttributes<VertexAttributes>
	{
	public:

		VertexAttributeHolder()
		{
		}

		VertexAttributeHolder(VertexAttributeHolder const& other)
		{
			mAttributes = other.mAttributes;
		}

		uint32_t createAttribute(void const* data) override
		{
			VertexAttributes const* va = static_cast<VertexAttributes const*>(data);
			return addAttribute(*va);
		}

		void updateAttribute(uint32_t index, void const* data) override
		{
			VertexAttributes const* va = static_cast<VertexAttributes const*>(data);
			setAttribute(index, *va);
		}

		void const* readAttribute(uint32_t index) override
		{
			VertexAttributes const& va = getAttribute(index);
			return static_cast<void const*>(&va);
		}

		void getRgbaAttribute(void const* data, float& r, float& g, float& b, float& a) const override
		{
			VertexAttributes const* va = static_cast<VertexAttributes const*>(data);
			r = va->r;
			g = va->g;
			b = va->b;
			a = 1.0f;
		}
	};

	class VertexAttributeFactory : public wp::geometry::UserAttributesFactory
	{
	public:

		wp::geometry::UserAttributesBase* create() override
		{
			return new VertexAttributeHolder();
		}

		wp::geometry::UserAttributesBase* copy(wp::geometry::UserAttributesBase const* source) override
		{
			return new VertexAttributeHolder(*dynamic_cast<VertexAttributeHolder const*>(source));
		}
	};

	class EdgeAttributeHolder : public wp::geometry::UserAttributes<EdgeAttributes>
	{
	public:

		EdgeAttributeHolder()
		{
		}

		EdgeAttributeHolder(EdgeAttributeHolder const& other)
		{
			mAttributes = other.mAttributes;
		}

		uint32_t createAttribute(void const* data) override
		{
			EdgeAttributes const* ea = static_cast<EdgeAttributes const*>(data);
			return addAttribute(*ea);
		}

		void updateAttribute(uint32_t index, void const* data) override
		{
			EdgeAttributes const* ea = static_cast<EdgeAttributes const*>(data);
			setAttribute(index, *ea);
		}

		void const* readAttribute(uint32_t index) override
		{
			EdgeAttributes const& ea = getAttribute(index);
			return static_cast<void const*>(&ea);
		}

		void getRgbaAttribute(void const* data, float& r, float& g, float& b, float& a) const override
		{
			EdgeAttributes const* ea = static_cast<EdgeAttributes const*>(data);
			r = ea->r;
			g = ea->g;
			b = ea->b;
			a = 1.0f;
		}
	};

	class EdgeAttributeFactory : public wp::geometry::UserAttributesFactory
	{
	public:

		wp::geometry::UserAttributesBase* create() override
		{
			return new EdgeAttributeHolder();
		}

		wp::geometry::UserAttributesBase* copy(wp::geometry::UserAttributesBase const* source) override
		{
			return new EdgeAttributeHolder(*dynamic_cast<EdgeAttributeHolder const*>(source));
		}
	};

	class PolygonAttributeHolder : public wp::geometry::UserAttributes<PolygonAttributes>
	{
	public:

		PolygonAttributeHolder()
		{
		}

		PolygonAttributeHolder(PolygonAttributeHolder const& other)
		{
			mAttributes = other.mAttributes;
		}

		uint32_t createAttribute(void const* data) override
		{
			PolygonAttributes const* pa = static_cast<PolygonAttributes const*>(data);
			return addAttribute(*pa);
		}

		void updateAttribute(uint32_t index, void const* data) override
		{
			PolygonAttributes const* pa = static_cast<PolygonAttributes const*>(data);
			setAttribute(index, *pa);
		}

		void const* readAttribute(uint32_t index) override
		{
			PolygonAttributes const& pa = getAttribute(index);
			return static_cast<void const*>(&pa);
		}

		void getRgbaAttribute(void const* data, float& r, float& g, float& b, float& a) const override
		{
			PolygonAttributes const* pa = static_cast<PolygonAttributes const*>(data);
			r = pa->r;
			g = pa->g;
			b = pa->b;
			a = 1.0f;
		}

		void getTexturesAttribute(void const* data, std::vector<std::string>& textures) const override
		{
			PolygonAttributes const* pa = static_cast<PolygonAttributes const*>(data);
			textures = pa->textures;
		}

		void getPolygonColourType(void const* data, wp::geometry::UserAttributePolygonColourType& type) const override
		{
			PolygonAttributes const* pa = static_cast<PolygonAttributes const*>(data);
			type = pa->colourType;
		}

	};

	class PolygonAttributeFactory : public wp::geometry::UserAttributesFactory
	{
	public:

		wp::geometry::UserAttributesBase* create() override
		{
			return new PolygonAttributeHolder();
		}

		wp::geometry::UserAttributesBase* copy(wp::geometry::UserAttributesBase const* source) override
		{
			return new PolygonAttributeHolder(*dynamic_cast<PolygonAttributeHolder const*>(source));
		}
	};

	class PolygonVertexAttributeHolder : public wp::geometry::UserAttributes<PolygonVertexAttributes>
	{
	public:

		PolygonVertexAttributeHolder()
		{
		}

		PolygonVertexAttributeHolder(PolygonVertexAttributeHolder const& other)
		{
			mAttributes = other.mAttributes;
		}

		uint32_t createAttribute(void const* data) override
		{
			PolygonVertexAttributes const* pva = static_cast<PolygonVertexAttributes const*>(data);
			return addAttribute(*pva);
		}

		void updateAttribute(uint32_t index, void const* data) override
		{
			PolygonVertexAttributes const* pva = static_cast<PolygonVertexAttributes const*>(data);
			setAttribute(index, *pva);
		}

		void const* readAttribute(uint32_t index) override
		{
			PolygonVertexAttributes const& pva = getAttribute(index);
			return static_cast<void const*>(&pva);
		}

		void getUvAttribute(void const* data, uint32_t textureIndex, float& u, float& v) const override
		{
			PolygonVertexAttributes const* pva = static_cast<PolygonVertexAttributes const*>(data);

			if (textureIndex == 0)
			{
				u = pva->u0;
				v = pva->v0;
			}
			else
			{
				u = pva->u1;
				v = pva->v1;
			}
		}

		void getUvWeightAttribute(void const* data, uint32_t textureIndex, float& weight) const override
		{
			PolygonVertexAttributes const* pva = static_cast<PolygonVertexAttributes const*>(data);

			if (textureIndex == 0)
			{
				weight = pva->w0;
			}
			else
			{
				weight = pva->w1;
			}
		}

		void getRgbaAttribute(void const* data, float& r, float& g, float& b, float& a) const override
		{
			PolygonVertexAttributes const* pva = static_cast<PolygonVertexAttributes const*>(data);
			r = pva->r;
			g = pva->g;
			b = pva->b;
			a = 1.0f;
		}
	};

	class PolygonVertexAttributeFactory : public wp::geometry::UserAttributesFactory
	{
	public:

		wp::geometry::UserAttributesBase* create() override
		{
			return new PolygonVertexAttributeHolder();
		}

		wp::geometry::UserAttributesBase* copy(wp::geometry::UserAttributesBase const* source) override
		{
			return new PolygonVertexAttributeHolder(*dynamic_cast<PolygonVertexAttributeHolder const*>(source));
		}
	};

} // applib
