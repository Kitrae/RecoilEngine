/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanFontRenderer.h"

#include <array>
#include <string_view>
#include <utility>

#include "VulkanDrawBatch.h"
#include "VulkanTextRenderer.h"
#include "Rendering/Fonts/CFontTexture.h"
#include "Rendering/Fonts/glFont.h"
#include "Rendering/GlobalRendering.h"
#include "System/Log/ILog.h"

namespace
{
	Vulkan::Vertex2D ConvertVertex(const VA_TYPE_TC& vertex, float inverseWidth, float inverseHeight)
	{
		return {
			{vertex.pos.x, vertex.pos.y},
			{vertex.s * inverseWidth, vertex.t * inverseHeight},
			{vertex.c.r, vertex.c.g, vertex.c.b, vertex.c.a},
		};
	}
}

CglVulkanFontRenderer::CglVulkanFontRenderer()
{
	primaryQuads.reserve(NUM_BUFFER_ELEMS);
	outlineQuads.reserve(NUM_BUFFER_ELEMS);
}

CglVulkanFontRenderer::~CglVulkanFontRenderer() = default;

void CglVulkanFontRenderer::AddQuadTrianglesPB(
	VA_TYPE_TC&& tl,
	VA_TYPE_TC&& tr,
	VA_TYPE_TC&& br,
	VA_TYPE_TC&& bl
) {
	primaryQuads.push_back({
		std::move(tl),
		std::move(tr),
		std::move(br),
		std::move(bl),
	});
}

void CglVulkanFontRenderer::AddQuadTrianglesOB(
	VA_TYPE_TC&& tl,
	VA_TYPE_TC&& tr,
	VA_TYPE_TC&& br,
	VA_TYPE_TC&& bl
) {
	outlineQuads.push_back({
		std::move(tl),
		std::move(tr),
		std::move(br),
		std::move(bl),
	});
}

void CglVulkanFontRenderer::HandleTextureUpdate(CFontTexture& fontTexture, bool onlyUpload)
{
	if (!onlyUpload)
		fontTexture.UpdateGlyphAtlasTexture();

	if (textRenderer == nullptr) {
		textRenderer = std::make_unique<Vulkan::TextRenderer>(
			*globalRendering,
			static_cast<CglFont&>(fontTexture)
		);
	}

	if (!textRenderer->Prepare(std::span<const std::string_view>{})) {
		LOG_L(L_ERROR, "[CglVulkanFontRenderer::%s] Failed updating the Vulkan font atlas", __func__);
		return;
	}

	fontTexture.needsTextureUpload = false;
	fontTexture.isColor = fontTexture.needsColor;
}

void CglVulkanFontRenderer::PushGLState(const CglFont&)
{
}

void CglVulkanFontRenderer::PopGLState(const CglFont&)
{
}

void CglVulkanFontRenderer::DrawTraingleElements()
{
	if (primaryQuads.empty() && outlineQuads.empty())
		return;
	if (
		textRenderer == nullptr ||
		textRenderer->GetTexture() == Vulkan::INVALID_TEXTURE_HANDLE ||
		textRenderer->GetAtlasWidth() <= 0 ||
		textRenderer->GetAtlasHeight() <= 0
	) {
		primaryQuads.clear();
		outlineQuads.clear();
		return;
	}

	const float inverseWidth = 1.0f / textRenderer->GetAtlasWidth();
	const float inverseHeight = 1.0f / textRenderer->GetAtlasHeight();
	const auto texture = textRenderer->GetTexture();
	Vulkan::DrawBatch batch;

	const auto appendQuads = [&](const std::vector<Quad>& quads) {
		for (const auto& quad : quads) {
			const std::array<Vulkan::Vertex2D, 4> vertices = {{
				ConvertVertex(quad[0], inverseWidth, inverseHeight),
				ConvertVertex(quad[1], inverseWidth, inverseHeight),
				ConvertVertex(quad[2], inverseWidth, inverseHeight),
				ConvertVertex(quad[3], inverseWidth, inverseHeight),
			}};
			if (!batch.AddQuad(texture, vertices))
				return false;
		}

		return true;
	};

	const bool composed = appendQuads(outlineQuads) && appendQuads(primaryQuads);
	const bool submitted = composed && batch.Append(*globalRendering);
	if (!submitted)
		LOG_L(L_ERROR, "[CglVulkanFontRenderer::%s] Failed appending Vulkan font geometry", __func__);

	primaryQuads.clear();
	outlineQuads.clear();
	++submitCount;
}

void CglVulkanFontRenderer::GetStats(std::array<size_t, 8>& stats) const
{
	stats = {};
	stats[0] = primaryQuads.size() * 4;
	stats[1] = primaryQuads.size() * 6;
	stats[2] = submitCount;
	stats[4] = outlineQuads.size() * 4;
	stats[5] = outlineQuads.size() * 6;
	stats[6] = submitCount;
}
