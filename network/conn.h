#ifndef __QUARREL_CONN_H_
#define __QUARREL_CONN_H_

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <functional>

#include "ptype.h"
#include "config.h"
#include "idgen.hpp"
#include "lrumap.hpp"
#include "paxos_group.h"

namespace quarrel {

using ResponseCallback = std::function<int(std::shared_ptr<PaxosMsg>)>;

using RequestHandler =
    std::function<int(std::shared_ptr<PaxosMsg> ptr, ResponseCallback cb)>;

struct RpcReqData {
  uint32_t timeout_ms_;
  ResponseCallback cb_;
  std::shared_ptr<PaxosMsg> data_;
};

enum ConnType {
  ConnType_INVALID = 0,
  ConnType_LOCAL = 1,
  ConnType_REMOTE = 2,
};

// Conn is a connection abstraction to an acceptor.
// real connection can be created on top of tcp or udp or even in-process queue.
class Conn {
 public:
  Conn(int type, AddrInfo addr)
      : fd_(-1), type_(type), addr_(std::move(addr)) {}
  virtual ~Conn() {}

  int GetFd() const { return fd_; }
  int GetType() const { return type_; }
  const AddrInfo& GetAddr() const { return addr_; }
  bool IsLocal() const { return type_ == ConnType_LOCAL; }

  // DoRpcRequest performs an *ASYNCHRONOUS* rpc reqeust to the connected
  // acceptor. user must provide a callback to handle the corresponding
  // response.
  virtual int DoRpcRequest(RpcReqData req) = 0;

 private:
  Conn(const Conn&) = delete;
  Conn& operator=(const Conn&) = delete;

 protected:
  int fd_;
  int type_;
  AddrInfo addr_;
};

// LocalConn is a conceptual connection to local acceptor within the same
// process. a default implementation is provided, usually user won't need to
// customize it.
class LocalConn : public Conn {
 public:
  explicit LocalConn(AddrInfo addr) : Conn(ConnType_LOCAL, std::move(addr)) {}
};

// RemoteConn is a real network connection to a remote acceptor.
// a default tcp based socket implementation is provided, usually this suits
// most use cases. However network management differs a lot from framework to
// framework user can wrap their own RemoteConn using tcp/udp/http/rpc/etc.
class RemoteConn : public Conn {
 public:
  RemoteConn(int concur_num, AddrInfo addr)
      : Conn(ConnType_REMOTE, std::move(addr)),
        reqid_(0xff, 1),
        reqToRemote_(concur_num) {}

  virtual ~RemoteConn() {}

  // customization point: implementation needed here
  // WARNING: this function may be called from multiple threads.
  // make sure it is thread safe.
  // also implementation of this function is expected to be ASYNCHRONOUS.
  virtual int DoWrite(std::shared_ptr<PaxosMsg> msg) = 0;

  virtual int DoRpcRequest(RpcReqData req) {
    {
      std::lock_guard<std::mutex> l(lock_);
      req.data_->reqid_ = reqid_.GetAndInc();
      reqToRemote_.Put(req.data_->reqid_, req);
    }

    auto ret = DoWrite(req.data_);

    if (ret != kErrCode_OK) {
      std::lock_guard<std::mutex> l(lock_);
      reqToRemote_.Del(req.data_->reqid_);
      return ret;
    }

    return kErrCode_OK;
  }

  // HandleRecv handle msg received from the connected acceptor.
  // which may be a rsp for a previous req.
  // or just a plain new request for local acceptor.
  virtual int HandleRecv(std::shared_ptr<PaxosMsg> req) {
    RpcReqData* rd = NULL;
    RpcReqData origin_req;

    {
      // check if the received msg is a rsponse and get the previous request.
      std::lock_guard<std::mutex> l(lock_);
      rd = reqToRemote_.GetPtr(req->reqid_);
      if (rd) {
        origin_req = std::move(*rd);
        rd = &origin_req;
        reqToRemote_.Del(req->reqid_);
      }
    }

    auto write_rsp = [this](std::shared_ptr<PaxosMsg> m) {
      return DoWrite(std::move(m));
    };

    if (!rd) {
      // handle incoming request
      return onReq_(std::move(req), write_rsp);
    }

    // handle response for previous req
    rd->cb_(std::move(req));

    return 0;
  }

 private:
  std::mutex lock_;
  IdGenByDate reqid_;
  RequestHandler onReq_;

  // TODO: reduce global lock by sharding.
  // generally this is not necessary, since each conn should be accessed from one thread only.
  LruMap<uint64_t, RpcReqData> reqToRemote_;
};

// ConnCreator creates connection specified by AddrInfo, a default creator is
// only capable of creating default local conn and default remote conn.
// cumstomization is needed if user have different implementations of LocalConn
// or RemoteConn.
using ConnCreator = std::function<std::unique_ptr<Conn>(AddrInfo)>;

class ConnMng {
 public:
  ConnMng(std::shared_ptr<Configure> config, std::shared_ptr<PaxosGroupBase> mapper)
      : config_(std::move(config)), pg_mapper_(std::move(mapper)) {}

  int CreateConn();

  void SetConnCreator(ConnCreator creator) {
    conn_creator_ = std::move(creator);
  }

  std::vector<std::unique_ptr<Conn>>& GetAllConn(uint64_t pinst) {
    // FIXME: select remote peers for pinst
    (void)pinst;
    return conn_;
  }

 private:
  ConnMng(const ConnMng&) = delete;
  ConnMng& operator=(const ConnMng&) = delete;

  std::mutex lock_;
  ConnCreator conn_creator_;
  std::shared_ptr<Configure> config_;
  std::shared_ptr<PaxosGroupBase> pg_mapper_;

  // conn is ordered by svr id
  std::vector<std::unique_ptr<Conn>> conn_;
};

}  // namespace quarrel

#endif
