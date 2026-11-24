#version 420 core
#include "attributes.glslh"
#include "Skinning.glslh"

uniform mat4 modelMatrix 	= mat4(1.0f);
uniform mat4 viewMatrix 	= mat4(1.0f);
uniform mat4 projMatrix 	= mat4(1.0f);
uniform mat4 shadowMatrix 	= mat4(1.0f);

uniform vec4 		objectColour = vec4(1,1,1,1);

uniform bool hasVertexColours = false;

uniform bool hasAnim = false;

out Vertex
{
	vec4 colour;
	vec2 texCoord;
	vec4 shadowProj;
	vec3 normal;
	vec3 worldPos;
} OUT;

void main(void)
{
	mat4 mvp 		  = (projMatrix * viewMatrix * modelMatrix);
	mat3 normalMatrix = transpose ( inverse ( mat3 ( modelMatrix )));

	vec3 pos	= position;
	vec3 norm	= normal;

	if(hasAnim) {
		SkinVertex(position, normal, pos, norm);
	}

	OUT.shadowProj 	=  shadowMatrix * vec4 ( pos,1);
	OUT.worldPos 	= ( modelMatrix * vec4 ( pos ,1)). xyz ;
	OUT.normal 		= normalize ( normalMatrix * normalize ( norm ));
	
	OUT.texCoord	= texCoord;
	OUT.colour		= objectColour;

	if(hasVertexColours) {
		OUT.colour		= objectColour * colour;
	}
	gl_Position		= mvp * vec4(pos, 1.0);
}