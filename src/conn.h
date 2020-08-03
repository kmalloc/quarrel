#ifndef __QUARREL_CONN_H_
#define __QUARREL_CONN_H_

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <sys/time.h>

#include "ptype.h"
#include "config.h"
#include "idgen.hpp"
#include "lrumap.hpp"

namespace quarrel {

    using ResponseCallback = std::function<int(std::unique_ptr<PaxosMsg>)>;
    using RequestHandler = std::function<int (std::unique_ptr<PaxosMsg> ptr, ResponseCallback cb)>;

    struct RpcReqData {
        uint32_t timeout_ms_;
        ResponseCallback cb_;
        std::shared_ptr<PaxosMsg> data_;
    };

    enum ConnType {
        ConnType_INVALID = 0,
        ConnType_LOCAL = 1,
        ConnType_Remote = 2,
    };

    // conn is a connection abstraction to an acceptor.
    class Conn {
        public:
            Conn(int type, AddrInfo addr): fd_(-1), type_(type), addr_(std::move(addr)) {}
            virtual ~Conn() {}

            int GetFd() const { return fd_; }
            int GetType() const { return type_; }
            const AddrInfo& GetAddr() const { return addr_; }

            // DoRequest performs an *ASYNCHRONOUS* rpc reqeust to the connected acceptor.
            // user must provide a callback for storing the conresponding response.
            virtual int DoRpcRequest(RpcReqData req) = 0;

        private:
            Conn(const Conn&) = delete;
            Conn& operator=(const Conn&) = delete;

        protected:
            int fd_;
            int type_;
            AddrInfo addr_;
    };

    class LocalConn : public Conn {
        public:
            explicit LocalConn(AddrInfo addr): Conn(ConnType_LOCAL, std::move(addr)) {}
    };

    class RemoteConn: public Conn {
        public:
            RemoteConn(int concur_num, AddrInfo addr)
            : Conn(ConnType_Remote, std::move(addr)), reqid_(1,1), req_(concur_num) {
                struct timeval tv;
                gettimeofday(&tv, NULL);
                reqid_.SetGreatThan(tv.tv_sec & 0xff);
            }

            virtual ~RemoteConn() {}

            virtual int DoRpcRequest(RpcReqData req) {
                req.data_->reqid_ = reqid_.GetAndInc();
                req_.Put(req.data_->reqid_, req);
                auto ret = DoWrite(req.data_);
                if (ret != kErrCode_OK) {
                    req_.Del(req.data_->reqid_);
                    return ret;
                }

                return kErrCode_OK;
            }

            // HandleRecv handle msg received from the connected acceptor.
            virtual int HandleRecv(std::unique_ptr<PaxosMsg> req) {
                auto rd = req_.GetPtr(req->reqid_);
                auto noop = [](std::unique_ptr<PaxosMsg>){ return 0; };
                if (!rd) {
                    return onReq_(std::move(req), noop);
                }

                rd->cb_(std::move(req));
                req_.Del(req->reqid_);
                return 0;
            }

            // implementation needed
            virtual int DoWrite(std::shared_ptr<PaxosMsg> msg) = 0;

        private:
            IdGen reqid_;
            RequestHandler onReq_;
            LruMap<uint64_t, RpcReqData> req_;
    };

    using ConnCreator = std::function<std::unique_ptr<Conn>(AddrInfo)>;

    class ConnMng {
        public:
            explicit ConnMng(std::shared_ptr<Configure> config): config_(std::move(config)) {}

            int CreateConn();

            // poll conn & recv.
            int StartWorker();
            int StopWorker();

            void SetConnCreator(ConnCreator creator) {
                conn_creator_ = std::move(creator);
            }

            std::unique_ptr<LocalConn>& GetLocalConn() {
                return local_conn_;
            }

            std::vector<std::unique_ptr<RemoteConn>>& GetRemoteConn() {
                return remote_conn_;
            }

        private:
            ConnMng(const ConnMng&) = delete;
            ConnMng& operator=(const ConnMng&) = delete;

            ConnCreator conn_creator_;
            std::shared_ptr<Configure> config_;
            std::unique_ptr<LocalConn> local_conn_;
            std::vector<std::unique_ptr<RemoteConn>> remote_conn_;
    };
}

#endif
