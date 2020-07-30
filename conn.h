#ifndef __QUARREL_CONN_H_
#define __QUARREL_CONN_H_

#include <string>

#include "ptype.h"

namespace quarrel {

    using RequestCallback = int(void*, int);
    using ResponseCallback = int(void*, int);

    struct ReqData {
        int size_;
        void* data_;
        ResponseCallback cb_;
    };

    class Conn {
        public:
            Conn(std::string addr, int port);

            int GetFd() const { return fd_; }

            virtual int DoRequest(ReqData req) = 0;

            virtual int HandleRequest(std::unique_ptr<PaxosMsg> req);

        private:
            Conn(const Conn&) = delete;
            Conn& operator=(const Conn&) = delete;

        protected:
            int fd_;
            int port_;
            std::string addr_;
            RequestCallback onReq_;
    };

    class LocalConn : public Conn {
    };

    class RemoteConn: public Conn {
    };

    using ConnCreator = std::unique_ptr<Conn>(std::string, int);

    class ConnMng {
        public:
            // poll conn & recv.
            int StartWorker();

        private:
            ConnMng(const ConnMng&) = delete;
            ConnMng& operator=(const ConnMng&) = delete;
    };
}

#endif
