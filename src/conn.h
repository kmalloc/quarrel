#ifndef __QUARREL_CONN_H_
#define __QUARREL_CONN_H_

#include <string>
#include <vector>
#include <memory>
#include <functional>

#include "ptype.h"
#include "config.h"
#include "lrumap.hpp"

namespace quarrel {

    using ResponseCallback = std::function<int(std::shared_ptr<PaxosMsg>)>;
    using RequestHandler = std::function<int (void*, int, ResponseCallback cb)>;

    struct RpcReqData {
        uint32_t timeout_ms_;
        ResponseCallback cb_;
        std::shared_ptr<PaxosMsg> data_;
    };

    // conn is a connection abstraction to an acceptor.
    class Conn {
        public:
            explicit Conn(AddrInfo addr);
            virtual ~Conn();

            int GetFd() const { return fd_; }

            // DoRequest performs an *ASYNCHRONOUS* rpc reqeust to the connected acceptor.
            // user must provide a callback for storing the conresponding response.
            virtual int DoRpcRequest(RpcReqData req) = 0;

        private:
            Conn(const Conn&) = delete;
            Conn& operator=(const Conn&) = delete;

        protected:
            int fd_;
            AddrInfo addr_;
    };

    class LocalConn : public Conn {
        public:
            LocalConn(AddrInfo addr);
    };

    class RemoteConn: public Conn {
        public:
            // HandleRecv handle msg received from the connected acceptor.
            virtual int HandleRecv(std::unique_ptr<PaxosMsg> req) = 0;

        private:
            RequestHandler onReq_;
            LruMap<uint64_t, RpcReqData> req_;
    };

    using ConnCreator = std::unique_ptr<Conn>(AddrInfo);

    class ConnMng {
        public:
            explicit ConnMng(std::shared_ptr<Configure> config);

            std::unique_ptr<LocalConn>& GetLocalConn();
            std::vector<std::unique_ptr<RemoteConn>>& GetRemoteConn();

            // poll conn & recv.
            int StartWorker();
            int StopWorker();

        private:
            ConnMng(const ConnMng&) = delete;
            ConnMng& operator=(const ConnMng&) = delete;

            std::unique_ptr<LocalConn> local_conn_;
            std::vector<std::unique_ptr<Conn>> remote_conn_;
    };
}

#endif
