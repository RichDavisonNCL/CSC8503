#ifdef USE_OPENGL
#include "GameTechOpenGLRenderer.h"
#include "GameObject.h"
#include "RenderObject.h"
#include "Camera.h"
#include "TextureLoader.h"
#include "MshLoader.h"

#include "Debug.h"

#include "../GLTFLoader/GLTFLoader.h"
#include "../CSC8503CoreClasses/GLTFRenderObject.h"

using namespace NCL;
using namespace Rendering;
using namespace CSC8503;

#define SHADOWSIZE 4096

const int _SUN_RADIUS = 1000.0f;

Matrix4 biasMatrix = Matrix::Translation(Vector3(0.5f, 0.5f, 0.5f)) * Matrix::Scale(Vector3(0.5f, 0.5f, 0.5f));

GameTechOpenGLRenderer::GameTechOpenGLRenderer(GameWorld& world) : OGLRenderer(*Window::GetWindow()), gameWorld(world)	{
	glEnable(GL_DEPTH_TEST);

	debugShader		= new OGLShader("debug.vert", "debug.frag");
	shadowShader	= new OGLShader("shadow.vert", "shadow.frag");
	sceneShader		= new OGLShader("scene.vert", "scene.frag");

	glGenTextures(1, &shadowTex);
	glBindTexture(GL_TEXTURE_2D, shadowTex);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT,
			     SHADOWSIZE, SHADOWSIZE, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
	
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenFramebuffers(1, &shadowFBO);
	glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D, shadowTex, 0);
	glDrawBuffer(GL_NONE);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glClearColor(1, 1, 1, 1);

	GLTFLoader::SetTextureConstructionFunction(
		[&](std::string& input) ->  SharedTexture {
			return std::shared_ptr<Texture>(OGLTexture::TextureFromFile(input));
		}
	);

	GLTFLoader::SetMeshConstructionFunction(
		[&]()-> std::shared_ptr<Mesh> {return std::shared_ptr<Mesh>(new OGLMesh()); }
	);

	//Skybox!
	skyboxShader = new OGLShader("skybox.vert", "skybox.frag");
	skyboxMesh = new OGLMesh();
	skyboxMesh->SetVertexPositions({Vector3(-1, 1,-1), Vector3(-1,-1,-1) , Vector3(1,-1,-1) , Vector3(1,1,-1) });
	skyboxMesh->SetVertexIndices({ 0,1,2,2,3,0 });
	skyboxMesh->UploadToGPU();

	LoadSkybox();

	glGenVertexArrays(1, &lineVAO);
	glGenVertexArrays(1, &textVAO);

	glGenBuffers(1, &lineVertVBO);
	glGenBuffers(1, &textVertVBO);
	glGenBuffers(1, &textColourVBO);
	glGenBuffers(1, &textTexVBO);

	SetDebugStringBufferSizes(10000);
	SetDebugLineBufferSizes(1000);
}

GameTechOpenGLRenderer::~GameTechOpenGLRenderer()	{
	glDeleteTextures(1, &shadowTex);
	glDeleteFramebuffers(1, &shadowFBO);
}

bool GameTechOpenGLRenderer::LoadGLTF(const std::string& filename, NCL::Rendering::GLTFScene& intoScene) {
	if (!GLTFLoader::Load(filename, intoScene))  {
		return false;
	}	

	for (const auto& m : intoScene.meshes) {
		m->UploadToGPU(this);
	}

	return true;
}

void GameTechOpenGLRenderer::LoadSkybox() {
	std::string filenames[6] = {
		"/Cubemap/skyrender0004.png",
		"/Cubemap/skyrender0001.png",
		"/Cubemap/skyrender0003.png",
		"/Cubemap/skyrender0006.png",
		"/Cubemap/skyrender0002.png",
		"/Cubemap/skyrender0005.png"
	};

	uint32_t width[6]		= { 0 };
	uint32_t height[6]		= { 0 };
	uint32_t channels[6]	= { 0 };
	uint32_t flags[6]		= { 0 };

	vector<char*> texData(6, nullptr);

	for (int i = 0; i < 6; ++i) {
		TextureLoader::LoadTexture(filenames[i], texData[i], width[i], height[i], channels[i], flags[i]);
		if (i > 0 && (width[i] != width[0] || height[0] != height[0])) {
			std::cout << __FUNCTION__ << " cubemap input textures don't match in size?\n";
			return;
		}
	}
	glGenTextures(1, &skyboxTex);
	glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxTex);

	GLenum type = channels[0] == 4 ? GL_RGBA : GL_RGB;

	for (int i = 0; i < 6; ++i) {
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, width[i], height[i], 0, type, GL_UNSIGNED_BYTE, texData[i]);
	}

	glTexParameterf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

