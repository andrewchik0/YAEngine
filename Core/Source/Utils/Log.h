#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace YAEngine
{
  enum class LogLevel : uint8_t
  {
    Verbose = 0,
    Info    = 1,
    Warning = 2,
    Error   = 3
  };

  class Log
  {
  public:
    static void SetLevel(LogLevel level) { s_Level = level; }
    static LogLevel GetLevel() { return s_Level; }

    static void Write(LogLevel level, const char* tag, const char* file, int line, const char* fmt, ...)
    {
      if (level < s_Level) return;

      const char* levelStr = "";
      const char* colorBegin = "";
      const char* colorEnd   = "\033[0m";
      FILE* out = stdout;
      switch (level)
      {
        case LogLevel::Verbose: levelStr = "VERBOSE"; break;
        case LogLevel::Info:    levelStr = "INFO";    break;
        case LogLevel::Warning: levelStr = "WARNING"; colorBegin = "\033[33m"; break;
        case LogLevel::Error:   levelStr = "ERROR";   colorBegin = "\033[31m"; out = stderr; break;
      }

      const char* filename = file;
      if (const char* slash = strrchr(file, '/'))
        filename = slash + 1;
      else if (const char* bslash = strrchr(file, '\\'))
        filename = bslash + 1;

      fprintf(out, "%s[%s] [%s] %s:%d: ", colorBegin, levelStr, tag, filename, line);

      va_list args;
      va_start(args, fmt);
      vfprintf(out, fmt, args);
      va_end(args);

      fprintf(out, "%s\n", colorEnd);
    }

  private:
#ifdef NDEBUG
    static inline LogLevel s_Level = LogLevel::Info;
#else
    static inline LogLevel s_Level = LogLevel::Verbose;
#endif
  };
}

#define YA_LOG_VERBOSE(tag, fmt, ...) ::YAEngine::Log::Write(::YAEngine::LogLevel::Verbose, tag, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define YA_LOG_INFO(tag, fmt, ...)    ::YAEngine::Log::Write(::YAEngine::LogLevel::Info,    tag, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define YA_LOG_WARN(tag, fmt, ...)    ::YAEngine::Log::Write(::YAEngine::LogLevel::Warning, tag, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define YA_LOG_ERROR(tag, fmt, ...)   ::YAEngine::Log::Write(::YAEngine::LogLevel::Error,   tag, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
