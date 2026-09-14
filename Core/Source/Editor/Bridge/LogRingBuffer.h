#pragma once

#include "Pch.h"
#include "Utils/Log.h"

namespace YAEngine
{
  struct LogRingEntry
  {
    uint64_t seq = 0;
    LogLevel level = LogLevel::Info;
    int32_t line = 0;
    std::string tag;
    std::string file;
    std::string text;
  };

  // The newest log lines, for log.tail. A process-lifetime singleton: Log::Write calls the sink
  // from any thread, so nothing the sink touches may be destroyed while threads still log.
  class LogRingBuffer
  {
  public:
    static constexpr size_t CAPACITY = 4096;

    static LogRingBuffer& Get();
    // Installed with Log::SetSink.
    static void Sink(LogLevel level, const char* tag, const char* file, int line, const char* text);

    void Push(LogLevel level, const char* tag, const char* file, int line, const char* text);

    // Newest entries at or above minLevel with seq > afterSeq, returned oldest first. Stops at
    // maxCount entries or once the entries would exceed byteBudget. outLastSeq is the newest seq
    // stored, filtered out or not, so it can be passed back as afterSeq.
    void Tail(size_t maxCount, LogLevel minLevel, uint64_t afterSeq, size_t byteBudget,
      std::vector<LogRingEntry>& outEntries, uint64_t& outLastSeq) const;

  private:
    LogRingBuffer();

    mutable std::mutex m_Mutex;
    std::vector<LogRingEntry> m_Entries;
    // Slot the next line is written to; the oldest line once the buffer is full
    size_t m_Next = 0;
    uint64_t m_NextSeq = 1;
  };

  // Protocol spelling: verbose, info, warning, error.
  const char* GetLogLevelName(LogLevel level);
  std::optional<LogLevel> ParseLogLevelName(std::string_view name);
}
