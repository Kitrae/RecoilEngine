/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstdint>

namespace Vulkan
{
	enum class TextureFormat : uint8_t
	{
		Rgba8Srgb,
		Rgba8Unorm,
		Rg32Float,
		R32Float,
	};
}
