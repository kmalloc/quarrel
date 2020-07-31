#ifndef __QUARREL_CONFIGURE_H_
#define __QUARREL_CONFIGURE_H_

#include <string>
#include <vector>

namespace quarrel {

    struct AddrInfo {
        int type_; // addr type, 1 for ipv4 2 for ipv6 3 for in-process queue(for testing).
        std::string addr_;
    };

    struct Configure {
        int timeout_;  // timeout for propose operation.
        int local_id_; // local paxos id
        int storage_type_; // storage type for plog
        int msg_version_; // msg version
        std::string local_storage_path_; // storage path

        AddrInfo local_;
        std::vector<AddrInfo> peer_;
    };

    int InitConfigFromFile(const std::string& path, Configure& config);
}

#endif
