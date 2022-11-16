#include "GameTechVulkanRenderer.h"
#include "GameObject.h"
#include "RenderObject.h"
#include "Camera.h"
#include "VulkanUtils.h"
#ifdef USEVULKAN
using namespace NCL;
using namespace Rendering;
using namespace CSC8503;

const int _SHADOWSIZE = 8192;
const int _FRAMECOUNT = 3;

const int _TEXCOUNT = 128;

const uint32_t lineStride = sizeof(Vector4) + sizeof(Vector4);
const uint32_t textStride = sizeof(Vector2) + sizeof(Vector4) + sizeof(Vector2);

GameTechVulkanRenderer::GameTechVulkanRenderer(GameWorld& world) : VulkanRenderer(*Window::GetWindow()), gameWorld(world) {
	globalDataLayout = VulkanDescriptorSetLayoutBuilder("Global Data Layout")
		.WithUniformBuffers(1)	//0: The global data uniform buffer
		.WithStorageBuffers(1)	//1: The list of model matrices
		.WithStorageBuffers(1)  //2: The list of object indices	
		.WithSamplers(1)		//3: The scene shadow map
		.WithSamplers(1)		//4: The scene cubemap

		.Build(device);

	objectTextxureLayout = VulkanDescriptorSetLayoutBuilder("Object Texture Layout")
		.WithSamplers(_TEXCOUNT)		//0: Object textures live here
		.Build(device);

	objectTextxureDescriptor = BuildUniqueDescriptorSet(*objectTextxureLayout);

	defaultTexture = VulkanTexture::TextureFromFile("Default.png");

	defaultSampler = GetDevice().createSamplerUnique(
		vk::SamplerCreateInfo()
		.setAnisotropyEnable(false)
		.setMaxAnisotropy(16)
		.setMinFilter(vk::Filter::eLinear)
		.setMagFilter(vk::Filter::eLinear)
		.setMipmapMode(vk::SamplerMipmapMode::eLinear)
		.setMaxLod(80.0f)
	);
	for (int i = 0; i < _TEXCOUNT; ++i) {
		UpdateImageDescriptor(*objectTextxureDescriptor, 0, i, defaultTexture->GetDefaultView(), *defaultSampler);
	}

	textSampler = GetDevice().createSamplerUnique(
		vk::SamplerCreateInfo()
		.setAnisotropyEnable(false)
		.setMaxAnisotropy(1)
		.setMinFilter(vk::Filter::eNearest)
		.setMagFilter(vk::Filter::eNearest)
		.setMipmapMode(vk::SamplerMipmapMode::eNearest)
		.setMaxLod(0.0f)
	);

	{//Setting up the data for the skybox
		quadMesh = GenerateQuad();

		cubeTex = VulkanTexture::VulkanCubemapFromFiles(
			"Cubemap/skyrender0004.png", "Cubemap/skyrender0001.png",
			"Cubemap/skyrender0003.png", "Cubemap/skyrender0006.png",
			"Cubemap/skyrender0002.png", "Cubemap/skyrender0005.png",
			"Cubemap Texture!"
		);

		skyboxShader = VulkanShaderBuilder("Basic Shader!")
			.WithVertexBinary("skybox.vert.spv")
			.WithFragmentBinary("skybox.frag.spv")
			.Build(device);

		skyboxPipeline = VulkanPipelineBuilder("CubeMapRenderer Skybox Pipeline")
			.WithVertexInputState(quadMesh->GetVertexInputState())
			.WithTopology(quadMesh->GetVulkanTopology())
			.WithShader(skyboxShader)
			.WithDepthState(vk::CompareOp::eAlways, true, false, false)
			.WithColourFormats({ surfaceFormat })
			.WithDepthStencilFormat(depthBuffer->GetFormat())
			.WithDescriptorSetLayout(*globalDataLayout) //Set 0
			.Build(device, pipelineCache);
	}
	 
	{//Setting up all the data we need for shadow maps!
		shadowShader = VulkanShaderBuilder("Shadow Fill Shader")
			.WithVertexBinary("Shadow.vert.spv")
			.WithFragmentBinary("Shadow.frag.spv")
			.Build(device);

		shadowMap = VulkanTexture::CreateDepthTexture(_SHADOWSIZE, _SHADOWSIZE, "Shadow Map", false);

		shadowViewport = vk::Viewport(0.0f, (float)_SHADOWSIZE, (float)_SHADOWSIZE, -(float)_SHADOWSIZE, 0.0f, 1.0f);
		shadowScissor = vk::Rect2D(vk::Offset2D(0, 0), vk::Extent2D(_SHADOWSIZE, _SHADOWSIZE));
	}
	{//
		sceneShader = VulkanShaderBuilder("Scene Shader")
			.WithVertexBinary("Scene.vert.spv")
			.WithFragmentBinary("Scene.frag.spv")
			.Build(device);
	}
	{//Data structures used by the debug drawing
		BuildDebugPipelines();
	}
	//We're going to have 3 frames in-flight at once, with their own per-frame data

	allFrames = new FrameData[_FRAMECOUNT];
	for (int i = 0; i < _FRAMECOUNT; ++i) {
		allFrames[i].fence = device.createFenceUnique({});

		{//We store scene object matrices in a big UBO
			allFrames[i].debugVertSize = 10000;
			allFrames[i].objectCount = 1024;
			allFrames[i].matrixData = CreateBuffer(sizeof(Matrix4) * allFrames[i].objectCount, vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
			allFrames[i].matrixMemory = (Matrix4*)device.mapMemory(*allFrames[i].matrixData.deviceMem, 0, allFrames[i].matrixData.allocInfo.allocationSize);

			allFrames[i].indexData = CreateBuffer(sizeof(int) * allFrames[i].objectCount, vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
			allFrames[i].indexMemory = (int*)device.mapMemory(*allFrames[i].indexData.deviceMem, 0, allFrames[i].indexData.allocInfo.allocationSize);
			
			allFrames[i].globalData = CreateBuffer(sizeof(GlobalData), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
			allFrames[i].globalDataMemory = (GlobalData*)device.mapMemory(*allFrames[i].globalData.deviceMem, 0, allFrames[i].globalData.allocInfo.allocationSize);

			allFrames[i].debugData = CreateBuffer(sizeof(GlobalData), vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
			allFrames[i].debugDataMemory = device.mapMemory(*allFrames[i].debugData.deviceMem, 0, allFrames[i].debugData.allocInfo.allocationSize);

			allFrames[i].dataDescriptor = BuildUniqueDescriptorSet(*globalDataLayout);

			Vulkan::SetDebugName(device, vk::ObjectType::eBuffer, Vulkan::GetVulkanHandle(*allFrames[i].matrixData.buffer), "Object Matrix Data");
			Vulkan::SetDebugName(device, vk::ObjectType::eBuffer, Vulkan::GetVulkanHandle(*allFrames[i].indexData.buffer), "Object Index Data");
			Vulkan::SetDebugName(device, vk::ObjectType::eBuffer, Vulkan::GetVulkanHandle(*allFrames[i].globalData.buffer), "Global Frame Data");
			Vulkan::SetDebugName(device, vk::ObjectType::eBuffer, Vulkan::GetVulkanHandle(*allFrames[i].debugData.buffer), "Frame Debug Data");

			UpdateBufferDescriptor(*allFrames[i].dataDescriptor, allFrames[i].globalData, 0, vk::DescriptorType::eUniformBuffer);
			UpdateBufferDescriptor(*allFrames[i].dataDescriptor, allFrames[i].matrixData, 1, vk::DescriptorType::eStorageBuffer);
			UpdateBufferDescriptor(*allFrames[i].dataDescriptor, allFrames[i].indexData , 2, vk::DescriptorType::eStorageBuffer);
			UpdateImageDescriptor(*allFrames[i].dataDescriptor, 3, 0, shadowMap->GetDefaultView(), *defaultSampler, vk::ImageLayout::eDepthStencilReadOnlyOptimal);
			UpdateImageDescriptor(*allFrames[i].dataDescriptor, 4, 0, cubeTex->GetDefaultView(), *defaultSampler);
		}
	}
	currentFrameIndex = 0;
	currentFrame = &allFrames[currentFrameIndex];
}

GameTechVulkanRenderer::~GameTechVulkanRenderer() {

}
//Defer making the scene pipelines until a mesh has been loaded
//We're assuming that all loaded meshes have the same vertex format...
void GameTechVulkanRenderer::BuildScenePipelines(VulkanMesh* m) {
	scenePipeline = VulkanPipelineBuilder("Main Scene Pipeline")
		.WithVertexInputState(m->GetVertexInputState())
		.WithTopology(vk::PrimitiveTopology::eTriangleList)
		.WithShader(sceneShader)
		.WithBlendState(vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, false)
		.WithDepthState(vk::CompareOp::eLessOrEqual, true, true, false)
		.WithColourFormats({ surfaceFormat })
		.WithDepthStencilFormat(depthBuffer->GetFormat())
		.WithDescriptorSetLayout(*globalDataLayout)
		.WithDescriptorSetLayout(*objectTextxureLayout) //Set 1
		.WithPushConstant(vk::ShaderStageFlagBits::eVertex, 0, sizeof(Matrix4))
		.Build(device, pipelineCache);

	shadowPipeline = VulkanPipelineBuilder("Shadow Map Pipeline")
		.WithVertexInputState(m->GetVertexInputState())
		.WithTopology(vk::PrimitiveTopology::eTriangleList)
		.WithShader(shadowShader)
		.WithPushConstant(vk::ShaderStageFlagBits::eVertex, 0, sizeof(Matrix4))
		.WithDescriptorSetLayout(*globalDataLayout)
		.WithDescriptorSetLayout(*objectTextxureLayout) //Set 1
		.WithDepthState(vk::CompareOp::eLessOrEqual, true, true)
		.WithDepthFormat(shadowMap->GetFormat())
		.WithRaster(vk::CullModeFlagBits::eFront)
		.Build(device, pipelineCache);
}

void GameTechVulkanRenderer::BuildDebugPipelines() {
	debugLineShader = VulkanShaderBuilder("Debug Line Shader")
		.WithVertexBinary("Debug.vert.spv")
		.WithFragmentBinary("Debug.frag.spv")
		.Build(device);

	debugTextShader = VulkanShaderBuilder("Debug Text Shader")
		.WithVertexBinary("DebugText.vert.spv")
		.WithFragmentBinary("DebugText.frag.spv")
		.Build(device);

	vk::VertexInputAttributeDescription debugLineVertAttribs[2] = {
		//Shader input, binding, format, offset
		{0,0,vk::Format::eR32G32B32A32Sfloat, 0},
		{1,1,vk::Format::eR32G32B32A32Sfloat, 0},
	};

	vk::VertexInputBindingDescription debugLineVertBindings[2] = {
		//Binding, stride, input rate
		{0, lineStride, vk::VertexInputRate::eVertex},//Positions
		{1, lineStride, vk::VertexInputRate::eVertex},//Colours
	};

	vk::VertexInputAttributeDescription debugTextVertAttribs[3] = {
		//Shader input, binding, format, offset
		{0,0,vk::Format::eR32G32Sfloat		, 0},
		{1,1,vk::Format::eR32G32Sfloat		, 0},
		{2,2,vk::Format::eR32G32B32A32Sfloat, 0},
	};

	vk::VertexInputBindingDescription debugTextVertBindings[3] = {
		//Binding, stride, input rate
		{0, textStride, vk::VertexInputRate::eVertex},//Positions
		{1, textStride, vk::VertexInputRate::eVertex},//Tex coords
		{2, textStride, vk::VertexInputRate::eVertex},//Colours
	};

	vk::PipelineVertexInputStateCreateInfo lineVertexState({}, debugLineVertBindings, debugLineVertAttribs);
	vk::PipelineVertexInputStateCreateInfo textVertexState({}, debugTextVertBindings, debugTextVertAttribs);

	debugLinePipeline = VulkanPipelineBuilder("Debug Line Pipeline")
		.WithVertexInputState(lineVertexState)
		.WithTopology(vk::PrimitiveTopology::eLineList)
		.WithShader(debugLineShader)
		//.WithDepthState(vk::CompareOp::eLessOrEqual, true, true, false)
		.WithColourFormats({ surfaceFormat })
		.WithDepthStencilFormat(depthBuffer->GetFormat())
		.WithDescriptorSetLayout(*globalDataLayout)		//Set 0
		.WithDescriptorSetLayout(*objectTextxureLayout) //Set 1
		.Build(device, pipelineCache);

	debugTextPipeline = VulkanPipelineBuilder("Debug Text Pipeline")
		.WithVertexInputState(textVertexState)
		.WithTopology(vk::PrimitiveTopology::eTriangleList)
		.WithShader(debugTextShader)
		.WithColourFormats({ surfaceFormat })
		.WithDepthStencilFormat(depthBuffer->GetFormat())
		.WithDescriptorSetLayout(*globalDataLayout)		//Set 0
		.WithDescriptorSetLayout(*objectTextxureLayout) //Set 1
		.WithPushConstant(vk::ShaderStageFlagBits::eFragment, 0, sizeof(int)) //TexID to use
		.Build(device, pipelineCache);
}

void GameTechVulkanRenderer::RenderFrame() {
	TransitionSwapchainForRendering(defaultCmdBuffer);

	int texID = _TEXCOUNT - 1;
	VulkanTexture* tex = (VulkanTexture*)Debug::GetDebugFont()->GetTexture();
	UpdateImageDescriptor(*objectTextxureDescriptor, 0, texID, tex->GetDefaultView(), *textSampler);

	currentFrame->globalDataMemory->lightColour		= Vector4(0.8f, 0.8f, 0.5f, 1.0f);
	currentFrame->globalDataMemory->lightRadius		= 1000.0f;
	currentFrame->globalDataMemory->lightPosition	= Vector3(-100.0f, 60.0f, -100.0f);
	currentFrame->globalDataMemory->cameraPos		= gameWorld.GetMainCamera()->GetPosition();

	currentFrame->globalDataMemory->viewMatrix	= gameWorld.GetMainCamera()->BuildViewMatrix();
	currentFrame->globalDataMemory->projMatrix	= gameWorld.GetMainCamera()->BuildProjectionMatrix(hostWindow.GetScreenAspect());
	currentFrame->globalDataMemory->orthoMatrix = Matrix4::Orthographic(0.0, 100.0f, 100, 0, -1.0f, 1.0f);

	Matrix4 shadowViewMatrix = Matrix4::BuildViewMatrix(currentFrame->globalDataMemory->lightPosition, Vector3(0, 0, 0), Vector3(0, 1, 0));
	Matrix4 shadowProjMatrix = Matrix4::Perspective(50.0f, 500.0f, 1, 45.0f);

	currentFrame->globalDataMemory->shadowMatrix = shadowProjMatrix * shadowViewMatrix;

	UpdateObjectList();

	{//Render the shadow map for the frame
		TransitionSamplerToDepth(&*shadowMap, defaultCmdBuffer);
		VulkanDynamicRenderBuilder()
			.WithDepthAttachment(shadowMap->GetDefaultView())
			.WithRenderArea(shadowScissor)
			.BeginRendering(defaultCmdBuffer);

		defaultCmdBuffer.setViewport(0, 1, &shadowViewport);
		defaultCmdBuffer.setScissor(0, 1, &shadowScissor);

		RenderSceneObjects(shadowPipeline, defaultCmdBuffer); //Draw the shadow map

		EndRendering(defaultCmdBuffer);
	}
	
	//Now render the main scene view
	TransitionDepthToSampler(&*shadowMap, defaultCmdBuffer);

	defaultCmdBuffer.setViewport(0, 1, &defaultViewport);
	defaultCmdBuffer.setScissor(0, 1, &defaultScissor);

	VulkanDynamicRenderBuilder()
	.WithColourAttachment(swapChainList[currentSwap]->view)
	.WithDepthAttachment(depthBuffer->GetDefaultView())
	.WithRenderArea(defaultScreenRect)
	.BeginRendering(defaultCmdBuffer);

	RenderSkybox(defaultCmdBuffer);
	RenderSceneObjects(scenePipeline, defaultCmdBuffer); //Draw the main scene

	UpdateDebugData();
	RenderDebugLines(defaultCmdBuffer);
	RenderDebugText(defaultCmdBuffer);

	EndRendering(defaultCmdBuffer);

	currentFrameIndex = (currentFrameIndex + 1) % _FRAMECOUNT;
	currentFrame = &allFrames[currentFrameIndex];
}

void GameTechVulkanRenderer::UpdateObjectList() {
	activeObjects.clear();

	int objectCount = 0;

	gameWorld.OperateOnContents(
		[&](GameObject* o) {
			if (o->IsActive()) {
				objectCount++;
			}
		}
	);
	if (objectCount > currentFrame->objectCount) {
		device.unmapMemory(*currentFrame->matrixData.deviceMem);
		currentFrame->matrixData = CreateBuffer(sizeof(Matrix4) * currentFrame->objectCount, vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
		currentFrame->matrixMemory = (Matrix4*)device.mapMemory(*currentFrame->matrixData.deviceMem, 0, currentFrame->matrixData.allocInfo.allocationSize);

		device.unmapMemory(*currentFrame->indexData.deviceMem);
		currentFrame->indexData = CreateBuffer(sizeof(int) * currentFrame->objectCount, vk::BufferUsageFlagBits::eStorageBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
		currentFrame->indexMemory = (int*)device.mapMemory(*currentFrame->indexData.deviceMem, 0, currentFrame->indexData.allocInfo.allocationSize);

		currentFrame->objectCount = objectCount * 1.5f;
	}

	VulkanMesh* pipeMesh = nullptr;
	int at = 0;
	gameWorld.OperateOnContents(
		[&](GameObject* o) {
			if (o->IsActive()) {
				RenderObject* g = o->GetRenderObject();
				if (g) {
					activeObjects.emplace_back(g);
					currentFrame->matrixMemory[at] = g->GetTransform()->GetMatrix();

					if (g->GetMesh()) {
						pipeMesh = (VulkanMesh*)g->GetMesh();
					}
					if (g->GetDefaultTexture() != nullptr && g->GetAPIData() == 0) {
						for (int i = 0; i < loadedTextures.size(); ++i) {
							if (loadedTextures[i] == g->GetDefaultTexture()) {
								g->SetAPIData(i);
								break;
							}
						}
					}
					currentFrame->indexMemory[at] = g->GetAPIData();
					at++;
				}
			}
		}
	);
	if (pipeMesh && !scenePipeline.pipeline) {
		BuildScenePipelines(pipeMesh);
	}
}

void GameTechVulkanRenderer::UpdateDebugData() {
	const std::vector<Debug::DebugStringEntry>& strings = Debug::GetDebugStrings();
	const std::vector<Debug::DebugLineEntry>& lines = Debug::GetDebugLines();

	if (lines.size() > 20) {
		bool a = true;
	}

	textVertCount = 0;
	for (const auto& s : strings) {
		textVertCount += Debug::GetDebugFont()->GetVertexCountForString(s.data);
	}
	lineVertCount = lines.size() * 2;

	int dataSize = lineVertCount * lineStride;
	dataSize += textVertCount * textStride;

	if (dataSize > currentFrame->debugVertSize) {
		int newDataSize = currentFrame->debugVertSize * 2;
		device.unmapMemory(*currentFrame->debugData.deviceMem);
		currentFrame->debugData = CreateBuffer(newDataSize, vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
		currentFrame->debugDataMemory = device.mapMemory(*currentFrame->debugData.deviceMem, 0, currentFrame->debugData.allocInfo.allocationSize);

		currentFrame->debugVertSize = newDataSize;
	}
	char* data = (char*)currentFrame->debugDataMemory;

	lineVertOffset = 0;

	memcpy(data, lines.data(), lineVertCount * lineStride);

	data += lineVertCount * lineStride;

	textVertOffset = lineVertCount * textStride;

	std::vector< NCL::Rendering::SimpleFont::InterleavedTextVertex> verts;

	for (const auto& s : strings) {
		float size = 20.0f;
		Debug::GetDebugFont()->BuildInterleavedVerticesForString(s.data, s.position, s.colour, size, verts);
		//can now copy to GPU visible mem
		size_t count = verts.size() * textStride;
		memcpy(data, verts.data(), count);
		data += count;
		verts.clear();
	}
}

void GameTechVulkanRenderer::RenderSceneObjects(VulkanPipeline& pipe, vk::CommandBuffer cmds) {
	int index = 0;
	if (activeObjects.empty()) {
		return;
	}

	cmds.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipe.pipeline);
	cmds.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipe.layout, 0, 1, &*currentFrame->dataDescriptor, 0, nullptr);
	cmds.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipe.layout, 1, 1, &*objectTextxureDescriptor, 0, nullptr);

	VulkanMesh* activeMesh = (VulkanMesh*)activeObjects[0]->GetMesh();
	int count = 1;
	int startingIndex = 0;

	for (int i = 1; i < activeObjects.size(); ++i) {
		VulkanMesh* objectMesh = (VulkanMesh*)activeObjects[i]->GetMesh();
		int increment = 1;
		//The new mesh is different than previous meshes, flush out the old list
		if (activeMesh != objectMesh) {
			cmds.pushConstants(*pipe.layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(uint32_t), (void*)&startingIndex);
			SubmitDrawCall(*activeMesh, cmds, count);
			activeMesh = objectMesh;
			count = 1;
			startingIndex = i;
			increment = 0;
		}
		if (i == activeObjects.size()-1) {
			cmds.pushConstants(*pipe.layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(uint32_t), (void*)&startingIndex);
			SubmitDrawCall(*objectMesh, cmds, count);
		}
		else {
			count += increment;
		}
	}
}

void GameTechVulkanRenderer::RenderSkybox(vk::CommandBuffer cmds) {
	//First bit: Draw skybox!
	cmds.bindPipeline(vk::PipelineBindPoint::eGraphics, *skyboxPipeline.pipeline);
	cmds.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *skyboxPipeline.layout, 0, 1, &*currentFrame->dataDescriptor, 0, nullptr);
	SubmitDrawCall(*quadMesh, cmds);
}

void GameTechVulkanRenderer::RenderDebugLines(vk::CommandBuffer cmds) {
	cmds.bindPipeline(vk::PipelineBindPoint::eGraphics, *debugLinePipeline.pipeline);
	cmds.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *debugLinePipeline.layout, 0, 1, &*currentFrame->dataDescriptor, 0, nullptr);
	cmds.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *debugLinePipeline.layout, 1, 1, &*objectTextxureDescriptor, 0, nullptr);

	vk::Buffer attributeBuffers[2] = {
		*currentFrame->debugData.buffer,
		*currentFrame->debugData.buffer
	};

	vk::DeviceSize attributeOffsets[2] = {
		lineVertOffset + 0,
		lineVertOffset + sizeof(Vector4)
	};

	cmds.bindVertexBuffers(0, 2, attributeBuffers, attributeOffsets);
	cmds.draw(lineVertCount, 1, 0, 0);
}

void GameTechVulkanRenderer::RenderDebugText(vk::CommandBuffer cmds) {
	cmds.bindPipeline(vk::PipelineBindPoint::eGraphics, *debugTextPipeline.pipeline);
	cmds.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *debugTextPipeline.layout, 0, 1, &*currentFrame->dataDescriptor, 0, nullptr);
	cmds.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *debugTextPipeline.layout, 1, 1, &*objectTextxureDescriptor, 0, nullptr);

	int texID = _TEXCOUNT-1;
	cmds.pushConstants(*debugTextPipeline.layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(uint32_t), (void*)&texID);

	vk::Buffer attributeBuffers[3] = {
		*currentFrame->debugData.buffer,
		*currentFrame->debugData.buffer,
		*currentFrame->debugData.buffer
	};

	vk::DeviceSize attributeOffsets[3] = {
		textVertOffset + 0,
		textVertOffset + sizeof(Vector2), //jump over the position
		textVertOffset + sizeof(Vector2) + sizeof(Vector2) //jump over position and tex coord
	};

	cmds.bindVertexBuffers(0, 3, attributeBuffers, attributeOffsets);
	cmds.draw(textVertCount, 1, 0, 0);
}

