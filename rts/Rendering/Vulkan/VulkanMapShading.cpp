/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanMapShading.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
	struct Vec3
	{
		float x;
		float y;
		float z;
	};

	Vec3 operator-(const Vec3& lhs, const Vec3& rhs)
	{
		return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
	}

	Vec3& operator+=(Vec3& lhs, const Vec3& rhs)
	{
		lhs.x += rhs.x;
		lhs.y += rhs.y;
		lhs.z += rhs.z;
		return lhs;
	}

	Vec3 Cross(const Vec3& lhs, const Vec3& rhs)
	{
		return {
			lhs.y * rhs.z - lhs.z * rhs.y,
			lhs.z * rhs.x - lhs.x * rhs.z,
			lhs.x * rhs.y - lhs.y * rhs.x,
		};
	}

	Vec3 Normalize(const Vec3& value)
	{
		const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z;
		if (lengthSquared <= 0.0f)
			return {0.0f, 1.0f, 0.0f};

		const float inverseLength = 1.0f / std::sqrt(lengthSquared);
		return {value.x * inverseLength, value.y * inverseLength, value.z * inverseLength};
	}

	float Dot(const Vec3& lhs, const std::array<float, 3>& rhs)
	{
		return lhs.x * rhs[0] + lhs.y * rhs[1] + lhs.z * rhs[2];
	}

	uint8_t ToByte(float value)
	{
		return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
	}
}

namespace Vulkan
{
	MapShadingTextures BuildMapShadingTextures(
		std::span<const float> heights,
		uint32_t width,
		uint32_t height,
		const MapShadingParameters& parameters
	) {
		MapShadingTextures textures;
		const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
		if (width == 0 || height == 0 || heights.size() < pixelCount)
			return textures;

		textures.shading.resize(pixelCount * 4);
		textures.normals.resize(pixelCount * 2);

		const auto getVertex = [&](int x, int y) {
			const uint32_t clampedX = static_cast<uint32_t>(std::clamp(x, 0, static_cast<int>(width) - 1));
			const uint32_t clampedY = static_cast<uint32_t>(std::clamp(y, 0, static_cast<int>(height) - 1));
			return Vec3{
				clampedX * 8.0f,
				heights[static_cast<std::size_t>(clampedY) * width + clampedX],
				clampedY * 8.0f,
			};
		};

		constexpr float intensityMultiplier = 210.0f / 255.0f;
		for (uint32_t y = 0; y < height; ++y) {
			for (uint32_t x = 0; x < width; ++x) {
				const int pixelX = static_cast<int>(x);
				const int pixelY = static_cast<int>(y);
				const Vec3 center = getVertex(pixelX, pixelY);
				const Vec3 bottomLeft = getVertex(pixelX - 1, pixelY - 1) - center;
				const Vec3 bottomMiddle = getVertex(pixelX, pixelY - 1) - center;
				const Vec3 bottomRight = getVertex(pixelX + 1, pixelY - 1) - center;
				const Vec3 middleLeft = getVertex(pixelX - 1, pixelY) - center;
				const Vec3 middleRight = getVertex(pixelX + 1, pixelY) - center;
				const Vec3 topLeft = getVertex(pixelX - 1, pixelY + 1) - center;
				const Vec3 topMiddle = getVertex(pixelX, pixelY + 1) - center;
				const Vec3 topRight = getVertex(pixelX + 1, pixelY + 1) - center;

				Vec3 normal{};
				normal += Cross(topRight, middleRight);
				normal += Cross(middleRight, bottomRight);
				normal += Cross(bottomRight, bottomMiddle);
				normal += Cross(bottomMiddle, bottomLeft);
				normal += Cross(bottomLeft, middleLeft);
				normal += Cross(middleLeft, topLeft);
				normal += Cross(topLeft, topMiddle);
				normal += Cross(topMiddle, topRight);
				normal = Normalize(normal);

				const std::size_t pixel = static_cast<std::size_t>(y) * width + x;
				textures.normals[pixel * 2 + 0] = normal.x;
				textures.normals[pixel * 2 + 1] = normal.z;

				const float positiveNdotL = std::max(Dot(normal, parameters.lightDirection), 0.0f);
				std::array<float, 3> lightColor;
				for (std::size_t channel = 0; channel < lightColor.size(); ++channel) {
					lightColor[channel] = std::min(
						(parameters.ambientColor[channel] + parameters.diffuseColor[channel] * positiveNdotL) *
							intensityMultiplier,
						1.0f
					);
				}

				std::array<float, 3> shadingColor = lightColor;
				float alpha = 1.0f;
				const float mapHeight = heights[pixel];
				if (mapHeight < parameters.waterLevel) {
					const float relativeHeight = std::clamp(parameters.waterLevel - mapHeight, 0.0f, 1024.0f);
					float lightIntensity = std::min((positiveNdotL + 0.2f) * 2.0f, 1.0f);

					for (std::size_t channel = 0; channel < shadingColor.size(); ++channel) {
						const float waterColor = std::clamp(
							parameters.waterBaseColor[channel] - parameters.waterAbsorb[channel] * relativeHeight,
							parameters.waterMinColor[channel],
							1.0f
						);

						if (relativeHeight < 10.0f) {
							const float waterMix = relativeHeight * 0.1f;
							shadingColor[channel] =
								waterColor * (lightIntensity * waterMix) +
								lightColor[channel] * (1.0f - waterMix);
						} else {
							shadingColor[channel] = waterColor * lightIntensity;
						}
					}

					alpha = std::clamp((255.0f - 10.0f * relativeHeight) / 255.0f, 0.0f, 1.0f);
				}

				textures.shading[pixel * 4 + 0] = ToByte(shadingColor[0]);
				textures.shading[pixel * 4 + 1] = ToByte(shadingColor[1]);
				textures.shading[pixel * 4 + 2] = ToByte(shadingColor[2]);
				textures.shading[pixel * 4 + 3] = ToByte(alpha);
			}
		}

		return textures;
	}
}
