#include "vulkanexamplebase.h"


class VulkanExample : public VulkanExampleBase
{
public:
	struct Vertex
	{
		glm::vec3 position;
		glm::vec3 color;
	};

	struct VulkanBuffer
	{
		VkBuffer buffer;
		VkDeviceMemory memory;
	};

	VulkanBuffer vertexBuffer;
	VulkanBuffer indexBuffer;
	uint32_t indexCount{ 0 };

	struct UniformData
	{
		glm::mat4 projection;
		glm::mat4 view;
	};

	struct UniformBuffer : VulkanBuffer
	{
		VkDescriptorSet descriptorSet;
		void* mapped;
	};
	std::array<UniformBuffer, maxConcurrentFrames> uniformBuffers;

	std::array<VkFence, maxConcurrentFrames> waitFences;
	std::array<VkSemaphore, maxConcurrentFrames> presentCompleteSemaphores;
	std::vector<VkSemaphore> renderCompleteSemaphores;
	
	VkCommandPool commandPool{ VK_NULL_HANDLE };
	std::array<VkCommandBuffer, maxConcurrentFrames> drawCmdBuffers;

	VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };
	
	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
	VkPipeline pipeline{ VK_NULL_HANDLE };

	uint32_t currentFrame{ 0 };
	std::vector<VkImageLayout> swapChainImageLayouts;
	VkImageLayout depthImageLayout{ VK_IMAGE_LAYOUT_UNDEFINED };

	VulkanExample() : VulkanExampleBase()
	{
		title = "Base Triangle";
		settings.overlay = false;
		camera.setPosition(glm::vec3(0.0f, 0.0f, -2.5f));
		camera.setPerspective(60.0f, float(width) / float(height), 0.1f, 256.0f);
	}

	~VulkanExample()
	{
		if (device)
		{
			vkDestroyCommandPool(device, commandPool, nullptr);
			vkDestroyPipeline(device, pipeline, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			vkDestroyBuffer(device, vertexBuffer.buffer, nullptr);
			vkFreeMemory(device, vertexBuffer.memory, nullptr);
			vkDestroyBuffer(device, indexBuffer.buffer, nullptr);
			vkFreeMemory(device, indexBuffer.memory, nullptr);
			for (size_t i = 0; i < maxConcurrentFrames; i++)
			{
				vkDestroyBuffer(device, uniformBuffers[i].buffer, nullptr);
				vkFreeMemory(device, uniformBuffers[i].memory, nullptr);
				vkDestroySemaphore(device, presentCompleteSemaphores[i], nullptr);
				vkDestroyFence(device, waitFences[i], nullptr);
			}
			for (VkSemaphore semaphore : renderCompleteSemaphores)
			{
				vkDestroySemaphore(device, semaphore, nullptr);
			}
		}
	}

	uint32_t getMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties)
	{
		for (uint32_t i = 0; i < deviceMemoryProperties.memoryTypeCount; i++)
		{
			if (typeBits & (1 << i))
			{
				if ((deviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
				{
					return i;
				}
			}
		}
		throw "Could not find a suitable memory type!";
	}

	void createSynchronizationPrimitives()
	{
		VkFenceCreateInfo fenceInfo = vks::initializers::fenceCreateInfo();
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		VkSemaphoreCreateInfo semaphoreInfo = vks::initializers::semaphoreCreateInfo();
		
		for (size_t i = 0; i < maxConcurrentFrames; i++)
		{
			VK_CHECK_RESULT(vkCreateFence(device, &fenceInfo, nullptr, &waitFences[i]));
			VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &presentCompleteSemaphores[i]));
		}

		renderCompleteSemaphores.resize(swapChain.imageCount);
		for (size_t i = 0; i < swapChain.imageCount; i++)
		{
			VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderCompleteSemaphores[i]));
		}
	}

	void createCommandBuffers()
	{
		VkCommandPoolCreateInfo cmdPoolInfo = vks::initializers::commandPoolCreateInfo();
		cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		cmdPoolInfo.queueFamilyIndex = swapChain.queueNodeIndex;
		VK_CHECK_RESULT(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &commandPool));

		VkCommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::commandBufferAllocateInfo(commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, maxConcurrentFrames);
		VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, drawCmdBuffers.data()));
	}

	void createVertexBuffer()
	{
		const std::vector<Vertex> vertices
		{
			{ {  1.0f,  1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
			{ { -1.0f,  1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
			{ {  0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } }
		};
		uint32_t vertexBufferSize = static_cast<uint32_t>(sizeof(Vertex) * vertices.size());

		std::vector<uint32_t> indices = { 0, 1, 2 };
		indexCount = static_cast<uint32_t>(indices.size());
		uint32_t indexBufferSize = static_cast<uint32_t>(sizeof(uint32_t) * indices.size());

		// staging buffer
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingBufferMemory;
		VkBufferCreateInfo stagingBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vertexBufferSize + indexBufferSize);
		VK_CHECK_RESULT(vkCreateBuffer(device, &stagingBufferInfo, nullptr, &stagingBuffer));
		VkMemoryRequirements memReqs;
		vkGetBufferMemoryRequirements(device, stagingBuffer, &memReqs);
		VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &stagingBufferMemory));
		VK_CHECK_RESULT(vkBindBufferMemory(device, stagingBuffer, stagingBufferMemory, 0));

		// copy vertex data to staging buffer
		void* data;
		VK_CHECK_RESULT(vkMapMemory(device, stagingBufferMemory, 0, vertexBufferSize + indexBufferSize, 0, &data));
		memcpy(data, vertices.data(), vertexBufferSize);
		memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);

		// vertex buffer
		VkBufferCreateInfo vertexBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vertexBufferSize);
		VK_CHECK_RESULT(vkCreateBuffer(device, &vertexBufferInfo, nullptr, &vertexBuffer.buffer));
		vkGetBufferMemoryRequirements(device, vertexBuffer.buffer, &memReqs);
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &vertexBuffer.memory));
		VK_CHECK_RESULT(vkBindBufferMemory(device, vertexBuffer.buffer, vertexBuffer.memory, 0));
		
		// index buffer
		VkBufferCreateInfo indexBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, indexBufferSize);
		VK_CHECK_RESULT(vkCreateBuffer(device, &indexBufferInfo, nullptr, &indexBuffer.buffer));
		vkGetBufferMemoryRequirements(device, indexBuffer.buffer, &memReqs);
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &indexBuffer.memory));
		VK_CHECK_RESULT(vkBindBufferMemory(device, indexBuffer.buffer, indexBuffer.memory, 0));

		// copy vertex data to device local buffer
		VkCommandBuffer copyCmd;
		VkCommandBufferAllocateInfo copyCmdAllocInfo = vks::initializers::commandBufferAllocateInfo(commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
		VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &copyCmdAllocInfo, &copyCmd));

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(copyCmd, &cmdBufInfo));

		VkBufferCopy copyRegion = {};
		copyRegion.size = vertexBufferSize;
		vkCmdCopyBuffer(copyCmd, stagingBuffer, vertexBuffer.buffer, 1, &copyRegion);

		copyRegion.srcOffset = vertexBufferSize;
		copyRegion.size = indexBufferSize;
		vkCmdCopyBuffer(copyCmd, stagingBuffer, indexBuffer.buffer, 1, &copyRegion);

		VK_CHECK_RESULT(vkEndCommandBuffer(copyCmd));

		// submit copy command buffer
		VkFence copyFence;
		VkFenceCreateInfo copyFenceInfo = vks::initializers::fenceCreateInfo();
		VK_CHECK_RESULT(vkCreateFence(device, &copyFenceInfo, nullptr, &copyFence));

		VkSubmitInfo copySubmitInfo = vks::initializers::submitInfo();
		copySubmitInfo.commandBufferCount = 1;
		copySubmitInfo.pCommandBuffers = &copyCmd;
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &copySubmitInfo, copyFence));
		VK_CHECK_RESULT(vkWaitForFences(device, 1, &copyFence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));

		vkDestroyFence(device, copyFence, nullptr);
		vkDestroyBuffer(device, stagingBuffer, nullptr);
		vkFreeMemory(device, stagingBufferMemory, nullptr);
	}

	void createUniformBuffers()
	{
		VkBufferCreateInfo uniformBufferInfo = vks::initializers::bufferCreateInfo(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(UniformData));
		
		VkMemoryRequirements memReqs;
		VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();

		for (auto& uniformBuffer : uniformBuffers)
		{
			VK_CHECK_RESULT(vkCreateBuffer(device, &uniformBufferInfo, nullptr, &uniformBuffer.buffer));

			vkGetBufferMemoryRequirements(device, uniformBuffer.buffer, &memReqs);
			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &uniformBuffer.memory));
			VK_CHECK_RESULT(vkBindBufferMemory(device, uniformBuffer.buffer, uniformBuffer.memory, 0));
			VK_CHECK_RESULT(vkMapMemory(device, uniformBuffer.memory, 0, sizeof(UniformData), 0, &uniformBuffer.mapped));
		}
	}

	void createDescriptors()
	{
		std::vector<VkDescriptorPoolSize> poolSize =
		{
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxConcurrentFrames),
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSize, maxConcurrentFrames);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		VkDescriptorSetLayoutBinding setLayoutBindings = vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);
		
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = vks::initializers::descriptorSetLayoutCreateInfo(&setLayoutBindings, 1);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutInfo, nullptr, &descriptorSetLayout));

		for (auto& uniformBuffer : uniformBuffers)
		{
			VkDescriptorSetAllocateInfo descriptorSetAllocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &uniformBuffer.descriptorSet));

			VkDescriptorBufferInfo descriptorBufferInfo{};
			descriptorBufferInfo.buffer = uniformBuffer.buffer;
			descriptorBufferInfo.range = sizeof(UniformData);

			VkWriteDescriptorSet writeDescriptor = vks::initializers::writeDescriptorSet(uniformBuffer.descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &descriptorBufferInfo);
			vkUpdateDescriptorSets(device, 1, &writeDescriptor, 0, nullptr);
		}
	}

	void createPipeline()
	{
		VkPipelineRenderingCreateInfo renderingInfo = vks::initializers::pipelineRenderingCreateInfo();
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachmentFormats = &swapChain.colorFormat;
		renderingInfo.depthAttachmentFormat = depthFormat;
		renderingInfo.stencilAttachmentFormat = depthFormat;

		std::vector<VkPipelineShaderStageCreateInfo> shaderStages =
		{
			loadShader(getShadersPath() + "triangle/triangle.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
			loadShader(getShadersPath() + "triangle/triangle.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
		};

		std::vector<VkVertexInputBindingDescription> vertexInputBinding =
		{
			vks::initializers::vertexInputBindingDescription(0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX)
		};
		std::vector<VkVertexInputAttributeDescription> vertexInputAttribute = 
		{
			vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)),
			vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color))
		};
		VkPipelineVertexInputStateCreateInfo vertexInputState = vks::initializers::pipelineVertexInputStateCreateInfo(vertexInputBinding, vertexInputAttribute);

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1);

		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);

		VkPipelineMultisampleStateCreateInfo mutilsampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);

		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS);

		VkPipelineColorBlendAttachmentState colorBlendAttachment = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &colorBlendAttachment);

		std::vector<VkDynamicState> dynamicStates = 
		{
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStates.data(), static_cast<uint32_t>(dynamicStates.size()));

		VkPipelineLayoutCreateInfo pipelineLayoutInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

		VkGraphicsPipelineCreateInfo pipelineInfo = vks::initializers::pipelineCreateInfo();
		pipelineInfo.pNext = &renderingInfo;
		pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineInfo.pStages = shaderStages.data();
		pipelineInfo.pVertexInputState = &vertexInputState;
		pipelineInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizationState;
		pipelineInfo.pMultisampleState = &mutilsampleState;
		pipelineInfo.pDepthStencilState = &depthStencilState;
		pipelineInfo.pColorBlendState = &colorBlendState;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.layout = pipelineLayout;

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineInfo, nullptr, &pipeline));
	}

	void prepare() override
	{
		VulkanExampleBase::prepare();
		createSynchronizationPrimitives();
		createCommandBuffers();
		createVertexBuffer();
		createUniformBuffers();
		createDescriptors();
		createPipeline();
		prepared = true;
	}

	void render() override
	{
		VK_CHECK_RESULT(vkWaitForFences(device, 1, &waitFences[currentFrame], VK_TRUE, DEFAULT_FENCE_TIMEOUT));
		VK_CHECK_RESULT(vkResetFences(device, 1, &waitFences[currentFrame]));
		
		uint32_t currentImageIndex{ 0 };
		VkResult result = vkAcquireNextImageKHR(device, swapChain.swapChain, UINT64_MAX, presentCompleteSemaphores[currentFrame], VK_NULL_HANDLE, &currentImageIndex);
		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			windowResize();
			return;
		}
		else
		{
			VK_CHECK_RESULT(result);
		}

		UniformData uniformData{};
		uniformData.projection = camera.matrices.perspective;
		uniformData.view = camera.matrices.view;
		memcpy(uniformBuffers[currentFrame].mapped, &uniformData, sizeof(uniformData));

		VkCommandBuffer commandBuffer = drawCmdBuffers[currentFrame];
		VK_CHECK_RESULT(vkResetCommandBuffer(commandBuffer, 0));
		VkCommandBufferBeginInfo commandBufferInfo = vks::initializers::commandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &commandBufferInfo));

		vks::tools::insertImageMemoryBarrier2(commandBuffer, swapChain.images[currentImageIndex],
			0, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		// color attachment
		VkRenderingAttachmentInfo colorAttachment = vks::initializers::renderingAttachmentInfo();
		colorAttachment.imageView = swapChain.imageViews[currentImageIndex];
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.clearValue = { 0.1f, 0.1f, 0.1f, 1.0f };
		// depth/stencil attachment
		VkRenderingAttachmentInfo depthStencilAttachment = vks::initializers::renderingAttachmentInfo();
		depthStencilAttachment.imageView = depthStencil.view;
		depthStencilAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthStencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthStencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthStencilAttachment.clearValue = { 1.0f, 0 };

		VkRenderingInfo renderingInfo = vks::initializers::renderingInfo();
		renderingInfo.renderArea = { { 0, 0 }, { width, height } };
		renderingInfo.layerCount = 1;
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachments = &colorAttachment;
		renderingInfo.pDepthAttachment = &depthStencilAttachment;
		renderingInfo.pStencilAttachment = &depthStencilAttachment;

		vkCmdBeginRendering(commandBuffer, &renderingInfo);

		VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		VkViewport viewport{ 0, 0, (float)width, (float)height, 0, 1 };
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		VkRect2D scissor{ { 0, 0 }, { width, height } };
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &uniformBuffers[currentFrame].descriptorSet, 0, nullptr);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		VkDeviceSize offset{ 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer.buffer, &offset);

		vkCmdBindIndexBuffer(commandBuffer, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

		vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);

		vkCmdEndRendering(commandBuffer);

		vks::tools::insertImageMemoryBarrier2(commandBuffer, swapChain.images[currentImageIndex],
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 0,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
			{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

		VkSubmitInfo submitInfo = vks::initializers::submitInfo();
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &presentCompleteSemaphores[currentFrame];
		submitInfo.pWaitDstStageMask = &waitStageMask;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &renderCompleteSemaphores[currentImageIndex];
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, waitFences[currentFrame]));
	
		VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &renderCompleteSemaphores[currentImageIndex];
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapChain.swapChain;
		presentInfo.pImageIndices = &currentImageIndex;
		result = vkQueuePresentKHR(queue, &presentInfo);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
		{
			windowResize();
		}
		else
		{
			VK_CHECK_RESULT(result);
		}

		currentFrame = (currentFrame + 1) % maxConcurrentFrames;
	}
};

VULKAN_EXAMPLE_MAIN()