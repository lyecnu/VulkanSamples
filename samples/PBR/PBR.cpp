#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

class VulkanExample : public VulkanExampleBase
{
public:
	bool displaySkybox = true;

	struct Textures {
		vks::TextureCubeMap environmentCube;
		// Generated at runtime
		vks::TextureCubeMap irradianceCube;
		vks::TextureCubeMap prefilteredCube;
	} textures{};

	struct
	{
		vkglTF::Model skybox;
		std::vector<vkglTF::Model> objects;
		int32_t objectIndex = 0;
	} models;

	struct UniformBuffers
	{
		vks::Buffer scene;
		vks::Buffer skybox;
		vks::Buffer params;
	};
	std::array<UniformBuffers, maxConcurrentFrames> uniformBuffers;

	struct
	{
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
		glm::vec3 cameraPos;
	} uniformMatrices;
	struct
	{
		glm::vec4 lights[4];
	} uniformParams;

	std::vector<Material> materials;
	int32_t materialIndex = 0;
	std::vector<std::string> materialNames;
	std::vector<std::string> objectNames;

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
				buffer.params.destroy();
			}
			textures.environmentCube.destroy();
			textures.irradianceCube.destroy();
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
		uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::FlipY;
		// Skybox
		models.skybox.loadFromFile(getAssetPath() + "models/cube.gltf", vulkanDevice, queue, glTFLoadingFlags);
		// Objects
		std::vector<std::string> filenames = { "sphere.gltf", "teapot.gltf", "torusknot.gltf", "venus.gltf" };
		models.objects.resize(filenames.size());
		for (size_t i = 0; i < filenames.size(); i++) {
			models.objects[i].loadFromFile(getAssetPath() + "models/" + filenames[i], vulkanDevice, queue, glTFLoadingFlags);
		}
		// HDR cubemap
		textures.environmentCube.loadFromFile(getAssetPath() + "textures/hdr/pisa_cube.ktx", VK_FORMAT_R16G16B16A16_SFLOAT, vulkanDevice, queue);
	}

	void generateIrradianceCube()
	{
		auto tStart = std::chrono::high_resolution_clock::now();

		uint32_t dim = 64;
		VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;

		// Image
		VkImageCreateInfo imageInfo = vks::initializers::imageCreateInfo();
		imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = format;
		imageInfo.extent = { dim, dim, 1 };
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 6;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VK_CHECK_RESULT(vkCreateImage(device, &imageInfo, nullptr, &textures.irradianceCube.image));

		// Memory
		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(device, textures.irradianceCube.image, &memReqs);
		VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &textures.irradianceCube.deviceMemory));
		VK_CHECK_RESULT(vkBindImageMemory(device, textures.irradianceCube.image, textures.irradianceCube.deviceMemory, 0));

		// Image View
		VkImageViewCreateInfo imageViewInfo = vks::initializers::imageViewCreateInfo();
		imageViewInfo.image = textures.irradianceCube.image;
		imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		imageViewInfo.format = format;
		imageViewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 };
		VK_CHECK_RESULT(vkCreateImageView(device, &imageViewInfo, nullptr, &textures.irradianceCube.view));

		// Sampler
		VkSamplerCreateInfo samplerCreateInfo = vks::initializers::samplerCreateInfo();
		samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
		samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
		samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCreateInfo.minLod = 0.0f;
		samplerCreateInfo.maxLod = 1.0f;
		samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(device, &samplerCreateInfo, nullptr, &textures.irradianceCube.sampler));

		textures.irradianceCube.descriptor.sampler = textures.irradianceCube.sampler;
		textures.irradianceCube.descriptor.imageView = textures.irradianceCube.view;
		textures.irradianceCube.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		textures.irradianceCube.device = vulkanDevice;

		// Offscreen
		struct
		{
			VkImage image;
			VkImageView imageView;
			VkDeviceMemory memory;
		} offscreen;

		{
			// Image
			VkImageCreateInfo imageInfo = vks::initializers::imageCreateInfo();
			imageInfo.imageType = VK_IMAGE_TYPE_2D;
			imageInfo.format = format;
			imageInfo.extent = { dim, dim, 1 };
			imageInfo.mipLevels = 1;
			imageInfo.arrayLayers = 1;
			imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VK_CHECK_RESULT(vkCreateImage(device, &imageInfo, nullptr, &offscreen.image));

			// Memory
			VkMemoryRequirements memReqs;
			vkGetImageMemoryRequirements(device, offscreen.image, &memReqs);
			VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
			memAlloc.allocationSize = memReqs.size;
			memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &offscreen.memory));
			VK_CHECK_RESULT(vkBindImageMemory(device, offscreen.image, offscreen.memory, 0));

			// Image View
			VkImageViewCreateInfo imageViewInfo = vks::initializers::imageViewCreateInfo();
			imageViewInfo.image = offscreen.image;
			imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			imageViewInfo.format = format;
			imageViewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			VK_CHECK_RESULT(vkCreateImageView(device, &imageViewInfo, nullptr, &offscreen.imageView));
		}

		// Descriptorset
		VkDescriptorPool descriptorPool;
		std::vector<VkDescriptorPoolSize> poolSize = { vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1) };
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSize, 1);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		VkDescriptorSetLayout descriptorSetLayout;
		std::vector<VkDescriptorSetLayoutBinding> setlayoutBindings = { vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0) };
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = vks::initializers::descriptorSetLayoutCreateInfo(setlayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutInfo, nullptr, &descriptorSetLayout));

		VkDescriptorSet descriptorSet;
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));
		VkWriteDescriptorSet writeDescriptorSet = vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &textures.environmentCube.descriptor);
		vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);

		// Pipeline
		VkPipelineRenderingCreateInfo pipelineRenderingInfo = vks::initializers::pipelineRenderingCreateInfo();
		pipelineRenderingInfo.colorAttachmentCount = 1;
		pipelineRenderingInfo.pColorAttachmentFormats = &format;

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
		VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages =
		{
			loadShader(getShadersPath() + "ibl/cubemap.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
			loadShader(getShadersPath() + "ibl/irradianceCube.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
		};

		struct
		{
			glm::mat4 mvp;
		} pushBlock;
		VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(pushBlock), 0);
		VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(nullptr, 0);
		pipelineLayoutCI.pushConstantRangeCount = 1;
		pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
			
		VkPipelineLayout pipelinelayout;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelinelayout));

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo();
		pipelineCI.pNext = &pipelineRenderingInfo;
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.stageCount = 2;
		pipelineCI.pStages = shaderStages.data();
		pipelineCI.pVertexInputState = &emptyInputState;
		pipelineCI.layout = pipelinelayout;

		VkPipeline pipeline;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));

		// Prepare Rendering 
		std::vector<glm::mat4> matrices = {
			// POSITIVE_X
			glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			// NEGATIVE_X
			glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			// POSITIVE_Y
			glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			// NEGATIVE_Y
			glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			// POSITIVE_Z
			glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			// NEGATIVE_Z
			glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
		};

		VkRenderingAttachmentInfo colorAttachment = vks::initializers::renderingAttachmentInfo();
		colorAttachment.imageView = offscreen.imageView;
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.clearValue.color = defaultClearColor;

		VkRenderingInfo renderingInfo = vks::initializers::renderingInfo();
		renderingInfo.renderArea.extent = { dim, dim };
		renderingInfo.layerCount = 1;
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachments = &colorAttachment;

		VkCommandBuffer cmdBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		// Rendering
		{
			// Offscreen
			vks::tools::insertImageMemoryBarrier2(cmdBuffer, offscreen.image, 
				0, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
				VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
			// Irradiance Cube
			vks::tools::insertImageMemoryBarrier2(cmdBuffer, textures.irradianceCube.image,
				0, VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 });

			for (uint32_t faceID = 0; faceID < 6; faceID++)
			{
				vkCmdBeginRendering(cmdBuffer, &renderingInfo);

				VkViewport viewport = vks::initializers::viewport((float)dim, (float)dim, 0.0f, 1.0f);
				vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
				VkRect2D scissor = vks::initializers::rect2D(dim, dim, 0, 0);
				vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

				pushBlock.mvp = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 512.0f) * matrices[faceID];
				vkCmdPushConstants(cmdBuffer, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushBlock), &pushBlock);

				vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
				vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinelayout, 0, 1, &descriptorSet, 0, nullptr);

				vkCmdDraw(cmdBuffer, 3, 1, 0, 0);

				vkCmdEndRendering(cmdBuffer);

				// Offscreen
				vks::tools::insertImageMemoryBarrier2(cmdBuffer, offscreen.image,
					VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
					VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
					{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

				VkImageCopy copyRegion = {};
				copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
				copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, faceID, 1 };
				copyRegion.extent = { dim, dim, 1 };
				vkCmdCopyImage(cmdBuffer, offscreen.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, textures.irradianceCube.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

				// Offscreen
				vks::tools::insertImageMemoryBarrier2(cmdBuffer, offscreen.image,
					VK_ACCESS_2_TRANSFER_READ_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
					VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
					{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
			}

			// Irradiance Cube
			vks::tools::insertImageMemoryBarrier2(cmdBuffer, textures.irradianceCube.image,
				VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
				{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 });

			vulkanDevice->flushCommandBuffer(cmdBuffer, queue);
		}

		vkDestroyImageView(device, offscreen.imageView, nullptr);
		vkDestroyImage(device, offscreen.image, nullptr);
		vkFreeMemory(device, offscreen.memory, nullptr);
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		vkDestroyPipeline(device, pipeline, nullptr);
		vkDestroyPipelineLayout(device, pipelinelayout, nullptr);
		vkDestroyShaderModule(device, shaderStages[0].module, nullptr);
		vkDestroyShaderModule(device, shaderStages[1].module, nullptr);

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
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxConcurrentFrames * 2 * 2),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxConcurrentFrames * 1 * 2),
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, maxConcurrentFrames);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		// Sets per frame, just like the buffers themselves
		// Images do not need to be duplicated per frame, we reuse the same one for each frame
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		for (auto i = 0; i < uniformBuffers.size(); i++) {
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets[i].scene));
			std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
				vks::initializers::writeDescriptorSet(descriptorSets[i].scene, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].scene.descriptor),
				vks::initializers::writeDescriptorSet(descriptorSets[i].scene, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, &uniformBuffers[i].params.descriptor),
				vks::initializers::writeDescriptorSet(descriptorSets[i].scene, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &textures.irradianceCube.descriptor)
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets[i].scene));
			writeDescriptorSets = {
				vks::initializers::writeDescriptorSet(descriptorSets[i].skybox, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].scene.descriptor),
				vks::initializers::writeDescriptorSet(descriptorSets[i].skybox, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, &uniformBuffers[i].params.descriptor),
				vks::initializers::writeDescriptorSet(descriptorSets[i].skybox, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &textures.irradianceCube.descriptor)
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
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal, vkglTF::VertexComponent::UV });
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.layout = pipelineLayout;

