#include "Editor/Panels/AgentPanel.h"

#include "Editor/Bridge/BridgeDiscovery.h"
#include "Editor/Bridge/BridgeJson.h"
#include "Editor/Bridge/EditorBridge.h"
#include "Editor/EditorPreferences.h"

namespace YAEngine
{
  namespace
  {
    constexpr ImVec4 ERROR_COLOR { 0.95f, 0.4f, 0.35f, 1.0f };

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

  AgentPanel::AgentPanel(EditorBridge& bridge, EditorPreferences& preferences)
    : m_Bridge(bridge),
      m_Preferences(preferences)
  {
  }

  void AgentPanel::OnRender(EditorContext& context)
  {
    if (!ImGui::Begin("AI Agent"))
    {
      ImGui::End();
      return;
    }

    bool enabled = m_Bridge.IsEnabled();
    if (ImGui::Checkbox("Allow agent connections", &enabled))
    {
      m_Bridge.SetEnabled(enabled);
      m_Preferences.mcpEnabled = enabled;
      m_Preferences.Save();
    }

    DrawStatus();

    if (ImGui::Button("Copy MCP config"))
      ImGui::SetClipboardText(BuildMcpConfig().c_str());
    ImGui::SameLine();
    if (ImGui::Button("Copy Claude Code command"))
      ImGui::SetClipboardText(BuildClaudeCodeCommand().c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(m_Bridge.GetClientCount() == 0);
    if (ImGui::Button("Disconnect all"))
      m_Bridge.DisconnectAll();
    ImGui::EndDisabled();

    ImGui::Separator();
    DrawClients();
    ImGui::Separator();
    DrawActivity();

    ImGui::End();
  }

  void AgentPanel::DrawStatus()
  {
    if (!m_Bridge.IsEnabled())
    {
      ImGui::TextDisabled("Disabled");
      return;
    }

    const std::string& error = m_Bridge.GetError();
    if (!error.empty())
      ImGui::TextColored(ERROR_COLOR, "Error: %s", error.c_str());
    else if (m_Bridge.IsListening())
      ImGui::TextColored(AGENT_ACTIVE_COLOR, "Listening on 127.0.0.1:%u", static_cast<unsigned>(m_Bridge.GetPort()));
    else
      ImGui::TextDisabled("Starting...");
  }

  void AgentPanel::DrawClients()
  {
    const std::vector<BridgeClientInfo>& clients = m_Bridge.GetClients();
    ImGui::Text("Connected clients: %zu", clients.size());
    if (clients.empty())
      return;

    if (ImGui::BeginTable("AgentClients", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
    {
      ImGui::TableSetupColumn("Client");
      ImGui::TableSetupColumn("Connected at");
      ImGui::TableHeadersRow();

      for (const BridgeClientInfo& client : clients)
      {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(client.name.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(client.connectedAt.c_str());
      }
      ImGui::EndTable();
    }
  }

  void AgentPanel::DrawActivity()
  {
    const std::deque<BridgeActivityRecord>& activity = m_Bridge.GetActivity();
    ImGui::Text("Recent requests: %zu", activity.size());
    if (activity.empty())
      return;

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
            ImGui::TextColored(ERROR_COLOR, "%s", record.result.c_str());
          ImGui::TableNextColumn();
          ImGui::Text("%.1f", record.durationMs);
        }
      }
      ImGui::EndTable();
    }
  }
}
