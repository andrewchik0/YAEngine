#include "Editor/Bridge/BridgeUiTree.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_test_engine/imgui_te_engine.h>
#include <imgui_test_engine/imgui_te_utils.h>

namespace YAEngine
{
  namespace
  {
    // ImGuiTestItemInfo::DebugLabel keeps this many bytes, so a label that long may be cut.
    constexpr size_t REPORTED_LABEL_CAPACITY = sizeof(ImGuiTestItemInfo::DebugLabel) - 1;
    // How far past the previous match an item's text may appear. Plain text and separators
    // sit between items; anything further away most likely belongs to another window.
    constexpr size_t MAX_LINE_GAP = 40;
    // Combos report no label, so their text is only looked for close to the previous item.
    constexpr size_t MAX_UNLABELED_LINE_GAP = 2;
    // The log holds every window, so the items are aligned starting from each place the first
    // item's text occurs, and the start matching the most items wins.
    constexpr size_t MAX_ANCHORS = 8;
    constexpr size_t MIN_ANCHOR_LABEL_BYTES = 3;
    // Lines before an anchor where unlabeled items ahead of the first labeled one may sit
    constexpr size_t ANCHOR_LEAD_LINES = 4;
    // Parent chains deeper than this are treated as unresolvable
    constexpr int MAX_PARENT_DEPTH = 64;
    // Past this many items the PushID(label); Widget("") parent search, quadratic, is skipped
    constexpr size_t MAX_IDIOM_PARENT_SEARCH = 4096;

    // What ImGui's text log puts around a widget's label.
    enum class Decoration : uint8_t
    {
      None,
      Checkbox, // "[x] label"
      Radio,    // "(x) label"
      Frame,    // "{ value } label": drags, sliders, inputs, combos
      Button,   // "[ label ]"
      Header,   // "### label ###"
      Tree      // "> label"
    };

    struct Cursor
    {
      size_t line = 0;
      size_t column = 0;
    };

    struct ItemEntry
    {
      const ImGuiTestItemInfo* info = nullptr;
      std::string_view reported;
      std::string_view display;
      bool maybeCut = false;
    };

    struct Match
    {
      bool found = false;
      Decoration decoration = Decoration::None;
      std::string framedValue;
      // The full label, when the reported one was cut short or missing
      std::string recoveredLabel;
    };

    std::vector<std::string_view> SplitLines(std::string_view text)
    {
      std::vector<std::string_view> lines;
      size_t start = 0;
      while (start <= text.size())
      {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
          end = text.size();

        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r')
          line.remove_suffix(1);

        lines.push_back(line);
        start = end + 1;
      }
      return lines;
    }

    std::string_view TrimSpaces(std::string_view text)
    {
      while (!text.empty() && text.front() == ' ')
        text.remove_prefix(1);
      while (!text.empty() && text.back() == ' ')
        text.remove_suffix(1);
      return text;
    }

    bool IsTokenStart(std::string_view line, size_t position)
    {
      return position == 0 || line[position - 1] == ' ';
    }

    bool IsTokenEnd(std::string_view line, size_t end)
    {
      return end == line.size() || line[end] == ' ';
    }

    // Callers never pass an empty label: ImHashStr reads a zero size as "up to the terminator".
    ImGuiID HashLabel(std::string_view label, ImGuiID seed)
    {
      return ImHashStr(label.data(), label.size(), seed);
    }

    Decoration ReadDecoration(std::string_view line, size_t labelStart, std::string& outFramedValue)
    {
      std::string_view before = line.substr(0, labelStart);
      if (!before.empty() && before.back() == ' ')
        before.remove_suffix(1);

      if (before.ends_with("[x]") || before.ends_with("[ ]") || before.ends_with("[~]"))
        return Decoration::Checkbox;
      if (before.ends_with("(x)") || before.ends_with("( )"))
        return Decoration::Radio;
      if (before.ends_with("###"))
        return Decoration::Header;
      if (before.ends_with("["))
        return Decoration::Button;
      if (before.ends_with(">"))
        return Decoration::Tree;

      if (before.ends_with("}"))
      {
        size_t open = before.rfind('{');
        if (open != std::string_view::npos)
        {
          outFramedValue = std::string(TrimSpaces(before.substr(open + 1, before.size() - open - 2)));
          return Decoration::Frame;
        }
      }

      return Decoration::None;
    }

