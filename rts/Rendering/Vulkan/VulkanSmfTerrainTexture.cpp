/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "VulkanSmfTerrainTexture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "lib/squish/squish.h"
#include "Game/GameSetup.h"
#include "Map/MapInfo.h"
#include "Map/SMF/SMFFormat.h"
#include "Map/SMF/SMFMapFile.h"
#include "Map/SMF/SMFReadMap.h"
#include "System/Exceptions.h"
#include "System/FileSystem/FileHandler.h"
#include "System/FileSystem/FileSystem.h"
#include "System/Platform/byteorder.h"

namespace Vulkan
{
	namespace
	{
		constexpr uint32_t TILE_SIZE = 32;
		constexpr uint32_t TILE_GUTTER = 1;
		constexpr uint32_t TILE_CELL_SIZE = TILE_SIZE + TILE_GUTTER * 2;
		constexpr std::size_t TILE_PIXEL_SIZE = TILE_SIZE * TILE_SIZE * 4;

		std::vector<uint8_t> LoadTileBlocks(CSMFReadMap& map, const MapTileHeader& tileHeader)
		{
			std::vector<uint8_t> tileBlocks(static_cast<std::size_t>(tileHeader.numTiles) * SMALL_TILE_SIZE);
			auto* mapFile = map.GetMapFile().GetFileHandler();
			const auto& smf = mapInfo->smf;
			const std::string smfDirectory = FileSystem::GetDirectory(gameSetup->MapFileName());
			const bool overrideNames =
				!smf.smtFileNames.empty() &&
				smf.smtFileNames.size() == static_cast<std::size_t>(tileHeader.numTileFiles);

			int currentTile = 0;
			for (int fileIndex = 0; fileIndex < tileHeader.numTileFiles; ++fileIndex) {
				int fileTileCount = 0;
				std::array<char, 256> fileName{};
				mapFile->Read(&fileTileCount, sizeof(fileTileCount));
				mapFile->ReadString(fileName.data(), static_cast<int>(fileName.size() - 1));
				swabDWordInPlace(fileTileCount);

				if (
					fileTileCount < 0 ||
					currentTile > tileHeader.numTiles - fileTileCount
				) {
					throw content_error("SMF tile-file counts exceed the declared tile count");
				}

				const std::string headerName = fileName.data();
				const std::string configuredName = overrideNames
					? smf.smtFileNames[fileIndex]
					: headerName;
				std::string path = smfDirectory + configuredName;
				CFileHandler tileFile(path);
				if (!tileFile.FileExists()) {
					path = configuredName;
					tileFile.Open(path);
				}

				auto* destination = tileBlocks.data() + static_cast<std::size_t>(currentTile) * SMALL_TILE_SIZE;
				if (!tileFile.FileExists()) {
					std::fill_n(
						destination,
						static_cast<std::size_t>(fileTileCount) * SMALL_TILE_SIZE,
						0xaa
					);
					currentTile += fileTileCount;
					continue;
				}

				TileFileHeader fileHeader{};
				CSMFMapFile::ReadMapTileFileHeader(fileHeader, tileFile);
				if (
					std::strcmp(fileHeader.magic, "spring tilefile") != 0 ||
					fileHeader.version != 1 ||
					fileHeader.tileSize != TILE_SIZE ||
					fileHeader.compressionType != 1 ||
					fileHeader.numTiles != fileTileCount
				) {
					throw content_error("SMT file header does not match its SMF tile declaration");
				}

				const int byteCount = fileTileCount * static_cast<int>(SMALL_TILE_SIZE);
				if (tileFile.Read(destination, byteCount) != byteCount)
					throw content_error("SMT tile data ended before the declared tile count");

				currentTile += fileTileCount;
			}

			if (currentTile != tileHeader.numTiles)
				throw content_error("SMF tile files do not provide the declared tile count");

			return tileBlocks;
		}

