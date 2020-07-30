#ifndef __QUARREL_CONN_H_
#define __QUARREL_CONN_H_

#include <string>

namespace quarrel {
    class Conn {
        public:
            Conn(std::string addr, int port);

            int GetFd() const { return fd_; }

            virtual int SendData(void* data, int size) = 0;

        private:
            Conn(const Conn&) = delete;
            Conn& operator=(const Conn&) = delete;

        protected:
            int fd_;
            int port_;
            std::string addr_;
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
