#ifndef __QUARREL_CONN_H_
#define __QUARREL_CONN_H_

#include <string>
#include <vector>
#include <memory>

#include "ptype.h"
#include "config.h"
#include "lrumap.hpp"

namespace quarrel {

    using ResponseCallback = int(void*, int);
    using RequestHandler = int (void*, int, ResponseCallback cb);

    struct ReqData {
        int size_;
        uint32_t expire_ms;
        ResponseCallback cb_;
        std::unique_ptr<uint8_t> data_;
    };

    class Conn {
        public:
            Conn(AddrInfo& addr);
            virtual ~Conn();

            int GetFd() const { return fd_; }

            virtual int DoRequest(ReqData req) = 0;
            virtual int DoResponse(std::unique_ptr<PaxosMsg> rsp) = 0;

            virtual int HandleRecv(std::unique_ptr<PaxosMsg> req);

        private:
            Conn(const Conn&) = delete;
            Conn& operator=(const Conn&) = delete;

        protected:
            int fd_;
            AddrInfo addr_;
            RequestHandler onReq_;
    };

    class LocalConn : public Conn {
    };

    class RemoteConn: public Conn {
        private:
            LruMap<uint64_t, ReqData> req_;
    };

    using ConnCreator = std::unique_ptr<Conn>(std::string, int);

    class ConnMng {
        public:
            explicit ConnMng(std::shared_ptr<Configure> config);

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
