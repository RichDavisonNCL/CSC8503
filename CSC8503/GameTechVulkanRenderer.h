#pragma once
#ifdef USE_VULKAN
#include "GameTechRendererInterface.h"

#include "../VKQuick/Instance.h"

#include "VulkanMesh.h"
#include "VulkanTexture.h"

#include "GameWorld.h"

namespace NCL::CSC8503 {
	class RenderObject;

	using namespace Vulkan;
	class GameTechVulkanRenderer : public GameTechRendererInterface
	{
	public:
		GameTechVulkanRenderer(GameWorld& world);
		~GameTechVulkanRenderer();

		NCL::Rendering::Mesh*		LoadMesh(const std::string& name)		override;
		NCL::Rendering::Texture*	LoadTexture(const std::string& name)	override;

		void RenderFrame(float dt) override;

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

		struct ObjectState {
			Matrix4 modelMatrix;
			Vector4 colour;
			int		index[4];
		};

		struct ObjectData {
			VulkanTexture* cachedTex;
			int index;
		};
	
		//class VulkanGameTechTexture : public NCL::Rendering::Vulkan::VulkanTexture
		//{
		//	friend class GameTechVulkanRenderer;
		//public:
		//	VulkanGameTechTexture(const std::string& filename) : VulkanTexture(filename) {
		//		index = 0;
		//	}
		//protected:
		//	int index;
		//};

		struct FrameData {
			vk::UniqueFence fence;

			VKQuick::Buffer	dataBuffer;
			char*			dataStart;	//Start of our buffer
			char*			data;		//We're bumping this along as we write data

			int globalDataOffset	= 0;	//Where does the global data start in the buffer?
			int objectStateOffset	= 0;	//Where does the object states start?
			int debugLinesOffset	= 0;	//Where do the debug lines start?
			int debugTextOffset		= 0;	//Where do the debug text verts start?

			size_t			objectCount;
			size_t			debugVertSize;
			size_t			bytesWritten;

			size_t lineVertCount = 0;
			size_t textVertCount = 0;

			size_t lineVertSize = 0;
			size_t textVertSize = 0;

			vk::UniqueDescriptorSet dataDescriptor;

			template<typename T>
			void WriteData(T value) {
				memcpy(data, &value, sizeof(T));
				data += sizeof(T);
				bytesWritten += sizeof(T);
			}
			void WriteData(void* inData, size_t byteCount) {
				memcpy(data, inData, byteCount);
				data += byteCount;
				bytesWritten += byteCount;
			}

			void AlignData(size_t alignment) {
				char* oldData = data;
				//data = (char*)(((uintptr_t)data + alignment - 1) & ~(uintptr_t)alignment);

				data = (char*)((((uintptr_t)data + alignment - 1) / (uintptr_t)alignment) * (uintptr_t)alignment);
				bytesWritten += data - oldData;
			}
		};

		FrameData* allFrames;
		FrameData* currentFrame;



		//void RenderFrame(float dt)	override;
		//	Update(dt);
		//	Render();
		//}

		VKQuick::UniqueTexture LoadCubemap(
			const std::string& negativeXFile, const std::string& positiveXFile,
			const std::string& negativeYFile, const std::string& positiveYFile,
			const std::string& negativeZFile, const std::string& positiveZFile,
			const std::string& debugName = "CubeMap");

		UniqueVulkanTexture LoadTextureInternal(const std::string& filename);


		void BuildScenePipelines(VulkanMesh* m);
		void BuildDebugPipelines();

		void UpdateObjectList();
		void UpdateDebugData();
		 
		void RenderSceneObjects(VKQuick::Pipeline& pipe, vk::CommandBuffer cmds);
		void RenderSkybox(vk::CommandBuffer cmds);
		void RenderDebugLines(vk::CommandBuffer cmds);
		void RenderDebugText(vk::CommandBuffer cmds);

		UniqueVulkanMesh GenerateQuad();

		void UploadMeshWait(VulkanMesh& m, vk::BufferUsageFlags bufferUsage = {});

		GameWorld& gameWorld;
		std::vector<const RenderObject*> activeObjects;
		int currentFrameIndex;

		VKQuick::Pipeline	skyboxPipeline;
		VKQuick::Pipeline	shadowPipeline;
		VKQuick::Pipeline	scenePipeline;
		VKQuick::Pipeline	debugLinePipeline;
		VKQuick::Pipeline	debugTextPipeline;

		UniqueVulkanMesh	quadMesh;

		VKQuick::UniqueTexture	cubeTex;
		VKQuick::UniqueTexture	shadowMap;

		UniqueVulkanTexture	defaultTexture;

		VKQuick::Instance*		vkQuickInstance;

		vk::Viewport	shadowViewport;
		vk::Rect2D		shadowScissor;

		vk::UniqueDescriptorSetLayout objectTextxureLayout;

		vk::UniqueDescriptorSet objectTextxureDescriptor;

		vk::UniqueSampler	defaultSampler;
		vk::UniqueSampler	textSampler;

		std::vector<UniqueVulkanTexture> loadedTextures;
	};
}
#endif //USE_VULKAN