void GameTechOpenGLRenderer::RenderFrame() {
	glEnable(GL_CULL_FACE);
	glClearColor(1, 1, 1, 1);
	BuildObjectList();
	SortObjectList();
	ShadowMapPass();
	SkyboxPass();

	SceneDrawPass(activeOpaqueObjects, &OpaqueRenderObject, false, true);
	SceneDrawPass(activeTransparentObjects, TransparentRenderObject, true, true);

	glDisable(GL_CULL_FACE); //Todo - text indices are going the wrong way...
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	DebugLinePass();
	DebugTextPass();
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void GameTechOpenGLRenderer::BuildObjectList() {
	activeOpaqueObjects.clear();
	activeTransparentObjects.clear();

	gameWorld.OperateOnContents(
		[&](GameObject* o) {
			if (o->IsActive()) {
				const RenderObject* g = o->GetRenderObject();

				GameTechMaterial m = g->GetMaterial();

				if (m.type == MaterialType::Opaque) {
					activeOpaqueObjects.emplace_back(g);
				}
				else {
					activeTransparentObjects.emplace_back(g);
				}
			}
		}
	);
}

void GameTechOpenGLRenderer::SortObjectList() {

}

void GameTechOpenGLRenderer::ShadowMapPass() {
	glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
	glClear(GL_DEPTH_BUFFER_BIT);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glViewport(0, 0, SHADOWSIZE, SHADOWSIZE);

	glCullFace(GL_FRONT);

	BindShader(*shadowShader);
	int mvpLocation = glGetUniformLocation(shadowShader->GetProgramID(), "mvpMatrix");

	Matrix4 shadowViewMatrix = Matrix::View(gameWorld.GetSunPosition(), Vector3(0, 0, 0), Vector3(0, 1, 0));
	Matrix4 shadowProjMatrix = Matrix::Perspective(100.0f, 500.0f, 1, 45.0f);

	Matrix4 mvMatrix = shadowProjMatrix * shadowViewMatrix;

	shadowMatrix = biasMatrix * mvMatrix; //we'll use this one later on

	for (const auto&i : activeOpaqueObjects) {
		Matrix4 modelMatrix = (*i).GetTransform().GetMatrix();
		Matrix4 mvpMatrix	= mvMatrix * modelMatrix;
		glUniformMatrix4fv(mvpLocation, 1, false, (float*)&mvpMatrix);

		OpaqueRenderObject(this, i);
	}

	glViewport(0, 0, windowSize.x, windowSize.y);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glCullFace(GL_BACK);
}

void GameTechOpenGLRenderer::SkyboxPass() {
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);

	float screenAspect = (float)windowSize.x / (float)windowSize.y;
	Matrix4 viewMatrix = gameWorld.GetMainCamera().BuildViewMatrix();
	Matrix4 projMatrix = gameWorld.GetMainCamera().BuildProjectionMatrix(screenAspect);

	BindShader(*skyboxShader);

	int projLocation = glGetUniformLocation(skyboxShader->GetProgramID(), "projMatrix");
	int viewLocation = glGetUniformLocation(skyboxShader->GetProgramID(), "viewMatrix");
	int texLocation  = glGetUniformLocation(skyboxShader->GetProgramID(), "cubeTex");

	glUniformMatrix4fv(projLocation, 1, false, (float*)&projMatrix);
	glUniformMatrix4fv(viewLocation, 1, false, (float*)&viewMatrix);

	glUniform1i(texLocation, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxTex);

	BindMesh(*skyboxMesh);
	DrawBoundMesh();

	glEnable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
}

