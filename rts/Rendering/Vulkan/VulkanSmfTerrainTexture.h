/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

class CSMFReadMap;

namespace Vulkan
{
	struct SmfTerrainTextureData
	{
		std::vector<uint8_t> atlasPixels;
		std::vector<float> tileIndices;
		uint32_t atlasWidth = 0;
		uint32_t atlasHeight = 0;
		uint32_t atlasColumns = 0;
		uint32_t atlasRows = 0;
		uint32_t tileMapWidth = 0;
		uint32_t tileMapHeight = 0;
	};

	std::optional<SmfTerrainTextureData> LoadSmfTerrainTexture(
		CSMFReadMap& map,
		uint32_t maxTextureSize
	);
}
