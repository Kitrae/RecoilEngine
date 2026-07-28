/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanMapTexture.h"

#include <vector>

#include "VulkanDdsTexture.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/Textures/Bitmap.h"
#include "lib/squish/squish.h"

namespace
{
	std::optional<uint32_t> CreateBlockCompressedMapTexture(
		CGlobalRendering& rendering,
		std::span<const uint8_t> blocks,
		uint32_t width,
		uint32_t height,
		int squishFormat
	) {
		nv_dds::CSurface surface(width, height, 1, static_cast<uint32_t>(blocks.size()), blocks.data());
		const uint32_t format =
			squishFormat == squish::kDxt1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT :
			squishFormat == squish::kDxt3 ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT :
			GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
		auto pixels = Vulkan::DecodeDdsSurface(surface, format, 4);
		if (!pixels.has_value())
			return std::nullopt;

		return rendering.CreateVulkanTextureRGBA8(pixels->data(), width, height);
	}

	std::optional<uint32_t> CreateDdsMapTexture(CGlobalRendering& rendering, CBitmap& bitmap)
	{
		auto& image = bitmap.ddsimage;
		if (!image.is_valid() || image.get_type() != nv_dds::TextureFlat)
			return std::nullopt;

		const uint32_t width = image.get_width();
		const uint32_t height = image.get_height();
		nv_dds::CSurface surface(
			width,
			height,
			1,
			image.get_size(),
			static_cast<unsigned char*>(image)
		);
		auto pixels = Vulkan::DecodeDdsSurface(
			surface,
			image.get_format(),
			image.get_components()
		);
		if (!pixels.has_value())
			return std::nullopt;

		return rendering.CreateVulkanTextureRGBA8(pixels->data(), width, height);
	}
}

namespace Vulkan
{
	std::optional<uint32_t> CreateMapTexture(CGlobalRendering& rendering, CBitmap& bitmap)
	{
		if (bitmap.compressed)
			return CreateDdsMapTexture(rendering, bitmap);

		if (
			bitmap.Empty() ||
			bitmap.channels != 4 ||
			bitmap.GetDataTypeSize() != 1 ||
			bitmap.xsize <= 0 ||
			bitmap.ysize <= 0
		) {
			return std::nullopt;
		}

		return rendering.CreateVulkanTextureRGBA8(
			bitmap.GetRawMem(),
			static_cast<uint32_t>(bitmap.xsize),
			static_cast<uint32_t>(bitmap.ysize)
		);
	}

	std::optional<uint32_t> CreateDxt1MapTexture(
		CGlobalRendering& rendering,
		std::span<const uint8_t> blocks,
		uint32_t width,
		uint32_t height
	) {
		return CreateBlockCompressedMapTexture(rendering, blocks, width, height, squish::kDxt1);
	}
}
