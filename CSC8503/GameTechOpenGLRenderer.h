#pragma once
#ifdef USE_OPENGL
#include "GameTechRendererInterface.h"
#include "OGLRenderer.h"
#include "OGLShader.h"
#include "OGLTexture.h"
#include "OGLMesh.h"

#include "GameWorld.h"

namespace NCL {
	namespace CSC8503 {
		class RenderObject;
		class GameTechOpenGLRenderer;
		using ObjectDrawFunc = std::function<void(GameTechOpenGLRenderer*, const RenderObject*)>;

		class GameTechOpenGLRenderer : public OGLRenderer, public GameTechRendererInterface {
		public:
			GameTechOpenGLRenderer(GameWorld& world);
			~GameTechOpenGLRenderer();

			Mesh*		LoadMesh(const std::string& name) override;
			Texture*	LoadTexture(const std::string& name) override;


			bool LoadGLTF(const std::string& filename, NCL::Rendering::GLTFScene& intoScene) override;

			void RenderFrame(float dt)	override {
				Update(dt);
				Render();
			}

		protected:			
			Shader*		LoadShader(const std::string& vertex, const std::string& fragment);
			void DebugLinePass();
			void DebugTextPass();

			void RenderFrame()	override;

			OGLShader*		defaultShader;

			GameWorld&	gameWorld;

			void BuildObjectList();
			void SortObjectList();
			void ShadowMapPass();
			void SkyboxPass();

			void SceneDrawPass(const std::vector<const RenderObject*>& list, ObjectDrawFunc drawFunc, bool useBlend, bool useCull);

			void LoadSkybox();

			void SetDebugStringBufferSizes(size_t newVertCount);
			void SetDebugLineBufferSizes(size_t newVertCount);

			std::vector<const RenderObject*> activeOpaqueObjects;
			std::vector<const RenderObject*> activeTransparentObjects;


			static void OpaqueRenderObject(GameTechOpenGLRenderer* renderer, const RenderObject* o);
			static void TransparentRenderObject(GameTechOpenGLRenderer* renderer, const RenderObject* o);

			OGLShader*  debugShader;
			OGLShader*  skyboxShader;
			OGLMesh*	skyboxMesh;
			GLuint		skyboxTex;

			OGLShader*	sceneShader;

			OGLTexture* defaultTexture;

			//shadow mapping things
			OGLShader*	shadowShader;
			GLuint		shadowTex;
			GLuint		shadowFBO;
			Matrix4     shadowMatrix;



			//Debug data storage things
			std::vector<Vector3> debugLineData;

			std::vector<Vector3> debugTextPos;
			std::vector<Vector4> debugTextColours;
			std::vector<Vector2> debugTextUVs;

			GLuint lineVAO			= 0;
			GLuint lineVertVBO		= 0;
			size_t lineCount		= 0;

			GLuint textVAO			= 0;
			GLuint textVertVBO		= 0;
			GLuint textColourVBO	= 0;
			GLuint textTexVBO		= 0;
			size_t textCount		= 0;
		};
	}
}
#endif //USE_OPENGL
