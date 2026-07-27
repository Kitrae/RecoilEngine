/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <array>
#include <memory>
#include <vector>

#include "Rendering/Fonts/glFontRenderer.h"

namespace Vulkan
{
	class TextRenderer;
}

class CglVulkanFontRenderer final : public CglFontRenderer
{
public:
	CglVulkanFontRenderer();
	~CglVulkanFontRenderer() override;

	void AddQuadTrianglesPB(VA_TYPE_TC&& tl, VA_TYPE_TC&& tr, VA_TYPE_TC&& br, VA_TYPE_TC&& bl) override;
	void AddQuadTrianglesOB(VA_TYPE_TC&& tl, VA_TYPE_TC&& tr, VA_TYPE_TC&& br, VA_TYPE_TC&& bl) override;
	void DrawTraingleElements() override;
	void HandleTextureUpdate(CFontTexture& font, bool onlyUpload) override;
	void PushGLState(const CglFont& font) override;
	void PopGLState(const CglFont& font) override;

	bool IsLegacy() const override { return false; }
	bool IsValid() const override { return true; }
	void GetStats(std::array<size_t, 8>& stats) const override;

private:
	using Quad = std::array<VA_TYPE_TC, 4>;

	std::vector<Quad> primaryQuads;
	std::vector<Quad> outlineQuads;
	std::unique_ptr<Vulkan::TextRenderer> textRenderer;
	size_t submitCount = 0;
};
