#include "VulkanImGui.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>

#include "ImGui/imgui_impl_vulkan.h"

namespace YAEngine
{
  static void CkeckVkResult(VkResult err)
  {
    if (err == VK_SUCCESS) return;
    fprintf(stderr, "[Vulkan] Error: VkResult = %d\n", err);
    if (err < 0)
      abort();
  }

  void VulkanImGui::Init(GLFWwindow* window, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkQueue graphicsQueue, uint32_t swapchainImageCount, uint32_t graphicsQueueFamily, VkRenderPass renderPass)
  {
    m_Window = window;
    m_Device = device;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForVulkan(window, true);

    VkDescriptorPoolSize poolSizes[] =
    {
      { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
      { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
      { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
      { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
      { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
      { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
      { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
      { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
      { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
      { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
      { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000 * IM_ARRAYSIZE(poolSizes);
    poolInfo.poolSizeCount = (uint32_t)IM_ARRAYSIZE(poolSizes);
    poolInfo.pPoolSizes = poolSizes;

    vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_ImGuiDescriptorPool);

    VkPipelineCacheCreateInfo pipelineCacheInfo{};
    pipelineCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    pipelineCacheInfo.initialDataSize = 0;
    pipelineCacheInfo.pInitialData = nullptr;

    vkCreatePipelineCache(device, &pipelineCacheInfo, nullptr, &m_PipelineCache);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = instance;
    init_info.PhysicalDevice = physicalDevice;
    init_info.Device = device;
    init_info.QueueFamily = graphicsQueueFamily;
    init_info.Queue = graphicsQueue;
    init_info.PipelineCache = m_PipelineCache;
    init_info.DescriptorPool = m_ImGuiDescriptorPool;
    init_info.MinImageCount = swapchainImageCount;
    init_info.ImageCount = swapchainImageCount;
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = CkeckVkResult;
    init_info.PipelineInfoMain.RenderPass = renderPass;

    ImGui_ImplVulkan_Init(&init_info);
  }

  void VulkanImGui::Destroy()
  {
    if (ImGui::GetCurrentContext())
    {
      ImGui::UpdatePlatformWindows();
      ImGui::DestroyPlatformWindows();

      ImGui_ImplVulkan_Shutdown();
      ImGui_ImplGlfw_Shutdown();
      ImGui::DestroyContext();
    }

    if (m_ImGuiDescriptorPool != VK_NULL_HANDLE)
      vkDestroyDescriptorPool(m_Device, m_ImGuiDescriptorPool, nullptr);

    if (m_PipelineCache != VK_NULL_HANDLE)
      vkDestroyPipelineCache(m_Device, m_PipelineCache, nullptr);
  }
}
