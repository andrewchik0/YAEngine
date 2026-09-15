#include "Editor/Bridge/BridgeUi.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_test_engine/imgui_te_engine.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <imgui_test_engine/imgui_te_internal.h>
#include <Stb/stb_image_write.h>

#include "Editor/Bridge/BridgeDiscovery.h"
#include "Editor/Bridge/BridgeTypes.h"
#include "Editor/Bridge/BridgeUiTree.h"
#include "Editor/EditorPreferences.h"
#include "Editor/Utils/FileDialog.h"
#include "Render/Render.h"
#include "Utils/Log.h"
#include "Utils/ServiceRegistry.h"
#include "Utils/ThreadPool.h"

namespace YAEngine
{
  namespace
  {
    // Keeps the engine between the requests of one automation session; an idle editor drops
    // its hooks and its coroutine thread again after this long.
    constexpr std::chrono::seconds ENGINE_IDLE_LIFETIME { 15 };
    // GatherItems reads larger depths as unlimited
    constexpr int64_t MAX_TREE_DEPTH = 99;
    constexpr size_t MAX_DETAIL_BYTES = 2000;
    // Screenshots kept under Captures/mcp/ui, the new one included
    constexpr size_t MAX_KEPT_SCREENSHOTS = 64;
    constexpr size_t MAX_FILE_NAME_LENGTH = 64;
    // Keeps the index inside an int whatever a hand-made file name holds
    constexpr size_t MAX_INDEX_DIGITS = 9;

    constexpr const char* DO_ACTIONS[] = { "click", "check", "uncheck", "set", "open", "close", "select", "menu" };

    struct FileDialogItem
    {
      const char* label;
      const char* action;
    };

    // The editor's file dialog items and the actions that do the same with a path. Save Scene has
    // no "..." because it opens a dialog only on a scene that was never saved.
    constexpr FileDialogItem FILE_DIALOG_ITEMS[] = {
      { "Load Skybox...", "skybox.set" },
      { "Open Scene...", "scene.open" },
      { "Save Scene As...", "scene.save" },
      { "Import Model...", "model.import" },
      { "Save Scene", "scene.save" },
    };

    std::string_view TrimSpaces(std::string_view text)
    {
      while (!text.empty() && (text.front() == ' ' || text.front() == '\n' || text.front() == '\r'))
        text.remove_prefix(1);
      while (!text.empty() && (text.back() == ' ' || text.back() == '\n' || text.back() == '\r'))
        text.remove_suffix(1);
      return text;
    }

    // Submitted last frame (or this one, inside a job), not a background dock tab, not collapsed.
    bool IsWindowShown(const ImGuiWindow* window)
    {
      if (!window->Active || window->Hidden || window->Collapsed)
        return false;
      return !window->DockIsActive || window->DockTabIsVisible;
    }

    std::string DescribeHiddenWindow(const ImGuiWindow* window)
    {
      std::string name = std::string("'") + window->Name + "'";
      std::string ref = EscapeBridgeUiRefSegment(window->Name);
      if (!window->Active)
        return name + " is not open; editor panels open from the View menu, e.g. ui.do menu \"##MainMenuBar/View/"
          + ref + "\"";
      if (window->DockIsActive && !window->DockTabIsVisible)
        return name + " is a background tab; ui.do click on \"" + ref + "/#TAB\" brings it to the front";
      if (window->Collapsed)
        return name + " is collapsed; ui.do click on \"" + ref + "/#COLLAPSE\" expands it";
      return name + " is hidden this frame; try again";
    }

    // Empty when the item may be pressed. "..." marks the items that open a native file dialog,
    // which would stall the editor's main thread, and with it the bridge, until closed by hand.
    std::string DescribeFileDialogRefusal(std::string_view label)
    {
      std::string_view display = TrimSpaces(GetBridgeUiDisplayLabel(label));
      if (!display.ends_with("..."))
        return {};

      std::string quoted = "'" + std::string(display) + "'";
      for (const FileDialogItem& item : FILE_DIALOG_ITEMS)
      {
        if (display.ends_with(item.label))
          return quoted + " opens a native file dialog, which blocks the editor until someone closes it; run the action "
            + item.action + " with a path instead (actions.run)";
      }
      return quoted + " ends with '...', which in this editor opens a native file dialog that blocks the editor; "
        "actions.list names the actions that take a path instead";
    }

    // For an item without "..." that opened a file dialog anyway, which FileDialog suppressed.
    std::string DescribeSuppressedDialog(const std::string& path)
    {
      std::string label = GetBridgeUiLastRefSegment(path);
      std::string_view display = TrimSpaces(GetBridgeUiDisplayLabel(label));

      std::string detail = "'" + path + "' tried to open a native file dialog, which is suppressed during ui requests "
        "because it blocks the editor until someone closes it; ";
      for (const FileDialogItem& item : FILE_DIALOG_ITEMS)
      {
        if (!display.empty() && display.ends_with(item.label))
          return detail + "run the action " + item.action + " with a path instead (actions.run)";
      }
      return detail + "actions.list names the actions that take a path instead";
    }

