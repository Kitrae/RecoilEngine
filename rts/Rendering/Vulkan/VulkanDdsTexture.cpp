/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanDdsTexture.h"

#include <algorithm>
#include <array>
#include <span>

#include "lib/squish/squish.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/Textures/nv_dds.h"

namespace
{
	std::optional<std::vector<uint8_t>> DecodeBlockCompressedSurface(
		std::span<const uint8_t> blocks,
		uint32_t width,
		uint32_t height,
		int squishFormat,
		std::size_t blockSize
	) {
		const uint32_t blockColumns = (width + 3) / 4;
		const uint32_t blockRows = (height + 3) / 4;
		const std::size_t requiredSize = static_cast<std::size_t>(blockColumns) * blockRows * blockSize;
		if (blocks.size() < requiredSize)
			return std::nullopt;

		std::vector<uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
		std::array<squish::u8, 64> decodedBlock;

		for (uint32_t blockY = 0; blockY < blockRows; ++blockY) {
			for (uint32_t blockX = 0; blockX < blockColumns; ++blockX) {
				const std::size_t blockIndex = static_cast<std::size_t>(blockY) * blockColumns + blockX;
				squish::Decompress(
					decodedBlock.data(),
					blocks.data() + blockIndex * blockSize,
					squishFormat
				);

				for (uint32_t y = 0; y < 4 && blockY * 4 + y < height; ++y) {
					for (uint32_t x = 0; x < 4 && blockX * 4 + x < width; ++x) {
						const std::size_t source = (y * 4 + x) * 4;
						const std::size_t destination =
							(static_cast<std::size_t>(blockY * 4 + y) * width + blockX * 4 + x) * 4;
						std::copy_n(decodedBlock.data() + source, 4, pixels.data() + destination);
					}
				}
			}
		}

		return pixels;
	}
}

namespace Vulkan
{
	std::optional<std::vector<uint8_t>> DecodeDdsSurface(
		const nv_dds::CSurface& surface,
		uint32_t format,
		uint32_t components
	) {
		const uint32_t width = surface.get_width();
		const uint32_t height = surface.get_height();
		if (width == 0 || height == 0)
			return std::nullopt;

		const auto* data = static_cast<unsigned char*>(surface);
		const std::span<const uint8_t> source(data, surface.get_size());

		switch (format) {
			case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
				return DecodeBlockCompressedSurface(source, width, height, squish::kDxt1, 8);
			case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
				return DecodeBlockCompressedSurface(source, width, height, squish::kDxt3, 16);
			case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
				return DecodeBlockCompressedSurface(source, width, height, squish::kDxt5, 16);
			default:
				break;
		}

		if (
			(format != GL_BGRA && format != GL_BGR && format != GL_LUMINANCE) ||
			(components != 4 && components != 3 && components != 1)
		) {
			return std::nullopt;
		}

		const std::size_t rowStride = source.size() / height;
		if (rowStride < static_cast<std::size_t>(width) * components)
			return std::nullopt;

		std::vector<uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
		for (uint32_t y = 0; y < height; ++y) {
			for (uint32_t x = 0; x < width; ++x) {
				const auto* sourcePixel = source.data() + y * rowStride + x * components;
				auto* destinationPixel = pixels.data() + (static_cast<std::size_t>(y) * width + x) * 4;

				if (format == GL_LUMINANCE) {
					destinationPixel[0] = sourcePixel[0];
					destinationPixel[1] = sourcePixel[0];
					destinationPixel[2] = sourcePixel[0];
					destinationPixel[3] = 255;
					continue;
				}

				destinationPixel[0] = sourcePixel[2];
				destinationPixel[1] = sourcePixel[1];
				destinationPixel[2] = sourcePixel[0];
				destinationPixel[3] = components == 4 ? sourcePixel[3] : 255;
			}
		}

		return pixels;
	}
}
