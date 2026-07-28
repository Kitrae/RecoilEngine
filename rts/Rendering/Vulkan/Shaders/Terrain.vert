/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#version 450

layout(push_constant) uniform TerrainData
{
	mat4 viewProjection;
	uvec4 terrainInfo;
};

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 inputTextureCoordinates;

layout(location = 0) out vec2 textureCoordinates;
layout(location = 1) flat out uint tileIndex;

layout(binding = 1) uniform sampler2D heightMap;
layout(binding = 2) uniform sampler2D tileMap;

void main()
{
	const uint tileMapWidth = terrainInfo.x;
	const uint tileX = gl_InstanceIndex % tileMapWidth;
	const uint tileZ = gl_InstanceIndex / tileMapWidth;
	const ivec2 heightCoordinate = ivec2(
		tileX * 4 + uint(position.x),
		tileZ * 4 + uint(position.z)
	);
	const float height = texelFetch(heightMap, heightCoordinate, 0).r;
	const vec3 worldPosition = vec3(heightCoordinate.x * 8, height, heightCoordinate.y * 8);

	gl_Position = viewProjection * vec4(worldPosition, 1.0);
	gl_Position.y = -gl_Position.y;
	gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
	textureCoordinates = inputTextureCoordinates;
	tileIndex = uint(texelFetch(tileMap, ivec2(tileX, tileZ), 0).r + 0.5);
}
