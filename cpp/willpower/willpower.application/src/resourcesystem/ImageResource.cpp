#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

// GL_UNSIGNED_BYTE/GL_RGB/GL_RGBA below used to arrive transitively through an mpp header; that
// stopped once MassivePolyPusher decoupled its public headers from GL, so this now includes GLEW
// directly, matching every other GL-constant user in this codebase.
#include <GL/glew.h>

#include <mpp/ProgrammaticTextureStream.h>

#include <limits>
#include <memory>

#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/ImageResource.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
using namespace std;

ImageResource::ImageResource(string const& name, string const& namesp, string const& source, map<string, string> const& tags, ResourceLocation* location)
    : Resource(name, namesp, "Image", source, tags, location), mData(nullptr), mSize(0), mWidth(0), mHeight(0), mNumChannels(0) {
}

void ImageResource::parseData(DataStreamPtr dataPtr) {
  if (dataPtr->getSize() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    throw ResourceException(this, "image data is too large.");
  }

  int width = 0;
  int height = 0;
  int channels = 0;
  std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decoded(
      stbi_load_from_memory(dataPtr->getData(), static_cast<int>(dataPtr->getSize()), &width, &height, &channels, 0), stbi_image_free);
  if (decoded == nullptr) {
    throw ResourceException(this, "failed to decode image.");
  }

  if (channels != 3 && channels != 4) {
    throw ResourceException(this, "unsupported image channel count. Only RGB and RGBA images are supported.");
  }

  const size_t rowSize = static_cast<size_t>(width) * channels;
  if (rowSize > std::numeric_limits<uint32_t>::max() / static_cast<size_t>(height)) {
    throw ResourceException(this, "decoded image is too large.");
  }

  mWidth = width;
  mHeight = height;
  mNumChannels = channels;
  mSize = static_cast<uint32_t>(rowSize * mHeight);
  mData = new uint8_t[mSize];

  // Convert top-to-bottom decoded rows to the bottom-to-top layout expected by
  // the existing OpenGL texture path.
  for (int y = 0; y < mHeight; ++y) {
    memcpy(mData + static_cast<size_t>(y) * rowSize, decoded.get() + static_cast<size_t>(mHeight - y - 1) * rowSize, rowSize);
  }
}

void ImageResource::destroy() {
  delete[] mData;
  mData = nullptr;

  mSize = 0;
  mWidth = 0;
  mHeight = 0;
  mNumChannels = 0;
}

bool ImageResource::load(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) {
  WP_UNUSED(renderSystem);

  // Is this to be treated as an atlas?
  auto tags = getTags();

  bool atlas = false;
  if (tags.find("uv-style") != tags.end()) {
    if (tags["uv-style"] == "atlas") {
      atlas = true;
    }
  }

  mpp::ProgrammaticTextureStream* textureStream = new mpp::ProgrammaticTextureStream(resourceMgr);

  textureStream->setAtlas(atlas);
  textureStream->setTarget(mpp::TextureTarget::Texture2D);
  textureStream->setData([this](string const& id) {
    WP_UNUSED(id);

    mpp::TextureData data;

    data.width = getWidth();
    data.height = getHeight();
    data.bitsPerPixel = getNumChannels() * 8;
    data.dataType = GL_UNSIGNED_BYTE;

    switch (data.bitsPerPixel) {
      case 24:
        data.pixelFormat = GL_RGB;
        break;
      case 32:
        data.pixelFormat = GL_RGBA;
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
  if (tags.find("filtering") != tags.end()) {
    if (tags["filtering"] == "none") {
      filtered = false;
    }
  }

  if (filtered) {
    textureStream->setFiltering(mpp::TextureParams::MinFilter::Linear, mpp::TextureParams::MagFilter::Linear);
  } else {
    textureStream->setFiltering(mpp::TextureParams::MinFilter::Nearest, mpp::TextureParams::MagFilter::Nearest);
  }

  mMppResource = resourceMgr->declareResource(getQualifiedName(), mpp::ResourceStreamPtr(textureStream)).first;
  mMppResource->acquire(this);

  return true;
}

bool ImageResource::unload(mpp::RenderSystem* renderSystem, mpp::ResourceManager* resourceMgr) {
  WP_UNUSED(renderSystem);
  WP_UNUSED(resourceMgr);

  mMppResource->release(this);
  return true;
}

uint8_t const* ImageResource::getData() const {
  return mData;
}

uint32_t ImageResource::getSize() const {
  return mSize;
}

int ImageResource::getWidth() const {
  return mWidth;
}

int ImageResource::getHeight() const {
  return mHeight;
}

int ImageResource::getNumChannels() const {
  return mNumChannels;
}

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE