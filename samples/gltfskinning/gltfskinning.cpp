
#include "gltfskinning.h"

glm::mat4 VulkanglTFModel::Node::getLocalMatrix()
{
	return glm::translate(glm::mat4(1.0f), translation) * glm::mat4(rotation) * glm::scale(glm::mat4(1.0f), scale) * matrix;
}

VulkanglTFModel::~VulkanglTFModel()
{
	vkDestroyBuffer(vulkanDevice->logicalDevice, vertices.buffer, nullptr);
	vkFreeMemory(vulkanDevice->logicalDevice, vertices.memory, nullptr);
	vkDestroyBuffer(vulkanDevice->logicalDevice, indices.buffer, nullptr);
	vkFreeMemory(vulkanDevice->logicalDevice, indices.memory, nullptr);
	for (auto& image : images)
	{
		vkDestroyImageView(vulkanDevice->logicalDevice, image.texture.view, nullptr);
		vkDestroyImage(vulkanDevice->logicalDevice, image.texture.image, nullptr);
		vkDestroySampler(vulkanDevice->logicalDevice, image.texture.sampler, nullptr);
		vkFreeMemory(vulkanDevice->logicalDevice, image.texture.deviceMemory, nullptr);
	}
	for (auto& skin : skins)
	{
		for (auto& buffer : skin.storageBuffers)
		{
			buffer.destroy();
		}
	}
}

void VulkanglTFModel::loadImages(tinygltf::Model& input)
{
	images.resize(input.images.size());
	for (size_t i = 0; i < input.images.size(); i++)
	{
		tinygltf::Image& gltfImage = input.images[i];
		unsigned char* buffer = nullptr;
		VkDeviceSize bufferSize = 0;
		bool deleteBuffer = false;

		if (gltfImage.component == 3)
		{
			bufferSize = gltfImage.width * gltfImage.height * 4;
			buffer = new unsigned char[bufferSize];
			unsigned char* rgba = buffer;
			unsigned char* rgb = &gltfImage.image[0];
			for (size_t i = 0; i < gltfImage.width * gltfImage.height; ++i)
			{
				memcpy(rgba, rgb, sizeof(unsigned char) * 3);
				rgba += 4;
				rgb += 3;
			}
			deleteBuffer = true;
		}
		else
		{
			buffer = &gltfImage.image[0];
			bufferSize = gltfImage.image.size();
		}
			
		images[i].texture.fromBuffer(buffer, bufferSize, VK_FORMAT_R8G8B8A8_UNORM, gltfImage.width, gltfImage.height, vulkanDevice, copyQueue);
		if (deleteBuffer)
		{
			delete[] buffer;
		}
	}
}

void VulkanglTFModel::loadTextures(tinygltf::Model& input)
{
	textures.resize(input.textures.size());
	for (size_t i = 0; i < input.textures.size(); i++)
	{
		textures[i].imageIndex = input.textures[i].source;
	}
}

void VulkanglTFModel::loadMaterials(tinygltf::Model& input)
{
	materials.resize(input.materials.size());
	for (size_t i = 0; i < input.materials.size(); i++)
	{
		tinygltf::Material& gltfMaterial = input.materials[i];
		if (gltfMaterial.values.find("baseColorFactor") != gltfMaterial.values.end())
		{
			materials[i].baseColorFactor = glm::make_vec4(gltfMaterial.values["baseColorFactor"].ColorFactor().data());
		}
		if (gltfMaterial.values.find("baseColorTexture") != gltfMaterial.values.end())
		{
			materials[i].baseColorTextureIndex = gltfMaterial.values["baseColorTexture"].TextureIndex();
		}
	}
}

VulkanglTFModel::Node* VulkanglTFModel::findNode(Node* parent, uint32_t index)
{
	Node* nodeFound = nullptr;
	if (parent->index == index)
	{
		return parent;
	}
	else
	{
		for (auto& child : parent->children)
		{
			nodeFound = findNode(child, index);
			if (nodeFound)
			{
				break;
			}
		}
	}
	return nodeFound;
}