    Json DoOutcome(bool ok, std::string detail)
    {
      Json result = Json::object();
      result["ok"] = ok;
      result["detail"] = std::move(detail);
      return result;
    }

    std::string CollectTestErrors(ImGuiTest& test)
    {
      ImGuiTextBuffer buffer;
      test.Output.Log.ExtractLinesForVerboseLevels(ImGuiTestVerboseLevel_Error, ImGuiTestVerboseLevel_Error, &buffer);
      std::string text(TrimSpaces(std::string_view(buffer.c_str(), size_t(buffer.size()))));
      if (text.size() > MAX_DETAIL_BYTES)
        text = text.substr(0, MAX_DETAIL_BYTES) + "...";
      return text;
    }

    struct ScreenshotFile
    {
      int index = 0;
      std::filesystem::path path;
    };

    // NNN_<name>.png with at least three digits.
    std::optional<int> ParseScreenshotIndex(const std::wstring& name)
    {
      size_t digits = 0;
      while (digits < name.size() && name[digits] >= L'0' && name[digits] <= L'9')
        digits++;

      if (digits < 3 || digits > MAX_INDEX_DIGITS || digits >= name.size() || name[digits] != L'_'
        || !name.ends_with(L".png"))
        return std::nullopt;

      return std::stoi(name.substr(0, digits));
    }

    // Oldest first.
    std::vector<ScreenshotFile> ListScreenshots(const std::filesystem::path& root)
    {
      std::vector<ScreenshotFile> files;
      std::error_code ec;
      for (const auto& entry : std::filesystem::directory_iterator(root, ec))
      {
        std::error_code typeError;
        if (!entry.is_regular_file(typeError))
          continue;

        if (std::optional<int> index = ParseScreenshotIndex(entry.path().filename().wstring()))
          files.push_back(ScreenshotFile { .index = *index, .path = entry.path() });
      }

      std::sort(files.begin(), files.end(),
        [](const ScreenshotFile& a, const ScreenshotFile& b) { return a.index < b.index; });
      return files;
    }

    // Leaves room for the screenshot about to be written.
    void PruneScreenshots(const std::vector<ScreenshotFile>& oldestFirst)
    {
      if (oldestFirst.size() < MAX_KEPT_SCREENSHOTS)
        return;

      size_t excess = oldestFirst.size() - MAX_KEPT_SCREENSHOTS + 1;
      for (size_t i = 0; i < excess; i++)
      {
        std::error_code ec;
        std::filesystem::remove(oldestFirst[i].path, ec);
        if (ec)
        {
          YA_LOG_WARN("Bridge", "UI: cannot delete the old screenshot '%s': %s",
            PathToUtf8(oldestFirst[i].path).c_str(), ec.message().c_str());
        }
      }
    }

    // A window name ends up in a file name, so it keeps only characters that cannot leave the
    // directory or trip Windows file naming rules.
    std::string SanitizeFileName(std::string_view name)
    {
      std::string sanitized;
      for (char c : name.substr(0, MAX_FILE_NAME_LENGTH))
      {
        bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        sanitized += keep ? c : '_';
      }
      return sanitized.empty() ? std::string("window") : sanitized;
    }

    void AppendPngBytes(void* context, void* data, int size)
    {
      auto* bytes = static_cast<std::vector<uint8_t>*>(context);
      const auto* begin = static_cast<const uint8_t*>(data);
      bytes->insert(bytes->end(), begin, begin + size);
    }

    struct CropRect
    {
      bool fullFrame = true;
      int32_t x0 = 0;
      int32_t y0 = 0;
      int32_t x1 = 0;
      int32_t y1 = 0;
    };

