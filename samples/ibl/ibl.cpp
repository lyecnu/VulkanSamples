#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

#define SHADOW_MAP_SIZE 2048

struct Material
{
	struct
	{
		float roughness;
		float metallic;
		float r, g, b;
	} params;
	std::string name;
	Material() {};
	Material(std::string n, glm::vec3 color, float r, float m) :
		name(n), params{r, m, color.r, color.g, color.b } {}
};

class VulkanExample : public VulkanExampleBase
{
public:
	struct
	{
		std::vector<vkglTF::Model> objects;
		int32_t objectIndex = 0;
	} models;

	struct UniformBuffers
	{
		vks::Buffer scene;
		vks::Buffer params;
	};
	std::array<UniformBuffers, maxConcurrentFrames> uniformBuffers;

	struct
	{
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
		glm::vec3 cameraPos;
	} unifomrMatrices;
	struct
	{
		glm::vec4 lights[4];
	} uniformParams;

	std::vector<Material> materials;
	int32_t materialIndex = 0;
	std::vector<std::string> materialNames;
	std::vector<std::string> objectNames;

	VkPipeline pipeline{ VK_NULL_HANDLE };
	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };
	std::array<VkDescriptorSet, maxConcurrentFrames> descriptorSets;

	VulkanExample() : VulkanExampleBase()
	{
		title = "Physical based shading basics";
		camera.type = Camera::CameraType::firstperson;
		camera.setPosition(glm::vec3(10.0f, 13.0f, 1.8f));
		camera.setRotation(glm::vec3(-62.5f, 90.0f, 0.0f));
		camera.movementSpeed = 4.0f;
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
		camera.rotationSpeed = 0.25f;
		timerSpeed *= 0.25f;

		// Setup some default materials (source: https://seblagarde.wordpress.com/2011/08/17/feeding-a-physical-based-lighting-mode/)
		materials.push_back(Material("Gold", glm::vec3(1.0f, 0.765557f, 0.336057f), 0.1f, 1.0f));
		materials.push_back(Material("Copper", glm::vec3(0.955008f, 0.637427f, 0.538163f), 0.1f, 1.0f));
		materials.push_back(Material("Chromium", glm::vec3(0.549585f, 0.556114f, 0.554256f), 0.1f, 1.0f));
		materials.push_back(Material("Nickel", glm::vec3(0.659777f, 0.608679f, 0.525649f), 0.1f, 1.0f));
		materials.push_back(Material("Titanium", glm::vec3(0.541931f, 0.496791f, 0.449419f), 0.1f, 1.0f));
		materials.push_back(Material("Cobalt", glm::vec3(0.662124f, 0.654864f, 0.633732f), 0.1f, 1.0f));
		materials.push_back(Material("Platinum", glm::vec3(0.672411f, 0.637331f, 0.585456f), 0.1f, 1.0f));
		// Testing materials
		materials.push_back(Material("White", glm::vec3(1.0f), 0.1f, 1.0f));
		materials.push_back(Material("Red", glm::vec3(1.0f, 0.0f, 0.0f), 0.1f, 1.0f));
		materials.push_back(Material("Blue", glm::vec3(0.0f, 0.0f, 1.0f), 0.1f, 1.0f));
		materials.push_back(Material("Black", glm::vec3(0.0f), 0.1f, 1.0f));

		for (auto& material : materials) {
			materialNames.push_back(material.name);
		}
		objectNames = { "Sphere", "Teapot", "Torusknot", "Venus" };

		materialIndex = 0;
	}

	~VulkanExample()
	{
		if (device)
		{
			vkDestroyPipeline(device, pipeline, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			for (auto& buffer : uniformBuffers)
			{
				buffer.scene.destroy();
				buffer.params.destroy();
			}
		}
	}

	void loadAssets()
	{
		std::vector<std::string> filenames = { "sphere.gltf", "teapot.gltf", "torusknot.gltf", "venus.gltf" };
		models.objects.resize(filenames.size());
		for (size_t i = 0; i < filenames.size(); i++)
		{
			models.objects[i].loadFromFile(getAssetPath() + filenames[i], vulkanDevice, queue, vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::FlipY);
		}
	}

	void setupDescriptors()
	{
		// Pool
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxConcurrentFrames * 2)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, maxConcurrentFrames);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		// Sets per frame, just like the buffers themselves
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		for (auto i = 0; i < uniformBuffers.size(); i++) {
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets[i]));
			std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
				vks::initializers::writeDescriptorSet(descriptorSets[i], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].scene.descriptor),
				vks::initializers::writeDescriptorSet(descriptorSets[i], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, &uniformBuffers[i].params.descriptor)
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

