#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"
#include "vulkanexamplebase.h"

class VulkanglTFModel
{
public:
	vks::VulkanDevice* vulkanDevice;
	VkQueue copyQueue;

	struct Vertex
	{
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec2 uv;
		glm::vec3 color;
	};

	struct
	{
		VkBuffer buffer;
		VkDeviceMemory memory;
	} vertices;

	struct
	{
		int count;
		VkBuffer buffer;
		VkDeviceMemory memory;
	} indices;
	
	struct Primitive
	{
		uint32_t firstIndex;
		uint32_t indexCount;
		int32_t materialIndex;
	};

	struct Mesh
	{
		std::vector<Primitive> primitives;
	};

	struct Node
	{
		Node* parent;
		std::vector<Node*> children;
		Mesh mesh;
		glm::mat4 matrix;
		~Node()
		{
			for (auto child : children)
			{
				delete child;
			}
		}
	};

	struct Material
	{
		glm::vec4 baseColorFactor = glm::vec4(1.0f);
		uint32_t baseColorTextureIndex;
	};

	struct Image
	{
		vks::Texture2D texture;
		VkDescriptorSet descriptorSet;
	};

	struct Texture
	{
		uint32_t imageIndex;
	};

	std::vector<Image> images;
	std::vector<Texture> textures;
	std::vector<Material> materials;
	std::vector<Node*> nodes;

	~VulkanglTFModel()
	{
		for (auto node : nodes)
		{
			delete node;
		}

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
	}

	void loadImages(tinygltf::Model& input)
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

	void loadTextures(tinygltf::Model& input)
	{
		textures.resize(input.textures.size());
		for (size_t i = 0; i < input.textures.size(); i++)
		{
			textures[i].imageIndex = input.textures[i].source;
		}
	}

	void loadMaterials(tinygltf::Model& input)
	{
		materials.resize(input.materials.size());
		for (size_t i = 0; i < input.materials.size(); i++)
		{
			tinygltf::Material& gltfMaterial = input.materials[i];
			Material& material = materials[i];
			if (gltfMaterial.values.find("baseColorFactor") != gltfMaterial.values.end())
			{
				material.baseColorFactor = glm::make_vec4(gltfMaterial.values["baseColorFactor"].ColorFactor().data());
			}
			if (gltfMaterial.values.find("baseColorTexture") != gltfMaterial.values.end())
			{
				material.baseColorTextureIndex = gltfMaterial.values["baseColorTexture"].TextureIndex();
			}
		}
	}

	void loadNode(tinygltf::Model& input)
	{
		// Implementation for loading nodes goes here
	}
};

class VulkanExample : public VulkanExampleBase
{
public:
	VulkanglTFModel gltfModel;

	VulkanExample()
	{
		title = "glTF Model Loading";
	}

	~VulkanExample()
	{

	}

	void loadglTFFile(std::string filename)
	{
		this->device = device;

		tinygltf::Model gltfInput;
		tinygltf::TinyGLTF gltfContext;
		std::string err, warning;

		bool fileLoaded = gltfContext.LoadASCIIFromFile(&gltfInput, &err, &warning, filename);

		gltfModel.vulkanDevice = vulkanDevice;
		gltfModel.copyQueue = queue;

		std::vector<uint32_t> indices;
		std::vector<VulkanglTFModel::Vertex> vertices;

		if (fileLoaded)
		{
			gltfModel.loadImages(gltfInput);
			gltfModel.loadTextures(gltfInput);
			gltfModel.loadMaterials(gltfInput);

			//const tinygltf::Scene& scene = gltfInput.scenes[0];
			//for (size_t i = 0; i < )
		}

		size_t vertexBufferSize = vertices.size() * sizeof(VulkanglTFModel::Vertex);
	}

	void loadAssets()
	{
		loadglTFFile(getAssetPath() + "models/FlightHelmet/glTF/FlightHelmet.gltf");
	}

	void prepareUniformBuffers()
	{

	}

	void setupDescriptors()
	{
	}

	void preparePipelines()
	{
	}

	void prepare() override
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
	}

	void buildCommandBuffer()
	{
	}

	void render()
	{
		if (!prepared) return;
		VulkanExampleBase::prepareFrame();
		updateUniformBuffers();
		buildCommandBuffer();
		VulkanExampleBase::submitFrame();
	}
};

VULKAN_EXAMPLE_MAIN()