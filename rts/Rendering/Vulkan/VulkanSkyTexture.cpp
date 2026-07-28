/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanSkyTexture.h"

#include <algorithm>
#include <vector>

#include "VulkanDdsTexture.h"
#include "VulkanTexture.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/Textures/Bitmap.h"

namespace Vulkan
{
	std::optional<uint32_t> CreateSkyTexture(CGlobalRendering& rendering, CBitmap& bitmap)
	{
		auto& image = bitmap.ddsimage;
		if (!bitmap.compressed || !image.is_valid() || image.get_type() != nv_dds::TextureCubemap)
			return std::nullopt;

		const uint32_t faceSize = image.get_width();
		if (faceSize == 0 || image.get_height() != faceSize)
			return std::nullopt;

		const std::size_t faceDataSize = static_cast<std::size_t>(faceSize) * faceSize * 4;
		std::vector<uint8_t> cubemapData(faceDataSize * 6);

		for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
			const auto& face = image.get_cubemap_face(faceIndex);
			if (face.get_width() != faceSize || face.get_height() != faceSize)
				return std::nullopt;

			auto pixels = DecodeDdsSurface(face, image.get_format(), image.get_components());
			if (!pixels.has_value() || pixels->size() != faceDataSize)
				return std::nullopt;

			std::copy(pixels->begin(), pixels->end(), cubemapData.begin() + faceIndex * faceDataSize);
		}

		return rendering.CreateVulkanCubemap(
			cubemapData.data(),
			cubemapData.size(),
			faceSize,
			TextureFormat::Rgba8Srgb
		);
	}
}
