#pragma once

#include "Editor/Bridge/BridgeJson.h"

struct ImGuiTestItemList;

namespace YAEngine
{
  // Turns one GatherItems() result into ui.tree items. The test engine reports ids, status
  // flags and a label cut at 31 bytes, but neither the widget kind nor its value, so both are
  // read from the text ImGui logged for one frame, matched to the items in submission order.
  Json BuildBridgeUiTreeItems(const ImGuiTestItemList& items, std::string_view loggedText);

  // One label as a segment of a test engine reference path, where '/', '\' and a leading "$$"
  // mean something.
  std::string EscapeBridgeUiRefSegment(std::string_view label);
  // What ImGui displays of a label: everything before "##".
  std::string_view GetBridgeUiDisplayLabel(std::string_view label);
  // Splits a reference path at its last unescaped '/'; both halves stay escaped.
  bool SplitBridgeUiRefPath(std::string_view path, std::string& outParent, std::string& outLast);
  // The last segment of a reference path, unescaped.
  std::string GetBridgeUiLastRefSegment(std::string_view path);
}
