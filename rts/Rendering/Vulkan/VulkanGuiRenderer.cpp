/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanGuiRenderer.h"

#include <algorithm>

#include "VulkanDrawBatch.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/Textures/Bitmap.h"

namespace Vulkan
{
	bool DrawGuiTexturedQuad(
		CGlobalRendering& rendering,
		uint32_t texture,
		float x0,
		float y0,
		float x1,
		float y1,
		const SColor& color
	) {
		DrawBatch batch;
		if (!batch.AddQuad(texture, x0, y0, x1, y1, 0.0f, 1.0f, 1.0f, 0.0f, color.rgba))
			return false;

		return batch.Append(rendering);
	}

	bool DrawGuiQuad(
		CGlobalRendering& rendering,
		float x0,
		float y0,
		float x1,
		float y1,
		const SColor& color
	) {
		return DrawGuiTexturedQuad(
			rendering,
			rendering.GetVulkanSolidTexture(),
			x0,
			y0,
			x1,
			y1,
			color
		);
	}

	bool DrawGuiOutline(
		CGlobalRendering& rendering,
		float x0,
		float y0,
		float x1,
		float y1,
		const SColor& color,
		float width
	) {
		const float horizontalWidth = width / std::max(1, rendering.viewSizeX);
		const float verticalWidth = width / std::max(1, rendering.viewSizeY);
		DrawBatch batch;
		const auto texture = rendering.GetVulkanSolidTexture();

		const bool composed =
			batch.AddQuad(texture, x0, y0, x1, y0 + verticalWidth, 0.0f, 0.0f, 1.0f, 1.0f, color.rgba) &&
			batch.AddQuad(texture, x0, y1 - verticalWidth, x1, y1, 0.0f, 0.0f, 1.0f, 1.0f, color.rgba) &&
			batch.AddQuad(texture, x0, y0 + verticalWidth, x0 + horizontalWidth, y1 - verticalWidth, 0.0f, 0.0f, 1.0f, 1.0f, color.rgba) &&
			batch.AddQuad(texture, x1 - horizontalWidth, y0 + verticalWidth, x1, y1 - verticalWidth, 0.0f, 0.0f, 1.0f, 1.0f, color.rgba);
		return composed && batch.Append(rendering);
	}

	std::optional<uint32_t> CreateGuiTexture(CGlobalRendering& rendering, const CBitmap& bitmap)
	{
		if (
			bitmap.Empty() ||
			bitmap.compressed ||
			bitmap.channels != 4 ||
			bitmap.GetDataTypeSize() != 1 ||
			bitmap.xsize <= 0 ||
			bitmap.ysize <= 0
		) {
			return std::nullopt;
		}

		return rendering.CreateVulkanTextureRGBA8(
			bitmap.GetRawMem(),
			static_cast<uint32_t>(bitmap.xsize),
			static_cast<uint32_t>(bitmap.ysize)
		);
	}
}
