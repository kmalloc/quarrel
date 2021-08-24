#include "conn.h"
#include "stl.hpp"

namespace quarrel {

int ConnMng::CreateConn() {
  int c = 0;
  conn_.clear();

  for (auto i = 0ull; i < config_->peer_.size(); i++) {
    c++;
    auto conn = conn_creator_(config_->peer_[i]);

    if (config_->peer_[i] == config_->local_) {
      conn_.insert(conn_.begin(), std::move(conn));
    } else {
      conn_.push_back(std::move(conn));
    }
  }

  return c;
}

}  // namespace quarrel