VulkanglTFModel::Node* VulkanglTFModel::nodeFromIndex(uint32_t index)
{
	Node* nodeFound = nullptr;
	for (auto& node : nodes)
	{
		nodeFound = findNode(node, index);
		if (nodeFound)
		{
			break;
		}
	}
	return nodeFound;
}

void VulkanglTFModel::loadSkins(tinygltf::Model& input)
{
	skins.resize(input.skins.size());
	for (size_t i = 0; i < input.skins.size(); i++)
	{
		tinygltf::Skin& gltfSkin = input.skins[i];
		skins[i].name = gltfSkin.name;
		skins[i].skeletonRoot = nodeFromIndex(gltfSkin.skeleton);
		for (int jointIndex : gltfSkin.joints)
		{
			;
		}
		skins[i].jointIndices = gltfSkin.jointIndices;
	}
}

void VulkanglTFModel::loadNode(const tinygltf::Node& inputNode, const tinygltf::Model& input, VulkanglTFModel::Node* parent, uint32_t nodeIndex, std::vector<uint32_t>& indexBuffer, std::vector<VulkanglTFModel::Vertex>& vertexBuffer)
{
	Node* node = new Node();
	node->parent = parent;
	node->matrix = glm::mat4(1.0f);
	node->index = nodeIndex;
	node->skin = inputNode.skin;

	if (inputNode.translation.size() == 3)
	{
		node->translation = glm::make_vec3(inputNode.translation.data());
	}
	if (inputNode.rotation.size() == 4)
	{
		node->rotation = glm::make_quat(inputNode.rotation.data());
	}
	if (inputNode.scale.size() == 3)
	{
		node->matrix = glm::scale(node->matrix, glm::vec3(glm::make_vec3(inputNode.scale.data())));
	}

		if (inputNode.matrix.size() == 16)
		{
			node->matrix = glm::make_mat4x4(inputNode.matrix.data());
		}
		else
		{

		}

		if (inputNode.children.size() > 0)
		{
			for (size_t i = 0; i < inputNode.children.size(); i++)
			{
				loadNode(input.nodes[inputNode.children[i]], input, node, indexBuffer, vertexBuffer);
			}
		}

		if (inputNode.mesh > -1)
		{
			const tinygltf::Mesh& mesh = input.meshes[inputNode.mesh];
			for (size_t i = 0; i < mesh.primitives.size(); ++i)
			{
				const tinygltf::Primitive& glTFPrimitive = mesh.primitives[i];
				uint32_t firstIndex = static_cast<uint32_t>(indexBuffer.size());
				uint32_t vertexStart = static_cast<uint32_t>(vertexBuffer.size());
				uint32_t indexCount = 0;
				// Vertices
				{
					const float* positionsBuffer = nullptr;
					const float* normalsBuffer = nullptr;
					const float* texCoordsBuffer = nullptr;
					size_t vertexCount = 0;

					// Get buffer data for vertex positions
					if (glTFPrimitive.attributes.find("POSITION") != glTFPrimitive.attributes.end()) {
						const tinygltf::Accessor& accessor = input.accessors[glTFPrimitive.attributes.find("POSITION")->second];
						const tinygltf::BufferView& view = input.bufferViews[accessor.bufferView];
						positionsBuffer = reinterpret_cast<const float*>(&(input.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
						vertexCount = accessor.count;
					}
					// Get buffer data for vertex normals
					if (glTFPrimitive.attributes.find("NORMAL") != glTFPrimitive.attributes.end()) {
						const tinygltf::Accessor& accessor = input.accessors[glTFPrimitive.attributes.find("NORMAL")->second];
						const tinygltf::BufferView& view = input.bufferViews[accessor.bufferView];
						normalsBuffer = reinterpret_cast<const float*>(&(input.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
					}
					// Get buffer data for vertex texture coordinates
					// glTF supports multiple sets, we only load the first one
					if (glTFPrimitive.attributes.find("TEXCOORD_0") != glTFPrimitive.attributes.end()) {
						const tinygltf::Accessor& accessor = input.accessors[glTFPrimitive.attributes.find("TEXCOORD_0")->second];
						const tinygltf::BufferView& view = input.bufferViews[accessor.bufferView];
						texCoordsBuffer = reinterpret_cast<const float*>(&(input.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
					}

					for (size_t v = 0; v < vertexCount; v++)
					{
						Vertex vertex = {};
						vertex.position = glm::vec3(glm::make_vec3(&positionsBuffer[v * 3]));
						vertex.normal = glm::normalize(glm::vec3(normalsBuffer ? glm::make_vec3(&normalsBuffer[v * 3]) : glm::vec3(0.0f)));
						vertex.uv = texCoordsBuffer ? glm::make_vec2(&texCoordsBuffer[v * 2]) : glm::vec3(0.0f);
						vertex.color = glm::vec3(1.0f);
						vertexBuffer.push_back(vertex);
					}
				}
				// Indices
				{
					const tinygltf::Accessor& accessor = input.accessors[glTFPrimitive.indices];
					const tinygltf::BufferView& bufferView = input.bufferViews[accessor.bufferView];
					const tinygltf::Buffer& buffer = input.buffers[bufferView.buffer];

					indexCount += static_cast<uint32_t>(accessor.count);

					// glTF supports different component types of indices
					switch (accessor.componentType) {
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
						const uint32_t* buf = reinterpret_cast<const uint32_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
						for (size_t index = 0; index < accessor.count; index++) {
							indexBuffer.push_back(buf[index] + vertexStart);
						}
						break;
					}
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
						const uint16_t* buf = reinterpret_cast<const uint16_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
						for (size_t index = 0; index < accessor.count; index++) {
							indexBuffer.push_back(buf[index] + vertexStart);
						}
						break;
					}
					case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
						const uint8_t* buf = reinterpret_cast<const uint8_t*>(&buffer.data[accessor.byteOffset + bufferView.byteOffset]);
						for (size_t index = 0; index < accessor.count; index++) {
							indexBuffer.push_back(buf[index] + vertexStart);
						}
						break;
					}
					default:
						std::cerr << "Index component type " << accessor.componentType << " not supported!" << std::endl;
						return;
					}
				}
				Primitive primitive{};
				primitive.firstIndex = firstIndex;
				primitive.indexCount = indexCount;
				primitive.materialIndex = glTFPrimitive.material;
				node->mesh.primitives.push_back(primitive);
			}
		}
		if (parent)
		{
			parent->children.push_back(node);
		}
		else
		{
			nodes.push_back(node);
		}
	}

	void drawNode(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, Node* node)
	{
		if (node->mesh.primitives.size() > 0)
		{
			glm::mat4 nodeMatrix = node->matrix;
			Node* currentParent = node->parent;
			while (currentParent)
			{
				nodeMatrix = currentParent->matrix * nodeMatrix;
				currentParent = currentParent->parent;
			}
			vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &nodeMatrix);
			for (auto& primitive : node->mesh.primitives)
			{
				if (primitive.indexCount > 0)
				{
					auto texture = textures[materials[primitive.materialIndex].baseColorTextureIndex];
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &images[texture.imageIndex].descriptorSet, 0, nullptr);
					vkCmdDrawIndexed(commandBuffer, primitive.indexCount, 1, primitive.firstIndex, 0, 0);
				}
			}
		}
		for (auto& child : node->children)
		{
			drawNode(commandBuffer, pipelineLayout, child);
		}
	}

	void draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout)
	{
		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertices.buffer, offsets);
		vkCmdBindIndexBuffer(commandBuffer, indices.buffer, 0, VK_INDEX_TYPE_UINT32);
		for (auto& node : nodes)
		{
			drawNode(commandBuffer, pipelineLayout, node);
		}
	}
};

