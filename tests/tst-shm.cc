/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <string.h>
#include <iostream>
#include <cstdlib>
#include <errno.h>

int main(int argc, char **argv)
{
  int ret = 0;
  std::cerr << "Running shm tests" << std::endl;
  key_t shmkey = 1;
  int shmid = shmget(shmkey, 1024, IPC_CREAT | 0666);
  if (shmid < 0) {
    std::cerr << "Cannot create shm segment for key " << shmkey << ": " << strerror(errno) << std::endl;
    return 1;
  }
  char *addr1 = (char*)shmat(shmid, NULL, 0);
  if (addr1 == MAP_FAILED) {
    std::cerr << "Cannot attach to shm id " << shmid << ": " << strerror(errno) << std::endl;
    return 1;
  }
  std::cerr << "Attached addr1: " << static_cast<const void*>(addr1) << std::endl;
  char *addr2 = (char*)shmat(shmid, NULL, 0);
  if (addr2 == MAP_FAILED) {
    std::cerr << "Cannot attach to shm id " << shmid << " second time: " << strerror(errno) << std::endl;
    return 1;
  }
  std::cerr << "Attached addr2: " << static_cast<const void*>(addr2) << std::endl;
  strcpy(addr1, "shmem test");
  if (strcmp(addr1, addr2) != 0) {
    std::cerr << "shm test failed" << std::endl;
    ret = 1;
  } else {
    std::cerr << "shm test succeeded" << std::endl;
  }
  shmdt(addr1);
  shmdt(addr2);
  shmctl(shmid, IPC_RMID, NULL);

  ////////////////////////////////////////////////////////////////////////////////
  // shmctl()'s documentation explains that IPC_RMID only marks the shared
  // memory segment for destruction - it will only actually be destructed
  // when the last attachment of it is detached. Let's confirm that this works.
  shmkey = 2;
  shmid = shmget(shmkey, 1024, IPC_CREAT | 0666);
  if (shmid < 0) {
    std::cerr << "Cannot create shm segment for key " << shmkey << ": " << strerror(errno) << std::endl;
    return 1;
  }
  // Let's also check that calling shmget() multiple times works (it just
  // returns the same id again). IPC_CREAT is ignored (without IPC_EXCL).
  int shmid2 = shmget(shmkey, 1024, IPC_CREAT | 0666);
  if (shmid2 != shmid) {
      std::cerr << "Second shmget returned " << shmid2 << ", expected " << shmid << std::endl;
  }
  addr1 = (char*)shmat(shmid, NULL, 0);
  if (addr1 == MAP_FAILED) {
    std::cerr << "Cannot attach to shm id " << shmid << ": " << strerror(errno) << std::endl;
    return 1;
  }
  std::cerr << "Attached addr1: " << static_cast<const void*>(addr1) << std::endl;
  addr2 = (char*)shmat(shmid, NULL, 0);
  if (addr2 == MAP_FAILED) {
    std::cerr << "Cannot attach to shm id " << shmid << " second time: " << strerror(errno) << std::endl;
    return 1;
  }
  std::cerr << "Attached addr2: " << static_cast<const void*>(addr2) << std::endl;
  std::cerr << " shmdt " << addr1 << "\n";
  if (shmdt(addr1) < 0) {
      std::cerr << "Cannot detach shm segment at " << addr1 << ": " << strerror(errno) << std::endl;
      return 1;
  }
  std::cerr << " shmctl IPC_RMID " << shmid << "\n";
  if (shmctl(shmid, IPC_RMID, NULL) < 0) {
      std::cerr << "Cannot remove shm id " << shmid << ": " << strerror(errno) << std::endl;
      return 1;
  }
  // At this point, the segment is still not destroyed, until the shmdt()
  // call below.
  std::cerr << " shmdt " << addr2 << "\n";
  if (shmdt(addr2) < 0) {
      std::cerr << "Cannot detach shm segment at " << addr2 << ": " << strerror(errno) << std::endl;
      return 1;
  }
  std::cerr << "shm test 2 succeeded" << std::endl;

  return ret;
}
