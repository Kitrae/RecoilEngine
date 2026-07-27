/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#version 450

layout(binding = 0) uniform sampler2D sourceTexture;

layout(location = 0) in vec2 textureCoordinates;
layout(location = 1) in vec4 vertexColor;
layout(location = 0) out vec4 outputColor;

void main()
{
	outputColor = texture(sourceTexture, textureCoordinates) * vertexColor;
}