#ifdef _DEBUG
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages =
		{
			loadShader(getShadersPath() + "ibl/ibl_debug.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
			loadShader(getShadersPath() + "ibl/ibl_debug.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
		};
#else
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages =
		{
			loadShader(getShadersPath() + "ibl/ibl.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
			loadShader(getShadersPath() + "ibl/ibl.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
		};
#endif

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
		std::vector<VkPushConstantRange> pushConstantRanges =
		{
			vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::vec3), 0),
			vks::initializers::pushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(Material::params), sizeof(glm::vec3))
		};
		pipelineLayoutCreateInfo.pushConstantRangeCount = 2;
		pipelineLayoutCreateInfo.pPushConstantRanges = pushConstantRanges.data();
		vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout);

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo();
		pipelineCI.pNext = &pipelineRenderingInfo;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal });
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.layout = pipelineLayout;

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
	}

	void prepareUniformBuffers()
	{
		for (auto& buffer : uniformBuffers) {
			VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer.scene, sizeof(unifomrMatrices)));
			VK_CHECK_RESULT(buffer.scene.map());
			VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer.params, sizeof(uniformParams)));
			VK_CHECK_RESULT(buffer.params.map());
		}
	}

	void updateLights()
	{
		const float p = 15.0f;
		uniformParams.lights[0] = glm::vec4(-p, -p * 0.5f, -p, 1.0f);
		uniformParams.lights[1] = glm::vec4(-p, -p * 0.5f, p, 1.0f);
		uniformParams.lights[2] = glm::vec4(p, -p * 0.5f, p, 1.0f);
		uniformParams.lights[3] = glm::vec4(p, -p * 0.5f, -p, 1.0f);

		if (!paused)
		{
			// Two lights are static, the other two move around
			uniformParams.lights[0].x = sin(glm::radians(timer * 360.0f)) * 20.0f;
			uniformParams.lights[0].z = cos(glm::radians(timer * 360.0f)) * 20.0f;
			uniformParams.lights[1].x = cos(glm::radians(timer * 360.0f)) * 20.0f;
			uniformParams.lights[1].y = sin(glm::radians(timer * 360.0f)) * 20.0f;
		}

		memcpy(uniformBuffers[currentBuffer].params.mapped, &uniformParams, sizeof(uniformParams));
	}

	void updateUniformBuffers()
	{
		unifomrMatrices.projection = camera.matrices.perspective;
		unifomrMatrices.view = camera.matrices.view;
		unifomrMatrices.model = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f + (models.objectIndex == 1 ? 45.0f : 0.0f)), glm::vec3(0.0f, 1.0f, 0.0f));
		unifomrMatrices.cameraPos = -camera.position;
		memcpy(uniformBuffers[currentBuffer].scene.mapped, &unifomrMatrices, sizeof(unifomrMatrices));
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadAssets();
		prepareUniformBuffers();
		setupDescriptors();
		preparePipelines();
		prepared = true;
	}

	void buildCommandBuffer()
	{
		//VkCommandBuffer cmdBuffer = drawCmdBuffers[currentBuffer];

		//VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		//VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));

		///*
		//	First pass: Generate shadow map by rendering the scene from light's POV
		//*/
		//{
		//	vks::tools::insertImageMemoryBarrier2(cmdBuffer, depthMapAttachment.image,
		//		0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		//		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
		//		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
		//		{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 });

		//	VkRenderingAttachmentInfo depthStencilAttachment = vks::initializers::renderingAttachmentInfo();
		//	depthStencilAttachment.imageView = depthMapAttachment.view;
		//	depthStencilAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
		//	depthStencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		//	depthStencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		//	depthStencilAttachment.clearValue = { 1.0f, 0 };

		//	VkRenderingInfo renderingInfo = vks::initializers::renderingInfo();
		//	renderingInfo.renderArea.extent = { depthMapAttachment.width, depthMapAttachment.height };
		//	renderingInfo.layerCount = 1;
		//	renderingInfo.colorAttachmentCount = 0;
		//	renderingInfo.pColorAttachments = nullptr;
		//	renderingInfo.pDepthAttachment = &depthStencilAttachment;

		//	vkCmdBeginRendering(cmdBuffer, &renderingInfo);

		//	VkViewport viewport = vks::initializers::viewport((float)depthMapAttachment.width, (float)depthMapAttachment.height, 0.0f, 1.0f);
		//	vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
		//	VkRect2D scissor = vks::initializers::rect2D(depthMapAttachment.width, depthMapAttachment.height, 0, 0);
		//	vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

		//	vkCmdSetDepthBias(cmdBuffer, depthBiasConstant, 0.0f, depthBiasSlope);

		//	vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentBuffer].depthMap, 0, nullptr);
		//	vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.depthMap);

		//	scenes[sceneIndex].draw(cmdBuffer);

		//	vkCmdEndRendering(cmdBuffer);

		//	vks::tools::insertImageMemoryBarrier2(cmdBuffer, depthMapAttachment.image,
		//		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
		//		VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
		//		VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		//		{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 });
		//}

		//VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffer));
	}

	virtual void render()
	{
		if (!prepared)
			return;
		VulkanExampleBase::prepareFrame();
		updateLights();
		updateUniformBuffers();
		buildCommandBuffer();
		VulkanExampleBase::submitFrame();
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay)
	{
		if (overlay->header("Settings"))
		{
			overlay->comboBox("Material", &materialIndex, materialNames);
			overlay->comboBox("Object type", &models.objectIndex, objectNames);
		}
	}
};

VULKAN_EXAMPLE_MAIN()