    // Runs on a worker thread and touches nothing but its arguments. Empty on success.
    std::string WriteScreenshotPng(const SwapchainReadback& readback, CropRect crop, const std::filesystem::path& file)
    {
      const int32_t width = int32_t(readback.width);
      const int32_t height = int32_t(readback.height);
      if (crop.fullFrame)
        crop = CropRect { .fullFrame = true, .x0 = 0, .y0 = 0, .x1 = width, .y1 = height };

      const int32_t x0 = std::clamp(crop.x0, 0, width);
      const int32_t y0 = std::clamp(crop.y0, 0, height);
      const int32_t x1 = std::clamp(crop.x1, 0, width);
      const int32_t y1 = std::clamp(crop.y1, 0, height);
      if (x1 <= x0 || y1 <= y0)
        return "the window lies outside the editor framebuffer";

      const int32_t cropWidth = x1 - x0;
      const int32_t cropHeight = y1 - y0;

      // The alpha the UI leaves in the swapchain means nothing on screen
      std::vector<uint8_t> rgb(size_t(cropWidth) * size_t(cropHeight) * 3);
      for (int32_t y = 0; y < cropHeight; y++)
      {
        const uint8_t* source = readback.rgba.data() + (size_t(y0 + y) * size_t(width) + size_t(x0)) * 4;
        uint8_t* target = rgb.data() + size_t(y) * size_t(cropWidth) * 3;
        for (int32_t x = 0; x < cropWidth; x++, source += 4, target += 3)
        {
          target[0] = source[0];
          target[1] = source[1];
          target[2] = source[2];
        }
      }

      std::vector<uint8_t> png;
      if (stbi_write_png_to_func(AppendPngBytes, &png, cropWidth, cropHeight, 3, rgb.data(), cropWidth * 3) == 0)
        return "PNG encoding failed";

      std::ofstream out(file, std::ios::binary);
      out.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
      if (!out)
        return "cannot write '" + PathToUtf8(file) + "'";

      return {};
    }
  }

  BridgeUi::BridgeUi() = default;
  BridgeUi::~BridgeUi() = default;

  void BridgeUi::Init(ServiceRegistry& registry, BridgeMethodRegistry& methods, std::function<bool()> isCaptureBusy,
    std::function<bool()> isMinimized)
  {
    m_Registry = &registry;
    m_IsCaptureBusy = std::move(isCaptureBusy);
    m_IsMinimized = std::move(isMinimized);

    methods.Register("ui.windows", [this](const Json&, const BridgeReply& reply) {
      HandleWindows(reply);
    });

    methods.Register("ui.tree", [this](const Json& params, const BridgeReply& reply) {
      HandleTree(params, reply);
    });

    methods.Register("ui.do", [this](const Json& params, const BridgeReply& reply) {
      HandleDo(params, reply);
    });

    methods.Register("ui.screenshot", [this](const Json& params, const BridgeReply& reply) {
      HandleScreenshot(params, reply);
    });
  }

  void BridgeUi::LateUpdate()
  {
    UpdateJob();
    UpdateScreenshot();
  }

  void BridgeUi::Shutdown(std::string_view reason)
  {
    // First, so an aborted job body runs to its end before its reply is failed
    DestroyEngine();

    if (m_Job)
    {
      FileDialog::SetSuppressed(false);
      m_Job->reply.Fail(BridgeErrorCode::FAILED, reason);
      m_Job.reset();
    }

    if (m_Screenshot)
    {
      if (m_Screenshot->encoding)
        m_Screenshot->encode.wait();
      m_Screenshot->reply.Fail(BridgeErrorCode::FAILED, reason);
      m_Screenshot.reset();
    }
  }

  void BridgeUi::HandleWindows(const BridgeReply& reply) const
  {
    ImGuiContext& g = *ImGui::GetCurrentContext();

    Json windows = Json::array();
    for (ImGuiWindow* window : g.Windows)
    {
      const ImGuiWindowFlags flags = window->Flags;
      const bool popup = (flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_ChildMenu)) != 0;
      if (window->IsFallbackWindow || (flags & ImGuiWindowFlags_Tooltip) != 0)
        continue;
      // Docking gives docked panels the child flag too; ImGui tells them apart the same way
      if ((flags & ImGuiWindowFlags_ChildWindow) != 0 && !window->DockIsActive && !popup)
        continue;
      // A popup only exists while it is open; the closed ones are leftovers
      if (popup && !window->Active)
        continue;

      Json entry = Json::object();
      entry["name"] = window->Name;
      entry["visible"] = IsWindowShown(window);
      entry["focused"] = g.NavWindow != nullptr && g.NavWindow->RootWindow == window;
      entry["collapsed"] = window->Collapsed;
      entry["docked"] = bool(window->DockIsActive);
      windows.push_back(std::move(entry));
    }

