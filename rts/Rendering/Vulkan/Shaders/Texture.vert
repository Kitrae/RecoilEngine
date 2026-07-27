/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 inputTextureCoordinates;
layout(location = 2) in vec4 color;

layout(location = 0) out vec2 textureCoordinates;
layout(location = 1) out vec4 vertexColor;

void main()
{
	const vec2 clipPosition = vec2(position.x * 2.0 - 1.0, 1.0 - position.y * 2.0);
	gl_Position = vec4(clipPosition, 0.0, 1.0);
	textureCoordinates = inputTextureCoordinates;
	vertexColor = color;
}
