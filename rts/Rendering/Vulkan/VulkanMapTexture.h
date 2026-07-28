/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstdint>
#include <optional>
#include <span>

class CBitmap;
class CGlobalRendering;

namespace Vulkan
{
	std::optional<uint32_t> CreateMapTexture(CGlobalRendering& rendering, CBitmap& bitmap);
	std::optional<uint32_t> CreateDxt1MapTexture(
		CGlobalRendering& rendering,
		std::span<const uint8_t> blocks,
		uint32_t width,
		uint32_t height
	);
}
