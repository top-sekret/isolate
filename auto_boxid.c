#include "isolate.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/file.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

enum lock_status {LOCK_NONE, LOCK_FREE, LOCK_LOCKED, LOCK_ZOMBIE};

struct lock
{
    enum lock_status status;
    pid_t owner_pid;
    time_t created;
};

void auto_boxid_path(char* sem_path)
{
  sprintf(sem_path, "%s/autoboxid_sem", cf_box_root);
}

struct lock *auto_boxid_read_locks(int semfd)
{
  struct lock *locks = calloc(cf_num_boxes, sizeof(struct lock));
  pread(semfd, locks, cf_num_boxes*sizeof(struct lock), 0);
  for (int boxid = 0; boxid < cf_num_boxes; boxid++)
  {
    if (box_exists(boxid) && locks[boxid].status != LOCK_LOCKED)
      locks[boxid].status = LOCK_ZOMBIE;
    else if (locks[boxid].status == LOCK_NONE)
      locks[boxid].status = LOCK_FREE;
    else if (locks[boxid].status == LOCK_ZOMBIE && !box_exists(boxid))
      locks[boxid].status = LOCK_FREE;
  }
  return locks;
}

int auto_boxid_get(void)
{
  char sem_path[1024];
  int tries = 0;
  retry:
  tries++;
  auto_boxid_path(sem_path);
  int sem_fd = open(sem_path, O_RDWR|O_CREAT, 0600);
  if (flock(sem_fd, LOCK_EX))
    die("flock: %m");

  struct lock *locks = auto_boxid_read_locks(sem_fd);
  int result = -1;

  /* make orphans LOCK_ZOMBIE */
  for (int boxid = 0; boxid < cf_num_boxes; boxid++)
  {
    if (locks[boxid].status == LOCK_LOCKED && locks[boxid].owner_pid != 0 &&
        kill(locks[boxid].owner_pid, 0) != 0)
    {
      locks[boxid].status = LOCK_ZOMBIE;
      if (verbose > 1) msg("[auto_boxid] orphaned box %d marked as LOCK_ZOMBIE\n", boxid);
    }
  }

  /* delete zombies */
  for (int boxid = 0; boxid < cf_num_boxes; boxid++)
  {
    if (locks[boxid].status == LOCK_ZOMBIE)
    {
      box_delete(boxid);
      locks[boxid].status = LOCK_FREE;
      if (verbose > 1) msg("[auto_boxid] zombie box %d deleted\n", boxid);
    }
  }

  /* find LOCK_FREE */
  for (int boxid = 0; boxid < cf_num_boxes && result < 0; boxid++)
  {
    if (locks[boxid].status == LOCK_FREE) {
      locks[boxid].status = LOCK_LOCKED;
      locks[boxid].owner_pid = getpid();
      locks[boxid].created = time(0);
      result = boxid;
      if (verbose > 1) msg("[auto_boxid] returning %d\n", boxid);
    }
  }

  if (pwrite(sem_fd, locks, cf_num_boxes*sizeof(struct lock), 0) != cf_num_boxes*sizeof(struct lock))
    die("writing locks: %m");

  if (flock(sem_fd, LOCK_UN))
    die("flock: %m");

  free(locks);

  if (result < 0)
  {
    msg("[auto_boxid] failed to auto-assign box_id. retry in 1 second.\n");
    sleep(1);
    goto retry;
  }
  meta_printf("auto_boxid_tries:%d\n", tries);
  meta_printf("auto_boxid:%d\n", result);

  return result;
}

void auto_boxid_release(int box_id)
{
  char sem_path[1024];
  auto_boxid_path(sem_path);
  int sem_fd = open(sem_path, O_RDWR|O_CREAT, 0600);
  if (flock(sem_fd, LOCK_EX))
    die("flock: %m");

  struct lock *locks = auto_boxid_read_locks(sem_fd);
  locks[box_id].status = LOCK_FREE;

  if (pwrite(sem_fd, locks, cf_num_boxes*sizeof(struct lock), 0) != cf_num_boxes*sizeof(struct lock))
    die("writing locks: %m");

  if (flock(sem_fd, LOCK_UN))
    die("flock: %m");

  free(locks);
}