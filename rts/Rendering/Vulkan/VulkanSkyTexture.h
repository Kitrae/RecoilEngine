/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstdint>
#include <optional>

class CBitmap;
class CGlobalRendering;

namespace Vulkan
{
	std::optional<uint32_t> CreateSkyTexture(CGlobalRendering& rendering, CBitmap& bitmap);
}
