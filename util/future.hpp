#ifndef __QUARREL_FUTURE_H__
#define __QUARREL_FUTURE_H__

#include <chrono>
#include <thread>
#include <future>
#include <vector>
#include <algorithm>

#include "time.hpp"

namespace quarrel {

template <typename Iterator>
Iterator future_find_first_ready(Iterator begin, Iterator end) {
  return std::find_if(begin, end,
                      [](decltype(*begin) &fr) { return fr.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
}

template <typename T>
int future_wait_any(std::vector<std::future<T>> &arr, uint32_t timeout_ms, uint32_t sleep_us = 1) {
  if (arr.empty()) {
    return -1;
  }

  auto iterator = arr.end();
  auto start_tm = GetCurrTimeUS();

  do {
    iterator = future_find_first_ready(arr.begin(), arr.end());

    if (iterator != arr.end()) {
      break;
    }

    if (sleep_us == 0) {
      std::this_thread::yield();
    } else {
      std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
    }

    auto elapsed = GetCurrTimeUS() - start_tm;
    if (elapsed > timeout_ms * 1000) {
      return -2;
    }

  } while (1);

  return iterator - arr.begin();
}

}  // namespace quarrel

#endif