void GameTechOpenGLRenderer::OpaqueRenderObject(GameTechOpenGLRenderer* renderer, const RenderObject* o) {
	if (const GLTFRenderObject* gltf = dynamic_cast<const GLTFRenderObject*>(o)) {
		//Oh dear. If you look very carefully, you'll be able to spot where I have hacked
		//in support for rendering GLTF files.
		GLTFScene* s = gltf->GetScene();

		for (auto& node : s->sceneNodes) {
			if (node.meshID >= 0) {
				OGLMesh* m = (OGLMesh*)s->meshes[node.meshID].get();
				//Absolutely fantastic...
				Matrix4 modelMat = o->GetTransform().GetMatrix() * node.worldMatrix;

				renderer->SendUniform("modelMatrix", modelMat);

				renderer->BindMesh(*m);
				int layerCount = m->GetSubMeshCount();

				for (int i = 0; i < layerCount; ++i) {
					auto mat = s->meshMaterials[node.meshID];

					auto matLayer = s->materials[mat.layers[i]];

					if (matLayer.albedo != -1) {
						OGLTexture* tex = (OGLTexture*)s->textures[matLayer.albedo].get();
						renderer->BindTextureToShader(*tex, "mainTex", 0);
					}

					renderer->SendUniform("hasTexture", matLayer.albedo >= 0 ? 1 : 0);

					renderer->DrawBoundMesh(i);
				}
			}
		}
	}
	else {
		renderer->BindMesh(*(OGLMesh*)(*o).GetMesh());
		int layerCount = (*o).GetMesh()->GetSubMeshCount();
		for (int i = 0; i < layerCount; ++i) {
			renderer->DrawBoundMesh(i);
		}
	}
}

void GameTechOpenGLRenderer::TransparentRenderObject(GameTechOpenGLRenderer* renderer, const RenderObject* o) {
	renderer->BindMesh(*(OGLMesh*)(*o).GetMesh());
	int layerCount = (*o).GetMesh()->GetSubMeshCount();

	glCullFace(GL_FRONT);
	for (int i = 0; i < layerCount; ++i) {
		renderer->DrawBoundMesh(i);
	}
	glCullFace(GL_BACK);
	for (int i = 0; i < layerCount; ++i) {
		renderer->DrawBoundMesh(i);
	}
}

void GameTechOpenGLRenderer::SceneDrawPass(const std::vector<const RenderObject*>& list, ObjectDrawFunc drawFunc, bool useBlend, bool useCull) {
	float screenAspect = (float)windowSize.x / (float)windowSize.y;
	Matrix4 viewMatrix = gameWorld.GetMainCamera().BuildViewMatrix();
	Matrix4 projMatrix = gameWorld.GetMainCamera().BuildProjectionMatrix(screenAspect);

	BindShader(*sceneShader);

	const int program = sceneShader->GetProgramID();

	const int projLocation = glGetUniformLocation(program, "projMatrix");
	const int viewLocation = glGetUniformLocation(program, "viewMatrix");
	const int modelLocation = glGetUniformLocation(program, "modelMatrix");
	const int shadowLocation = glGetUniformLocation(program, "shadowMatrix");
	const int colourLocation = glGetUniformLocation(program, "objectColour");
	const int hasVColLocation = glGetUniformLocation(program, "hasVertexColours");
	const int hasTexLocation = glGetUniformLocation(program, "hasTexture");

	const int lightPosLocation = glGetUniformLocation(program, "lightPos");
	const int lightColourLocation = glGetUniformLocation(program, "lightColour");
	const int lightRadiusLocation = glGetUniformLocation(program, "lightRadius");
	const int shadowTexLocation = glGetUniformLocation(program, "shadowTex");
	const int cameraLocation = glGetUniformLocation(program, "cameraPos");

	//TODO - PUT IN FUNCTION
	glActiveTexture(GL_TEXTURE0 + 1);
	glBindTexture(GL_TEXTURE_2D, shadowTex);
	glUniform1i(shadowTexLocation, 1);

	Vector3 camPos = gameWorld.GetMainCamera().GetPosition();
	glUniform3fv(cameraLocation, 1, camPos.array);

	glUniformMatrix4fv(projLocation, 1, false, (float*)&projMatrix);
	glUniformMatrix4fv(viewLocation, 1, false, (float*)&viewMatrix);

	Vector3 sunPos = gameWorld.GetSunPosition();
	Vector3 sunCol = gameWorld.GetSunColour();

	glUniform3fv(lightPosLocation, 1, (float*)&sunPos);
	glUniform4fv(lightColourLocation, 1, (float*)&sunCol);
	glUniform1f(lightRadiusLocation, _SUN_RADIUS);

	if (useBlend) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else {
		glDisable(GL_BLEND);
	}

	if (useCull) {
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
	}
	else {
		glDisable(GL_CULL_FACE);
	}

	for (const auto& i : list) {
		GameTechMaterial mat = i->GetMaterial();

		if (mat.diffuseTex) {
			BindTextureToShader(*(OGLTexture*)mat.diffuseTex, "mainTex", 0);
		}

		Matrix4 modelMatrix = (*i).GetTransform().GetMatrix();
		glUniformMatrix4fv(modelLocation, 1, false, (float*)&modelMatrix);

		Matrix4 fullShadowMat = shadowMatrix * modelMatrix;
		glUniformMatrix4fv(shadowLocation, 1, false, (float*)&fullShadowMat);

		Vector4 colour = i->GetColour();
		glUniform4fv(colourLocation, 1, colour.array);

		if ((*i).GetMesh()) {
			glUniform1i(hasVColLocation, !(*i).GetMesh()->GetColourData().empty());
		}
		else {
			glUniform1i(hasVColLocation, 0);
		}

		glUniform1i(hasTexLocation, mat.diffuseTex ? 1 : 0);

		drawFunc(this, i);
	}
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
}

