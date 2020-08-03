#include "conn.h"
#include "stl.hpp"

namespace quarrel {

    int ConnMng::CreateConn() {
        remote_conn_.clear();
        local_conn_ = dynamic_unique_ptr_cast_nodel<LocalConn, Conn>(std::move(conn_creator_(config_->local_)));

        auto i = 0;
        for(i = 0; i < config_->peer_.size(); i++) {
            auto conn = conn_creator_(config_->peer_[i]);
            auto conn2 = dynamic_unique_ptr_cast_nodel<RemoteConn, Conn>(std::move(conn));
            remote_conn_.push_back(std::move(conn2));
        }

        return i + 1;
    }

}
