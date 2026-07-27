/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#version 450

layout(location = 0) in vec3 fragmentColor;
layout(location = 0) out vec4 outputColor;

void main()
{
	outputColor = vec4(fragmentColor, 1.0);
}
