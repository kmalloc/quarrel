#include "logger.h"
#include <thread>
#include <stdio.h>
#include <assert.h>
#include <sys/time.h>

namespace quarrel {

__thread char t_errnobuf[512];
__thread char t_time[64];
__thread time_t t_lastSecond;

const char* strerror_tl(int savedErrno) {
  return strerror_r(savedErrno, t_errnobuf, sizeof t_errnobuf);
}

Logger::LogLevel initLogLevel() {
    return Logger::INFO;
}

Logger::LogLevel g_logLevel = initLogLevel();

const char* LogLevelName[Logger::NUM_LOG_LEVELS] = {
  "TRACE ",
  "DEBUG ",
  "INFO  ",
  "WARN  ",
  "ERROR ",
  "FATAL ",
};

class T {
 public:
  T(const char* str, unsigned len)
    :str_(str),
     len_(len) {
    assert(strlen(str) == len_);
  }

  const char* str_;
  const unsigned len_;
};

template <int N>
inline LogStream& operator<<(LogStream& s, const char (&arr)[N]) {
  s.write(arr, N-1);
  return s;
}

inline LogStream& operator<<(LogStream& s, T v) {
  s.write(v.str_, v.len_);
  return s;
}

inline LogStream& operator<<(LogStream& s, const Logger::SourceFile& v) {
  s.write(v.data_, v.size_);
  return s;
}

void defaultOutput(const char* msg, int len) {
  size_t n = fwrite(msg, 1, len, stdout);
  //FIXME check n
  (void)n;
}

void defaultFlush() {
  fflush(stdout);
}

Logger::FlushFunc g_flush = defaultFlush;
Logger::OutputFunc g_output = defaultOutput;

Logger::Impl::Impl(LogLevel level, int savedErrno, const SourceFile& file, int line)
   :line_(line),
    level_(level),
    stream_(),
    basename_(file) {
  formatTime();
  stream_ << T(LogLevelName[level], 6);

  if (savedErrno != 0) {
    stream_ << strerror_tl(savedErrno) << " (errno=" << savedErrno << ") ";
  }
}

void Logger::Impl::formatTime() {
  struct timeval tv;
  gettimeofday(&tv,NULL);
  int64_t microSecondsSinceEpoch =  1000000 * tv.tv_sec + tv.tv_usec;

  time_t seconds = static_cast<time_t>(microSecondsSinceEpoch / 1000000);
  int microseconds = static_cast<int>(microSecondsSinceEpoch % 1000000);

  if (seconds != t_lastSecond) {
    t_lastSecond = seconds;
    struct tm tm_time;
    ::gmtime_r(&seconds, &tm_time);

    int len = snprintf(t_time, sizeof(t_time), "%4d%02d%02d %02d:%02d:%02d",
        tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
        tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);
    assert(len == 17); (void)len;
  }

  char ud[64];
  auto sz = snprintf(ud, sizeof(ud), ".%06dZ ", microseconds);
  stream_ << T(t_time, 17) << T(ud, sz);
}

void Logger::Impl::finish() {
  stream_ << " - " << basename_ << ':' << line_ << '\n';
}

Logger::Logger(SourceFile file, int line)
  : impl_(INFO, 0, file, line) {
}

Logger::Logger(SourceFile file, int line, LogLevel level, const char* func)
  : impl_(level, 0, file, line) {
  impl_.stream_ << func << ' ';
}

Logger::Logger(SourceFile file, int line, LogLevel level)
  : impl_(level, 0, file, line) {
}

Logger::Logger(SourceFile file, int line, bool toAbort)
  : impl_(toAbort?FATAL:ERROR, errno, file, line) {
}

Logger::~Logger() {
  impl_.finish();
  auto str = stream().str();
  g_output(str.data(), (int)str.length());
  if (impl_.level_ == FATAL) {
    g_flush();
    abort();
  }
}

void Logger::setLogLevel(Logger::LogLevel level) {
  g_logLevel = level;
}

void Logger::setOutput(OutputFunc out) {
  g_output = out;
}

void Logger::setFlush(FlushFunc flush) {
  g_flush = flush;
}

void Logger::resetOutput() {
    g_output = defaultOutput;
}

void Logger::resetFlush() {
    g_flush = defaultFlush;
}

}
