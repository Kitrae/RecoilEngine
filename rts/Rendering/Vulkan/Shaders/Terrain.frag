/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#version 450

layout(push_constant) uniform TerrainData
{
	mat4 viewProjection;
	uvec4 terrainInfo;
};

layout(binding = 0) uniform sampler2D tileAtlas;

layout(location = 0) in vec2 textureCoordinates;
layout(location = 1) flat in uint tileIndex;
layout(location = 0) out vec4 outputColor;

void main()
{
	const float tileSize = 32.0;
	const float cellSize = 34.0;
	const uint atlasColumns = terrainInfo.z;
	const vec2 cell = vec2(tileIndex % atlasColumns, tileIndex / atlasColumns);
	const vec2 atlasSize = textureSize(tileAtlas, 0);
	const vec2 atlasCoordinates =
		(cell * cellSize + vec2(1.0) + textureCoordinates * tileSize) /
		atlasSize;
	outputColor = vec4(texture(tileAtlas, atlasCoordinates).rgb, 1.0);
}
