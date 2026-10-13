#ifdef USE_VULKAN

#include "GameTechVulkanRenderer.h"
#include "GameObject.h"
#include "RenderObject.h"
#include "Camera.h"
#include "Debug.h"

#include "../VKQuick/Utils.h"
#include "../VKQuick/TextureBuilder.h"
#include "../VKQuick/VMAMemoryManager.h"

#ifdef _WIN32
#include "../NCLCoreClasses/Win32Window.h"
#endif

using namespace VKQuick;

using namespace NCL;
using namespace Rendering;
using namespace CSC8503;

const int _SHADOWSIZE = 8192;
const int _FRAMECOUNT = 3;

const int _TEXCOUNT = 128;

const size_t lineStride = sizeof(Vector4) + sizeof(Vector4);
const size_t textStride = sizeof(Vector2) + sizeof(Vector4) + sizeof(Vector2);

GameTechVulkanRenderer::GameTechVulkanRenderer(GameWorld& world) : gameWorld(world) {
	VKQuick::VKQuickInitialisation vkInit;

#ifdef _WIN32
	Win32Code::Win32Window* hostWindow = (Win32Code::Win32Window*)Window::GetWindow();
	vkInit.win32Handle = hostWindow->GetHandle();
	vkInit.win32Instance = hostWindow->GetInstance();

	vkInit.initialWidth = hostWindow->GetScreenSize().x;
	vkInit.initialHeight = hostWindow->GetScreenSize().y;
#endif

	vkQuickInstance = new VKQuick::Instance(vkInit);


	FrameContext const& context = vkQuickInstance->GetFrameContext();
	const vk::Device device = context.device;

	objectTextxureLayout = DescriptorSetLayoutBuilder(context.device)
		.WithImageSamplers(0, _TEXCOUNT, vk::ShaderStageFlagBits::eFragment, vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eVariableDescriptorCount)
		.WithCreationFlags(vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool)
		.Build("Bindless Data");

	objectTextxureDescriptor = CreateDescriptorSet(context.device, context.descriptorPool, *objectTextxureLayout, _TEXCOUNT);

	defaultTexture = LoadTextureInternal("Default.png");

	//loadedTextures.push_back(defaultTexture); //Default will always be index 0!

	defaultSampler = device.createSamplerUnique(
		vk::SamplerCreateInfo()
		.setAnisotropyEnable(false)
		.setMaxAnisotropy(16)
		.setMinFilter(vk::Filter::eLinear)
		.setMagFilter(vk::Filter::eLinear)
		.setMipmapMode(vk::SamplerMipmapMode::eLinear)
		.setMaxLod(80.0f)
	);
	for (int i = 0; i < _TEXCOUNT; ++i) {
		WriteCombinedImageDescriptor(context.device, *objectTextxureDescriptor, 0, i, defaultTexture->GetTex().GetDefaultView(), *defaultSampler);
	}

	textSampler = device.createSamplerUnique(
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

		cubeTex = LoadCubemap(
			"Cubemap/skyrender0004.png", "Cubemap/skyrender0001.png",
			"Cubemap/skyrender0003.png", "Cubemap/skyrender0006.png",
			"Cubemap/skyrender0002.png", "Cubemap/skyrender0005.png",
			"Cubemap Texture!"
		);

		skyboxPipeline = vkQuickInstance->PipelineBuilder()
			.WithVertexInputState(quadMesh->GetMesh()->GetVertexInputState())
			.WithTopology(quadMesh->GetPrimitiveTopology())

			.WithShaderBinary("skybox.vert.spv", vk::ShaderStageFlagBits::eVertex)
			.WithShaderBinary("skybox.frag.spv", vk::ShaderStageFlagBits::eFragment)

			.WithColourAttachment(context.colourFormat)
			.WithDepthAttachment(context.depthFormat)
		.Build("CubeMapRenderer Skybox Pipeline");
	}
	 
	{//Setting up all the data we need for shadow maps!

		//shadowMap = vkQuickInstance->TextureBuilder()
		//	.WithCommandBuffer(context.cmdBuffer)
		//	.WithDimension(_SHADOWSIZE, _SHADOWSIZE)
		//	.WithAspects(vk::ImageAspectFlagBits::eDepth)
		//	.WithFormat(vk::Format::eD32Sfloat)
		//	.WithLayout(vk::ImageLayout::eDepthAttachmentOptimal)
		//	.WithUsages(vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled)
		//	.WithMips(false)
		//.BuildEmpty("Shadowmap");

		shadowViewport	= vk::Viewport(0.0f, (float)_SHADOWSIZE, (float)_SHADOWSIZE, -(float)_SHADOWSIZE, 0.0f, 1.0f);
		shadowScissor	= vk::Rect2D(vk::Offset2D(0, 0), vk::Extent2D(_SHADOWSIZE, _SHADOWSIZE));
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

			allFrames[i].dataBuffer = vkQuickInstance->GetMemoryManager().CreateBuffer(
				{
					.size = sizeof(1024 * 1024 * 64),
					.usage =	vk::BufferUsageFlagBits::eStorageBuffer | 
								vk::BufferUsageFlagBits::eUniformBuffer | 
								vk::BufferUsageFlagBits::eVertexBuffer
				},
				vk::MemoryPropertyFlagBits::eHostVisible,
				"Frame Data"
			);

			allFrames[i].data = (char*)allFrames[i].dataBuffer.Map();
			allFrames[i].dataStart = allFrames[i].data;

			allFrames[i].dataDescriptor = CreateDescriptorSet(context.device, context.descriptorPool, scenePipeline.GetSetLayout(0)); //Both use compatible layout

			WriteCombinedImageDescriptor(device, *allFrames[i].dataDescriptor, 2, 0, shadowMap->GetDefaultView(), *defaultSampler, vk::ImageLayout::eDepthStencilReadOnlyOptimal);
			WriteCombinedImageDescriptor(device, *allFrames[i].dataDescriptor, 3, 0, cubeTex->GetDefaultView(), *defaultSampler);
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
	FrameContext const& context = vkQuickInstance->GetFrameContext();
	const vk::Device device = context.device;

	scenePipeline = vkQuickInstance->PipelineBuilder()
		.WithVertexInputState(m->GetMesh()->GetVertexInputState())
		.WithTopology(vk::PrimitiveTopology::eTriangleList)
		.WithShaderBinary("scene.vert.spv", vk::ShaderStageFlagBits::eVertex)
		.WithShaderBinary("scene.frag.spv", vk::ShaderStageFlagBits::eFragment)

		.WithColourAttachment(context.colourFormat, vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha)
		.WithDepthAttachment(context.depthFormat, vk::CompareOp::eLessOrEqual, true, true)
		//.WithDescriptorSetLayout(*globalDataLayout)
		.WithDescriptorSetLayout(1, *objectTextxureLayout) //Set 1
		//.WithPushConstant(vk::ShaderStageFlagBits::eVertex, 0, sizeof(Matrix4))
	.Build("Main Scene Pipeline");

	shadowPipeline = vkQuickInstance->PipelineBuilder()
		.WithVertexInputState(m->GetMesh()->GetVertexInputState())
		.WithTopology(vk::PrimitiveTopology::eTriangleList)
		.WithShaderBinary("shadow.vert.spv", vk::ShaderStageFlagBits::eVertex)
		.WithShaderBinary("shadow.frag.spv", vk::ShaderStageFlagBits::eFragment)
//		.WithDescriptorSetLayout(*globalDataLayout)
		.WithDescriptorSetLayout(1, *objectTextxureLayout) //Set 1
		.WithDepthAttachment(shadowMap->GetFormat(), vk::CompareOp::eLessOrEqual, true, true)

		.WithRasterState(vk::CullModeFlagBits::eFront)
	.Build("Shadow Map Pipeline");
}

void GameTechVulkanRenderer::BuildDebugPipelines() {
	FrameContext const& context = vkQuickInstance->GetFrameContext();
	const vk::Device device = context.device;

	//debugLineShader = VulkanShaderBuilder("Debug Line Shader")
	//	.WithVertexBinary("Debug.vert.spv")
	//	.WithFragmentBinary("Debug.frag.spv")
	//	.Build(device);

	//debugTextShader = VulkanShaderBuilder("Debug Text Shader")
	//	.WithVertexBinary("DebugText.vert.spv")
	//	.WithFragmentBinary("DebugText.frag.spv")
	//	.Build(device);

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

	vk::PipelineVertexInputStateCreateInfo lineVertexState = {
		//.vertexBindingDescriptionCount = 3,
		//.pVertexBindingDescriptions = &debugTextVertBindings
		//.vertexAttributeDescriptionCount = 2,
		//.pVertexAttributeDescriptions = &debugLineVertAttribs
	};

	vk::PipelineVertexInputStateCreateInfo textVertexState = {

	};
	//({}, debugTextVertBindings, debugTextVertAttribs);

	debugLinePipeline = vkQuickInstance->PipelineBuilder()
		.WithVertexInputState(lineVertexState)
		.WithTopology(vk::PrimitiveTopology::eLineList)

		.WithShaderBinary("debug.vert.spv", vk::ShaderStageFlagBits::eVertex)
		.WithShaderBinary("debug.frag.spv", vk::ShaderStageFlagBits::eFragment)

		.WithColourAttachment(context.colourFormat, vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha)
		.WithDepthAttachment(context.depthFormat, vk::CompareOp::eLessOrEqual, true, true)

		//.WithColourFormats({ surfaceFormat })
		//.WithDepthStencilFormat(depthBuffer->GetFormat())
		//.WithDescriptorSetLayout(*globalDataLayout)		//Set 0
		.WithDescriptorSetLayout(1, *objectTextxureLayout) //Set 1
		.Build("Debug Line Pipeline");

	debugTextPipeline = vkQuickInstance->PipelineBuilder()
		.WithVertexInputState(textVertexState)
		.WithTopology(vk::PrimitiveTopology::eTriangleList)
	
		.WithShaderBinary("debugText.vert.spv", vk::ShaderStageFlagBits::eVertex)
		.WithShaderBinary("debugText.frag.spv", vk::ShaderStageFlagBits::eFragment)

		.WithColourAttachment(context.colourFormat, vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha)
		.WithDepthAttachment(context.depthFormat, vk::CompareOp::eLessOrEqual, true, true)

		//.WithColourFormats({ surfaceFormat })
		//.WithDepthStencilFormat(depthBuffer->GetFormat())
		//.WithDescriptorSetLayout(*globalDataLayout)		//Set 0
		.WithDescriptorSetLayout(1, *objectTextxureLayout) //Set 1
		//.WithPushConstant(vk::ShaderStageFlagBits::eFragment, 0, sizeof(int)) //TexID to use
		.Build("Debug Text Pipeline");
}

void GameTechVulkanRenderer::RenderFrame(float dt) {
	//TransitionSwapchainForRendering(defaultCmdBuffer);

	FrameContext const& context = vkQuickInstance->GetFrameContext();
	vk::CommandBuffer cmdBuffer = context.cmdBuffer;


	int texID = _TEXCOUNT - 1;
	VulkanTexture* tex = (VulkanTexture*)Debug::GetDebugFont()->GetTexture();
	//UpdateImageDescriptor(*objectTextxureDescriptor, 0, texID, tex->GetDefaultView(), *textSampler);

	GlobalData frameData;
	frameData.lightColour = lightColour;
	frameData.lightRadius = lightRadius;
	frameData.lightPosition = lightPosition;
	frameData.cameraPos		= gameWorld.GetMainCamera().GetPosition();

	frameData.viewMatrix	= gameWorld.GetMainCamera().BuildViewMatrix();
	frameData.projMatrix	= gameWorld.GetMainCamera().BuildProjectionMatrix(Window::GetWindow()->GetScreenAspect());
	frameData.orthoMatrix	= Matrix::Orthographic(0.0f, 100.0f, 100.0f, 0.0f, -1.0f, 1.0f);
	frameData.shadowMatrix  =	Matrix::Perspective(50.0f, 500.0f, 1, 45.0f) * 
								Matrix::View(frameData.lightPosition, Vector3(0, 0, 0), Vector3(0, 1, 0));

	currentFrame->data				= currentFrame->dataStart;
	currentFrame->bytesWritten		= 0;
	currentFrame->globalDataOffset	= 0;
	currentFrame->objectStateOffset = sizeof(GlobalData);
	currentFrame->debugLinesOffset	= currentFrame->objectStateOffset;

	currentFrame->WriteData<GlobalData>(frameData); //Let's start filling up our frame data!

	currentFrame->AlignData(128);
	currentFrame->objectStateOffset = currentFrame->bytesWritten;
	
	UpdateObjectList();

	currentFrame->AlignData(128);

	size_t objectSize = currentFrame->bytesWritten - currentFrame->objectStateOffset;

	//UpdateBufferDescriptorOffset(*currentFrame->dataDescriptor, currentFrame->dataBuffer, 0, vk::DescriptorType::eUniformBuffer, 0, sizeof(GlobalData));
	//UpdateBufferDescriptorOffset(*currentFrame->dataDescriptor, currentFrame->dataBuffer, 1, vk::DescriptorType::eStorageBuffer, currentFrame->objectStateOffset, objectSize);

	{//Render the shadow map for the frame	
		
		VKQuick::DynamicRenderBuilder(context.cmdBuffer)
		.WithDepthAttachment({
			//.imageView = *shadowMap,
			.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
			.loadOp = vk::AttachmentLoadOp::eClear,
			.clearValue = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f)
			})
		.WithRenderArea(shadowScissor)
		.BeginRendering();

		cmdBuffer.setViewport(0, 1, &shadowViewport);
		cmdBuffer.setScissor( 0, 1, &shadowScissor);

		RenderSceneObjects(shadowPipeline, cmdBuffer); //Draw the shadow map

		cmdBuffer.endRendering();
	}

	//Now render the main scene view
	TransitionDepthToSampler(cmdBuffer, *shadowMap);

	vkQuickInstance->BeginRenderToScreen(cmdBuffer);

	RenderSkybox(cmdBuffer);
	RenderSceneObjects(scenePipeline, cmdBuffer); //Draw the main scene

	UpdateDebugData();
	RenderDebugLines(cmdBuffer);
	RenderDebugText(cmdBuffer);

	cmdBuffer.endRendering();

	TransitionSamplerToDepth(cmdBuffer, *shadowMap);

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

	VulkanMesh* pipeMesh = nullptr;
	int at = 0;
	gameWorld.OperateOnContents(
		[&](GameObject* o) {
			if (o->IsActive()) {
				RenderObject* g = o->GetRenderObject();
				if (g) {
					activeObjects.emplace_back(g);

					ObjectState state;
					state.modelMatrix = g->GetTransform().GetMatrix();
					state.colour = g->GetColour();
					state.index[0] = 0;
					if (g->GetMesh()) {
						pipeMesh = (VulkanMesh*)g->GetMesh();
					}
					//if (g->GetDefaultTexture()) {
					//	VulkanGameTechTexture* t = (VulkanGameTechTexture*)g->GetDefaultTexture();
					//	state.index[0] = t->index;
					//}
					currentFrame->WriteData<ObjectState>(state);
					currentFrame->debugLinesOffset += sizeof(ObjectState);
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
	const std::vector<Debug::DebugLineEntry>& lines		= Debug::GetDebugLines();

	currentFrame->textVertCount = 0;
	currentFrame->lineVertCount = 0;

	for (const auto& s : strings) {
		currentFrame->textVertCount += Debug::GetDebugFont()->GetVertexCountForString(s.data);
	}
	currentFrame->lineVertCount = (int)lines.size() * 2;

	currentFrame->lineVertSize = currentFrame->lineVertCount * lineStride;
	currentFrame->textVertSize = currentFrame->textVertCount * textStride;

	currentFrame->debugLinesOffset = currentFrame->bytesWritten;

	currentFrame->WriteData((void*)lines.data(), (size_t)currentFrame->lineVertCount * lineStride);

	currentFrame->debugTextOffset = currentFrame->bytesWritten;
	std::vector< NCL::Rendering::SimpleFont::InterleavedTextVertex> verts;

	for (const auto& s : strings) {
		float size = 20.0f;
		Debug::GetDebugFont()->BuildInterleavedVerticesForString(s.data, s.position, s.colour, size, verts);
		//can now copy to GPU visible mem
		size_t count = verts.size() * textStride;
		memcpy(currentFrame->data, verts.data(), count);
		currentFrame->data += count;
		currentFrame->bytesWritten += count;
		verts.clear();
	}
}

void GameTechVulkanRenderer::RenderSceneObjects(VKQuick::Pipeline& pipe, vk::CommandBuffer cmds) {
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

			activeMesh->GetMesh()->BindToCommandBuffer(cmds);
			activeMesh->GetMesh()->Draw(cmds, count);

			//SubmitDrawCall(*activeMesh, cmds, count);
			activeMesh = objectMesh;
			count = 1;
			startingIndex = i;
			increment = 0;
		}
		if (i == activeObjects.size()-1) {
			cmds.pushConstants(*pipe.layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(uint32_t), (void*)&startingIndex);
			//SubmitDrawCall(*objectMesh, cmds, count);

			activeMesh->GetMesh()->BindToCommandBuffer(cmds);
			activeMesh->GetMesh()->Draw(cmds, count);
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
	//SubmitDrawCall(*quadMesh, cmds);
	quadMesh->GetMesh()->BindToCommandBuffer(cmds);
	quadMesh->GetMesh()->Draw(cmds);
}

void GameTechVulkanRenderer::RenderDebugLines(vk::CommandBuffer cmds) {
	cmds.bindPipeline(vk::PipelineBindPoint::eGraphics, *debugLinePipeline.pipeline);
	cmds.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *debugLinePipeline.layout, 0, 1, &*currentFrame->dataDescriptor, 0, nullptr);
	cmds.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *debugLinePipeline.layout, 1, 1, &*objectTextxureDescriptor, 0, nullptr);

	vk::Buffer attributeBuffers[2] = {
		currentFrame->dataBuffer.GetHandle(),
		currentFrame->dataBuffer.GetHandle()
	};

	vk::DeviceSize attributeOffsets[2] = {
		currentFrame->debugLinesOffset + 0,
		currentFrame->debugLinesOffset + sizeof(Vector4)
	};

	cmds.bindVertexBuffers(0, 2, attributeBuffers, attributeOffsets);
	cmds.draw((uint32_t)currentFrame->lineVertCount, 1, 0, 0);
}

void GameTechVulkanRenderer::RenderDebugText(vk::CommandBuffer cmds) {
	cmds.bindPipeline(vk::PipelineBindPoint::eGraphics, *debugTextPipeline.pipeline);
	cmds.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *debugTextPipeline.layout, 0, 1, &*currentFrame->dataDescriptor, 0, nullptr);
	cmds.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *debugTextPipeline.layout, 1, 1, &*objectTextxureDescriptor, 0, nullptr);

	int texID = _TEXCOUNT-1;
	cmds.pushConstants(*debugTextPipeline.layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(uint32_t), (void*)&texID);

	vk::Buffer attributeBuffers[3] = {
		currentFrame->dataBuffer.GetHandle(),
		currentFrame->dataBuffer.GetHandle(),
		currentFrame->dataBuffer.GetHandle()
	};

	vk::DeviceSize attributeOffsets[3] = {
		currentFrame->debugTextOffset + 0,
		currentFrame->debugTextOffset + sizeof(Vector2), //jump over the position
		currentFrame->debugTextOffset + sizeof(Vector2) + sizeof(Vector2) //jump over position and tex coord
	};

	cmds.bindVertexBuffers(0, 3, attributeBuffers, attributeOffsets);
	cmds.draw((uint32_t)currentFrame->textVertCount, 1, 0, 0);
}