class VulkanExample : public VulkanExampleBase
{
public:
	bool wireFrame = false;

	VulkanglTFModel glTFModel;

	struct UniformData
	{
		glm::mat4 projection;
		glm::mat4 view;
		glm::vec3 lightPos = glm::vec3(0.0f, 0.0f, 5.0f);
	} uniformData;
	std::array<vks::Buffer, maxConcurrentFrames> uniformBuffers;

	struct
	{
		VkPipeline solid{ VK_NULL_HANDLE };
		VkPipeline wireframe{ VK_NULL_HANDLE };
	} pipelines;
	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };

	struct DescriptorSetLayouts
	{
		VkDescriptorSetLayout matrices{ VK_NULL_HANDLE };
		VkDescriptorSetLayout textures{ VK_NULL_HANDLE };
	} descriptorSetLayouts;
	std::array<VkDescriptorSet, maxConcurrentFrames> descriptorSets{};

	VulkanExample()
	{
		title = "glTF Model Loading";
		camera.type = Camera::CameraType::lookat;
		camera.flipY = true;
		camera.setPosition(glm::vec3(0.0f, -0.1f, -1.0f));
		camera.setRotation(glm::vec3(0.0f, 45.0f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
	}

	~VulkanExample()
	{
		if (device) {
			vkDestroyPipeline(device, pipelines.solid, nullptr);
			if (pipelines.wireframe != VK_NULL_HANDLE) {
				vkDestroyPipeline(device, pipelines.wireframe, nullptr);
			}
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.matrices, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.textures, nullptr);
			for (auto& buffer : uniformBuffers) {
				buffer.destroy();
			}
		}
	}

	void getEnabledFeatures() override
	{
		if (deviceFeatures.fillModeNonSolid)
		{
			enabledFeatures.fillModeNonSolid = VK_TRUE;
		}
	}

	void loadglTFFile(std::string filename)
	{
		this->device = device;

		tinygltf::Model glTFInput;
		tinygltf::TinyGLTF glTFContext;
		std::string err, warning;

		bool fileLoaded = glTFContext.LoadASCIIFromFile(&glTFInput, &err, &warning, filename);
		glTFModel.vulkanDevice = vulkanDevice;
		glTFModel.copyQueue = queue;

		std::vector<uint32_t> indices;
		std::vector<VulkanglTFModel::Vertex> vertices;

		if (fileLoaded)
		{
			glTFModel.loadImages(glTFInput);
			glTFModel.loadTextures(glTFInput);
			glTFModel.loadMaterials(glTFInput);
			const tinygltf::Scene& scene = glTFInput.scenes[0];
			for (size_t i = 0; i < scene.nodes.size(); ++i)
			{
				const tinygltf::Node node = glTFInput.nodes[scene.nodes[i]];
				glTFModel.loadNode(node, glTFInput, nullptr, indices, vertices);
			}
		}
		else
		{
			vks::tools::exitFatal("Could not load glTF file", -1);
			return;
		}

		size_t vertexBufferSize = vertices.size() * sizeof(VulkanglTFModel::Vertex);
		size_t indexBufferSize = indices.size() * sizeof(uint32_t);
		glTFModel.indices.count = static_cast<int>(indices.size());

		vks::Buffer vertexStaging, indexStaging;
		
		vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&vertexStaging,
			vertexBufferSize,
			vertices.data());
		vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&indexStaging,
			indexBufferSize,
			indices.data());

		vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			vertexBufferSize,
			&glTFModel.vertices.buffer,
			&glTFModel.vertices.memory);
		vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			indexBufferSize,
			&glTFModel.indices.buffer,
			&glTFModel.indices.memory);

		VkCommandBuffer copyCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		VkBufferCopy copyRegion = {};

		copyRegion.size = vertexBufferSize;
		vkCmdCopyBuffer(
			copyCmd,
			vertexStaging.buffer,
			glTFModel.vertices.buffer,
			1,
			&copyRegion);

		copyRegion.size = indexBufferSize;
		vkCmdCopyBuffer(
			copyCmd,
			indexStaging.buffer,
			glTFModel.indices.buffer,
			1,
			&copyRegion);

		vulkanDevice->flushCommandBuffer(copyCmd, queue, true);

		vertexStaging.destroy();
		indexStaging.destroy();
	}

	void loadAssets()
	{
		loadglTFFile(getAssetPath() + "models/FlightHelmet/glTF/FlightHelmet.gltf");
	}

	void prepareUniformBuffers()
	{
		for (auto& buffer : uniformBuffers)
		{
			VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer, sizeof(UniformData)));
			VK_CHECK_RESULT(buffer.map());
		}
	}

	void setupDescriptors()
	{
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxConcurrentFrames),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(glTFModel.images.size() * maxConcurrentFrames))
		};
		const uint32_t maxSetCount = static_cast<uint32_t>(glTFModel.images.size() + 1) * maxConcurrentFrames;
		VkDescriptorPoolCreateInfo descriptorPoolCI = vks::initializers::descriptorPoolCreateInfo(poolSizes, maxSetCount);
		vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorPool);

		VkDescriptorSetLayoutBinding descriptorSetLayoutBinding = vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(&descriptorSetLayoutBinding, 1);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.matrices));

		descriptorSetLayoutBinding = vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.textures));

		for (auto i = 0; i < uniformBuffers.size(); i++)
		{
			VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.matrices, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets[i]));
			VkWriteDescriptorSet writeDescriptorSet = vks::initializers::writeDescriptorSet(descriptorSets[i], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].descriptor);
			vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
		}

		for (auto& image : glTFModel.images)
		{
			VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.textures, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &image.descriptorSet));
			VkWriteDescriptorSet writeDescriptorSet = vks::initializers::writeDescriptorSet(image.descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &image.texture.descriptor);
			vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
		}
	}

	void preparePipelines()
	{
		VkPipelineRenderingCreateInfo pipelineRenderingCI{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
		pipelineRenderingCI.colorAttachmentCount = 1;
		pipelineRenderingCI.pColorAttachmentFormats = &swapChain.colorFormat;
		pipelineRenderingCI.depthAttachmentFormat = depthFormat;
		pipelineRenderingCI.stencilAttachmentFormat = depthFormat;

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStagesCI
		{
			loadShader(getShadersPath() + "gltfloading/gltfloading.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
			loadShader(getShadersPath() + "gltfloading/gltfloading.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
		};

		std::vector<VkVertexInputBindingDescription> vertexInputBindingDescriptions
		{
			vks::initializers::vertexInputBindingDescription(0, sizeof(VulkanglTFModel::Vertex), VK_VERTEX_INPUT_RATE_VERTEX)
		};
		std::vector<VkVertexInputAttributeDescription> vertexInputAttributeDescriptions
		{
			vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VulkanglTFModel::Vertex, position)),
			vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VulkanglTFModel::Vertex, normal)),
			vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32G32_SFLOAT, offsetof(VulkanglTFModel::Vertex, uv)),
			vks::initializers::vertexInputAttributeDescription(0, 3, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VulkanglTFModel::Vertex, color)),
		};
		VkPipelineVertexInputStateCreateInfo vertexInputStateCI = vks::initializers::pipelineVertexInputStateCreateInfo(vertexInputBindingDescriptions, vertexInputAttributeDescriptions);

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);

		VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::pipelineViewportStateCreateInfo(1, 1);

		VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);

		VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);

		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);

		VkPipelineColorBlendAttachmentState colorBlendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::pipelineColorBlendStateCreateInfo(1, &colorBlendAttachmentState);

		std::vector<VkDynamicState> dynamicStates =
		{
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStates);

		std::array<VkDescriptorSetLayout, 2> setLayouts =
		{
			descriptorSetLayouts.matrices,
			descriptorSetLayouts.textures
		};
		VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), static_cast<uint32_t>(setLayouts.size()));
		VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::mat4), 0);
		pipelineLayoutCI.pushConstantRangeCount = 1;
		pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo();
		pipelineCI.pNext = &pipelineRenderingCI;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStagesCI.size());
		pipelineCI.pStages = shaderStagesCI.data();
		pipelineCI.pVertexInputState = &vertexInputStateCI;
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCI.pViewportState = &viewportStateCI;
		pipelineCI.pRasterizationState = &rasterizationStateCI;
		pipelineCI.pMultisampleState = &multisampleStateCI;
		pipelineCI.pDepthStencilState = &depthStencilStateCI;
		pipelineCI.pColorBlendState = &colorBlendStateCI;
		pipelineCI.pDynamicState = &dynamicStateCI;
		pipelineCI.layout = pipelineLayout;

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.solid));

		if (deviceFeatures.fillModeNonSolid)
		{
			rasterizationStateCI.polygonMode = VK_POLYGON_MODE_LINE;
			VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.wireframe));
		}
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

	void updateUniformBuffers()
	{
		uniformData.projection = camera.matrices.perspective;
		uniformData.view = camera.matrices.view;
		memcpy(uniformBuffers[currentBuffer].mapped, &uniformData, sizeof(UniformData));
	}

	void buildCommandBuffer()
	{
		VkCommandBuffer cmdBuffer = drawCmdBuffers[currentBuffer];
		VkCommandBufferBeginInfo cmdBufferBeginInfo = vks::initializers::commandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufferBeginInfo));

		vks::tools::insertImageMemoryBarrier2(cmdBuffer, swapChain.images[currentImageIndex], 0, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
		vks::tools::insertImageMemoryBarrier2(cmdBuffer, depthStencil.image, 0, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, { VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1 });

		VkRenderingAttachmentInfo colorAttachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		colorAttachment.imageView = swapChain.imageViews[currentImageIndex];
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.clearValue = { 0.2f, 0.2f, 0.2f, 1.0f };
		VkRenderingAttachmentInfo depthStencilAttachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		depthStencilAttachment.imageView = depthStencil.view;
		depthStencilAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
		depthStencilAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthStencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthStencilAttachment.clearValue = { 1.0f, 0.0f };

		VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
		renderingInfo.renderArea = { 0, 0, width, height };
		renderingInfo.layerCount = 1;
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachments = &colorAttachment;
		renderingInfo.pDepthAttachment = &depthStencilAttachment;
		renderingInfo.pStencilAttachment = &depthStencilAttachment;

		vkCmdBeginRendering(cmdBuffer, &renderingInfo);

		VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
		vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
		VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
		vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

		vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentBuffer], 0, nullptr);
		vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, wireFrame ? pipelines.wireframe : pipelines.solid);

		glTFModel.draw(cmdBuffer, pipelineLayout);

		drawUI(cmdBuffer);

		vkCmdEndRendering(cmdBuffer);

		vks::tools::insertImageMemoryBarrier2(cmdBuffer, swapChain.images[currentImageIndex], VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 0, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

		VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffer));
	}

	void render()
	{
		if (!prepared) return;
		VulkanExampleBase::prepareFrame();
		updateUniformBuffers();
		buildCommandBuffer();
		VulkanExampleBase::submitFrame();
	}

	void OnUpdateUIOverlay(vks::UIOverlay* overlay) override
	{
		if (overlay->header("Settings"))
		{
			overlay->checkBox("wireframe", &wireFrame);
		}
	}
};

VULKAN_EXAMPLE_MAIN()