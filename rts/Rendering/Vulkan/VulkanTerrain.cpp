/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanTerrain.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

#include "VulkanSmfTerrainTexture.h"
#include "VulkanTerrainRenderer.h"
#include "VulkanTexture.h"
#include "Game/Camera.h"
#include "Map/ReadMap.h"
#include "Map/SMF/SMFReadMap.h"
#include "Rendering/GlobalRendering.h"

namespace Vulkan
{
	namespace
	{
		constexpr uint32_t SQUARES_PER_TILE = 4;
		constexpr uint32_t VERTICES_PER_TILE_EDGE = SQUARES_PER_TILE + 1;
		constexpr uint32_t INVALID_TEXTURE = std::numeric_limits<uint32_t>::max();

		uint32_t terrainAtlas = INVALID_TEXTURE;
		uint32_t terrainTileMap = INVALID_TEXTURE;

		void DestroyTerrainTextures(CGlobalRendering& rendering)
		{
			if (terrainAtlas != INVALID_TEXTURE)
				rendering.DestroyVulkanTexture(terrainAtlas);
			if (terrainTileMap != INVALID_TEXTURE)
				rendering.DestroyVulkanTexture(terrainTileMap);

			terrainAtlas = INVALID_TEXTURE;
			terrainTileMap = INVALID_TEXTURE;
		}

		std::vector<TerrainVertex> CreateTileVertices()
		{
			std::vector<TerrainVertex> vertices;
			vertices.reserve(VERTICES_PER_TILE_EDGE * VERTICES_PER_TILE_EDGE);
			for (uint32_t z = 0; z < VERTICES_PER_TILE_EDGE; ++z) {
				for (uint32_t x = 0; x < VERTICES_PER_TILE_EDGE; ++x) {
					vertices.push_back({
						{
							static_cast<float>(x),
							0.0f,
							static_cast<float>(z),
						},
						{
							static_cast<float>(x) / SQUARES_PER_TILE,
							static_cast<float>(z) / SQUARES_PER_TILE,
						},
					});
				}
			}
			return vertices;
		}

		std::vector<uint32_t> CreateTileIndices()
		{
			std::vector<uint32_t> indices;
			indices.reserve(SQUARES_PER_TILE * SQUARES_PER_TILE * 6);
			for (uint32_t z = 0; z < SQUARES_PER_TILE; ++z) {
				for (uint32_t x = 0; x < SQUARES_PER_TILE; ++x) {
					const uint32_t topLeft = z * VERTICES_PER_TILE_EDGE + x;
					const uint32_t bottomLeft = topLeft + VERTICES_PER_TILE_EDGE;
					indices.insert(indices.end(), {
						topLeft,
						bottomLeft,
						topLeft + 1,
						topLeft + 1,
						bottomLeft,
						bottomLeft + 1,
					});
				}
			}
			return indices;
		}
	}

	bool InitializeTerrain(CGlobalRendering& rendering, CReadMap& map)
	{
		auto* smfMap = dynamic_cast<CSMFReadMap*>(&map);
		if (smfMap == nullptr || rendering.maxTextureSize <= 0)
			return false;

		const auto texture = LoadSmfTerrainTexture(
			*smfMap,
			static_cast<uint32_t>(rendering.maxTextureSize)
		);
		if (!texture.has_value())
			return false;

		const auto atlas = rendering.CreateVulkanTexture(
			texture->atlasPixels.data(),
			texture->atlasPixels.size(),
			texture->atlasWidth,
			texture->atlasHeight,
			TextureFormat::Rgba8Srgb
		);
		if (!atlas.has_value())
			return false;
		terrainAtlas = atlas.value();

		const auto tileMap = rendering.CreateVulkanTexture(
			texture->tileIndices.data(),
			texture->tileIndices.size() * sizeof(float),
			texture->tileMapWidth,
			texture->tileMapHeight,
			TextureFormat::R32Float
		);
		if (!tileMap.has_value()) {
			DestroyTerrainTextures(rendering);
			return false;
		}
		terrainTileMap = tileMap.value();

		const auto vertices = CreateTileVertices();
		const auto indices = CreateTileIndices();
		const uint32_t instanceCount = texture->tileMapWidth * texture->tileMapHeight;
		const std::array<uint32_t, 3> textures{
			terrainAtlas,
			map.GetHeightMapTexture(),
			terrainTileMap,
		};
		const std::array<uint32_t, 4> terrainInfo{
			texture->tileMapWidth,
			texture->tileMapHeight,
			texture->atlasColumns,
			texture->atlasRows,
		};

		if (rendering.SetVulkanTerrain(
			vertices,
			indices,
			textures,
			instanceCount,
			terrainInfo
		)) {
			return true;
		}

		DestroyTerrainTextures(rendering);
		return false;
	}

	void DrawTerrain(CGlobalRendering& rendering, const CCamera& activeCamera)
	{
		std::array<float, 16> viewProjection{};
		const auto& matrix = activeCamera.GetViewProjectionMatrix();
		std::copy(std::begin(matrix.m), std::end(matrix.m), viewProjection.begin());
		rendering.SetVulkanTerrainTransform(viewProjection);
	}

	void KillTerrain(CGlobalRendering& rendering)
	{
		rendering.ClearVulkanTerrain();
		DestroyTerrainTextures(rendering);
	}
}
