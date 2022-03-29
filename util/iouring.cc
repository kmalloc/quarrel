#include "base/iouring.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <unistd.h>

namespace quarrel {
enum {
  REQ_TYPE_START,
  REQ_TYPE_READ,
  REQ_TYPE_WRIET,
  REQ_TYPE_READ_BATCH,
  REQ_TYPE_WRIET_BATCH,
  REQ_TYPE_INTERNAL_REQ,
  REQ_TYPE_END,
};

struct PendingReq {
  int fd_;
  int type_;
  void* buff_;
  uint64_t size_;
  uint64_t offset_;

  void* ud_;
  IoUring::IoDoneNotify done_;
};

struct IoContext {
  PendingReq req_;
  struct iovec iov_;
};

struct WorkerInfo {
  std::thread th_;
  PendingReq req_notifier_;

  struct io_uring ring_;
  LockFreeQueueV2<PendingReq> req_queue_;

  std::atomic<uint32_t> unsubmit_{0};

  // statistic counter
  uint64_t statistic_[IOURING_STATISTIC_END - 1];
};

int event_fd_alloc() {
  return syscall(__NR_eventfd, 0, 0);
  // return syscall(__NR_eventfd, 0, EFD_NONBLOCK);
}

void event_fd_release(int fd) {
  close(fd);
}

IoContext* req_alloc() {
  return new IoContext;
}

void req_release(IoContext* req) {
  delete req;
}

void SanitizeOptions(iouring::UringOptions& opt) {
  if (opt.instances <= 0) {
    opt.instances = 1;
  }
  if (opt.iodepth <= 0) {
    opt.iodepth = 256;
  }
  if (opt.max_batch_num <= 0) {
    opt.max_batch_num = 256;
  }

  opt.max_batch_num++;  // 1 more for internal usage
  opt.enable_sq_thread = false;
  opt.enable_io_dev_poll = false;
}

IoUring::IoUring(const UringOptions& opt) : opt_(opt) {
  SanitizeOptions(opt_);
  waiting_req_.Init(opt_.instances, opt_.iodepth, req_alloc, req_release);
  event_fd_pool_.Init(opt_.instances, opt_.iodepth, event_fd_alloc, event_fd_release);
}

IoUring::~IoUring() {
  Stop();
}

int IoUring::Stop() {
  if (stop_) return 0;

  stop_ = true;
  uint64_t v = 1;
  for (auto& w : workers_) {
    write(w->req_notifier_.fd_, &v, sizeof(v));

    w->th_.join();
    close(w->req_notifier_.fd_);
    io_uring_queue_exit(&w->ring_);
  }

  reporter_th_.join();
  return 0;
}

int IoUring::Start() {
  if (opt_.instances <= 0) return IOURING_ERR_INIT_FAILED;

  uint32_t flags = 0;
  if (opt_.enable_sq_thread) {
    flags |= IORING_SETUP_SQPOLL;
  }
  if (opt_.enable_io_dev_poll) {
    flags |= IORING_SETUP_IOPOLL;
  }
  if (opt_.enable_no_drop) {
    flags |= IORING_FEAT_NODROP;
  }

  stop_ = false;
  workers_.resize(opt_.instances);

  for (int i = 0; i < opt_.instances; i++) {
    auto efd = eventfd(0, 0);
    if (efd < 0) {
      return IOURING_ERR_REGISTER_EFD_FAILED;
    }

    workers_[i] = std::unique_ptr<WorkerInfo>(new WorkerInfo);
    memset(workers_[i]->statistic_, 0, sizeof(workers_[i]->statistic_));

    // iodepth + 1 to ensure 1 more position for internal req notifier
    auto ret = io_uring_queue_init(opt_.iodepth + 1, &workers_[i]->ring_, flags);

    if (ret) {
      close(efd);
      if (ret == -12) {
        return IOURING_ERR_LOCK_MEM_NOT_ENOUGH;
      }
      return ret;
    }

    workers_[i]->req_notifier_.fd_ = efd;
    workers_[i]->req_notifier_.type_ = REQ_TYPE_INTERNAL_REQ;
    workers_[i]->req_queue_.Init(opt_.iodepth);

    // io_uring_register_eventfd(&workers_[i]->ring_, efd);
    struct io_uring_sqe* sqe = io_uring_get_sqe(&workers_[i]->ring_);

    io_uring_prep_poll_add(sqe, efd, POLLIN);
    io_uring_sqe_set_data(sqe, &workers_[i]->req_notifier_);
    io_uring_submit(&workers_[i]->ring_);  // for io req notify

    workers_[i]->th_ = std::thread(&IoUring::pollLoop, this, i);
  }

  reporter_th_ = std::thread(&IoUring::reportLoop, this);

  return 0;
}

int IoUring::doIoOpSync(int fd, uint64_t offset, void* buff, uint64_t size, IoOpAsyncFunc op) {
  int notifier = event_fd_pool_.AllocByShard(fd);

  if (notifier == -1) {
    return IOURING_ERR_NOTIFIER_FD_EMPTY;
  }

  struct RetType {
    int ret;
    int notifier;
  } retv;

  retv.notifier = notifier;

  // make sure not to capture more than 1 variable.
  // otherwise it will introduce heap alloc
  auto cb = [](int res, void* ud) -> int {
    auto r = (struct RetType*)ud;
    uint64_t v = 1;
    r->ret = res;  // WARN no fast return is permitted in following, otherwise this will cause bad memory access.
    write(r->notifier, &v, sizeof(v));
    return 0;
  };

  int ret = op(this, fd, offset, buff, size, std::move(cb), &retv);

  if (ret) {
    event_fd_pool_.ReleaseByShard(fd, notifier);
    return ret;
  }

  struct pollfd pf = {notifier, POLLIN};
  ret = poll(&pf, 1, -1);

  if (ret > 0) {
    uint64_t ev;
    ret = read(notifier, &ev, sizeof(ev));
  }

  event_fd_pool_.ReleaseByShard(fd, notifier);
  return retv.ret;
}

int IoUring::AddReadReqSync(int fd, uint64_t offset, void* buff, uint64_t size) {
  // std::function will incur heap alloc if sizeof(lambda) > 8.
  // a plain old lambda(whithout capture) behave exactly like a function pointer
  auto op = [](IoUring* self, int fd, uint64_t offset, void* buff, uint64_t size, IoDoneNotify cb, void* ud) -> int {
    return self->AddReadReqAsync(fd, offset, buff, size, std::move(cb), ud);
  };

  return doIoOpSync(fd, offset, buff, size, std::move(op));
}

int IoUring::AddWriteReqSync(int fd, uint64_t offset, const void* data, uint64_t size) {
  auto op = [](IoUring* self, int fd, uint64_t offset, void* data, uint64_t size, IoDoneNotify cb, void* ud) -> int {
    return self->AddWriteReqAsync(fd, offset, data, size, std::move(cb), ud);
  };

  return doIoOpSync(fd, offset, (void*)data, size, std::move(op));
}

int IoUring::AddBatchReadReqSync(int fd, uint64_t offset, struct iovec* buff, uint64_t size) {
  auto op = [](IoUring* self, int fd, uint64_t offset, void* buff, uint64_t size, IoDoneNotify cb, void* ud) -> int {
    return self->AddBatchReadReqAsync(fd, offset, (struct iovec*)buff, size, std::move(cb), ud);
  };

  return doIoOpSync(fd, offset, buff, size, std::move(op));
}

int IoUring::AddBatchWriteReqSync(int fd, uint64_t offset, const struct iovec* data, uint64_t size) {
  auto op = [](IoUring* self, int fd, uint64_t offset, void* data, uint64_t size, IoDoneNotify cb, void* ud) -> int {
    return self->AddBatchWriteReqAsync(fd, offset, (const struct iovec*)data, size, std::move(cb), ud);
  };

  return doIoOpSync(fd, offset, (void*)data, size, std::move(op));
}

int IoUring::AddReadReqAsync(int fd, uint64_t offset, void* buff, uint64_t size, IoDoneNotify n, void* ud) {
  PendingReq req;
  req.ud_ = ud;
  req.fd_ = fd;
  req.buff_ = buff;
  req.size_ = size;
  req.offset_ = offset;
  req.type_ = REQ_TYPE_READ;
  req.done_ = std::move(n);
  return issueRequest(req);
}

int IoUring::AddWriteReqAsync(int fd, uint64_t offset, const void* data, uint64_t size, IoDoneNotify n, void* ud) {
  PendingReq req;
  req.fd_ = fd;
  req.ud_ = ud;
  req.size_ = size;
  req.offset_ = offset;
  req.buff_ = (void*)data;
  req.type_ = REQ_TYPE_WRIET;
  req.done_ = std::move(n);
  return issueRequest(req);
}

int IoUring::AddBatchReadReqAsync(int fd, uint64_t offset, struct iovec* buff, uint64_t size, IoDoneNotify n, void* ud) {
  PendingReq req;
  req.ud_ = ud;
  req.fd_ = fd;
  req.buff_ = buff;
  req.size_ = size;
  req.offset_ = offset;
  req.type_ = REQ_TYPE_READ_BATCH;
  req.done_ = std::move(n);
  return issueRequest(req);
}

int IoUring::AddBatchWriteReqAsync(int fd, uint64_t offset, const struct iovec* data, uint64_t size, IoDoneNotify n, void* ud) {
  PendingReq req;
  req.fd_ = fd;
  req.ud_ = ud;
  req.size_ = size;
  req.offset_ = offset;
  req.buff_ = (void*)data;
  req.type_ = REQ_TYPE_WRIET_BATCH;
  req.done_ = std::move(n);
  return issueRequest(req);
}

int IoUring::issueRequest(PendingReq& req) {
  int idx = 0;
  auto& w = workers_[idx];

  if (opt_.dispatch_type == 1) {
    idx = req.fd_ % workers_.size();
  } else {
    idx = rand() % workers_.size();
  }

  if (w->req_queue_.Enqueue(std::move(req), false)) {
    return IOURING_ERR_QUEUE_FULL;
  }

  auto waiting = w->unsubmit_.fetch_add(1, std::memory_order_relaxed);

  if (waiting == 0) {
    uint64_t v = 1;
    write(w->req_notifier_.fd_, &v, sizeof(v));
  }

  return 0;
}

void IoUring::reportLoop() {
  while (!stop_.load(std::memory_order_relaxed)) {
    if (reporter_) {
      for (auto i = 0; i < opt_.instances; i++) {
        for (int j = IOURING_STATISTIC_START; j < IOURING_STATISTIC_END; j++) {
          if (workers_[i]->statistic_[j] == 0) {
            continue;
          }

          reporter_(i, j + 1, workers_[i]->statistic_[j]);
          workers_[i]->statistic_[j] = 0;  // not thread safe but not really matter
        }
      }
    }

    std::this_thread::sleep_for(std::chrono::seconds(4));
  }
}

void IoUring::pollLoop(int wid) {
  bool wait_block = true;
  bool rearm_notifier = false;

  std::vector<IoContext*> unsubmitted;
  unsubmitted.reserve(opt_.iodepth + 1);

  while (!stop_.load(std::memory_order_relaxed)) {
    int ret = 0;
    struct io_uring_cqe* cqe = NULL;

    if (wait_block) {
      ret = io_uring_wait_cqe(&workers_[wid]->ring_, &cqe);
    } else {
      ret = io_uring_peek_cqe(&workers_[wid]->ring_, &cqe);
    }

    if (wait_block && ret < 0) {
      // assert(0);
      workers_[wid]->statistic_[IOURING_STATISTIC_POLL_FAIL - 1]++;
      continue;
    }

    if (ret == 0 && cqe) {
      IoContext* ctx = (IoContext*)cqe->user_data;

      switch (ctx->req_.type_) {
        case REQ_TYPE_READ:
        case REQ_TYPE_READ_BATCH:
          ctx->req_.done_(cqe->res, ctx->req_.ud_);
          waiting_req_.ReleaseByShard(wid, ctx);
          break;
        case REQ_TYPE_WRIET:
        case REQ_TYPE_WRIET_BATCH:
          ctx->req_.done_(cqe->res, ctx->req_.ud_);
          waiting_req_.ReleaseByShard(wid, ctx);
          break;
        case REQ_TYPE_INTERNAL_REQ:
          // rearm notifier
          uint64_t ev;
          rearm_notifier = true;
          read(ctx->req_.fd_, &ev, sizeof(ev));
          workers_[wid]->statistic_[IOURING_STATISTIC_INTERNAL_REQ - 1]++;
          break;
      }

      io_uring_cqe_seen(&workers_[wid]->ring_, cqe);
    }

    int batch_num = 0;
    PendingReq pending_req;

    if (rearm_notifier) {
      struct io_uring_sqe* sqe = io_uring_get_sqe(&workers_[wid]->ring_);
      if (sqe) {
        batch_num++;
        io_uring_prep_poll_add(sqe, workers_[wid]->req_notifier_.fd_, POLLIN);
        io_uring_sqe_set_data(sqe, &workers_[wid]->req_notifier_);
      }
    }

    while (workers_[wid]->req_queue_.Dequeue(pending_req, false) == 0 && batch_num < opt_.max_batch_num) {
      batch_num++;
      workers_[wid]->unsubmit_.fetch_sub(1, std::memory_order_relaxed);

      if (pending_req.type_ <= REQ_TYPE_START || pending_req.type_ >= REQ_TYPE_END) {
        // unknown type
        assert(0);
        continue;
      }

      auto ctx = waiting_req_.AllocByShard(wid);
      if (ctx == NULL) {
        workers_[wid]->statistic_[IOURING_STATISTIC_ALLOC_CTX_FAIL - 1]++;
        pending_req.done_(IOURING_ERR_ALLOC_REQ_DATA_FAILED, pending_req.ud_);
        continue;
      }

      struct io_uring_sqe* sqe = io_uring_get_sqe(&workers_[wid]->ring_);
      if (!sqe) {
        pending_req.done_(IOURING_ERR_ALLOC_SUBMIT_FAILED, pending_req.ud_);
        waiting_req_.ReleaseByShard(wid, ctx);
        workers_[wid]->statistic_[IOURING_STATISTIC_GET_SQE_FAIL - 1]++;
        continue;
      }

      uint32_t num_iov = 0;
      struct iovec* iv = NULL;

      if (pending_req.type_ == REQ_TYPE_READ || pending_req.type_ == REQ_TYPE_WRIET) {
        num_iov = 1;
        iv = &ctx->iov_;
        ctx->iov_.iov_len = pending_req.size_;
        ctx->iov_.iov_base = pending_req.buff_;
      } else if (pending_req.type_ == REQ_TYPE_READ_BATCH || pending_req.type_ == REQ_TYPE_WRIET_BATCH) {
        num_iov = pending_req.size_;
        iv = (struct iovec*)pending_req.buff_;
      }

      // use readv/writev, read/write is not suppported in 5.4 kernel.
      if (pending_req.type_ == REQ_TYPE_READ || pending_req.type_ == REQ_TYPE_READ_BATCH) {
        io_uring_prep_readv(sqe, pending_req.fd_, iv, num_iov, pending_req.offset_);
      } else if (pending_req.type_ == REQ_TYPE_WRIET || pending_req.type_ == REQ_TYPE_WRIET_BATCH) {
        io_uring_prep_writev(sqe, pending_req.fd_, iv, num_iov, pending_req.offset_);
      }

      ctx->req_ = std::move(pending_req);
      io_uring_sqe_set_data(sqe, ctx);
      unsubmitted.push_back(ctx);
    }

    if (batch_num > 0) {
      auto ret = io_uring_submit(&workers_[wid]->ring_);
      if (ret < 0) {
        if (ret == -EBUSY) {
          ret = IOURING_ERR_SUBMIT_QUEUE_FULL;
        }
        for (auto i = 0u; i < unsubmitted.size(); i++) {
          unsubmitted[i]->req_.done_(ret, unsubmitted[i]->req_.ud_);
          waiting_req_.ReleaseByShard(wid, unsubmitted[i]);
        }
        workers_[wid]->statistic_[IOURING_STATISTIC_SUBMIT_FAIL - 1]++;
      } else {
        unsubmitted.clear();
        rearm_notifier = false;
      }
      workers_[wid]->statistic_[IOURING_STATISTIC_SUBMIT - 1]++;
    }

    wait_block = (batch_num < opt_.max_batch_num);
    workers_[wid]->statistic_[IOURING_STATISTIC_REQ - 1] += batch_num;
  }
}

}  // namespace quarrel
