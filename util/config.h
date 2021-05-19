#ifndef __QUARREL_CONFIGURE_H_
#define __QUARREL_CONFIGURE_H_

#include <string>
#include <vector>

#include "ptype.h"

namespace quarrel {
struct AddrInfo {
  int id_;    // svr id
  int type_;  // addr type, 1 for ipv4 2 for ipv6 3 for in-process queue(for testing).
  std::string addr_;
};

inline bool operator==(const AddrInfo& left, const AddrInfo& right) {
  if (&left == &right) return true;

  return (left.id_ == right.id_) && (left.type_ == right.type_) && (left.addr_ == right.addr_);
}

struct Configure {
  uint32_t timeout_{0};  // timeout for propose operation.
  uint32_t pid_cookie_{0};

  uint32_t total_acceptor_{0};
  uint32_t pg_type_{PGT_Quorum3};
  uint32_t acceptor_worker_count_{0};
  uint32_t entry_cache_num_{100000};
  uint32_t worker_msg_queue_sz_{10000};

  uint32_t msg_version_{0};         // msg version
  uint32_t storage_type_{0};        // storage type for plog
  uint32_t plog_inst_num_{0};       // plog instance number
  std::string local_storage_path_;  // storage path

  AddrInfo local_;
  std::vector<AddrInfo> peer_;
};

int InitConfigFromFile(const std::string& path, Configure& config);
}  // namespace quarrel

#endif
