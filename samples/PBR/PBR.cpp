#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

class VulkanExample : public VulkanExampleBase
{
public:
	bool displaySkybox = true;

	struct Textures {
		vks::TextureCubeMap environmentCube;
		vks::TextureCubeMap prefilteredCube;
		// Object texture maps
		vks::Texture2D albedoMap;
		vks::Texture2D normalMap;
		vks::Texture2D aoMap;
		vks::Texture2D metallicMap;
		vks::Texture2D roughnessMap;
	} textures{};

	struct
	{
		vkglTF::Model skybox;
		vkglTF::Model object;
	} models;

	struct UniformBuffers
	{
		vks::Buffer scene;
		vks::Buffer skybox;
	};
	std::array<UniformBuffers, maxConcurrentFrames> uniformBuffers;

	struct
	{
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
		glm::vec3 cameraPos;
	} uniformMatrices;

	glm::vec3 irradianceSHCoeffs[9];

	struct
	{
		vks::Buffer buffer;
		VkDescriptorPool descriptorPool;
		VkDescriptorSetLayout descriptorSetLayout;
		VkDescriptorSet descriptorSet;
		VkPipelineLayout pipelineLayout;
		VkPipeline pipeline;
	} irradianceSHCompute;

	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
	struct
	{
		VkPipeline scene{ VK_NULL_HANDLE };
		VkPipeline skybox{ VK_NULL_HANDLE };
	} pipelines;
	
	VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };
	struct DescriptorSets
	{
		VkDescriptorSet scene{ VK_NULL_HANDLE };
		VkDescriptorSet skybox{ VK_NULL_HANDLE };
	};
	std::array<DescriptorSets, maxConcurrentFrames> descriptorSets{};

	VulkanExample() : VulkanExampleBase()
	{
		title = "PBR with IBL";
		camera.type = Camera::CameraType::firstperson;
		camera.movementSpeed = 4.0f;
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
		camera.rotationSpeed = 0.25f;
		camera.setRotation({ -7.75f, 150.25f, 0.0f });
		camera.setPosition({ 0.7f, 0.1f, 1.7f });
	}

	~VulkanExample()
	{
		if (device)
		{
			vkDestroyPipeline(device, pipelines.scene, nullptr);
			vkDestroyPipeline(device, pipelines.skybox, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			for (auto& buffer : uniformBuffers)
			{
				buffer.scene.destroy();
				buffer.skybox.destroy();
			}
			textures.environmentCube.destroy();
		}
	}

	void getEnabledFeatures() override
	{
		if (deviceFeatures.samplerAnisotropy) {
			enabledFeatures.samplerAnisotropy = VK_TRUE;
		}
	}

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		// Skybox
		models.skybox.loadFromFile(getAssetPath() + "models/cube.gltf", vulkanDevice, queue, glTFLoadingFlags);
		// Objects
		models.object.loadFromFile(getAssetPath() + "models/cerberus/cerberus.gltf", vulkanDevice, queue, glTFLoadingFlags);
		// HDR Cubes
		textures.environmentCube.loadFromFile(getAssetPath() + "textures/hdr/gcanyon_cube.ktx", VK_FORMAT_R16G16B16A16_SFLOAT, vulkanDevice, queue);
		// PBR Textures
	}

	void generateIrradianceSH()
	{
		auto tStart = std::chrono::high_resolution_clock::now();

		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&irradianceSHCompute.buffer,
			sizeof(irradianceSHCoeffs) 
		));

		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 1);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &irradianceSHCompute.descriptorPool));

		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &irradianceSHCompute.descriptorSetLayout));

		VkPipelineLayoutCreateInfo pipelineLayoutInfo = vks::initializers::pipelineLayoutCreateInfo(&irradianceSHCompute.descriptorSetLayout);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &irradianceSHCompute.pipelineLayout));

		VkComputePipelineCreateInfo computePipelineInfo = vks::initializers::computePipelineCreateInfo(irradianceSHCompute.pipelineLayout);
		computePipelineInfo.stage = loadShader(getShadersPath() + "PBR/irradianceSH.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);
		VK_CHECK_RESULT(vkCreateComputePipelines(device, pipelineCache, 1, &computePipelineInfo, nullptr, &irradianceSHCompute.pipeline));

		std::vector<VkWriteDescriptorSet> writeDescriptorSets =
		{
			vks::initializers::writeDescriptorSet(irradianceSHCompute.descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &textures.environmentCube.descriptor),
			vks::initializers::writeDescriptorSet(irradianceSHCompute.descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, &irradianceSHCompute.buffer.descriptor)
		};
		vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, nullptr);

		VkCommandBuffer cmdBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, irradianceSHCompute.pipeline);
		vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, irradianceSHCompute.pipelineLayout, 0, 1, &irradianceSHCompute.descriptorSet, 0, nullptr);
		vkCmdDispatch(cmdBuffer, 1, 1, 1);
		vulkanDevice->flushCommandBuffer(cmdBuffer, queue);

		vks::Buffer staging;
		vulkanDevice->createBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &staging, sizeof(irradianceSHCoeffs));
		VK_CHECK_RESULT(staging.map());

		VkCommandBuffer copyCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		VkBufferCopy copyRegion = {};
		copyRegion.size = sizeof(irradianceSHCoeffs);
		vkCmdCopyBuffer(copyCmd, irradianceSHCompute.buffer.buffer, staging.buffer, 1, &copyRegion);
		vulkanDevice->flushCommandBuffer(cmdBuffer, queue);

		memcpy(irradianceSHCoeffs, staging.buffer, sizeof(irradianceSHCoeffs));
		staging.destroy();

		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
		std::cout << "Generating Irradiance Cube took " << tDiff << " ms" << std::endl;
	}

	//void generateBRDFLUT()
	//{
	//	auto tStart = std::chrono::high_resolution_clock::now();

	//	const VkFormat format = VK_FORMAT_R16G16_SFLOAT;
	//	const int32_t dim = 512;

	//	// Image
	//	VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
	//	imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	//	imageCreateInfo.format = format;
	//	imageCreateInfo.extent = { dim, dim, 1 };
	//	imageCreateInfo.mipLevels = 1;
	//	imageCreateInfo.arrayLayers = 1;
	//	imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	//	imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	//	imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	//	VK_CHECK_RESULT(vkCreateImage(device, &imageCreateInfo, nullptr, &textures.lutBrdf.image));

	//	VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
	//	VkMemoryRequirements memReqs;
	//	vkGetImageMemoryRequirements(device, textures.lutBrdf.image, &memReqs);
	//	memAlloc.allocationSize = memReqs.size;
	//	memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	//	VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &textures.lutBrdf.deviceMemory));
	//	VK_CHECK_RESULT(vkBindImageMemory(device, textures.lutBrdf.image, textures.lutBrdf.deviceMemory, 0));

	//	// Image View
	//	VkImageViewCreateInfo imageViewCreateInfo = vks::initializers::imageViewCreateInfo();
	//	imageViewCreateInfo.image = textures.lutBrdf.image;
	//	imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	//	imageViewCreateInfo.format = format;
	//	imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	//	imageViewCreateInfo.subresourceRange.levelCount = 1;
	//	imageViewCreateInfo.subresourceRange.layerCount = 1;
	//	VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCreateInfo, nullptr, &textures.lutBrdf.view));

	//	// Sampler
	//	VkSamplerCreateInfo samplerCreateInfo = vks::initializers::samplerCreateInfo();
	//	samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
	//	samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
	//	samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	//	samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	//	samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	//	samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	//	samplerCreateInfo.minLod = 0.0f;
	//	samplerCreateInfo.maxLod = 1.0f;
	//	samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	//	VK_CHECK_RESULT(vkCreateSampler(device, &samplerCreateInfo, nullptr, &textures.lutBrdf.sampler));

	//	textures.lutBrdf.descriptor.sampler = textures.lutBrdf.sampler;
	//	textures.lutBrdf.descriptor.imageView = textures.lutBrdf.view;
	//	textures.lutBrdf.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	//	textures.lutBrdf.device = vulkanDevice;

	//	// Pipeline layout
	//	VkPipelineLayout pipelinelayout;
	//	VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(nullptr, 0);
	//	VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelinelayout));

	//	// Pipeline
	//	VkRenderingAttachmentInfo colorAttachment = vks::initializers::renderingAttachmentInfo();
	//	colorAttachment.imageView = textures.lutBrdf.view;
	//	colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
	//	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	//	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	//	colorAttachment.clearValue.color = defaultClearColor;

	//	VkRenderingInfo renderingInfo = vks::initializers::renderingInfo();
	//	renderingInfo.renderArea.extent = { dim, dim };
	//	renderingInfo.layerCount = 1;
	//	renderingInfo.colorAttachmentCount = 1;
	//	renderingInfo.pColorAttachments = &colorAttachment;

	//	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
	//	VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
	//	VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
	//	VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
	//	VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);
	//	VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1);
	//	VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);
	//	std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	//	VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
	//	VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
	//	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

	//	VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo();
	//	pipelineCI.pNext = &renderingInfo;
	//	pipelineCI.pInputAssemblyState = &inputAssemblyState;
	//	pipelineCI.pRasterizationState = &rasterizationState;
	//	pipelineCI.pColorBlendState = &colorBlendState;
	//	pipelineCI.pMultisampleState = &multisampleState;
	//	pipelineCI.pViewportState = &viewportState;
	//	pipelineCI.pDepthStencilState = &depthStencilState;
	//	pipelineCI.pDynamicState = &dynamicState;
	//	pipelineCI.stageCount = 2;
	//	pipelineCI.pStages = shaderStages.data();
	//	pipelineCI.pVertexInputState = &emptyInputState;
	//	pipelineCI.layout = pipelinelayout;

	//	// Look-up-table (from BRDF) pipeline
	//	shaderStages[0] = loadShader(getShadersPath() + "ibl/genbrdflut.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
	//	shaderStages[1] = loadShader(getShadersPath() + "ibl/genbrdflut.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
	//	VkPipeline pipeline;
	//	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));

	//	// Render
	//	VkCommandBuffer cmdBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

	//	vks::tools::insertImageMemoryBarrier2(cmdBuffer, textures.lutBrdf.image,
	//		0, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
	//		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
	//		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
	//		{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

	//	vkCmdBeginRendering(cmdBuffer, &renderingInfo);

	//	VkViewport viewport = vks::initializers::viewport((float)dim, (float)dim, 0.0f, 1.0f);
	//	vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
	//	VkRect2D scissor = vks::initializers::rect2D(dim, dim, 0, 0);
	//	vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

	//	vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	//	vkCmdDraw(cmdBuffer, 3, 1, 0, 0);

	//	vkCmdEndRendering(cmdBuffer);

	//	vks::tools::insertImageMemoryBarrier2(cmdBuffer, textures.lutBrdf.image,
	//		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
	//		VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	//		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
	//		{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
	//	
	//	vulkanDevice->flushCommandBuffer(cmdBuffer, queue);

	//	vkDestroyPipeline(device, pipeline, nullptr);
	//	vkDestroyPipelineLayout(device, pipelinelayout, nullptr);
	//	vkDestroyShaderModule(device, shaderStages[0].module, nullptr);
	//	vkDestroyShaderModule(device, shaderStages[1].module, nullptr);

	//	auto tEnd = std::chrono::high_resolution_clock::now();
	//	auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
	//	std::cout << "Generating BRDF LUT took " << tDiff << " ms" << std::endl;
	//}

	void setupDescriptors()
	{
		// Pool
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxConcurrentFrames * 3),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxConcurrentFrames),
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, maxConcurrentFrames * 2);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 2)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		// Sets per frame, just like the buffers themselves
		// Images do not need to be duplicated per frame, we reuse the same one for each frame
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		for (auto i = 0; i < uniformBuffers.size(); i++) {
			// Scene
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets[i].scene));
			std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
				vks::initializers::writeDescriptorSet(descriptorSets[i].scene, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].scene.descriptor),
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
			// Skybox
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets[i].skybox));
			writeDescriptorSets = {
				vks::initializers::writeDescriptorSet(descriptorSets[i].skybox, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].skybox.descriptor),
				vks::initializers::writeDescriptorSet(descriptorSets[i].skybox, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textures.environmentCube.descriptor)
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}
	}

	void preparePipelines()
	{
		VkPipelineRenderingCreateInfo pipelineRenderingInfo = vks::initializers::pipelineRenderingCreateInfo();
		pipelineRenderingInfo.colorAttachmentCount = 1;
		pipelineRenderingInfo.pColorAttachmentFormats = &swapChain.colorFormat;
		pipelineRenderingInfo.depthAttachmentFormat = depthFormat;
		pipelineRenderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);

		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);

		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);

		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);

		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);

		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout);
		vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout);

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo();
		pipelineCI.pNext = &pipelineRenderingInfo;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal, vkglTF::VertexComponent::UV, vkglTF::VertexComponent::Tangent });
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.layout = pipelineLayout;

