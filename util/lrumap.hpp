#ifndef _QUARREL_LRUMAP_H_
#define	_QUARREL_LRUMAP_H_

#include <list>
#include <cstddef>
#include <stdexcept>
#include <unordered_map>

namespace quarrel {

template <typename key_t, typename value_t>
class LruMap {
 public:
  using key_value_pair_t = std::pair<key_t, value_t>;
  using list_iterator_t = typename std::list<key_value_pair_t>::iterator;

  LruMap(size_t max_size) : max_size_(max_size) {}

  void Put(const key_t& key, value_t value) {
    auto it = kv_.find(key);
    order_.emplace_front(key, std::move(value));
    if (it != kv_.end()) {
      order_.erase(it->second);
    }
    kv_[key] = order_.begin();

    if (kv_.size() > max_size_) {
      auto last = order_.end();
      last--;
      kv_.erase(last->first);
      order_.pop_back();
    }
  }

  const value_t& Get(const key_t& key) {
    auto ptr = GetPtr(key);
    if (ptr == NULL) {
      throw std::range_error("key not exists");
    }

    return *ptr;
  }

  const value_t* GetPtr(const key_t& key) {
    auto it = kv_.find(key);
    if (it == kv_.end()) {
      return NULL;
    } else {
      order_.splice(order_.begin(), order_, it->second);
      return &it->second->second;
    }
  }

  bool Del(const key_t& key) {
    auto it = kv_.find(key);
    if (it == kv_.end()) return false;

    order_.erase(it->second);
    kv_.erase(it);
    return true;
  }

  bool Exists(const key_t& key) const { return kv_.find(key) != kv_.end(); }

  size_t Size() const { return kv_.size(); }
  size_t Capacity() const { return max_size_; }

 private:
  size_t max_size_;
  std::list<key_value_pair_t> order_;
  std::unordered_map<key_t, list_iterator_t> kv_;
};

}  // namespace quarrel

#endif
