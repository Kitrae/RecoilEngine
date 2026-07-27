/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "VulkanContext.h"

class CBitmap;
class CGlobalRendering;
class CglFont;

namespace Vulkan
{
	class DrawBatch;

	enum class TextAlignment
	{
		Left,
		Center,
		Right,
	};

	class TextRenderer
	{
	public:
		TextRenderer(CGlobalRendering& rendering, CglFont& font);

		bool Prepare(std::span<const std::string_view> text);
		bool AddText(
			DrawBatch& batch,
			std::string_view text,
			float x,
			float baselineY,
			float scale,
			const std::array<uint8_t, 4>& color,
			TextAlignment alignment = TextAlignment::Left
		) const;

		TextureHandle GetTexture() const { return texture; }
		int GetAtlasWidth() const { return atlasWidth; }
		int GetAtlasHeight() const { return atlasHeight; }

	private:
		bool UploadAtlas(const CBitmap& atlas);

		CGlobalRendering& rendering;
		CglFont& font;
		TextureHandle texture = INVALID_TEXTURE_HANDLE;
		uint64_t uploadedRevision = UINT64_MAX;
		int atlasWidth = 0;
		int atlasHeight = 0;
		std::vector<uint8_t> atlasRGBA;
	};
}
