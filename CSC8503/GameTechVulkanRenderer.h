#pragma once
#ifdef USEVULKAN
#include "VulkanRenderer.h"
#include "VulkanMesh.h"
#include "GameWorld.h"

namespace NCL::Rendering {
	class TextureBase;
	class ShaderBase;
	class GameTechVulkanRenderer : public VulkanRenderer
	{
	public:
		GameTechVulkanRenderer(GameWorld& world);
		~GameTechVulkanRenderer();

		MeshGeometry*	LoadMesh(const string& name);
		TextureBase*	LoadTexture(const string& name);
		ShaderBase*		LoadShader(const string& vertex, const string& fragment);

	protected:
		struct GlobalData {
			Matrix4 shadowMatrix;
			Matrix4 viewProjMatrix;
			Matrix4 viewMatrix;
			Matrix4 projMatrix;
			Matrix4 orthoMatrix;
			Vector3	lightPosition;
			float	lightRadius;
			Vector4	lightColour;
			Vector3	cameraPos;
		};

		struct ObjectData {
			VulkanTexture* cachedTex;
			int index;
		};

		struct FrameData {
			vk::UniqueFence fence;

			//This is the GPU-side list of all matrices of objects in the scene
			VulkanBuffer	matrixData;
			Matrix4*		matrixMemory;
			//Texture index of each object in the list
			VulkanBuffer	indexData;
			int*			indexMemory;

			VulkanBuffer	globalData;
			GlobalData*		globalDataMemory;

			VulkanBuffer	debugData;
			void*			debugDataMemory;

			int				objectCount;
			int				debugVertSize;

			vk::UniqueDescriptorSet dataDescriptor;
		};
		FrameData* allFrames;
		FrameData* currentFrame;

		void RenderFrame()	override;

		void BuildScenePipelines(VulkanMesh* m);
		void BuildDebugPipelines();

		void UpdateObjectList();
		void UpdateDebugData();
		 
		void RenderSceneObjects(VulkanPipeline& pipe, vk::CommandBuffer cmds);
		void RenderSkybox(vk::CommandBuffer cmds);
		void RenderDebugLines(vk::CommandBuffer cmds);
		void RenderDebugText(vk::CommandBuffer cmds);

		UniqueVulkanMesh GenerateQuad();

		GameWorld& gameWorld;
		vector<const RenderObject*> activeObjects;
		int currentFrameIndex;

		VulkanPipeline	skyboxPipeline;
		VulkanPipeline	shadowPipeline;
		VulkanPipeline	scenePipeline;
		VulkanPipeline	debugLinePipeline;
		VulkanPipeline	debugTextPipeline;

		UniqueVulkanShader skyboxShader;
		UniqueVulkanShader shadowShader;
		UniqueVulkanShader sceneShader;

		UniqueVulkanShader debugLineShader;
		UniqueVulkanShader debugTextShader;

		UniqueVulkanMesh	quadMesh;
		UniqueVulkanTexture	cubeTex;

		UniqueVulkanTexture shadowMap;
		UniqueVulkanTexture defaultTexture;
	
		vk::Viewport	shadowViewport;
		vk::Rect2D		shadowScissor;

		vk::UniqueDescriptorSetLayout globalDataLayout;
		vk::UniqueDescriptorSetLayout objectTextxureLayout;
		vk::UniqueDescriptorSet objectTextxureDescriptor;

		vk::UniqueSampler	defaultSampler;
		vk::UniqueSampler	textSampler;

		vector<VulkanTexture*> loadedTextures;

		int lineVertCount = 0;
		int textVertCount = 0;

		int lineVertOffset = 0;
		int textVertOffset = 0;
	};
}
#endif

