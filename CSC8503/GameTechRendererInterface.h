#pragma once

namespace NCL::Rendering {
	class Mesh;
	class Texture;
	class Shader;
}

namespace NCL::CSC8503 {
	class GameTechRendererInterface
	{
	public:
		virtual NCL::Rendering::Mesh* LoadMesh(const std::string& name)			= 0;
		virtual NCL::Rendering::Texture* LoadTexture(const std::string& name)	= 0;

		virtual void RenderFrame(float dt) = 0;

		GameTechRendererInterface() = default;
		virtual ~GameTechRendererInterface() = default;

	protected:

	};
}

