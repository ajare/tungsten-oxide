#pragma once

#include <map>
#include <stdexcept>

#include <mpp/BufferDataProvider.h>
#include <mpp/Resource.h>

#include "imgui/imgui.h"

class ImGuiDataProvider : public mpp::BufferDataProvider
{
	struct DrawDataCommand
	{
		int8_t* vertexData;
		int8_t* indexData;
		size_t vertexSize;
		size_t indexSize;
		std::vector<mpp::VertexBufferRenderCommand> subCommands;
	};

private:

	ImDrawData* mDrawData;

	std::vector<DrawDataCommand> mDrawCommands;

	std::map<uint32_t, mpp::ResourcePtr> mTextureIdMap;

public:

	explicit ImGuiDataProvider(std::vector<mpp::ResourcePtr> const& textures)
		: mDrawData(nullptr)
	{
		for (auto texture : textures)
		{
			auto textureId = texture->getId();
			mTextureIdMap[textureId] = texture;
		}
	}

	void getBounds(glm::vec3& bMin, glm::vec3& bMax) override
	{
		bMin = glm::vec3(-1, -1, -1);
		bMax = glm::vec3(1, 1, 1);
	}

	uint32_t getNumCommands() override
	{
		return (uint32_t)mDrawCommands.size();
	}

	int8_t* const getVertexData(uint32_t command) override
	{
		return mDrawCommands[command].vertexData;
	}

	uint32_t getVertexStride(uint32_t command) override
	{
		return 20;
	}

	uint32_t getVertexCount(uint32_t command) override
	{
		return (uint32_t)mDrawCommands[command].vertexSize / getVertexStride(command);
	}

	int8_t* const getIndexData(uint32_t command) override
	{
		return mDrawCommands[command].indexData;
	}

	uint32_t getIndexWidth(uint32_t command) override
	{
		return sizeof(ImDrawIdx) << 3;
	}

	uint32_t getIndexCount(uint32_t command) override
	{
		return (uint32_t)mDrawCommands[command].indexSize / sizeof(ImDrawIdx);
	}

	std::vector<mpp::VertexBufferRenderCommand> getRenderCommands(uint32_t command) override
	{
		return mDrawCommands[command].subCommands;
	}

	void setDrawData(ImDrawData* drawData)
	{
		mDrawCommands.clear();

		int fbWidth = (int)(drawData->DisplaySize.x * drawData->FramebufferScale.x);
		int fbHeight = (int)(drawData->DisplaySize.y * drawData->FramebufferScale.y);

		if (fbWidth <= 0 || fbHeight <= 0)
		{
			// Clear everything
			mDrawData = nullptr;
			return;
		}

		mDrawData = drawData;

		ImVec2 clipOff = drawData->DisplayPos;
		ImVec2 clipScale = drawData->FramebufferScale;

		for (int n = 0; n < drawData->CmdListsCount; n++)
		{
			ImDrawList const* drawList = drawData->CmdLists[n];

			const size_t vertexSize = (size_t)drawList->VtxBuffer.Size * (int)sizeof(ImDrawVert);
			const size_t indexSize = (size_t)drawList->IdxBuffer.Size * (int)sizeof(ImDrawIdx);

			mDrawCommands.push_back({
				(int8_t*)drawList->VtxBuffer.Data,
				(int8_t*)drawList->IdxBuffer.Data,
				vertexSize,
				indexSize
				});

			auto& drawCmd = mDrawCommands.back();

			for (int cmd_i = 0; cmd_i < drawList->CmdBuffer.Size; cmd_i++)
			{
				ImDrawCmd const* pcmd = &drawList->CmdBuffer[cmd_i];

				if (pcmd->UserCallback != nullptr)
				{
					throw std::exception("User callbacks not supported.");
				}
				else
				{
					// Project scissor/clipping rectangles into framebuffer space
					ImVec2 clipMin((pcmd->ClipRect.x - clipOff.x) * clipScale.x, (pcmd->ClipRect.y - clipOff.y) * clipScale.y);
					ImVec2 clipMax((pcmd->ClipRect.z - clipOff.x) * clipScale.x, (pcmd->ClipRect.w - clipOff.y) * clipScale.y);

					if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y)
					{
						continue;
					}

					mpp::VertexBufferRenderCommand vbrc;

					vbrc.clipMin[0] = (int)clipMin.x;
					vbrc.clipMin[1] = (int)((float)fbHeight - clipMax.y);
					vbrc.clipSize[0] = (int)(clipMax.x - clipMin.x);
					vbrc.clipSize[1] = (int)(clipMax.y - clipMin.y);

					auto texId = (uint32_t)pcmd->GetTexID();
					auto textureIt = mTextureIdMap.find(texId);
					if (textureIt == mTextureIdMap.end())
					{
						throw std::runtime_error("ImGui draw command references an unknown texture.");
					}
					vbrc.textures.push_back(textureIt->second);

					vbrc.offset = pcmd->IdxOffset;
					vbrc.count = pcmd->ElemCount;

					drawCmd.subCommands.push_back(vbrc);
				}
			}
		}
	}
};
