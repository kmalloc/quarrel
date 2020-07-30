#ifndef _QUARREL_LRUMAP_H_
#define	_QUARREL_LRUMAP_H_

#include <list>
#include <cstddef>
#include <stdexcept>
#include <unordered_map>

namespace quarrel {

template<typename key_t, typename value_t>
class lru_map{
public:
	using key_value_pair_t = std::pair<key_t, value_t>;
	using list_iterator_t = typename std::list<key_value_pair_t>::iterator;

	lru_map(size_t max_size) : _max_size(max_size) {}

	void put(const key_t& key, value_t value) {
		auto it = kv_.find(key);
		order_.emplace_front(key, std::move(value));
		if (it != kv_.end()) {
			order_.erase(it->second);
			kv_.erase(it);
		}
		kv_[key] = order_.begin();

		if (kv_.size() > _max_size) {
			auto last = order_.end();
			last--;
			kv_.erase(last->first);
			order_.pop_back();
		}
	}

	const value_t& get(const key_t& key) {
		auto it = kv_.find(key);
		if (it == kv_.end()) {
			throw std::range_error("key not exists");
		} else {
			order_.splice(order_.begin(), order_, it->second);
			return it->second->second;
		}
	}

    bool del(const key_t& key) {
        auto it = kv_.find(key);
        if (it == kv_.end()) return false;

        order_.erase(it->second);
        kv_.erase(it);
        return true;
    }

	bool exists(const key_t& key) const {
		return kv_.find(key) != kv_.end();
	}

	size_t size() const {
		return kv_.size();
	}

private:
	size_t _max_size;
	std::list<key_value_pair_t> order_;
	std::unordered_map<key_t, list_iterator_t> kv_;
};

} // namespace

#endif
