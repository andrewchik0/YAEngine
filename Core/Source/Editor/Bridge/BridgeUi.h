#pragma once

#include "Editor/Bridge/BridgeMethods.h"

struct ImGuiTest;
struct ImGuiTestContext;
struct ImGuiTestEngine;

namespace YAEngine
{
  class ServiceRegistry;

  // ui.windows, ui.tree, ui.do and ui.screenshot. ui.tree and ui.do drive the editor through
  // Dear ImGui Test Engine, which is created by the first of them and destroyed again once no
  // request has come for a while: an editor nobody automates carries neither its hooks nor its
  // coroutine thread. Main thread only, apart from the job body running on that coroutine,
  // which never runs at the same time as the main thread.
  class BridgeUi
  {
  public:
    BridgeUi();
    ~BridgeUi();

    BridgeUi(const BridgeUi&) = delete;
    BridgeUi& operator=(const BridgeUi&) = delete;

    void Init(ServiceRegistry& registry, BridgeMethodRegistry& methods, std::function<bool()> isCaptureBusy,
      std::function<bool()> isMinimized);
    // Queues, finishes and times out jobs; outside any ImGui frame.
    void LateUpdate();
    // Fails whatever has not finished and destroys the test engine. Needs the ImGui context.
    void Shutdown(std::string_view reason);

  private:
    enum class JobKind : uint8_t
    {
      Tree,
      Do
    };

    struct Job
    {
      JobKind kind = JobKind::Tree;
      BridgeReply reply;

      // ui.tree
      std::string window;
      int maxDepth = -1;

      // ui.do
      std::string path;
      std::string action;
      int mouseButton = 0;
      bool doubleClick = false;
      bool valueIsNumber = false;
      double numberValue = 0.0;
      std::string stringValue;

      // Written by the job body: an error code, or the result
      std::string errorCode;
      std::string errorMessage;
      Json result = Json::object();
    };

    struct ScreenshotJob
    {
      BridgeReply reply;
      // Empty for the whole editor window
      std::string window;
      std::filesystem::path file;
      // Reported once the readback has come back, so the render state returns to idle
      std::string error;
      // Framebuffer pixels, taken from the ImGui state of the frame the copy was recorded in
      bool cropKnown = false;
      bool fullFrame = true;
      int32_t cropX0 = 0;
      int32_t cropY0 = 0;
      int32_t cropX1 = 0;
      int32_t cropY1 = 0;
      bool encoding = false;
      // Empty on success, the reason otherwise
      std::future<std::string> encode;
    };

    void HandleWindows(const BridgeReply& reply) const;
    void HandleTree(const Json& params, const BridgeReply& reply);
    void HandleDo(const Json& params, const BridgeReply& reply);
    void HandleScreenshot(const Json& params, const BridgeReply& reply);

    void StartJob(std::unique_ptr<Job> job);
    void UpdateJob();
    void FinishJob();
    void UpdateScreenshot();
    void CaptureScreenshotCrop(ScreenshotJob& job) const;
    void CreateEngine();
    void DestroyEngine();

    // Test function of the one registered test; runs on the test engine coroutine.
    static void RunJob(ImGuiTestContext* ctx);
    void RunTree(ImGuiTestContext* ctx, Job& job) const;
    void RunDo(ImGuiTestContext* ctx, Job& job) const;

    ServiceRegistry* m_Registry = nullptr;
    std::function<bool()> m_IsCaptureBusy;
    // ui.tree, ui.do and ui.screenshot finish in a later frame, and no frame advances while minimized
    std::function<bool()> m_IsMinimized;

    ImGuiTestEngine* m_Engine = nullptr;
    ImGuiTest* m_Test = nullptr;
    std::unique_ptr<Job> m_Job;
    bool b_JobQueued = false;
    std::chrono::steady_clock::time_point m_LastJobEnd;

    std::unique_ptr<ScreenshotJob> m_Screenshot;
  };
}
