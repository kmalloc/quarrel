#ifndef __QUARREL_LOGGING_H_
#define __QUARREL_LOGGING_H_

#include <sstream>
#include <functional>
#include <string.h>

namespace quarrel {

// FIXME
class LogStream: public std::ostringstream {};

class Logger {
 public:
  enum LogLevel {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL,
    NUM_LOG_LEVELS,
  };

  // compile time calculation of basename of source file
  class SourceFile {
   public:
    template <int N>
    SourceFile(const char (&arr)[N]) : data_(arr), size_(N - 1) {
      const char* slash = strrchr(data_, '/');  // builtin function
      if (slash) {
        data_ = slash + 1;
        size_ -= static_cast<int>(data_ - arr);
      }
    }

    explicit SourceFile(const char* filename) : data_(filename) {
      const char* slash = strrchr(filename, '/');
      if (slash) {
        data_ = slash + 1;
      }
      size_ = static_cast<int>(strlen(data_));
    }

    const char* data_;
    int size_;
  };

  Logger(SourceFile file, int line);
  Logger(SourceFile file, int line, LogLevel level);
  Logger(SourceFile file, int line, LogLevel level, const char* func);
  Logger(SourceFile file, int line, bool toAbort);
  ~Logger();

  LogStream& stream() { return impl_.stream_; }

  static LogLevel logLevel();
  static void setLogLevel(LogLevel level);

  using FlushFunc = std::function<void()>;
  using OutputFunc = std::function<void(const char*, int)>;

  static void setFlush(FlushFunc);
  static void setOutput(OutputFunc);
  static void resetFlush();
  static void resetOutput();

 private:
  class Impl {
   public:
    using LogLevel = Logger::LogLevel;
    Impl(LogLevel level, int old_errno, const SourceFile& file, int line);
    void formatTime();
    void finish();

    int line_;
    LogLevel level_;

    LogStream stream_;
    SourceFile basename_;
  };

  Impl impl_;
};

extern Logger::LogLevel g_logLevel;
inline Logger::LogLevel Logger::logLevel() { return g_logLevel; }

}  // namespace quarrel

#define LOG_WARN Logger(__FILE__, __LINE__, Logger::WARN).stream()
#define LOG_INFO Logger(__FILE__, __LINE__, Logger::INFO).stream()
#define LOG_ERR Logger(__FILE__, __LINE__, Logger::ERROR).stream()

#endif
