#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

#define SHADOW_MAP_SIZE 2048

class VulkanExample : public VulkanExampleBase
{
public:
	float zNear = 1.0f;
	float zFar = 256.0f;

	glm::vec3 lightPos;
	float lightFov = 45.0f;

	std::vector<vkglTF::Model> scenes;

	struct
	{
		glm::mat4 depthMVP;
	} shadowMapData;

	struct UniformData {
		glm::mat4 projection;
		glm::mat4 view;
		glm::vec4 lightPos{ 0.0f, 2.0f, 1.0f, 0.0f };
	} uniformData;

	struct UniformBuffers
	{
		vks::Buffer depthMap;
		vks::Buffer scene;
	};
	std::array<UniformBuffers, maxConcurrentFrames> uniformBuffers;

	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };

	struct DescriptorSets
	{
		VkDescriptorSet depthMap;
	};
	std::array<DescriptorSets, maxConcurrentFrames> descriptorSets{};

	struct
	{
		VkPipeline depthMap{ VK_NULL_HANDLE };
		VkPipeline scene{ VK_NULL_HANDLE };
	} pipelines;

	struct
	{
		uint32_t width{ SHADOW_MAP_SIZE }, height{ SHADOW_MAP_SIZE };
		VkImage image{ VK_NULL_HANDLE };
		VkImageView view{ VK_NULL_HANDLE };
		VkDeviceMemory memory{ VK_NULL_HANDLE };
	} shadowMapAttachment;

	const VkFormat shadowMapFormat{ VK_FORMAT_D16_UNORM };

	VulkanExample() : VulkanExampleBase()
	{
		title = "Shadow Mapping";
		camera.type = Camera::CameraType::lookat;
		camera.setPosition(glm::vec3(0.0f, 0.0f, -12.5f));
		camera.setRotation(glm::vec3(-25.0f, -390.0f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 1.0f, 256.0f);
	}

	//~VulkanExample()
	//{
	//	if (device)
	//	{
	//		vkDestroyPipeline(device, pipeline, nullptr);
	//		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
	//		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
	//		for (auto& buffer : uniformBuffers) {
	//			buffer.destroy();
	//		}
	//	}
	//}

	void prepareShadowMap()
	{
		VkImageCreateInfo imageInfo = vks::initializers::imageCreateInfo();
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = shadowMapFormat;
		imageInfo.extent = { shadowMapAttachment.width, shadowMapAttachment.height, 1 };
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VK_CHECK_RESULT(vkCreateImage(device, &imageInfo, nullptr, &shadowMapAttachment.image));

		VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(device, shadowMapAttachment.image, &memReqs);
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &shadowMapAttachment.memory));
		VK_CHECK_RESULT(vkBindImageMemory(device, shadowMapAttachment.image, shadowMapAttachment.memory, 0));

		VkImageViewCreateInfo viewInfo = vks::initializers::imageViewCreateInfo();
		viewInfo.image = shadowMapAttachment.image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = shadowMapFormat;
		viewInfo.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
		VK_CHECK_RESULT(vkCreateImageView(device, &viewInfo, nullptr, &shadowMapAttachment.view));
	}

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		scenes.resize(2);
		scenes[0].loadFromFile(getAssetPath() + "models/vulkanscene_shadow.gltf", vulkanDevice, queue, glTFLoadingFlags);
		scenes[1].loadFromFile(getAssetPath() + "models/samplescene.gltf", vulkanDevice, queue, glTFLoadingFlags);
	}

	void setupDescriptors()
	{
		// Pool
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxConcurrentFrames)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, maxConcurrentFrames);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			// Binding 0 : Vertex shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		// Sets per frame, just like the buffers themselves
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		for (auto i = 0; i < uniformBuffers.size(); i++) {
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets[i].depthMap));
			std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
				// Binding 0 : Vertex shader uniform buffer
				vks::initializers::writeDescriptorSet(descriptorSets[i].depthMap, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].depthMap.descriptor)
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}
	}

	void preparePipelines()
	{
		// Rendering Info
		VkPipelineRenderingCreateInfo pipelineRenderingInfo = vks::initializers::pipelineRenderingCreateInfo();
		pipelineRenderingInfo.colorAttachmentCount = 0;
		pipelineRenderingInfo.pColorAttachmentFormats = nullptr;
		pipelineRenderingInfo.depthAttachmentFormat = shadowMapFormat;
		pipelineRenderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

		// shader stages
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages =
		{
			loadShader(getShadersPath() + "shadowmapping/depthMap.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
			loadShader(getShadersPath() + "shadowmapping/depthMap.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
		};

		// Layout
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

		// Most state is shared between all pipelines
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_LINE_WIDTH, };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);


		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo();
		pipelineCI.pNext = &pipelineRenderingInfo;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal, vkglTF::VertexComponent::Color });
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.layout = pipelineLayout;

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.depthMap));
	}

	void prepareUniformBuffers()
	{
		for (auto& buffer : uniformBuffers) {
			VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer.depthMap, sizeof(shadowMapData)));
			VK_CHECK_RESULT(buffer.depthMap.map());
		}
	}

	void updateLight()
	{
		// Animate the light source
		lightPos.x = cos(glm::radians(timer * 360.0f)) * 40.0f;
		lightPos.y = -50.0f + sin(glm::radians(timer * 360.0f)) * 20.0f;
		lightPos.z = 25.0f + sin(glm::radians(timer * 360.0f)) * 5.0f;
	}

	void updateUniformBuffers()
	{
		glm::mat4 lightProjection = glm::perspective(glm::radians(lightFov), 1.0f, zNear, zFar);
		glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 lightModel = glm::mat4(1.0f);
		shadowMapData.depthMVP = lightProjection * lightView * lightModel;
		memcpy(uniformBuffers[currentBuffer].depthMap.mapped, &shadowMapData, sizeof(shadowMapData));
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadAssets();
		prepareShadowMap();
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

		/*
			First render pass: Generate shadow map by rendering the scene from light's POV
		*/
		{
			vks::tools::insertImageMemoryBarrier2(cmdBuffer, swapChain.images[currentImageIndex],
				0, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
				VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

			vks::tools::insertImageMemoryBarrier2(cmdBuffer, shadowMapAttachment.image,
				0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
				VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
				{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 });

			VkRenderingAttachmentInfo depthStencilAttachment = vks::initializers::renderingAttachmentInfo();
			depthStencilAttachment.imageView = shadowMapAttachment.view;
			depthStencilAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
			depthStencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			depthStencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depthStencilAttachment.clearValue = { 1.0f, 0 };

			VkRenderingInfo renderingInfo = vks::initializers::renderingInfo();
			renderingInfo.renderArea.extent = { shadowMapAttachment.width, shadowMapAttachment.height };
			renderingInfo.layerCount = 1;
			renderingInfo.colorAttachmentCount = 0;
			renderingInfo.pColorAttachments = nullptr;
			renderingInfo.pDepthAttachment = &depthStencilAttachment;

			vkCmdBeginRendering(cmdBuffer, &renderingInfo);

			VkViewport viewport = vks::initializers::viewport((float)shadowMapAttachment.width, (float)shadowMapAttachment.height, 0.0f, 1.0f);
			vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
			VkRect2D scissor = vks::initializers::rect2D(shadowMapAttachment.width, shadowMapAttachment.height, 0, 0);
			vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

			vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentBuffer].depthMap, 0, nullptr);
			scenes[0].bindBuffers(cmdBuffer);
			
			vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.depthMap);
			
			scenes[0].bindBuffers(cmdBuffer);

			scenes[0].draw(cmdBuffer);
			
			
			vkCmdEndRendering(cmdBuffer);
		}

			//drawUI(cmdBuffer);

		vks::tools::insertImageMemoryBarrier2(cmdBuffer, swapChain.images[currentImageIndex],
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 0,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
			{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffer));
	}

	virtual void render()
	{
		if (!prepared)
			return;
		VulkanExampleBase::prepareFrame();
		if (!paused || camera.updated)
		{
			updateLight();
		}
		updateUniformBuffers();
		buildCommandBuffer();
		VulkanExampleBase::submitFrame();
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay)
	{
	}
};

VULKAN_EXAMPLE_MAIN()
