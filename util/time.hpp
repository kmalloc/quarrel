#include <sys/time.h>

#include <stdint.h>

namespace quarrel {

inline uint64_t GetCurrTimeUS() {
  struct timeval tv;
  gettimeofday(&tv, NULL);

  return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

}  // namespace quarrel
