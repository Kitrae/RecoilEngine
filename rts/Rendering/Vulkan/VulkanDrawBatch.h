/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "VulkanContext.h"

class CGlobalRendering;

namespace Vulkan
{
	class DrawBatch
	{
	public:
		void Clear();
		bool AddQuad(TextureHandle texture, const std::array<Vertex2D, 4>& quad);
		bool AddQuad(
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
		);
		bool Submit(CGlobalRendering& rendering) const;
		bool Append(CGlobalRendering& rendering) const;

		bool Empty() const { return indices.empty(); }
		std::span<const Vertex2D> GetVertices() const { return vertices; }
		std::span<const uint32_t> GetIndices() const { return indices; }
		std::span<const DrawRange> GetRanges() const { return ranges; }

	private:
		std::vector<Vertex2D> vertices;
		std::vector<uint32_t> indices;
		std::vector<DrawRange> ranges;
	};
}
