/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace nv_dds
{
	class CSurface;
}

namespace Vulkan
{
	std::optional<std::vector<uint8_t>> DecodeDdsSurface(
		const nv_dds::CSurface& surface,
		uint32_t format,
		uint32_t components
	);
}
