#define GLFW_INCLUDE_VULKAN
#include <GLFW\glfw3.h>

#include <iostream>
#include <stdexcept>
#include <functional>
#include <vector>
#include <algorithm>
#include <map>

VkResult CreateDebugReportCallbackEXT(
		VkInstance instance, 
		const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, 
		const VkAllocationCallbacks* pAllocator, 
		VkDebugReportCallbackEXT* pCallback
	) 
{
	auto func = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");

	if (func != nullptr)
		return func(instance, pCreateInfo, pAllocator, pCallback);
	
	return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void DestroyDebugReportCallbackEXT(
		VkInstance instance, 
		VkDebugReportCallbackEXT callback,
		const VkAllocationCallbacks* pAllocator
	) 
{
	auto func = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");

	if (func != nullptr)
		func(instance, callback, pAllocator);
}

template <typename T>
class VDeleter {
public:
	VDeleter() : 
		VDeleter([](T, VkAllocationCallbacks *) {}) 
	{
		// EMPTY
	}

	VDeleter(std::function<void(T, VkAllocationCallbacks *)> deletef)
	{
		this->deleter = [=](T obj) {deletef(obj, nullptr); };
	}

	VDeleter(const VDeleter<VkInstance> & instance, std::function<void(VkInstance, T, VkAllocationCallbacks *)> deletef)
	{
		this->deleter = [&instance, deletef](T obj) {deletef(instance, obj, nullptr); };
	}

	VDeleter(const VDeleter<VkDevice> & device, std::function<void(VkDevice, T, VkAllocationCallbacks *)> deletef)
	{
		this->deleter = [&device, deletef](T obj) {deletef(device, obj, nullptr); };
	}

	~VDeleter()
	{
		cleanup();
	}

	T * replace()
	{
		cleanup();
		
		return &object;
	}

	void operator = (T rhs)
	{
		if (rhs == object)
			return;

		cleanup();
		object = rhs;
	}

	const T * operator & () const { return &object; }

	operator T () const	{ return object; }

	template<typename V>
	bool operator == (V rhs) { return object == T(rhs);	}

private:
	void cleanup()
	{
		if (object != VK_NULL_HANDLE)
			deleter(object);

		object = VK_NULL_HANDLE;
	}

private:
	T object{ VK_NULL_HANDLE };
	std::function<void(T)> deleter;
};

class HelloTriangleApplication {
public:
	void run() {
		initWindow();
		initVulkan();
		mainLoop();
	}

private: // Helper struct
	struct QueueFamilyIndicies
	{
		int graphicsFamily = -1;
		int presentFamily = -1;

		bool isComplete() {
			return graphicsFamily >= 0 && presentFamily >= 0;
		}
	};

private:
	void initWindow() {
		glfwInit();

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

		window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
	}

	void initVulkan() {
		createInstance();
		setupDebugCallback();
		pickPhysicalDevice();
		createLogicalDevice();
	}

	void createSurface() {
		if (glfwCreateWindowSurface(instance, window, nullptr, surface.replace()) != VK_SUCCESS)
			throw std::runtime_error("failed to create window surface");


	}

	void createLogicalDevice() {
		QueueFamilyIndicies indicies = findQueueFamilies(physicalDevice);

		VkDeviceQueueCreateInfo queueCreateInfo = {};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = indicies.graphicsFamily;
		queueCreateInfo.queueCount = 1;

		float queuePriority = 1.0f;
		queueCreateInfo.pQueuePriorities = &queuePriority;

		VkPhysicalDeviceFeatures deviceFeatures = {};

		VkDeviceCreateInfo deviceCreateInfo = {};
		deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
		deviceCreateInfo.queueCreateInfoCount = 1;
		deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
		deviceCreateInfo.enabledExtensionCount = 0;

		if (enableValidationLayers)
		{
			deviceCreateInfo.enabledLayerCount = validationLayers.size();
			deviceCreateInfo.ppEnabledLayerNames = validationLayers.data();
		}
		else
			deviceCreateInfo.enabledLayerCount = 0;

		if (vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, device.replace()) != VK_SUCCESS)
			throw std::runtime_error("failed to create logical device");

		vkGetDeviceQueue(device, indicies.graphicsFamily, 1, &graphicsQueue);
	}

	QueueFamilyIndicies findQueueFamilies(VkPhysicalDevice device) {
		QueueFamilyIndicies indicies;

		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

		std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

		int i = 0;
		for (const auto & queueFamily : queueFamilies)
		{
			if (queueFamily.queueCount > 0 &&
				queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) 
			{
				indicies.graphicsFamily = i;
				indicies.presentFamily = i;
			}

			if (indicies.isComplete())
				break;

			i++;
		}

		return indicies;
	}

	void pickPhysicalDevice() {
		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

		if (deviceCount == 0)
			throw std::runtime_error("No GPU with Vulkan");

		std::vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

		std::map<int, VkPhysicalDevice> candidates;
		for (const auto & device : devices) {
			int score = rateDeviceSuitability(device);
			candidates[score] = device;
		}

		if (candidates.begin()->first > 0 &&
			findQueueFamilies(candidates.begin()->second).isComplete())
		{
			physicalDevice = candidates.begin()->second;
		}
		else
			throw std::runtime_error("No suitable GPU at all");
	}

	int rateDeviceSuitability(VkPhysicalDevice device) {
		VkPhysicalDeviceProperties deviceProperies;
		vkGetPhysicalDeviceProperties(device, &deviceProperies);

		VkPhysicalDeviceFeatures deviceFeatures;
		vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

		int score = 0;

		if (deviceProperies.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			score += 1000; // significant advantage over integrated

		score += deviceProperies.limits.maxImageDimension2D; // = max texture size ~ memory

		// shiet tier gpu
		if (!deviceFeatures.geometryShader)
			return 0;

		return score;
	}

	void setupDebugCallback() {
		if (!enableValidationLayers)
			return;

		VkDebugReportCallbackCreateInfoEXT createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
		createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
		createInfo.pfnCallback = debugCallback;

		if (CreateDebugReportCallbackEXT(instance, &createInfo, nullptr, callback.replace()) != VK_SUCCESS)
			throw std::runtime_error("Failed to setup debug callback");
	}

	void createInstance() {
		if (enableValidationLayers && !checkValidationLayerSupport())
			throw std::runtime_error("Validations layers are not supported");

		VkApplicationInfo appInfo = {};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "Hello Triangle";
		appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pEngineName = "No Engine";
		appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.apiVersion = VK_API_VERSION_1_0;

		VkInstanceCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;

		auto requiredExtensions = getRequiredExtensions();
		createInfo.enabledExtensionCount = requiredExtensions.size();
		createInfo.ppEnabledExtensionNames = requiredExtensions.data();

		if (enableValidationLayers)
		{
			createInfo.enabledLayerCount = validationLayers.size();
			createInfo.ppEnabledLayerNames = validationLayers.data();
		}
		else
		{
			createInfo.enabledLayerCount = 0;
		}

		if (vkCreateInstance(&createInfo, nullptr, instance.replace()) != VK_SUCCESS)
			throw std::runtime_error("failed to create instance");

		unsigned int glfwExtensionCount = 0;
		const char ** glfwExtensions;

		glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		uint32_t extensionCount = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
		std::vector<VkExtensionProperties> extensions(extensionCount);
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

		std::cout << "available extensions: " << std::endl;
		for (auto&& extension : extensions)
			std::cout << "\t" << extension.extensionName << std::endl;

		// check if all required extensions supported
		for (int i = 0; i < glfwExtensionCount; i++)
		{
			const char * neededExt = glfwExtensions[i];

			/*
			bool isSupported = std::find_if(
				extensions.begin(), 
				extensions.end(), 
				[=](const VkExtensionProperties & ex) -> bool { return strcmp((char *)ex.extensionName, neededExt); }) == extensions.end();
				*/

			bool isSupported = false;
			for (auto && supportedExtension : extensions)
			{
				const char * n1 = supportedExtension.extensionName;
				const char * n2 = neededExt;

				if (strcmp(n1, n2))
					isSupported = true;
			}
			
			if (!isSupported)
				std::cout << "missing " << neededExt<< std::endl;
		}

	}

	std::vector<const char *> getRequiredExtensions()
	{
		std::vector<const char *> extensions;

		unsigned int glfwExtensionCount = 0;
		const char ** glfwExtensions = nullptr;
		glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
		
		for (unsigned int i = 0; i < glfwExtensionCount; i++)
			extensions.push_back(glfwExtensions[i]);

		if (enableValidationLayers)
			extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);

		return extensions;
	}

	bool checkValidationLayerSupport() {
		uint32_t layerCount;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

		std::vector<VkLayerProperties> availableLayers(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

		for (const char * layerName : validationLayers)
		{
			bool isLayerFound = false;

			for (const auto & layerProperties : availableLayers)
				if (strcmp(layerName, layerProperties.layerName) == 0)
				{
					isLayerFound = true;
					break;
				}
			
			if (!isLayerFound)
				return false;
		}

		return true;
	}

	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
			VkDebugReportFlagsEXT flags,
			VkDebugReportObjectTypeEXT objType,
			uint64_t obj,
			size_t location,
			int32_t code,
			const char * layerPrefix,
			const char * msg,
			void * userData
		)
	{
		std::cerr << "validation layer: " << msg << std::endl;

		return VK_FALSE;
	}

	void mainLoop() {
		while (!glfwWindowShouldClose(window))
			glfwPollEvents();
	}

private:

	const int WIDTH = 800;
	const int HEIGHT = 600;
	const std::vector<const char *> validationLayers = {
		"VK_LAYER_LUNARG_standard_validation"
	};
#ifdef NDEBUG
	const bool enableValidationLayers = false;
#else
	const bool enableValidationLayers = true;
#endif

	GLFWwindow * window{ nullptr };
	VDeleter<VkInstance> instance{ vkDestroyInstance };
	VDeleter<VkDebugReportCallbackEXT> callback{ instance, DestroyDebugReportCallbackEXT };
	VDeleter<VkSurfaceKHR> surface{ instance, vkDestroySurfaceKHR };
	VDeleter<VkDevice> device{ vkDestroyDevice };
	VkPhysicalDevice physicalDevice{ VK_NULL_HANDLE };
	VkQueue graphicsQueue;
};

int main() {
	HelloTriangleApplication app;

	try {
		app.run();
	}
	catch (const std::runtime_error & e) {
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}