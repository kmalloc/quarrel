#pragma once

#include <liburing.h>

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

#include "base/fix_count_allocator.hpp"
#include "base/lockfreequeue.hpp"

namespace iouring {

// io uring used locked memory.
// make sure to tune /etc/security/limits.conf accordingly.
struct UringOptions {
  int iodepth = 256;        // max pending req, default 256
  int instances = 2;        // number of uring instance. reqs dispatched randomly to each instances
  int max_batch_num = 256;  // max batch aggregate count, default to 256

  int dispatch_type = 1;            // 1 for hashed by fd / 2 for randomly select
  bool enable_sq_thread = false;    // not supported yet, need root privilege
  bool enable_io_dev_poll = false;  // not supported yet, device support is required
  bool enable_no_drop = false;      // not supported yet, kernel 5.4 not supported at this writing.
};

enum {
  IOURING_ERR_INIT_FAILED = -1001,
  IOURING_ERR_QUEUE_FULL = -1002,
  IOURING_ERR_NOTIFIER_FD_EMPTY = -1003,
  IOURING_ERR_REGISTER_EFD_FAILED = -1004,
  IOURING_ERR_ALLOC_SUBMIT_FAILED = -1005,
  IOURING_ERR_ALLOC_REQ_DATA_FAILED = -1006,
  IOURING_ERR_LOCK_MEM_NOT_ENOUGH = -1007,
  IOURING_ERR_SUBMIT_QUEUE_FULL = -1008,
};

struct IoContext;
struct WorkerInfo;
struct PendingReq;

class IoUring {
 public:
  explicit IoUring(const UringOptions& opt);
  ~IoUring();

  int Start();
  int Stop();

  // < 0 err
  // >= 0 data size read
  int AddReadReqSync(int fd, uint64_t offset, void* buff, uint64_t size);
  int AddBatchReadReqSync(int fd, uint64_t offset, struct iovec* buff, uint64_t size);

  // < 0 err
  // >= 0 data size write
  // NOTE: if fd is opened with O_APPEND, then param offset is ignored, data will be appended to the end
  int AddWriteReqSync(int fd, uint64_t offset, const void* data, uint64_t size);
  int AddBatchWriteReqSync(int fd, uint64_t offset, const struct iovec* data, uint64_t size);

  // ud is defined so that we can pass a plain old function pointer to std::function.
  // which will not incur heap allocation.
  using IoDoneNotify = std::function<int(int ret, void* ud)>;

  // callback will be called from io reaper thread, make sure it is light
  int AddReadReqAsync(int fd, uint64_t offset, void* buff, uint64_t size, IoDoneNotify n, void* ud);
  int AddBatchReadReqAsync(int fd, uint64_t offset, struct iovec* buff, uint64_t size, IoDoneNotify n, void* ud);

  int AddWriteReqAsync(int fd, uint64_t offset, const void* data, uint64_t size, IoDoneNotify n, void* ud);
  int AddBatchWriteReqAsync(int fd, uint64_t offset, const struct iovec* data, uint64_t size, IoDoneNotify n, void* ud);

 private:
  IoUring(const IoUring&) = delete;
  IoUring& operator=(const IoUring&) = delete;

  void pollLoop(int wid);
  int issueRequest(PendingReq& req);

  using IoOpAsyncFunc = std::function<int(IoUring*, int, uint64_t, void*, uint64_t, IoDoneNotify, void*)>;
  int doIoOpSync(int fd, uint64_t offset, void* buff, uint64_t size, IoOpAsyncFunc op);

 private:
  UringOptions opt_;
  std::atomic<bool> stop_;
  std::vector<std::unique_ptr<WorkerInfo>> workers_;
  base::FixCountLockFreeAllocator<int> event_fd_pool_;
  base::FixCountVectorAllocator<IoContext*> waiting_req_;
};

}  // namespace iouring
