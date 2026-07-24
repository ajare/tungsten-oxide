#pragma once

#include <vector>
#include <set>
#include <map>
#include <exception>

#include "willpower/common/Platform.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{

		enum class UserAttributePolygonColourType
		{
			Default,
			Polygon,
			Vertex,
			PolygonVertex
		};

		class UserAttributesBase
		{
		public:

			virtual ~UserAttributesBase() = default;

			virtual void clear() = 0;

			virtual std::map<int32_t, int32_t> compact(std::set<int32_t> const& input) = 0;

			virtual uint32_t createAttribute(void const* data) = 0;

			virtual void updateAttribute(uint32_t index, void const* data) = 0;

			virtual void const* readAttribute(uint32_t index) = 0;

			virtual void getUvAttribute(void const* data, uint32_t textureIndex, float& u, float& v) const
			{
				WP_UNUSED(data);
				WP_UNUSED(textureIndex);

				u = 0.0f;
				v = 0.0f;
			}

			virtual void getUvWeightAttribute(void const* data, uint32_t textureIndex, float& weight) const
			{
				WP_UNUSED(data);
				WP_UNUSED(textureIndex);

				weight = 1.0f;
			}

			virtual void getRgbaAttribute(void const* data, float& r, float& g, float& b, float& a) const
			{
				WP_UNUSED(data);

				r = 1.0f;
				g = 1.0f;
				b = 1.0f;
				a = 1.0f;
			}

			virtual void getMaterialAttribute(void const* data, std::string& material) const
			{
				WP_UNUSED(data);

				material = "";
			}

			virtual void getProgramAttribute(void const* data, std::string& program) const
			{
				WP_UNUSED(data);

				program = "";
			}

			virtual void getTexturesAttribute(void const* data, std::vector<std::string>& textures) const
			{
				WP_UNUSED(data);

				textures.clear();
			}

			virtual void getPolygonColourType(void const* data, UserAttributePolygonColourType& type) const
			{
				WP_UNUSED(data);
				type = UserAttributePolygonColourType::Default;
			}
		};

		template<typename T>
		class UserAttributes : public UserAttributesBase
		{
		protected:

			std::vector<T> mAttributes;

		public:

			void clear() override
			{
				mAttributes.clear();
			}

			std::map<int32_t, int32_t> compact(std::set<int32_t> const& input) override
			{
				std::map<int32_t, int32_t> remapped;
				
				uint32_t newLiveOffset{ 0 };
				for (uint32_t i = 0; i < mAttributes.size(); ++i)
				{
					if (input.find((int32_t)i) != input.end())
					{
						auto& compactedAttrib = mAttributes[newLiveOffset];
						compactedAttrib = mAttributes[i];

						remapped[i] = newLiveOffset;
						newLiveOffset++;
					}
				}

				for (uint32_t i = newLiveOffset; i < mAttributes.size(); ++i)
				{
					mAttributes.pop_back();
				}
				
				return remapped;
			}

			uint32_t addAttribute(T attr)
			{
				auto index = (uint32_t)mAttributes.size();
				mAttributes.push_back(attr);
				return index;
			}

			void setAttribute(uint32_t index, T attr)
			{
				if (index >= mAttributes.size())
				{
					throw std::exception("Out of bounds.");
				}

				mAttributes[index] = attr;
			}

			T const& getAttribute(uint32_t index) const
			{
				if (index >= mAttributes.size())
				{
					throw std::exception("Out of bounds.");
				}

				return mAttributes[index];
			}

		};

		class UserAttributesFactory
		{
		public:

			virtual UserAttributesBase* create() = 0;

			virtual UserAttributesBase* copy(UserAttributesBase const* source) = 0;
		};

	} // geometry
} // WP_NAMESPACE
