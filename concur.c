#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h> 
#include <assert.h>
#include <semaphore.h>
#include <sched.h>
#include <errno.h>

static const char *sem_name(const pid_t parent) {
  static char name[NAME_MAX] = {0};
  if (!*name) { 
    const int ret = snprintf(name, sizeof name, "/parallel.%ld.job.%ld", (long)getuid(), (long)parent);
    assert(ret < (int)sizeof name);
  }
  return name;
}

// This funciton gets called exactly once, by the person who created the semaphore
static void prepare_cleanup(const pid_t my_parent) {
  const pid_t cleaner = fork();
  assert(-1 != cleaner);
  if (cleaner) return;

  // What I'd like to do is just: waitid(P_PID, my_parent, NULL, WEXITED|WNOWAIT);
  // But that's not actually legal...
  
  // This doesn't work either, because we can't use ppid as it's not pp we're looking at:
  // while (getppid() == my_parent) sched_yield();

  // This works, but is less than ideal, because there's a race and it doesn't just block until we have something to do
  // The race isn't too bad though, because worst cast it just delays cleaning up a semaphore that nobody cares about
  signal(SIGTERM, SIG_IGN); // Don't get caught up in any signals meant for our parent
  signal(SIGINT, SIG_IGN);
  while (0 == kill(my_parent, 0)) sched_yield();

  // Parent shouldn't exist anymore
  sem_unlink(sem_name(my_parent));
  exit(0);
}

// Either create a new semaphore or open the existing one.
static sem_t *attach_semaphore(const unsigned int ncpus, const pid_t parent) {
  const char *name = sem_name(parent);
  sem_t *result = sem_open(name, O_CREAT|O_EXCL|O_RDWR, 0600, ncpus); // Needs O_EXCL to detect if we're the first user.
  if (!result && EEXIST == errno) {
    // Someone else got here first, just run with what they did. Doing it this way around is always race free for our usage.
    result = sem_open(name, O_RDWR);
    assert(result);
  }
  else {
    // We're the first execution with this parent, so we need to start watching for the cleanup stuff
    prepare_cleanup(parent);
  }
  return result;
}

static unsigned int count_cpus() {
  return sysconf(_SC_NPROCESSORS_ONLN);
}

int main(int argc, char **argv) {
  // TODO: Validate at least argc

  // We key everything off of our parent's pid. Other modes of operation might be possible, e.g. global, command itself
  const pid_t my_parent = getppid();

  // Find the global semaphore by name, initialise it with cpu count if it's new
  sem_t * const sem = attach_semaphore(count_cpus(), my_parent);
  assert(sem);

  // Wait until we can take the semaphore if we're already busy enough
  const int ret = sem_wait(sem);
  assert(!ret);

  // Detach ourselves from the process that find spawned
  const pid_t child = fork();
  assert (-1 != child);

  if (child) {
    // we're the parent, just exit and let find continue
    return 0;
  }

  const pid_t worker = vfork();
  if (!worker) {
    // we're the worker, just exec
    return execvp(argv[1], &argv[1]);
  }

  waitpid(worker, NULL, 0); // wait for exactly the worker, don't care if it succeded or not
  sem_post(sem);

}
