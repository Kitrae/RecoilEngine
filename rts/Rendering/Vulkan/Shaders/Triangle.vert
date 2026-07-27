/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#version 450

layout(location = 0) out vec3 fragmentColor;

const vec2 POSITIONS[3] = vec2[](
	vec2( 0.0, -0.6),
	vec2( 0.6,  0.6),
	vec2(-0.6,  0.6)
);

const vec3 COLORS[3] = vec3[](
	vec3(0.94, 0.32, 0.24),
	vec3(0.24, 0.76, 0.42),
	vec3(0.20, 0.48, 0.92)
);

void main()
{
	gl_Position = vec4(POSITIONS[gl_VertexIndex], 0.0, 1.0);
	fragmentColor = COLORS[gl_VertexIndex];
}
