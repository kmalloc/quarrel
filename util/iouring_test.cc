#include "base/iouring.h"
#include "gtest/gtest.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

using namespace iouring;

int GetFileSize(const char* fname) {
  struct stat sbuf;
  if (stat(fname, &sbuf) == 0) {
    return sbuf.st_size;
  }
  return 0;
}

TEST(iouring, verify_read_write) {
  UringOptions opt;
  opt.instances = 2;
  // opt.enable_no_drop = true;

  IoUring iu(opt);
  ASSERT_EQ(0, iu.Start());

  const char* fn = "/tmp/iouring_test_verify.log";

  unlink(fn);
  int fd = open(fn, O_CREAT | O_RDWR, 0644);

  ASSERT_GE(fd, 0);
  ASSERT_EQ(16, iu.AddWriteReqSync(fd, 0, "miliao-test-data", 16));

  char buff[25];
  ASSERT_EQ(16, iu.AddReadReqSync(fd, 0, buff, 16));

  buff[16] = 0;
  ASSERT_STREQ(buff, "miliao-test-data");

  ASSERT_EQ(11, iu.AddWriteReqSync(fd, 6, "miliao-test-data", 11));
  ASSERT_EQ(11, iu.AddReadReqSync(fd, 6, buff, 11));

  buff[11] = 0;
  ASSERT_STREQ(buff, "miliao-test");

  ASSERT_EQ(12, iu.AddReadReqSync(fd, 0, buff, 12));
  buff[12] = 0;
  ASSERT_STREQ(buff, "miliaomiliao");

  char* str0 = "miliao1";
  char* str1 = "miliao2";
  char* str2 = "miliao3";
  struct iovec iov[3];
  ssize_t nwritten;

  iov[0].iov_base = str0;
  iov[0].iov_len = strlen(str0);
  iov[1].iov_base = str1;
  iov[1].iov_len = strlen(str1);
  iov[2].iov_base = str2;
  iov[2].iov_len = strlen(str2);

  ASSERT_EQ(21, iu.AddBatchWriteReqSync(fd, 3, iov, 3));

  char buff0[10], buff1[10], buff2[10];
  memset(buff0, 0, 10);
  memset(buff1, 0, 10);
  memset(buff2, 0, 10);

  iov[0].iov_base = buff0;
  iov[0].iov_len = 6;
  iov[1].iov_base = buff1;
  iov[1].iov_len = 7;
  iov[2].iov_base = buff2;
  iov[2].iov_len = 8;
  ASSERT_EQ(21, iu.AddBatchReadReqSync(fd, 3, iov, 3));
  ASSERT_STREQ("miliao", buff0);
  ASSERT_STREQ("1miliao", buff1);
  ASSERT_STREQ("2miliao3", buff2);

  ASSERT_EQ(6, iu.AddWriteReqSync(fd, 0, "miliao", 6));
  auto offset = GetFileSize(fn);

  close(fd);
  fd = open(fn, O_CREAT | O_RDWR | O_APPEND, 0644);
  ASSERT_EQ(6, iu.AddWriteReqSync(fd, 0, "append", 6));

  memset(buff, 0, sizeof(buff));
  ASSERT_EQ(6, iu.AddReadReqSync(fd, 0, buff, 6));
  ASSERT_STREQ(buff, "miliao");

  memset(buff, 0, sizeof(buff));
  ASSERT_EQ(6, iu.AddReadReqSync(fd, offset, buff, 6));
  ASSERT_STREQ(buff, "append");

  close(fd);
  unlink(fn);
  iu.Stop();
}

TEST(iouring, concurrent_read_write) {
  UringOptions opt;
  opt.instances = 4;

  IoUring iu(opt);
  ASSERT_EQ(0, iu.Start());

  const char* all_files[] = {
      "uring_test_file1",
      "uring_test_file2",
      "uring_test_file3",
      "uring_test_file4",
      "uring_test_file5",
  };

  auto filesz = 100 * 1024;
  auto filenum = sizeof(all_files) / sizeof(all_files[0]);

  for (auto i = 0u; i < filenum; i++) {
    int fd = open(all_files[i], O_CREAT | O_RDWR, 0644);
    ASSERT_GE(fd, 0);
    std::string content(filesz, '0' + i);
    write(fd, content.data(), content.size());
    close(fd);
  }

  auto worker = [&](int wid) {
    int times = 0;

    while (times < 10000) {
      times++;
      bool read = (rand() % 2 == 0);

      auto fi = rand() % filenum;
      auto offset = rand() % filesz;
      auto sz = rand() % (filesz - offset);
      std::string content(sz, '0' + fi);

      int fd = open(all_files[fi], O_CREAT | O_RDWR, 0644);

      if (read) {
        std::string buff;
        buff.resize(sz);
        ASSERT_EQ(sz, iu.AddReadReqSync(fd, offset, &buff[0], sz)) << "offset:" << offset;
        ASSERT_STREQ(content.data(), buff.data());
      } else {
        ASSERT_EQ(sz, iu.AddWriteReqSync(fd, offset, &content[0], sz));
      }

      close(fd);
    }
  };

  int num_threads = 280;
  std::vector<std::thread> all_thread;
  for (auto i = 0; i < num_threads; i++) {
    all_thread.emplace_back(worker, i);
  }

  for (auto i = 0; i < num_threads; i++) {
    all_thread[i].join();
  }

  for (auto i = 0u; i < filenum; i++) {
    unlink(all_files[i]);
  }
}