    bool MatchLabeled(const std::vector<std::string_view>& lines, const ItemEntry& item, Cursor& cursor, Match& outMatch)
    {
      size_t lastLine = std::min(lines.size(), cursor.line + MAX_LINE_GAP + 1);
      for (size_t l = cursor.line; l < lastLine; l++)
      {
        std::string_view line = lines[l];
        size_t from = l == cursor.line ? cursor.column : 0;
        for (size_t position = line.find(item.display, from); position != std::string_view::npos;
          position = line.find(item.display, position + 1))
        {
          if (!IsTokenStart(line, position))
            continue;

          size_t end = position + item.display.size();
          std::string recovered;
          if (item.maybeCut)
          {
            // A cut label only gives a prefix; the full one is the extension whose hash is the id.
            // Ids that do not come from the label (pointers, integers) never confirm one.
            for (size_t candidateEnd = line.size(); candidateEnd > end; candidateEnd--)
            {
              if (!IsTokenEnd(line, candidateEnd))
                continue;

              std::string_view candidate = line.substr(position, candidateEnd - position);
              if (HashLabel(candidate, item.info->ParentID) == item.info->ID)
              {
                recovered = std::string(candidate);
                end = candidateEnd;
                break;
              }
            }
          }
          else if (!IsTokenEnd(line, end))
          {
            continue;
          }

          outMatch.found = true;
          outMatch.decoration = ReadDecoration(line, position, outMatch.framedValue);
          outMatch.recoveredLabel = std::move(recovered);
          cursor = Cursor { .line = l, .column = end };
          return true;
        }
      }
      return false;
    }

    // Combos report no label at all: look for a framed value followed by text that hashes to
    // the item's id.
    bool MatchUnlabeled(const std::vector<std::string_view>& lines, const ItemEntry& item, Cursor& cursor, Match& outMatch)
    {
      size_t lastLine = std::min(lines.size(), cursor.line + MAX_UNLABELED_LINE_GAP + 1);
      for (size_t l = cursor.line; l < lastLine; l++)
      {
        std::string_view line = lines[l];
        size_t from = l == cursor.line ? cursor.column : 0;
        for (size_t close = line.find("} ", from); close != std::string_view::npos; close = line.find("} ", close + 1))
        {
          size_t labelStart = close + 2;
          for (size_t candidateEnd = line.size(); candidateEnd > labelStart; candidateEnd--)
          {
            if (!IsTokenEnd(line, candidateEnd))
              continue;

            std::string_view candidate = line.substr(labelStart, candidateEnd - labelStart);
            if (HashLabel(candidate, item.info->ParentID) != item.info->ID)
              continue;

            outMatch.found = true;
            outMatch.decoration = ReadDecoration(line, labelStart, outMatch.framedValue);
            outMatch.recoveredLabel = std::string(candidate);
            cursor = Cursor { .line = l, .column = candidateEnd };
            return true;
          }
        }
      }
      return false;
    }

    size_t MatchAll(const std::vector<std::string_view>& lines, const std::vector<ItemEntry>& items, Cursor start,
      std::vector<Match>& outMatches)
    {
      outMatches.assign(items.size(), Match {});
      Cursor cursor = start;
      size_t matched = 0;
      for (size_t i = 0; i < items.size(); i++)
      {
        const ItemEntry& item = items[i];
        bool found = false;
        if (!item.display.empty())
          found = MatchLabeled(lines, item, cursor, outMatches[i]);
        else if (item.reported.empty())
          found = MatchUnlabeled(lines, item, cursor, outMatches[i]);

        if (found)
          matched++;
      }
      return matched;
    }

    std::vector<Match> MatchItemsToLog(const std::vector<std::string_view>& lines, const std::vector<ItemEntry>& items)
    {
      std::vector<Cursor> anchors;
      auto first = std::find_if(items.begin(), items.end(),
        [](const ItemEntry& item) { return item.display.size() >= MIN_ANCHOR_LABEL_BYTES; });

      if (first != items.end())
      {
        for (size_t l = 0; l < lines.size() && anchors.size() < MAX_ANCHORS; l++)
        {
          size_t position = lines[l].find(first->display);
          if (position != std::string_view::npos && IsTokenStart(lines[l], position))
            anchors.push_back(Cursor { .line = l - std::min(l, ANCHOR_LEAD_LINES), .column = 0 });
        }
      }
      if (anchors.empty())
        anchors.push_back(Cursor {});

      std::vector<Match> best;
      size_t bestCount = 0;
      std::vector<Match> candidate;
      for (const Cursor& anchor : anchors)
      {
        size_t count = MatchAll(lines, items, anchor, candidate);
        if (best.empty() || count > bestCount)
        {
          best = candidate;
          bestCount = count;
        }
      }
      return best;
    }