    Json result = Json::object();
    result["windows"] = std::move(windows);
    reply.Ok(std::move(result));
  }

  void BridgeUi::HandleTree(const Json& params, const BridgeReply& reply)
  {
    std::string windowName;
    int64_t maxDepth = -1;
    std::string error;
    if (!ReadOptionalParam(params, "window", windowName, error) || !ReadOptionalParam(params, "maxDepth", maxDepth, error))
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, error);
      return;
    }

    if (windowName.empty())
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'window' must name a window as ui.windows lists it");
      return;
    }

    auto depthParam = params.find("maxDepth");
    if (depthParam != params.end() && !depthParam->is_null() && maxDepth < 1)
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'maxDepth' must be at least 1");
      return;
    }

    if (m_IsMinimized && m_IsMinimized())
    {
      reply.Fail(BridgeErrorCode::BUSY, BRIDGE_MINIMIZED_MESSAGE);
      return;
    }

    ImGuiWindow* window = ImGui::FindWindowByName(windowName.c_str());
    if (window == nullptr)
    {
      reply.Fail(BridgeErrorCode::NOT_FOUND, "no window '" + windowName + "'; ui.windows lists the windows");
      return;
    }

    if (!IsWindowShown(window))
    {
      reply.Fail(BridgeErrorCode::FAILED, DescribeHiddenWindow(window));
      return;
    }

    if (m_Job)
    {
      reply.Fail(BridgeErrorCode::BUSY, "another ui.tree or ui.do request is still running");
      return;
    }

    auto job = std::make_unique<Job>();
    job->kind = JobKind::Tree;
    job->reply = reply;
    job->window = windowName;
    job->maxDepth = maxDepth < 1 ? -1 : int(std::min(maxDepth, MAX_TREE_DEPTH));
    StartJob(std::move(job));
  }

  void BridgeUi::HandleDo(const Json& params, const BridgeReply& reply)
  {
    std::string path;
    std::string action;
    std::string error;
    if (!ReadOptionalParam(params, "path", path, error) || !ReadOptionalParam(params, "action", action, error))
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, error);
      return;
    }

    if (path.empty())
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS,
        "'path' must be an item reference such as \"Render Settings/SSR\", as ui.tree lists them");
      return;
    }

    bool knownAction = std::any_of(std::begin(DO_ACTIONS), std::end(DO_ACTIONS),
      [&action](const char* name) { return action == name; });
    if (!knownAction)
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS,
        "'action' must be click, check, uncheck, set, open, close, select or menu");
      return;
    }

    auto job = std::make_unique<Job>();
    job->kind = JobKind::Do;
    job->reply = reply;
    job->path = path;
    job->action = action;

    auto value = params.find("value");
    const bool hasValue = value != params.end() && !value->is_null();
    if (action == "set")
    {
      if (hasValue && value->is_number())
      {
        job->valueIsNumber = true;
        job->numberValue = value->get<double>();
      }
      else if (hasValue && value->is_string())
      {
        job->stringValue = value->get<std::string>();
      }
      else
      {
        reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'set' needs a number or a string 'value'");
        return;
      }
    }
    else if (action == "click" && hasValue)
    {
      std::string button = value->is_string() ? value->get<std::string>() : std::string();
      if (button == "right")
        job->mouseButton = ImGuiMouseButton_Right;
      else if (button == "middle")
        job->mouseButton = ImGuiMouseButton_Middle;
      else if (button == "double")
        job->doubleClick = true;
      else if (button != "left")
      {
        reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'value' for click must be left, right, middle or double");
        return;
      }
    }
    else if (hasValue)
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, "'value' is only read by set and click");
      return;
    }

    if (m_IsMinimized && m_IsMinimized())
    {
      reply.Fail(BridgeErrorCode::BUSY, BRIDGE_MINIMIZED_MESSAGE);
      return;
    }

    if (m_Job)
    {
      reply.Fail(BridgeErrorCode::BUSY, "another ui.tree or ui.do request is still running");
      return;
    }

    if (m_IsCaptureBusy && m_IsCaptureBusy())
    {
      reply.Fail(BridgeErrorCode::BUSY, "ui.do has to wait for the running capture, which restores the view "
        "settings and the camera when it ends");
      return;
    }

    std::string refusal = DescribeFileDialogRefusal(GetBridgeUiLastRefSegment(path));
    if (!refusal.empty())
    {
      reply.Ok(DoOutcome(false, std::move(refusal)));
      return;
    }

    StartJob(std::move(job));
  }

  void BridgeUi::HandleScreenshot(const Json& params, const BridgeReply& reply)
  {
    std::string windowName;
    std::string error;
    if (!ReadOptionalParam(params, "window", windowName, error))
    {
      reply.Fail(BridgeErrorCode::INVALID_PARAMS, error);
      return;
    }

    if (m_Screenshot)
    {
      reply.Fail(BridgeErrorCode::BUSY, "another ui.screenshot is still being taken");
      return;
    }

    if (m_IsMinimized && m_IsMinimized())
    {
      reply.Fail(BridgeErrorCode::BUSY, BRIDGE_MINIMIZED_MESSAGE);
      return;
    }

    Render& render = m_Registry->Get<Render>();
    if (!render.IsSwapchainReadbackSupported())
    {
      reply.Fail(BridgeErrorCode::FAILED, "the window surface does not allow copying presented frames");
      return;
    }

    if (!windowName.empty())
    {
      ImGuiWindow* window = ImGui::FindWindowByName(windowName.c_str());
      if (window == nullptr)
      {
        reply.Fail(BridgeErrorCode::NOT_FOUND, "no window '" + windowName + "'; ui.windows lists the windows");
        return;
      }
      if (!IsWindowShown(window))
      {
        reply.Fail(BridgeErrorCode::FAILED, DescribeHiddenWindow(window));
        return;
      }
    }

    std::filesystem::path executable = GetBridgeExecutablePath();
    if (executable.empty())
    {
      reply.Fail(BridgeErrorCode::FAILED, "cannot determine the directory of the editor executable");
      return;
    }

    std::filesystem::path root = executable.parent_path() / "Captures" / "mcp" / "ui";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec)
    {
      reply.Fail(BridgeErrorCode::FAILED, "cannot create '" + PathToUtf8(root) + "': " + ec.message());
      return;
    }

    std::vector<ScreenshotFile> existing = ListScreenshots(root);
    int index = existing.empty() ? 0 : existing.back().index + 1;
    PruneScreenshots(existing);

    char fileName[96];
    std::snprintf(fileName, sizeof(fileName), "%03d_%s.png", index,
      SanitizeFileName(windowName.empty() ? std::string_view("editor") : std::string_view(windowName)).c_str());

    auto job = std::make_unique<ScreenshotJob>();
    job->reply = reply;
    job->window = windowName;
    job->file = root / fileName;
    m_Screenshot = std::move(job);

    render.RequestSwapchainReadback();
  }

  void BridgeUi::StartJob(std::unique_ptr<Job> job)
  {
    if (m_Engine == nullptr)
      CreateEngine();

    m_Job = std::move(job);
    b_JobQueued = false;
    // Items without "..." can open a dialog too, e.g. Save Scene on a scene that was never saved
    FileDialog::SetSuppressed(true);
  }

  void BridgeUi::UpdateJob()
  {
    if (m_Engine == nullptr)
      return;

    if (!m_Job)
    {
      if (std::chrono::steady_clock::now() - m_LastJobEnd >= ENGINE_IDLE_LIFETIME)
        DestroyEngine();
      return;
    }

    if (!b_JobQueued)
    {
      // QueueTest asserts on an engine whose hooks have not seen a frame, as in the frame it was created
      if (ImGuiTestEngine_GetFrameCount(m_Engine) < ImGui::GetFrameCount())
        return;

      ImGuiTestEngine_QueueTest(m_Engine, m_Test);
      b_JobQueued = true;
      return;
    }

    if (ImGuiTestEngine_IsTestQueueEmpty(m_Engine))
      FinishJob();
  }

  void BridgeUi::FinishJob()
  {
    std::unique_ptr<Job> job = std::move(m_Job);
    b_JobQueued = false;
    m_LastJobEnd = std::chrono::steady_clock::now();

    const uint32_t suppressedDialogs = FileDialog::GetSuppressedCount();
    FileDialog::SetSuppressed(false);

    if (!job->errorCode.empty())
    {
      job->reply.Fail(job->errorCode, job->errorMessage);
      return;
    }

    if (job->kind == JobKind::Do && suppressedDialogs > 0)
    {
      job->reply.Ok(DoOutcome(false, DescribeSuppressedDialog(job->path)));
      return;
    }

    ImGuiTestStatus status = m_Test->Output.Status;
    if (status != ImGuiTestStatus_Success)
    {
      std::string detail = CollectTestErrors(*m_Test);
      if (detail.empty())
        detail = status == ImGuiTestStatus_Error ? "the test engine reported an error" : "the request was aborted";

      const char* method = job->kind == JobKind::Tree ? "ui.tree" : "ui.do";
      YA_LOG_WARN("Bridge", "UI: %s did not finish: %s", method, detail.c_str());
      if (job->kind == JobKind::Do)
        job->reply.Ok(DoOutcome(false, std::move(detail)));
      else
        job->reply.Fail(BridgeErrorCode::FAILED, detail);
      return;
    }

    job->reply.Ok(std::move(job->result));
  }

  void BridgeUi::UpdateScreenshot()
  {
    if (!m_Screenshot)
      return;

    ScreenshotJob& job = *m_Screenshot;
    if (job.encoding)
    {
      if (job.encode.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;

      std::string error = job.encode.get();
      BridgeReply reply = job.reply;
      std::filesystem::path file = job.file;
      m_Screenshot.reset();

      if (!error.empty())
      {
        YA_LOG_WARN("Bridge", "UI: screenshot failed: %s", error.c_str());
        reply.Fail(BridgeErrorCode::FAILED, error);
        return;
      }

      Json result = Json::object();
      result["path"] = PathToUtf8(file);
      reply.Ok(std::move(result));
      return;
    }

    Render& render = m_Registry->Get<Render>();
    SwapchainReadbackState state = render.GetSwapchainReadbackState();

    // LateUpdate runs before the next frame is built, so ImGui still holds the layout of the
    // frame whose copy was just recorded
    if (state == SwapchainReadbackState::Recorded && !job.cropKnown)
      CaptureScreenshotCrop(job);
    if (state != SwapchainReadbackState::Ready)
      return;

    SwapchainReadback readback;
    render.ConsumeSwapchainReadback(readback);
    if (!job.cropKnown)
      CaptureScreenshotCrop(job);

    std::string error = !readback.error.empty() ? readback.error : job.error;
    if (!error.empty())
    {
      BridgeReply reply = job.reply;
      m_Screenshot.reset();
      reply.Fail(BridgeErrorCode::FAILED, error);
      return;
    }

    CropRect crop { .fullFrame = job.fullFrame, .x0 = job.cropX0, .y0 = job.cropY0, .x1 = job.cropX1, .y1 = job.cropY1 };
    std::filesystem::path file = job.file;
    // Encoding a full editor frame takes long enough to be felt on the main thread
    job.encode = m_Registry->Get<ThreadPool>().Submit([readback = std::move(readback), crop, file]() {
      return WriteScreenshotPng(readback, crop, file);
    });
    job.encoding = true;
  }

  void BridgeUi::CaptureScreenshotCrop(ScreenshotJob& job) const
  {
    job.cropKnown = true;
    job.fullFrame = job.window.empty();
    if (job.fullFrame)
      return;

    ImGuiWindow* window = ImGui::FindWindowByName(job.window.c_str());
    if (window == nullptr || !IsWindowShown(window))
    {
      job.error = window == nullptr ? "the window '" + job.window + "' went away before the screenshot"
        : DescribeHiddenWindow(window);
      return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 origin = window->Viewport != nullptr ? window->Viewport->Pos : ImVec2(0.0f, 0.0f);
    const ImVec2 scale = io.DisplayFramebufferScale;
    job.cropX0 = int32_t(std::floor((window->Pos.x - origin.x) * scale.x));
    job.cropY0 = int32_t(std::floor((window->Pos.y - origin.y) * scale.y));
    job.cropX1 = int32_t(std::ceil((window->Pos.x + window->Size.x - origin.x) * scale.x));
    job.cropY1 = int32_t(std::ceil((window->Pos.y + window->Size.y - origin.y) * scale.y));
  }

  void BridgeUi::CreateEngine()
  {
    m_Engine = ImGuiTestEngine_CreateContext();

    ImGuiTestEngineIO& io = ImGuiTestEngine_GetIO(m_Engine);
    // Nothing goes into imgui.ini, which is also what allows destroying the engine before the
    // ImGui context
    io.ConfigSavedSettings = false;
    io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
    io.ConfigMouseDrawCursor = false;
    // Restoring focus after a request would close the popup a right click has just opened
    io.ConfigRestoreFocusAfterTests = false;
    io.ConfigCaptureEnabled = false;
    io.ConfigVerboseLevel = ImGuiTestVerboseLevel_Warning;
    io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Info;
    io.ConfigLogToTTY = false;
    io.ConfigLogToDebugger = false;

    ImGuiTestEngine_Start(m_Engine, ImGui::GetCurrentContext());

    m_Test = ImGuiTestEngine_RegisterTest(m_Engine, "bridge", "ui");
    m_Test->TestFunc = &BridgeUi::RunJob;
    m_Test->UserData = this;
    m_LastJobEnd = std::chrono::steady_clock::now();

    YA_LOG_INFO("Bridge", "UI: automation engine created (Dear ImGui Test Engine)");
  }

  void BridgeUi::DestroyEngine()
  {
    if (m_Engine == nullptr)
      return;

    ImGuiTestEngine_AbortCurrentTest(m_Engine);
    // Runs the coroutine to its end, removes the context hooks and frees the registered test
    ImGuiTestEngine_DestroyContext(m_Engine);
    m_Engine = nullptr;
    m_Test = nullptr;
    b_JobQueued = false;

    // Unbinding leaves the item hook switch as the last query set it, and nothing would clear it
    if (ImGuiContext* context = ImGui::GetCurrentContext())
      context->TestEngineHookItems = false;

    YA_LOG_INFO("Bridge", "UI: automation engine destroyed");
  }

  void BridgeUi::RunJob(ImGuiTestContext* ctx)
  {
    auto* self = static_cast<BridgeUi*>(ctx->Test->UserData);
    Job* job = self->m_Job.get();
    if (job == nullptr)
      return;

    if (job->kind == JobKind::Tree)
      self->RunTree(ctx, *job);
    else
      self->RunDo(ctx, *job);
  }

  void BridgeUi::RunTree(ImGuiTestContext* ctx, Job& job) const
  {
    ImGuiWindow* window = ImGui::FindWindowByName(job.window.c_str());
    if (window == nullptr || !IsWindowShown(window))
    {
      job.errorCode = window == nullptr ? BridgeErrorCode::NOT_FOUND : BridgeErrorCode::FAILED;
      job.errorMessage = window == nullptr ? "no window '" + job.window + "'; ui.windows lists the windows"
        : DescribeHiddenWindow(window);
      return;
    }

    ImGuiContext& g = *ctx->UiContext;
    if (g.LogEnabled)
    {
      job.errorCode = BridgeErrorCode::FAILED;
      job.errorMessage = "ImGui text logging is in use by something else";
      return;
    }

    // ImGui's text log is the only record of the values widgets display. Logging also stops
    // ItemAdd from clipping, so widgets scrolled out of view still report their labels during
    // the gather. Depth 0 keeps it from forcing tree nodes open. LogWindow is cleared because
    // LogBegin takes the fallback window this job runs under, and ending that window at the end
    // of this very frame would finish the log.
    ImGui::LogToBuffer(0);
    g.LogWindow = nullptr;
    ctx->Yield();
    std::string logged(g.LogBuffer.c_str(), size_t(g.LogBuffer.size()));

    window = ImGui::FindWindowByName(job.window.c_str());
    ImGuiTestItemList items;
    if (window != nullptr && IsWindowShown(window))
      ctx->GatherItems(&items, ImGuiTestRef(window->ID), job.maxDepth);
    ImGui::LogFinish();

    if (window == nullptr || !IsWindowShown(window))
    {
      job.errorCode = BridgeErrorCode::FAILED;
      job.errorMessage = "'" + job.window + "' closed while its items were being listed";
      return;
    }
    if (ctx->IsError())
      return;

    job.result["items"] = BuildBridgeUiTreeItems(items, logged);
  }

  namespace
  {
    bool IsInOpenPopup(const ImGuiContext& g, const ImGuiWindow* window)
    {
      if (window == nullptr)
        return false;

      for (const ImGuiPopupData& popup : g.OpenPopupStack)
      {
        if (popup.Window != nullptr && popup.Window->RootWindow == window->RootWindow)
          return true;
      }
      return false;
    }

    // The window a ref path starts in: the one its first segment names, or the focused one for $FOCUSED
    ImGuiWindow* FindRefPathWindow(const ImGuiContext& g, std::string_view path)
    {
      if (path.starts_with("//"))
        path.remove_prefix(2);

      std::string name;
      for (size_t i = 0; i < path.size() && path[i] != '/'; i++)
      {
        if (path[i] == '\\' && i + 1 < path.size())
          i++;
        name += path[i];
      }

      if (name == "$FOCUSED")
        return g.NavWindow;
      return ImGui::FindWindowByName(name.c_str());
    }

    // An open popup blocks the mouse from everything outside it, and a user reaches such an item
    // with a click outside that closes the popup first. The simulated mouse cannot hover a blocked
    // item at all, and resolving a path may already click on the way (a collapsed group, a background
    // tab), so the popups are closed before the path is resolved unless the path starts inside one.
    void CloseBlockingPopups(ImGuiTestContext* ctx, std::string_view path)
    {
      const ImGuiContext& g = *ctx->UiContext;
      if (g.OpenPopupStack.Size == 0 || IsInOpenPopup(g, FindRefPathWindow(g, path)))
        return;

      ctx->PopupCloseAll();
      ctx->Yield();
    }
  }

  void BridgeUi::RunDo(ImGuiTestContext* ctx, Job& job) const
  {
    const std::string quoted = "'" + job.path + "'";

    if (job.action == "menu")
    {
      // MenuClick takes the window the menu belongs to as its first segment, from the root
      std::string menuPath = job.path.starts_with("//") ? job.path : "//" + job.path;
      ctx->MenuClick(menuPath.c_str());
      if (!ctx->IsError())
        job.result = DoOutcome(true, "clicked the menu item " + quoted);
      return;
    }

    if (job.action == "select")
    {
      std::string comboPath;
      std::string entry;
      if (!SplitBridgeUiRefPath(job.path, comboPath, entry))
      {
        job.result = DoOutcome(false, "'select' takes \"<combo path>/<entry label>\", e.g. \"Render Settings/Camera/Tonemapper/AgX\"");
        return;
      }

      CloseBlockingPopups(ctx, comboPath);
      ImGuiTestItemInfo combo = ctx->ItemInfoOpenFullPath(comboPath.c_str(), ImGuiTestOpFlags_NoError);
      if (combo.ID == 0)
      {
        job.result = DoOutcome(false, "no combo at '" + comboPath + "'; ui.tree lists the paths of a window");
        return;
      }
      if (combo.ItemFlags & ImGuiItemFlags_Disabled)
      {
        job.result = DoOutcome(false, "'" + comboPath + "' is disabled");
        return;
      }

      ctx->ItemClick(comboPath.c_str());
      if (ctx->IsError())
        return;

      ImGuiWindow* popup = ctx->GetWindowByRef("//$FOCUSED");
      if (popup == nullptr || (popup->Flags & ImGuiWindowFlags_Popup) == 0 || std::strncmp(popup->Name, "##Combo_", 8) != 0)
      {
        job.result = DoOutcome(false, "'" + comboPath + "' did not open a combo popup");
        return;
      }

      std::string entryRef = "//" + EscapeBridgeUiRefSegment(popup->Name) + "/**/" + entry;
      ImGuiTestItemInfo item = ctx->ItemInfo(entryRef.c_str(), ImGuiTestOpFlags_NoError);
      if (item.ID == 0 || (item.ItemFlags & ImGuiItemFlags_Disabled) != 0)
      {
        ctx->KeyPress(ImGuiKey_Escape);
        std::string label = GetBridgeUiLastRefSegment(job.path);
        job.result = DoOutcome(false, item.ID == 0
          ? "'" + comboPath + "' has no entry '" + label + "'"
          : "the entry '" + label + "' of '" + comboPath + "' is disabled");
        return;
      }

      ctx->ItemClick(entryRef.c_str());
      // Selectables that keep their popup open are confirmed the way a user would
      if (!ctx->IsError() && ctx->GetWindowByRef("//$FOCUSED") == popup)
        ctx->KeyPress(ImGuiKey_Enter);
      if (!ctx->IsError())
        job.result = DoOutcome(true, "selected " + quoted);
      return;
    }

    const char* path = job.path.c_str();
    CloseBlockingPopups(ctx, job.path);
    ImGuiTestItemInfo item = ctx->ItemInfoOpenFullPath(path, ImGuiTestOpFlags_NoError);
    if (item.ID == 0)
    {
      job.result = DoOutcome(false, "no item at " + quoted + "; ui.tree lists the paths of a window");
      return;
    }

    // The path may reach the item by a wildcard, so the label it resolved to is checked as well
    std::string refusal = DescribeFileDialogRefusal(std::string_view(item.DebugLabel, strnlen(item.DebugLabel, sizeof(item.DebugLabel))));
    if (!refusal.empty())
    {
      job.result = DoOutcome(false, std::move(refusal));
      return;
    }

    if (item.ItemFlags & ImGuiItemFlags_Disabled)
    {
      job.result = DoOutcome(false, quoted + " is disabled");
      return;
    }

    const ImGuiItemStatusFlags status = item.StatusFlags;
    std::string detail;
    if (job.action == "click")
    {
      if (job.doubleClick)
        ctx->ItemDoubleClick(path);
      else
        ctx->ItemClick(path, job.mouseButton);
      detail = (job.doubleClick ? "double-clicked " : job.mouseButton == ImGuiMouseButton_Right ? "right-clicked "
        : job.mouseButton == ImGuiMouseButton_Middle ? "middle-clicked " : "clicked ") + quoted;
    }
    else if (job.action == "check" || job.action == "uncheck")
    {
      if ((status & ImGuiItemStatusFlags_Checkable) == 0)
      {
        job.result = DoOutcome(false, quoted + " is not a checkbox or a checkable menu item");
        return;
      }

      bool check = job.action == "check";
      if (check)
        ctx->ItemCheck(path);
      else
        ctx->ItemUncheck(path);
      detail = (check ? "checked " : "unchecked ") + quoted;
    }
    else if (job.action == "set")
    {
      // The item flag is set by ItemAdd itself; the status flag only by the later item info hook
      if ((status & ImGuiItemStatusFlags_Inputable) == 0 && (item.ItemFlags & ImGuiItemFlags_Inputable) == 0)
      {
        job.result = DoOutcome(false, quoted + " takes no typed value; drags, sliders and text fields do");
        return;
      }

      char number[64];
      std::snprintf(number, sizeof(number), "%.9g", job.numberValue);
      const char* text = job.valueIsNumber ? number : job.stringValue.c_str();

      // A field already being edited, like the Outliner's rename field, is typed into directly:
      // aiming the mouse at it focuses its window, and focusing a docked window deactivates it.
      if (ctx->UiContext->ActiveId == item.ID)
        ctx->KeyCharsReplaceEnter(text);
      else
        ctx->ItemInputValue(path, text);
      detail = "typed " + (job.valueIsNumber ? Json(job.numberValue).dump() : Json(job.stringValue).dump())
        + " into " + quoted;
    }
    else
    {
      if ((status & ImGuiItemStatusFlags_Openable) == 0)
      {
        job.result = DoOutcome(false, quoted + " cannot be opened or closed; tree nodes, headers and menus can");
        return;
      }

      bool open = job.action == "open";
      if (open)
        ctx->ItemOpen(path);
      else
        ctx->ItemClose(path);
      detail = (open ? "opened " : "closed ") + quoted;
    }

    if (!ctx->IsError())
      job.result = DoOutcome(true, std::move(detail));
  }
}