Mesh* GameTechOpenGLRenderer::LoadMesh(const std::string& name) {
	OGLMesh* mesh = new OGLMesh();
	MshLoader::LoadMesh(name, *mesh);

	mesh->SetPrimitiveType(GeometryPrimitive::Triangles);
	mesh->UploadToGPU();
	return mesh;
}

void GameTechOpenGLRenderer::DebugLinePass() {
	const std::vector<Debug::DebugLineEntry>& lines = Debug::GetDebugLines();
	if (lines.empty()) {
		return;
	}
	float screenAspect = (float)windowSize.x / (float)windowSize.y;
	Matrix4 viewMatrix = gameWorld.GetMainCamera().BuildViewMatrix();
	Matrix4 projMatrix = gameWorld.GetMainCamera().BuildProjectionMatrix(screenAspect);
	
	Matrix4 viewProj  = projMatrix * viewMatrix;

	BindShader(*debugShader);
	int matSlot = glGetUniformLocation(debugShader->GetProgramID(), "viewProjMatrix");
	GLuint texSlot = glGetUniformLocation(debugShader->GetProgramID(), "useTexture");
	glUniform1i(texSlot, 0);

	glUniformMatrix4fv(matSlot, 1, false, (float*)viewProj.array);

	debugLineData.clear();

	int frameLineCount = lines.size() * 2;

	SetDebugLineBufferSizes(frameLineCount);

	glBindBuffer(GL_ARRAY_BUFFER, lineVertVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, frameLineCount * sizeof(Debug::DebugLineEntry), lines.data());
	

	glBindVertexArray(lineVAO);
	glDrawArrays(GL_LINES, 0, frameLineCount);
	glBindVertexArray(0);
}

void GameTechOpenGLRenderer::DebugTextPass() {
	const std::vector<Debug::DebugStringEntry>& strings = Debug::GetDebugStrings();
	if (strings.empty()) {
		return;
	}

	BindShader(*debugShader);

	OGLTexture* t = (OGLTexture*)Debug::GetDebugFont()->GetTexture();

	if (t) {
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, t->GetObjectID());
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glBindTexture(GL_TEXTURE_2D, 0);	
		BindTextureToShader(*t, "mainTex", 0);
	}
	Matrix4 proj = Matrix::Orthographic(0.0f, 100.0f, 100.0f, 0.0f, -1.0f, 1.0f);

	int matSlot = glGetUniformLocation(debugShader->GetProgramID(), "viewProjMatrix");
	glUniformMatrix4fv(matSlot, 1, false, (float*)proj.array);

	GLuint texSlot = glGetUniformLocation(debugShader->GetProgramID(), "useTexture");
	glUniform1i(texSlot, 1);

	debugTextPos.clear();
	debugTextColours.clear();
	debugTextUVs.clear();

	int frameVertCount = 0;
	for (const auto& s : strings) {
		frameVertCount += Debug::GetDebugFont()->GetVertexCountForString(s.data);
	}
	SetDebugStringBufferSizes(frameVertCount);

	for (const auto& s : strings) {
		float size = 20.0f;
		Debug::GetDebugFont()->BuildVerticesForString(s.data, s.position, s.colour, size, debugTextPos, debugTextUVs, debugTextColours);
	}

	glBindBuffer(GL_ARRAY_BUFFER, textVertVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, frameVertCount * sizeof(Vector3), debugTextPos.data());
	glBindBuffer(GL_ARRAY_BUFFER, textColourVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, frameVertCount * sizeof(Vector4), debugTextColours.data());
	glBindBuffer(GL_ARRAY_BUFFER, textTexVBO);
	glBufferSubData(GL_ARRAY_BUFFER, 0, frameVertCount * sizeof(Vector2), debugTextUVs.data());

	glBindVertexArray(textVAO);
	glDrawArrays(GL_TRIANGLES, 0, frameVertCount);
	glBindVertexArray(0);
}

