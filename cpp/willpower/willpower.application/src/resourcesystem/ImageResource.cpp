#include <freeimage/FreeImage.h>

#include <mpp/ProgrammaticTextureStream.h>

#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/ImageResource.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"

namespace WP_NAMESPACE
{
	namespace application
	{
		namespace resourcesystem
		{
			using namespace std;

			bool isBigEndian()
			{
				union
				{
					uint32_t i;
					char c[4];
				} bint{ 0x01020304 };

				return bint.c[0] == 1;
			}

			void FreeImageErrorHandler(FREE_IMAGE_FORMAT fif, const char *message)
			{
				WP_UNUSED(fif);
				WP_UNUSED(message);
				
				// Hook into logging
				// ...
			}

			ImageResource::ImageResource(string const& name, string const& namesp, string const& source, map<string, string> const& tags, ResourceLocation* location)
				: Resource(name, namesp, "Image", source, tags, location)
				, mData(nullptr)
				, mSize(0)
				, mWidth(0)
				, mHeight(0)
				, mNumChannels(0)
			{
			}

			void ImageResource::parseData(DataStreamPtr dataPtr)
			{
				FIMEMORY* fibuf = FreeImage_OpenMemory((BYTE*)dataPtr->getData(), dataPtr->getSize());
				FIBITMAP* bitmap = FreeImage_LoadFromMemory(FreeImage_GetFIFFromFilename(getSource().c_str()), fibuf);

				if (bitmap)
				{
					// Check it's supported
					FREE_IMAGE_TYPE imageType = FreeImage_GetImageType(bitmap);

					if (imageType != FIT_BITMAP)
					{
						FreeImage_Unload(bitmap);
						throw ResourceException(this, "unsupported image type.");
					}

					// Convert to 8 bit.
					if (imageType == FIT_RGB16 ||
						imageType == FIT_RGBA16 ||
						imageType == FIT_RGBF ||
						imageType == FIT_RGBAF)
					{
						bitmap = FreeImage_ColorQuantizeEx(bitmap, FIQ_WUQUANT);
						bitmap = FreeImage_ConvertTo8Bits(bitmap);
					}

					mWidth = (int)FreeImage_GetWidth(bitmap);
					mHeight = (int)FreeImage_GetHeight(bitmap);
					mNumChannels = (int)FreeImage_GetBPP(bitmap) / 8;

					uint32_t dataSpan = mWidth * mNumChannels;
					mSize = dataSpan * mHeight;

					mData = new uint8_t[mSize];
					memcpy(mData, (uint8_t*)FreeImage_GetBits(bitmap), mSize * sizeof(uint8_t));

					FreeImage_Unload(bitmap);
				}
				else
				{
					// Error
					// ...
				}
			}

			void ImageResource::destroy()
			{
				delete[] mData;
				mData = nullptr;

				mSize = 0;
				mWidth = 0;
				mHeight = 0;
				mNumChannels = 0;
			}

			bool ImageResource::load(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
			{
				WP_UNUSED(renderSystem);

				// Is this to be treated as an atlas?
				auto tags = getTags();

				bool atlas = false;
				if (tags.find("uv-style") != tags.end())
				{
					if (tags["uv-style"] == "atlas")
					{
						atlas = true;
					}
				}

				mpp::ProgrammaticTextureStream* textureStream = new mpp::ProgrammaticTextureStream(resourceMgr);

				textureStream->setAtlas(atlas);
				textureStream->setTarget(mpp::TextureTarget::Texture2D);
				textureStream->setData([this](string const& id)
				{
					WP_UNUSED(id);

					mpp::TextureData data;

					data.width = getWidth();
					data.height = getHeight();
					data.bitsPerPixel = getNumChannels() * 8;
					data.dataType = GL_UNSIGNED_BYTE;

					switch (data.bitsPerPixel)
					{
					case 24:
						data.pixelFormat = isBigEndian() ? GL_RGB : GL_BGR;
						break;
					case 32:
						data.pixelFormat = isBigEndian() ? GL_RGBA : GL_BGRA;
						break;
					default:
						throw ResourceException(this, "unsupported image bit depth.  Only 24- and 32-bit images are supported.");
					}

					size_t dataSize = (data.width * data.height * data.bitsPerPixel / 8);

					data.data = new uint8_t[dataSize];
					memcpy(data.data, mData, dataSize);
					return data;
				});

				// Other tags
				bool filtered = true;
				if (tags.find("filtering") != tags.end())
				{
					if (tags["filtering"] == "none")
					{
						filtered = false;
					}
				}

				if (filtered)
				{
					textureStream->setFiltering(mpp::TextureParams::MinFilter::Linear, mpp::TextureParams::MagFilter::Linear);
				}
				else
				{
					textureStream->setFiltering(mpp::TextureParams::MinFilter::Nearest, mpp::TextureParams::MagFilter::Nearest);
				}

				mMppResource = resourceMgr->declareResource(getQualifiedName(), mpp::ResourceStreamPtr(textureStream)).first;
				mMppResource->acquire(this);

				return true;
			}

			bool ImageResource::unload(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr)
			{
				WP_UNUSED(renderSystem);
				WP_UNUSED(resourceMgr);

				mMppResource->release(this);
				return true;
			}

			uint8_t const* ImageResource::getData() const
			{
				return mData;
			}

			uint32_t ImageResource::getSize() const
			{
				return mSize;
			}

			int ImageResource::getWidth() const
			{
				return mWidth;
			}

			int ImageResource::getHeight() const
			{
				return mHeight;
			}

			int ImageResource::getNumChannels() const
			{
				return mNumChannels;
			}

		} // resourcesystem
	} // application
} // WP_NAMESPACE