MeshGeometry* GameTechVulkanRenderer::LoadMesh(const string& name) {
	VulkanMesh* newMesh = new VulkanMesh(name);
	newMesh->SetPrimitiveType(NCL::GeometryPrimitive::Triangles);
	newMesh->SetDebugName(name);
	newMesh->UploadToGPU(this);
	return newMesh;
}
TextureBase* GameTechVulkanRenderer::LoadTexture(const string& name) {
	VulkanTexture* t = (VulkanTexture*)VulkanTexture::TextureFromFilenameLoader(name);
	//Write the texture to our big descriptor set
	UpdateImageDescriptor(*objectTextxureDescriptor, 0, loadedTextures.size(), t->GetDefaultView(), *defaultSampler);
	loadedTextures.push_back(t);
	return t;
}

ShaderBase* GameTechVulkanRenderer::LoadShader(const string& vertex, const string& fragment) {
	return VulkanShaderBuilder("Shader!")
		.WithVertexBinary(vertex + ".spv")
		.WithFragmentBinary(fragment + ".spv")
		.Build(device).release();
	return nullptr;
}

UniqueVulkanMesh GameTechVulkanRenderer::GenerateQuad() {
	VulkanMesh* quadMesh = new VulkanMesh();
	quadMesh->SetVertexPositions({ Vector3(-1,-1,0), Vector3(1,-1,0), Vector3(1,1,0), Vector3(-1,1,0) });
	quadMesh->SetVertexTextureCoords({ Vector2(0,0), Vector2(1,0), Vector2(1, 1), Vector2(0, 1) });
	quadMesh->SetVertexIndices({ 0,1,3,2 });
	quadMesh->SetDebugName("Fullscreen Quad");
	quadMesh->SetPrimitiveType(NCL::GeometryPrimitive::TriangleStrip);
	quadMesh->UploadToGPU(this);

	return UniqueVulkanMesh(quadMesh);
}
#endif