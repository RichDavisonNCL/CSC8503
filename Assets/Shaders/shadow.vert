#version 420 core
#include "attributes.glslh"
#include "Skinning.glslh"

uniform mat4 mvpMatrix 		= mat4(1.0f);

uniform bool hasAnim = false;

void main(void)
{
	vec3 pos = position;
	vec3 normal = normal;

	if(hasAnim) {
		SkinVertex(position, normal, pos, normal);
	}

	gl_Position		= mvpMatrix * vec4(pos, 1.0);
}