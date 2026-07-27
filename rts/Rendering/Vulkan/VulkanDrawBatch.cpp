/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanDrawBatch.h"

#include <limits>

#include "Rendering/GlobalRendering.h"

namespace Vulkan
{
	void DrawBatch::Clear()
	{
		vertices.clear();
		indices.clear();
		ranges.clear();
	}

	bool DrawBatch::AddQuad(TextureHandle texture, const std::array<Vertex2D, 4>& quad)
	{
		if (
			vertices.size() > std::numeric_limits<uint32_t>::max() - quad.size() ||
			indices.size() > std::numeric_limits<uint32_t>::max() - 6
		) {
			return false;
		}

		const uint32_t firstVertex = static_cast<uint32_t>(vertices.size());
		const uint32_t firstIndex = static_cast<uint32_t>(indices.size());
		vertices.insert(vertices.end(), quad.begin(), quad.end());
		indices.insert(indices.end(), {
			firstVertex + 0, firstVertex + 1, firstVertex + 2,
			firstVertex + 0, firstVertex + 2, firstVertex + 3,
		});

		if (!ranges.empty()) {
			auto& lastRange = ranges.back();
			if (lastRange.texture == texture && lastRange.firstIndex + lastRange.indexCount == firstIndex) {
				lastRange.indexCount += 6;
				return true;
			}
		}

		ranges.push_back({firstIndex, 6, texture});
		return true;
	}

	bool DrawBatch::AddQuad(
		TextureHandle texture,
		float x0,
		float y0,
		float x1,
		float y1,
		float u0,
		float v0,
		float u1,
		float v1,
		const std::array<uint8_t, 4>& color
	) {
		const std::array<Vertex2D, 4> quad = {{
			{{x0, y0}, {u0, v0}, color},
			{{x1, y0}, {u1, v0}, color},
			{{x1, y1}, {u1, v1}, color},
			{{x0, y1}, {u0, v1}, color},
		}};
		return AddQuad(texture, quad);
	}

	bool DrawBatch::Submit(CGlobalRendering& rendering) const
	{
		return rendering.SetVulkanDrawBatch(vertices, indices, ranges);
	}

	bool DrawBatch::Append(CGlobalRendering& rendering) const
	{
		return rendering.AppendVulkanDrawBatch(vertices, indices, ranges);
	}
}
