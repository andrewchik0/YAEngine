#include "VulkanImGui.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>

#include "ImGui/imgui_impl_vulkan.h"
#include "Utils/Log.h"

#ifdef YA_EDITOR
#include "Editor/Utils/EditorIcons.h"
#endif

namespace YAEngine
{
  static void CheckVkResult(VkResult err)
  {
    if (err == VK_SUCCESS) return;
    YA_LOG_ERROR("Vulkan", "VkResult = %d", err);
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
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

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

    VkResult result = vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_ImGuiDescriptorPool);
    if (result != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create ImGui descriptor pool: %d", result);
      throw std::runtime_error("Failed to create ImGui descriptor pool");
    }

    VkPipelineCacheCreateInfo pipelineCacheInfo{};
    pipelineCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    pipelineCacheInfo.initialDataSize = 0;
    pipelineCacheInfo.pInitialData = nullptr;

    result = vkCreatePipelineCache(device, &pipelineCacheInfo, nullptr, &m_PipelineCache);
    if (result != VK_SUCCESS)
    {
      YA_LOG_ERROR("Render", "Failed to create ImGui pipeline cache: %d", result);
      throw std::runtime_error("Failed to create ImGui pipeline cache");
    }

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
    init_info.CheckVkResultFn = CheckVkResult;
    init_info.PipelineInfoMain.RenderPass = renderPass;

    ImGui_ImplVulkan_Init(&init_info);

    io.Fonts->AddFontFromFileTTF(WORKING_DIR "/Assets/Fonts/Roboto-Regular.ttf", 15.0f);

#ifdef YA_EDITOR
    // Merge Font Awesome icons into the default font
    ImFontConfig iconConfig;
    iconConfig.MergeMode = true;
    iconConfig.GlyphMinAdvanceX = 15.0f;
    iconConfig.PixelSnapH = true;
    static const ImWchar iconRanges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    io.Fonts->AddFontFromFileTTF(WORKING_DIR "/Assets/Fonts/FontAwesome7.otf", 14.0f, &iconConfig, iconRanges);
#endif

    io.Fonts->Build();
    ImGui::PushFont(io.Fonts->Fonts[0]);
  }

  void VulkanImGui::Destroy()
  {
    ImGui::PopFont();
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
