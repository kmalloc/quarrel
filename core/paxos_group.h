#ifndef __QUARREL_PAXOS_GROUP_H_
#define __QUARREL_PAXOS_GROUP_H_

#include <memory>
#include <stdint.h>

namespace quarrel {

// mapping from svr id to paxos group member id and vice versus
class PaxosGroupBase {
 public:
  static std::unique_ptr<PaxosGroupBase> CreateGroup(int type);

  virtual int GetMemberIdBySvrId(uint64_t pinst, int id) = 0;
  virtual int GetSvrIdByMemberId(uint64_t pinst, int id) = 0;
  virtual int GetPaxosGroupMember(uint64_t pinst, int* out, int sz) = 0;
};

// 3-members quorum
class PaxosGroup3 : public PaxosGroupBase {
 public:
  PaxosGroup3();
  virtual int GetMemberIdBySvrId(uint64_t pinst, int id);
  virtual int GetSvrIdByMemberId(uint64_t pinst, int id);
  virtual int GetPaxosGroupMember(uint64_t pinst, int* out, int sz);

 private:
  // 3*2
  int mapping_[6][3];          // 6 paxos groups, member id to server id mapping.
  int mapping_reverse_[6][3];  // server id to member id mapping.
};

// 5-members quorum
class PaxosGroup5 : public PaxosGroupBase {
 public:
  PaxosGroup5();
  virtual int GetMemberIdBySvrId(uint64_t pinst, int id);
  virtual int GetSvrIdByMemberId(uint64_t pinst, int id);
  virtual int GetPaxosGroupMember(uint64_t pinst, int* out, int sz);

 private:
  // 5*4*3*2*1
  int mapping_[120][5];  // 120 paxos groups, member id to server id mapping.
  int mapping_reverse_[120][5];
};

// 3-members quorum: kv64 routing used by wechat
class PaxosGroup64 : public PaxosGroupBase {
};

}  // namespace quarrel

#endif
