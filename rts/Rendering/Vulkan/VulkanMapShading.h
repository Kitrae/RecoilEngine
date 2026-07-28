/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Vulkan
{
	struct MapShadingParameters
	{
		std::array<float, 3> ambientColor;
		std::array<float, 3> diffuseColor;
		std::array<float, 3> lightDirection;
		std::array<float, 3> waterBaseColor;
		std::array<float, 3> waterAbsorb;
		std::array<float, 3> waterMinColor;
		float waterLevel;
	};

	struct MapShadingTextures
	{
		std::vector<uint8_t> shading;
		std::vector<float> normals;
	};

	MapShadingTextures BuildMapShadingTextures(
		std::span<const float> heights,
		uint32_t width,
		uint32_t height,
		const MapShadingParameters& parameters
	);
}