    const char* ClassifyItem(const ImGuiTestItemInfo& info, Decoration decoration)
    {
      const bool inMenu = info.Window != nullptr
        && (info.Window->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_ChildMenu)) != 0;

      if (info.StatusFlags & ImGuiItemStatusFlags_Checkable)
        return inMenu ? "menuitem" : "checkbox";
      if ((info.StatusFlags & ImGuiItemStatusFlags_Inputable) || (info.ItemFlags & ImGuiItemFlags_Inputable))
        return "input";
      if (info.StatusFlags & ImGuiItemStatusFlags_Openable)
        return inMenu ? "menu" : decoration == Decoration::Header ? "header" : "tree";

      switch (decoration)
      {
        case Decoration::Frame: return "combo";
        case Decoration::Button: return "button";
        case Decoration::Radio: return "radio";
        default: return "item";
      }
    }
  }

  std::string EscapeBridgeUiRefSegment(std::string_view label)
  {
    std::string escaped;
    escaped.reserve(label.size());
    for (size_t i = 0; i < label.size(); i++)
    {
      char c = label[i];
      bool literalMarker = i == 0 && c == '$' && label.size() > 1 && label[1] == '$';
      if (c == '/' || c == '\\' || literalMarker)
        escaped += '\\';
      escaped += c;
    }
    return escaped;
  }

  std::string_view GetBridgeUiDisplayLabel(std::string_view label)
  {
    size_t hidden = label.find("##");
    return hidden == std::string_view::npos ? label : label.substr(0, hidden);
  }

  bool SplitBridgeUiRefPath(std::string_view path, std::string& outParent, std::string& outLast)
  {
    size_t lastSlash = std::string_view::npos;
    bool escaped = false;
    for (size_t i = 0; i < path.size(); i++)
    {
      if (path[i] == '\\' && !escaped)
      {
        escaped = true;
        continue;
      }
      if (path[i] == '/' && !escaped)
        lastSlash = i;
      escaped = false;
    }

    if (lastSlash == std::string_view::npos || lastSlash == 0 || lastSlash + 1 >= path.size())
      return false;

    outParent = std::string(path.substr(0, lastSlash));
    outLast = std::string(path.substr(lastSlash + 1));
    return true;
  }

  std::string GetBridgeUiLastRefSegment(std::string_view path)
  {
    std::string parent;
    std::string last;
    std::string_view segment = SplitBridgeUiRefPath(path, parent, last) ? std::string_view(last) : path;

    std::string unescaped;
    for (size_t i = 0; i < segment.size(); i++)
    {
      if (segment[i] == '\\' && i + 1 < segment.size())
        i++;
      unescaped += segment[i];
    }
    return unescaped;
  }

  Json BuildBridgeUiTreeItems(const ImGuiTestItemList& items, std::string_view loggedText)
  {
    std::vector<ItemEntry> entries;
    entries.reserve(items.size());
    std::unordered_map<ImGuiID, size_t> indexById;
    for (const ImGuiTestItemInfo& info : items)
    {
      ItemEntry entry;
      entry.info = &info;
      entry.reported = std::string_view(info.DebugLabel, strnlen(info.DebugLabel, sizeof(info.DebugLabel)));
      entry.display = GetBridgeUiDisplayLabel(entry.reported);
      entry.maybeCut = entry.reported.size() >= REPORTED_LABEL_CAPACITY;
      indexById.emplace(info.ID, entries.size());
      entries.push_back(entry);
    }

    std::vector<std::string_view> lines = SplitLines(loggedText);
    std::vector<Match> matches = MatchItemsToLog(lines, entries);

    // Full labels: recovered from the log where the reported one was cut or missing. A cut
    // label that could not be recovered cannot name the item.
    std::vector<std::string> labels(entries.size());
    std::vector<bool> labelKnown(entries.size(), false);
    for (size_t i = 0; i < entries.size(); i++)
    {
      if (!matches[i].recoveredLabel.empty())
      {
        labels[i] = matches[i].recoveredLabel;
        labelKnown[i] = true;
      }
      else
      {
        labels[i] = std::string(entries[i].reported);
        labelKnown[i] = !entries[i].reported.empty() && !entries[i].maybeCut;
      }
    }

    // The id each label is hashed with: the top of the id stack, except in the PushID(label);
    // Widget("") idiom of menu items, where that top is the item itself and the label hangs off
    // whatever lies below it, the window or another listed item.
    std::vector<ImGuiID> parentIds(entries.size());
    for (size_t i = 0; i < entries.size(); i++)
    {
      const ImGuiTestItemInfo& info = *entries[i].info;
      parentIds[i] = info.ParentID;
      if (info.ParentID != info.ID || !labelKnown[i])
        continue;

      if (info.Window != nullptr && HashLabel(labels[i], info.Window->ID) == info.ID)
      {
        parentIds[i] = info.Window->ID;
        continue;
      }
      if (entries.size() > MAX_IDIOM_PARENT_SEARCH)
        continue;
      for (const ItemEntry& other : entries)
      {
        if (other.info->ID != info.ID && HashLabel(labels[i], other.info->ID) == info.ID)
        {
          parentIds[i] = other.info->ID;
          break;
        }
      }
    }

    // Exact reference: every link from the window down hashes back to the next id
    std::vector<std::optional<std::string>> exactPaths(entries.size());
    std::vector<bool> exactResolved(entries.size(), false);
    auto resolveExact = [&](size_t index) {
      std::vector<size_t> chain;
      size_t current = index;
      std::optional<std::string> base;
      for (int depth = 0; depth < MAX_PARENT_DEPTH; depth++)
      {
        if (exactResolved[current])
        {
          base = exactPaths[current];
          break;
        }

        chain.push_back(current);
        const ImGuiTestItemInfo& info = *entries[current].info;
        if (info.Window != nullptr && parentIds[current] == info.Window->ID)
        {
          base = EscapeBridgeUiRefSegment(info.Window->Name);
          current = SIZE_MAX;
          break;
        }

        auto parent = indexById.find(parentIds[current]);
        if (parent == indexById.end() || parent->second == current)
          break;
        current = parent->second;
      }

      // Chain holds the unresolved items from the requested one up; resolve top-down.
      for (auto it = chain.rbegin(); it != chain.rend(); ++it)
      {
        size_t i = *it;
        const ImGuiTestItemInfo& info = *entries[i].info;
        std::optional<std::string> path;
        if (base && labelKnown[i] && HashLabel(labels[i], parentIds[i]) == info.ID)
        {
          std::string candidate = *base + "/" + EscapeBridgeUiRefSegment(labels[i]);
          if (ImHashDecoratedPath(candidate.c_str()) == info.ID)
            path = std::move(candidate);
        }
        exactPaths[i] = path;
        exactResolved[i] = true;
        base = path;
      }
    };

    // Wildcard anchor: the closest ancestor with an exact reference, else the item's window,
    // whose id is always on the item's id stack.
    auto findAnchor = [&](size_t index) -> std::string {
      const ImGuiTestItemInfo& start = *entries[index].info;
      ImGuiID parentId = parentIds[index];
      for (int depth = 0; depth < MAX_PARENT_DEPTH; depth++)
      {
        auto parent = indexById.find(parentId);
        if (parent == indexById.end() || parent->second == index)
          break;
        if (exactPaths[parent->second])
          return *exactPaths[parent->second];
        parentId = parentIds[parent->second];
      }
      return start.Window != nullptr ? EscapeBridgeUiRefSegment(start.Window->Name) : std::string();
    };

    Json result = Json::array();
    for (size_t i = 0; i < entries.size(); i++)
    {
      const ImGuiTestItemInfo& info = *entries[i].info;
      if (!exactResolved[i])
        resolveExact(i);

      Json path;
      if (exactPaths[i])
      {
        path = *exactPaths[i];
      }
      else if (labelKnown[i])
      {
        std::string anchor = findAnchor(i);
        if (!anchor.empty())
          path = anchor + "/**/" + EscapeBridgeUiRefSegment(labels[i]);
      }

      const Match& match = matches[i];
      const char* type = ClassifyItem(info, match.decoration);

      Json entry = Json::object();
      entry["path"] = std::move(path);
      entry["label"] = labels[i];
      entry["type"] = type;

      const bool checkable = (info.StatusFlags & ImGuiItemStatusFlags_Checkable) != 0;
      const bool openable = (info.StatusFlags & ImGuiItemStatusFlags_Openable) != 0;
      if (checkable)
        entry["value"] = (info.StatusFlags & ImGuiItemStatusFlags_Checked) != 0;
      else if (match.decoration == Decoration::Frame)
        entry["value"] = match.framedValue;

      Json flags = Json::object();
      flags["disabled"] = (info.ItemFlags & ImGuiItemFlags_Disabled) != 0;
      flags["checked"] = checkable ? Json((info.StatusFlags & ImGuiItemStatusFlags_Checked) != 0) : Json();
      flags["open"] = openable ? Json((info.StatusFlags & ImGuiItemStatusFlags_Opened) != 0) : Json();
      entry["flags"] = std::move(flags);

      result.push_back(std::move(entry));
    }
    return result;
  }
}