		void CopyTileToAtlas(
			const uint8_t* tilePixels,
			uint32_t tileIndex,
			SmfTerrainTextureData& texture
		) {
			const uint32_t cellX = (tileIndex % texture.atlasColumns) * TILE_CELL_SIZE;
			const uint32_t cellY = (tileIndex / texture.atlasColumns) * TILE_CELL_SIZE;

			for (uint32_t y = 0; y < TILE_CELL_SIZE; ++y) {
				const uint32_t sourceY = std::clamp(y, TILE_GUTTER, TILE_GUTTER + TILE_SIZE - 1) - TILE_GUTTER;
				for (uint32_t x = 0; x < TILE_CELL_SIZE; ++x) {
					const uint32_t sourceX = std::clamp(x, TILE_GUTTER, TILE_GUTTER + TILE_SIZE - 1) - TILE_GUTTER;
					const auto sourceOffset = (static_cast<std::size_t>(sourceY) * TILE_SIZE + sourceX) * 4;
					const auto destinationOffset =
						(static_cast<std::size_t>(cellY + y) * texture.atlasWidth + cellX + x) * 4;
					std::copy_n(
						tilePixels + sourceOffset,
						4,
						texture.atlasPixels.data() + destinationOffset
					);
				}
			}
		}
	}

	std::optional<SmfTerrainTextureData> LoadSmfTerrainTexture(
		CSMFReadMap& map,
		uint32_t maxTextureSize
	) {
		auto& smfFile = map.GetMapFile();
		auto* mapFile = smfFile.GetFileHandler();
		const auto& header = smfFile.GetHeader();
		mapFile->Seek(header.tilesPtr);

		MapTileHeader tileHeader{};
		CSMFMapFile::ReadMapTileHeader(tileHeader, *mapFile);
		if (
			tileHeader.numTiles <= 0 ||
			tileHeader.numTileFiles <= 0 ||
			map.tileMapSizeX <= 0 ||
			map.tileMapSizeY <= 0
		) {
			return std::nullopt;
		}

		const uint32_t maxCellsPerAxis = maxTextureSize / TILE_CELL_SIZE;
		if (maxCellsPerAxis == 0)
			return std::nullopt;

		const uint32_t tileCount = static_cast<uint32_t>(tileHeader.numTiles);
		const uint32_t squareColumns = static_cast<uint32_t>(std::ceil(std::sqrt(tileCount)));
		const uint32_t atlasColumns = std::min(squareColumns, maxCellsPerAxis);
		const uint32_t atlasRows = (tileCount + atlasColumns - 1) / atlasColumns;
		if (atlasRows > maxCellsPerAxis)
			return std::nullopt;

		auto tileBlocks = LoadTileBlocks(map, tileHeader);

		SmfTerrainTextureData texture;
		texture.atlasColumns = atlasColumns;
		texture.atlasRows = atlasRows;
		texture.atlasWidth = atlasColumns * TILE_CELL_SIZE;
		texture.atlasHeight = atlasRows * TILE_CELL_SIZE;
		texture.tileMapWidth = static_cast<uint32_t>(map.tileMapSizeX);
		texture.tileMapHeight = static_cast<uint32_t>(map.tileMapSizeY);
		texture.atlasPixels.resize(
			static_cast<std::size_t>(texture.atlasWidth) * texture.atlasHeight * 4
		);

		std::array<uint8_t, TILE_PIXEL_SIZE> tilePixels{};
		for (uint32_t tileIndex = 0; tileIndex < tileCount; ++tileIndex) {
			squish::DecompressImage(
				tilePixels.data(),
				TILE_SIZE,
				TILE_SIZE,
				tileBlocks.data() + static_cast<std::size_t>(tileIndex) * SMALL_TILE_SIZE,
				squish::kDxt1
			);
			CopyTileToAtlas(tilePixels.data(), tileIndex, texture);
		}
		tileBlocks.clear();
		tileBlocks.shrink_to_fit();

		const std::size_t mapTileCount =
			static_cast<std::size_t>(texture.tileMapWidth) * texture.tileMapHeight;
		if (mapTileCount > static_cast<std::size_t>(std::numeric_limits<int>::max()))
			return std::nullopt;

		std::vector<int> tileIndices(mapTileCount);
		if (mapFile->Read(tileIndices.data(), static_cast<int>(mapTileCount * sizeof(int))) != mapTileCount * sizeof(int))
			throw content_error("SMF tile map ended before the declared map dimensions");

		texture.tileIndices.resize(mapTileCount);
		for (std::size_t index = 0; index < mapTileCount; ++index) {
			swabDWordInPlace(tileIndices[index]);
			if (tileIndices[index] < 0 || tileIndices[index] >= tileHeader.numTiles)
				throw content_error("SMF tile map contains an invalid tile index");
			texture.tileIndices[index] = static_cast<float>(tileIndices[index]);
		}

		return texture;
	}
}