Texture* GameTechOpenGLRenderer::LoadTexture(const std::string& name) {
	UniqueOGLTexture t = OGLTexture::TextureFromFile(name);

	return t.release();
}

Shader* GameTechOpenGLRenderer::LoadShader(const std::string& vertex, const std::string& fragment) {
	return new OGLShader(vertex, fragment);
}

void GameTechOpenGLRenderer::SetDebugStringBufferSizes(size_t newVertCount) {
	if (newVertCount > textCount) {
		textCount = newVertCount;

		glBindBuffer(GL_ARRAY_BUFFER, textVertVBO);
		glBufferData(GL_ARRAY_BUFFER, textCount * sizeof(Vector3), nullptr, GL_DYNAMIC_DRAW);

		glBindBuffer(GL_ARRAY_BUFFER, textColourVBO);
		glBufferData(GL_ARRAY_BUFFER, textCount * sizeof(Vector4), nullptr, GL_DYNAMIC_DRAW);

		glBindBuffer(GL_ARRAY_BUFFER, textTexVBO);
		glBufferData(GL_ARRAY_BUFFER, textCount * sizeof(Vector2), nullptr, GL_DYNAMIC_DRAW);

		debugTextPos.reserve(textCount);
		debugTextColours.reserve(textCount);
		debugTextUVs.reserve(textCount);

		glBindVertexArray(textVAO);

		glVertexAttribFormat(0, 3, GL_FLOAT, false, 0);
		glVertexAttribBinding(0, 0);
		glBindVertexBuffer(0, textVertVBO, 0, sizeof(Vector3));

		glVertexAttribFormat(1, 4, GL_FLOAT, false, 0);
		glVertexAttribBinding(1, 1);
		glBindVertexBuffer(1, textColourVBO, 0, sizeof(Vector4));

		glVertexAttribFormat(2, 2, GL_FLOAT, false, 0);
		glVertexAttribBinding(2, 2);
		glBindVertexBuffer(2, textTexVBO, 0, sizeof(Vector2));

		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glEnableVertexAttribArray(2);

		glBindVertexArray(0);
	}
}

void GameTechOpenGLRenderer::SetDebugLineBufferSizes(size_t newVertCount) {
	if (newVertCount > lineCount) {
		lineCount = newVertCount;

		glBindBuffer(GL_ARRAY_BUFFER, lineVertVBO);
		glBufferData(GL_ARRAY_BUFFER, lineCount * sizeof(Debug::DebugLineEntry), nullptr, GL_DYNAMIC_DRAW);

		debugLineData.reserve(lineCount);

		glBindVertexArray(lineVAO);

		int realStride = sizeof(Debug::DebugLineEntry) / 2;

		glVertexAttribFormat(0, 3, GL_FLOAT, false, offsetof(Debug::DebugLineEntry, start));
		glVertexAttribBinding(0, 0);
		glBindVertexBuffer(0, lineVertVBO, 0, realStride);

		glVertexAttribFormat(1, 4, GL_FLOAT, false, offsetof(Debug::DebugLineEntry, colourA));
		glVertexAttribBinding(1, 0);
		glBindVertexBuffer(1, lineVertVBO, sizeof(Vector4), realStride);

		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);

		glBindVertexArray(0);
	}
}

#endif //USE_OPENGL