#ifdef _DEBUG
		shaderStages[0] = loadShader(getShadersPath() + "PBR/scene_debug.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "PBR/scene_debug.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
#else
		shaderStages[0] = loadShader(getShadersPath() + "PBR/scene.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "PBR/scene.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
#endif
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.scene));

#ifdef _DEBUG
		shaderStages[0] = loadShader(getShadersPath() + "PBR/skybox_debug.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "PBR/skybox_debug.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
#else
		shaderStages[0] = loadShader(getShadersPath() + "PBR/skybox.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "PBR/skybox.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
#endif
		rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
		depthStencilState.depthTestEnable = VK_FALSE;
		depthStencilState.depthWriteEnable = VK_FALSE;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.skybox));
	}

	void prepareUniformBuffers()
	{
		for (auto& buffer : uniformBuffers)
		{
			VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer.scene, sizeof(uniformMatrices)));
			VK_CHECK_RESULT(buffer.scene.map());
			VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer.skybox, sizeof(uniformMatrices)));
			VK_CHECK_RESULT(buffer.skybox.map());
		}
	}

	void updateUniformBuffers()
	{
		// scene
		uniformMatrices.projection = camera.matrices.perspective;
		uniformMatrices.view = camera.matrices.view;
		uniformMatrices.model = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		uniformMatrices.cameraPos = -camera.position;
		memcpy(uniformBuffers[currentBuffer].scene.mapped, &uniformMatrices, sizeof(uniformMatrices));
		// skybox
		uniformMatrices.view = glm::mat4(glm::mat3(camera.matrices.view));
		memcpy(uniformBuffers[currentBuffer].skybox.mapped, &uniformMatrices, sizeof(uniformMatrices));
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadAssets();
		generateIrradianceSH();
		//generateBRDFLUT();
		//generateIrradianceCube();
		prepareUniformBuffers();
		setupDescriptors();
		preparePipelines();
		prepared = true;
	}

	void buildCommandBuffer()
	{
		VkCommandBuffer cmdBuffer = drawCmdBuffers[currentBuffer];

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));

		vks::tools::insertImageMemoryBarrier2(cmdBuffer, swapChain.images[currentImageIndex],
			0, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		VkRenderingAttachmentInfo colorAttachment = vks::initializers::renderingAttachmentInfo();
		colorAttachment.imageView = swapChain.imageViews[currentImageIndex];
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.clearValue.color = defaultClearColor;

		VkRenderingAttachmentInfo depthStencilAttachment = vks::initializers::renderingAttachmentInfo();
		depthStencilAttachment.imageView = depthStencil.view;
		depthStencilAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
		depthStencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthStencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthStencilAttachment.clearValue = { 1.0f, 0 };

		VkRenderingInfo renderingInfo = vks::initializers::renderingInfo();
		renderingInfo.renderArea.extent = { width, height };
		renderingInfo.layerCount = 1;
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachments = &colorAttachment;
		renderingInfo.pDepthAttachment = &depthStencilAttachment;

		vkCmdBeginRendering(cmdBuffer, &renderingInfo);

		VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
		vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
		VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
		vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);
		
		if (displaySkybox)
		{
			vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.skybox);
			vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentBuffer].skybox, 0, nullptr);
			models.skybox.draw(cmdBuffer);
		}

		vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.scene);
		vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentBuffer].scene, 0, nullptr);
		models.object.draw(cmdBuffer);

		drawUI(cmdBuffer);

		vkCmdEndRendering(cmdBuffer);

		vks::tools::insertImageMemoryBarrier2(cmdBuffer, swapChain.images[currentImageIndex],
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 0,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
			{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		vkEndCommandBuffer(cmdBuffer);
	}

	virtual void render()
	{
		if (!prepared)
			return;
		VulkanExampleBase::prepareFrame();
		updateUniformBuffers();
		buildCommandBuffer();
		VulkanExampleBase::submitFrame();
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay)
	{
		if (overlay->header("Settings")) {
			overlay->checkBox("Skybox", &displaySkybox);
		}
	}
};

VULKAN_EXAMPLE_MAIN()