#ifdef _DEBUG
		shaderStages[0] = loadShader(getShadersPath() + "ibl/ibl_debug.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "ibl/ibl_debug.frag.spv", VK_SHADER_STAGE_VERTEX_BIT);
#else
		shaderStages[0] = loadShader(getShadersPath() + "ibl/ibl.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "ibl/ibl.frag.spv", VK_SHADER_STAGE_FR_BIT);
#endif
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.scene));

#ifdef _DEBUG
		shaderStages[0] = loadShader(getShadersPath() + "ibl/skybox_debug.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "ibl/skybox_debug.frag.spv", VK_SHADER_STAGE_VERTEX_BIT);
#else
		shaderStages[0] = loadShader(getShadersPath() + "ibl/skybox.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "ibl/skybox.frag.spv", VK_SHADER_STAGE_VERTEX_BIT);
#endif
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.skybox));
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
		generateIrradianceCube();
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

		vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.scene);
		vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentBuffer], 0, nullptr);
		
		Material mat = materials[materialIndex];
		const uint32_t gridSize = 7;

		// Render a 2D grid of objects with varying PBR parameters
		for (uint32_t y = 0; y < gridSize; y++) {
			for (uint32_t x = 0; x < gridSize; x++) {
				glm::vec3 pos = glm::vec3(float(x - (gridSize / 2.0f)) * 2.5f, 0.0f, float(y - (gridSize / 2.0f)) * 2.5f);
				vkCmdPushConstants(cmdBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::vec3), &pos);
				// Vary metallic and roughness, two important PBR parameters
				mat.params.metallic = glm::clamp((float)x / (float)(gridSize - 1), 0.1f, 1.0f);
				mat.params.roughness = glm::clamp((float)y / (float)(gridSize - 1), 0.05f, 1.0f);
				vkCmdPushConstants(cmdBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(glm::vec3), sizeof(Material::params), &mat);
				models.objects[models.objectIndex].draw(cmdBuffer);
			}
		}

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
		updateLights();
		updateUniformBuffers();
		buildCommandBuffer();
		VulkanExampleBase::submitFrame();
	}

	//virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay)
	//{
	//	if (overlay->header("Settings")) {
	//		overlay->comboBox("Material", &materialIndex, materialNames);
	//		overlay->comboBox("Object type", &models.objectIndex, objectNames);
	//		overlay->inputFloat("Exposure", &uniformDataParams.exposure, 0.1f, 2);
	//		overlay->inputFloat("Gamma", &uniformDataParams.gamma, 0.1f, 2);
	//		overlay->checkBox("Skybox", &displaySkybox);
	//	}
	//}
};

VULKAN_EXAMPLE_MAIN()
