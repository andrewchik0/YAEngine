#include "Editor/Bridge/LogRingBuffer.h"

namespace YAEngine
{
  namespace
  {
    // Keys, quotes and numbers around the strings of one serialized log.tail line
    constexpr size_t ENTRY_OVERHEAD_BYTES = 96;
  }

  LogRingBuffer::LogRingBuffer()
  {
    m_Entries.reserve(CAPACITY);
  }

  LogRingBuffer& LogRingBuffer::Get()
  {
    static LogRingBuffer s_Instance;
    return s_Instance;
  }

  void LogRingBuffer::Sink(LogLevel level, const char* tag, const char* file, int line, const char* text)
  {
    Get().Push(level, tag, file, line, text);
  }

  void LogRingBuffer::Push(LogLevel level, const char* tag, const char* file, int line, const char* text)
  {
    std::lock_guard lock(m_Mutex);

    // Once full, the evicted line is overwritten in place so its string buffers are reused
    LogRingEntry& entry = m_Entries.size() < CAPACITY ? m_Entries.emplace_back() : m_Entries[m_Next];
    m_Next = (m_Next + 1) % CAPACITY;

    entry.seq = m_NextSeq++;
    entry.level = level;
    entry.line = line;
    entry.tag.assign(tag != nullptr ? tag : "");
    entry.file.assign(file != nullptr ? file : "");
    entry.text.assign(text != nullptr ? text : "");
  }

  void LogRingBuffer::Tail(size_t maxCount, LogLevel minLevel, uint64_t afterSeq, size_t byteBudget,
    std::vector<LogRingEntry>& outEntries, uint64_t& outLastSeq) const
  {
    outEntries.clear();

    std::lock_guard lock(m_Mutex);
    size_t count = m_Entries.size();
    outLastSeq = count > 0 ? m_Entries[(m_Next + CAPACITY - 1) % CAPACITY].seq : 0;

    size_t bytes = 0;
    for (size_t i = 0; i < count && outEntries.size() < maxCount; i++)
    {
      const LogRingEntry& entry = m_Entries[(m_Next + CAPACITY - 1 - i) % CAPACITY];
      if (entry.seq <= afterSeq)
        break;
      if (entry.level < minLevel)
        continue;

      size_t entryBytes = entry.text.size() + entry.tag.size() + entry.file.size() + ENTRY_OVERHEAD_BYTES;
      if (bytes + entryBytes > byteBudget)
        break;

      bytes += entryBytes;
      outEntries.push_back(entry);
    }

    std::reverse(outEntries.begin(), outEntries.end());
  }

  const char* GetLogLevelName(LogLevel level)
  {
    switch (level)
    {
      case LogLevel::Verbose: return "verbose";
      case LogLevel::Info:    return "info";
      case LogLevel::Warning: return "warning";
      case LogLevel::Error:   return "error";
    }
    return "info";
  }

  std::optional<LogLevel> ParseLogLevelName(std::string_view name)
  {
    if (name == "verbose") return LogLevel::Verbose;
    if (name == "info")    return LogLevel::Info;
    if (name == "warning") return LogLevel::Warning;
    if (name == "error")   return LogLevel::Error;
    return std::nullopt;
  }
}
