#include "Editor/Panels/AgentPanel.h"

#include "Editor/Bridge/BridgeDiscovery.h"
#include "Editor/Bridge/BridgeJson.h"
#include "Editor/Bridge/EditorBridge.h"
#include "Editor/EditorPreferences.h"
#include "Editor/Utils/EditorFonts.h"
#include "Editor/Utils/EditorIcons.h"
#include "Editor/Utils/EditorStyle.h"
#include "Editor/Utils/EditorWidgets.h"

namespace YAEngine
{
  using namespace EditorWidgets;

  namespace
  {
    // Forward slashes survive every shell and JSON consumer the copied text may end up in
    std::string GetMcpServerDirectory()
    {
      std::u8string text = (GetBridgeRepoRoot() / "Tools" / "McpServer").generic_u8string();
      return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    }

    std::string BuildMcpConfig()
    {
      nlohmann::ordered_json server = nlohmann::ordered_json::object();
      server["command"] = "uv";
      server["args"] = nlohmann::ordered_json::array({ "run", "--directory", GetMcpServerDirectory(), "yaengine-mcp" });

      nlohmann::ordered_json config = nlohmann::ordered_json::object();
      config["mcpServers"]["yaengine"] = std::move(server);
      return config.dump(2, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
    }

    std::string BuildClaudeCodeCommand()
    {
      return "claude mcp add yaengine -- uv run --directory \"" + GetMcpServerDirectory() + "\" yaengine-mcp";
    }
  }

  AgentPanel::AgentPanel(EditorBridge& bridge, EditorPreferences& preferences, std::optional<bool> mcpOverride)
    : m_Bridge(bridge),
      m_Preferences(preferences),
      m_McpOverride(mcpOverride)
  {
  }

  void AgentPanel::OnRender(EditorContext& context)
  {
    if (!BeginPanel())
    {
      ImGui::End();
      return;
    }

    DrawConnection();

    if (BeginPropertyGroup("Setup", {
      .icon = ICON_LC_PLUG,
      .defaultOpen = false,
      .tooltip = "Connects an MCP client such as Claude Code to this editor" }))
    {
      if (PropertyButton("Copy MCP Config", {
        .icon = ICON_LC_COPY,
        .tooltip = "Copies an mcpServers entry that starts the YAEngine MCP server through uv, for clients configured with JSON" }))
      {
        ImGui::SetClipboardText(BuildMcpConfig().c_str());
      }
      if (PropertyButton("Copy Claude Code Command", {
        .icon = ICON_LC_TERMINAL,
        .tooltip = "Copies the claude mcp add command that registers the YAEngine MCP server" }))
      {
        ImGui::SetClipboardText(BuildClaudeCodeCommand().c_str());
      }
      EndPropertyGroup();
    }

    DrawClients();
    DrawActivity();

    ImGui::End();
  }

  void AgentPanel::DrawConnection()
  {
    BeginPropertyScope();

    bool enabled = m_Bridge.IsEnabled();
    if (PropertyBool("Allow Agent Connections", enabled, {
      .tooltip = "Runs the local bridge MCP clients connect to. The choice is saved for the next runs." }))
    {
      m_Bridge.SetEnabled(enabled);
      m_Preferences.mcpEnabled = enabled;
      m_Preferences.Save();
    }

    if (m_McpOverride.has_value())
    {
      PropertyStatus(nullptr, *m_McpOverride
        ? "Started with --mcp, which overrides the saved preference for this run"
        : "Started with --no-mcp, which overrides the saved preference for this run",
        StatusKind::Info);
    }

    const std::string& error = m_Bridge.GetError();
    if (!m_Bridge.IsEnabled())
    {
      PropertyStatus("Status", "Disabled");
    }
    else if (!error.empty())
    {
      PropertyStatus("Status", error.c_str(), StatusKind::Error);
    }
    else if (m_Bridge.IsListening())
    {
      char text[64];
      snprintf(text, sizeof(text), "Listening on 127.0.0.1:%u", uint32_t(m_Bridge.GetPort()));
      PropertyStatus("Status", text, StatusKind::Success);
    }
    else
    {
      PropertyStatus("Status", "Starting...");
    }

    if (PropertyButton("Disconnect All", {
      .icon = ICON_LC_UNPLUG,
      .tooltip = "Closes every open agent connection",
      .disabledReason = m_Bridge.GetClientCount() == 0 ? "No agent is connected" : nullptr }))
    {
      m_Bridge.DisconnectAll();
    }

    EndPropertyScope();
  }

  void AgentPanel::DrawClients()
  {
    const std::vector<BridgeClientInfo>& clients = m_Bridge.GetClients();
    char heading[48];
    snprintf(heading, sizeof(heading), "Connected Clients (%zu)", clients.size());
    PropertySubHeading(heading);

    if (clients.empty())
    {
      ImGui::TextDisabled("No agent is connected");
      return;
    }

    if (ImGui::BeginTable("AgentClients", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
    {
      ImGui::TableSetupColumn("Client");
      ImGui::TableSetupColumn("Connected At");
      ImGui::TableHeadersRow();

      EditorFonts::Push(EditorFontRole::Mono);
      for (const BridgeClientInfo& client : clients)
      {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(client.name.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(client.connectedAt.c_str());
      }
      EditorFonts::Pop();
      ImGui::EndTable();
    }
  }

  void AgentPanel::DrawActivity()
  {
    const std::deque<BridgeActivityRecord>& activity = m_Bridge.GetActivity();
    char heading[48];
    snprintf(heading, sizeof(heading), "Recent Requests (%zu)", activity.size());
    PropertySubHeading(heading);

    if (activity.empty())
    {
      ImGui::TextDisabled("No requests yet");
      return;
    }

    ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg
      | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("AgentActivity", 5, flags))
    {
      ImGui::TableSetupScrollFreeze(0, 1);
      ImGui::TableSetupColumn("Time");
      ImGui::TableSetupColumn("Client");
      ImGui::TableSetupColumn("Method", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Result");
      ImGui::TableSetupColumn("ms");
      ImGui::TableHeadersRow();

      const ImVec4 errorColor = ToImGuiColor(EditorStyle::GetTheme().error);
      EditorFonts::Push(EditorFontRole::Mono);

      // Newest first
      ImGuiListClipper clipper;
      clipper.Begin(static_cast<int>(activity.size()));
      while (clipper.Step())
      {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
        {
          const BridgeActivityRecord& record = activity[activity.size() - 1 - static_cast<size_t>(row)];
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(record.time.c_str());
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(record.client.c_str());
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(record.method.c_str());
          ImGui::TableNextColumn();
          if (record.result == "ok")
            ImGui::TextUnformatted("ok");
          else
            ImGui::TextColored(errorColor, "%s", record.result.c_str());
          ImGui::TableNextColumn();
          ImGui::Text("%.1f", record.durationMs);
        }
      }

      EditorFonts::Pop();
      ImGui::EndTable();
    }
  }
}