NCL::Rendering::Mesh* GameTechVulkanRenderer::LoadMesh(const std::string& name) {
	VulkanMesh* newMesh = new VulkanMesh();
	newMesh->SetPrimitiveType(NCL::GeometryPrimitive::Triangles);
	newMesh->SetDebugName(name);
	//newMesh->UploadToGPU(this);

	UploadMeshWait(*newMesh);
	return newMesh;
}

UniqueVulkanMesh GameTechVulkanRenderer::GenerateQuad() {
	VulkanMesh* quadMesh = new VulkanMesh();
	quadMesh->SetVertexPositions({ Vector3(-1,-1,0), Vector3(1,-1,0), Vector3(1,1,0), Vector3(-1,1,0) });
	quadMesh->SetVertexTextureCoords({ Vector2(0,0), Vector2(1,0), Vector2(1, 1), Vector2(0, 1) });
	quadMesh->SetVertexIndices({ 0,1,3,2 });
	quadMesh->SetDebugName("Fullscreen Quad");
	quadMesh->SetPrimitiveType(NCL::GeometryPrimitive::TriangleStrip);
	//quadMesh->UploadToGPU(this);

	UploadMeshWait(*quadMesh);

	return UniqueVulkanMesh(quadMesh);
}

NCL::Rendering::Texture* GameTechVulkanRenderer::LoadTexture(const std::string& name) {
	//FrameContext const& context = vkQuickInstance->GetFrameContext();

	//UniqueVulkanTexture tex = LoadTextureInternal(name);

	//tex->SetAssetID(loadedTextures.size());

	////Write the texture to our big descriptor set
	//WriteCombinedImageDescriptor(context.device, *objectTextxureDescriptor, 0, (int)loadedTextures.size(), tex->GetTex().GetDefaultView(), *defaultSampler);

	//loadedTextures.emplace_back(tex);
	//return (Texture*)tex.get();

	return nullptr;
}

