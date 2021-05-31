#include "paxos_group.h"

#include <assert.h>
#include <string.h>
#include <algorithm>

#include "stl.hpp"

namespace quarrel {

template <int M, int N>
int GenGroup(int arr[N], int out[M][N]) {
  int idx = 0;
  do {
    assert(idx < M);
    memcpy(out[idx++], arr, sizeof(int) * N);
  } while (std::next_permutation(arr, arr + N));

  assert(idx == M);
  return idx;
}

template <int M, int N>
int GenGroupRervse(int in[M][N], int out[M][N]) {
  for (auto i = 0; i < M; i++) {
    for (auto j = 0; j < N; j++) {
      int p = -1;
      for (auto k = 0; k < N; k++) {
        if (in[i][k] == j) {
          p = k;
          break;
        }
      }
      out[i][j] = p;
    }
  }

  return 0;
}

PaxosGroup3::PaxosGroup3() {
  int arr[3] = {0, 1, 2};
  GenGroup<6, 3>(arr, mapping_);
  GenGroupRervse<6, 3>(mapping_, mapping_reverse_);
}

int PaxosGroup3::GetMemberIdBySvrId(uint64_t pinst, int id) {
  if (id < 0 || id >= 3) return -1;

  pinst = pinst % 6;
  return mapping_reverse_[pinst][id];
}

int PaxosGroup3::GetSvrIdByMemberId(uint64_t pinst, int id) {
  if (id < 0 || id >= 3) return -1;

  pinst = pinst % 6;
  return mapping_[pinst][id];
}

int PaxosGroup3::GetPaxosGroupMember(uint64_t pinst, int* out, int sz) {
  if (sz != 3) return -1;

  pinst = pinst % 6;
  memcpy(out, mapping_[pinst], 3 * sizeof(int));
  return 3;
}

PaxosGroup5::PaxosGroup5() {
  int arr[5] = {0, 1, 2, 3, 4};
  GenGroup<120, 5>(arr, mapping_);
  GenGroupRervse<120, 5>(mapping_, mapping_reverse_);
}

int PaxosGroup5::GetMemberIdBySvrId(uint64_t pinst, int id) {
  if (id < 0 || id >= 5) return -1;

  pinst = pinst % 120;
  return mapping_reverse_[pinst][id];
}

int PaxosGroup5::GetSvrIdByMemberId(uint64_t pinst, int id) {
  if (id < 0 || id >= 5) return -1;

  pinst = pinst % 120;
  return mapping_[pinst][id];
}

int PaxosGroup5::GetPaxosGroupMember(uint64_t pinst, int* out, int sz) {
  if (sz != 5) return -1;

  pinst = pinst % 120;
  memcpy(out, mapping_[pinst], 5 * sizeof(int));
  return 5;
}

std::unique_ptr<PaxosGroupBase> PaxosGroupBase::CreateGroup(int type) {
  if (type == 3) {
    return make_unique<PaxosGroup3>();
  } else if (type == 5) {
    return make_unique<PaxosGroup5>();
  } else {
    assert(0);
  }

  return 0;
}

}  // namespace quarrel
