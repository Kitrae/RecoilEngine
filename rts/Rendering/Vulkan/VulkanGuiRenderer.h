/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstdint>
#include <optional>

#include "System/Color.h"

class CBitmap;
class CGlobalRendering;

namespace Vulkan
{
	bool DrawGuiQuad(
		CGlobalRendering& rendering,
		float x0,
		float y0,
		float x1,
		float y1,
		const SColor& color
	);
	bool DrawGuiTexturedQuad(
		CGlobalRendering& rendering,
		uint32_t texture,
		float x0,
		float y0,
		float x1,
		float y1,
		const SColor& color = SColor::One
	);
	bool DrawGuiOutline(
		CGlobalRendering& rendering,
		float x0,
		float y0,
		float x1,
		float y1,
		const SColor& color,
		float width = 1.0f
	);
	std::optional<uint32_t> CreateGuiTexture(CGlobalRendering& rendering, const CBitmap& bitmap);
}