UniqueVulkanTexture GameTechVulkanRenderer::LoadTextureInternal(const std::string& filename) {
	//FrameContext const& context = vkQuickInstance->GetFrameContext();
	//vk::UniqueCommandBuffer cmdBuffer = CmdBufferCreateBegin(context.device, context.commandPools[CommandType::Graphics], "VulkanTexture upload");

	//VKQuick::UniqueTexture tex = vkQuickInstance->TextureBuilder()
	//	.WithCommandBuffer(*cmdBuffer)
	//	.BuildFromFile(filename);

	//VKQuick::CmdBufferSubmit(
	//	{
	//		.buffer = *cmdBuffer,
	//		.queue = context.queues[CommandType::Graphics],
	//		.device = context.device,
	//		.wait = true
	//	}
	//);

	//return std::make_unique<VulkanTexture>(tex.release());

	return UniqueVulkanTexture();

}

VKQuick::UniqueTexture GameTechVulkanRenderer::LoadCubemap(
	const std::string& negativeXFile, const std::string& positiveXFile,
	const std::string& negativeYFile, const std::string& positiveYFile,
	const std::string& negativeZFile, const std::string& positiveZFile,
	const std::string& debugName) {

	//FrameContext const& context = vkQuickInstance->GetFrameContext();
	//vk::UniqueCommandBuffer cmdBuffer = CmdBufferCreateBegin(context.device, context.commandPools[CommandType::Graphics], "VulkanTexture upload");

	//VKQuick::UniqueTexture tex = vkQuickInstance->TextureBuilder()
	//	.WithCommandBuffer(*cmdBuffer)
	//	.BuildCubemapFromFile(negativeXFile, positiveXFile,
	//		negativeYFile, positiveYFile,
	//		negativeZFile, positiveZFile,
	//		debugName
	//	);

	//VKQuick::CmdBufferSubmit(
	//	{
	//		.buffer = *cmdBuffer,
	//		.queue	= context.queues[CommandType::Graphics],
	//		.device = context.device,
	//		.wait	= true
	//	}
	//);

	//return tex;
	VKQuick::UniqueTexture tex;
	return tex;
}


void GameTechVulkanRenderer::UploadMeshWait(VulkanMesh& m, vk::BufferUsageFlags flags) {
	FrameContext const& context = vkQuickInstance->GetFrameContext();

	vk::UniqueCommandBuffer cmdBuffer = CmdBufferCreateBegin(context.device, context.commandPools[CommandType::Graphics], "VulkanMesh upload");

	m.InitialiseGPUState(context.device, vkQuickInstance->GetMemoryManager(), flags);

	m.UploadAttributes(*cmdBuffer);

	VKQuick::CmdBufferSubmit(
		{
			.buffer = *cmdBuffer,
			.queue  = context.queues[CommandType::Graphics],
			.device = context.device,
			.wait	= true
		}
	);
}
#endif //USE_VULKAN