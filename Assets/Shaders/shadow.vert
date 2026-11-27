#version 420 core
#include "attributes.glslh"
#include "Skinning.glslh"

uniform mat4 modelMatrix 	= mat4(1.0f);
uniform mat4 viewMatrix 	= mat4(1.0f);
uniform mat4 projMatrix 	= mat4(1.0f);
uniform mat4 shadowMatrix 	= mat4(1.0f);

uniform bool hasAnim = false;

void main(void)
{
	vec3 pos = position;
	vec3 normal = normal;

	if(hasAnim) {
		SkinVertex(position, normal, pos, normal);
	}
	mat4 mvp 		  = (projMatrix * viewMatrix * modelMatrix);

	gl_Position		= mvp * vec4(pos, 1.0);
}