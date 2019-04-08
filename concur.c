#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/limits.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h> 
#include <assert.h>
#include <semaphore.h>

static const char *sem_name(const pid_t parent) {
  static char name[NAME_MAX] = {0};
  if (!*name) { 
    const int ret = snprintf(name, sizeof name, "/parallel.job.%ld", (long)parent);
    assert(ret < (int)sizeof name);
  }
  return name;
}

static sem_t *attach_semaphore(const unsigned int ncpus, const pid_t parent) {
  return sem_open(sem_name(parent), O_CREAT|O_RDWR, 0600, ncpus);
}

static unsigned int count_cpus() {
  return sysconf(_SC_NPROCESSORS_ONLN);
}

int main(int argc, char **argv) {
  // Validate at least argc

  // We key everything off of our parent's pid. Other modes of operation might be possible, e.g. global, command itself
  const pid_t my_parent = getppid();

  // Find the global semaphore by name, initialise it with cpu count if it's new
  sem_t * const sem = attach_semaphore(count_cpus(), my_parent);
  assert(sem);
  int original_sem_value;
  sem_getvalue(sem, &original_sem_value);

  // Wait until we can take the semaphore if we're already busy enough
  int ret = sem_wait(sem);
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

  // WNOWAIT keeps the parent waitable for someone else who could legitimately be waiting also.
  if (waitpid(my_parent, NULL, WNOWAIT|WNOHANG) < 0) {
    // Parent (probably) doesn't exist anymore, let's see if the semaphore is back to 'full' now
    // In theory pids could have wrapped around and we'd end up leaking a semaphore...
    int current_sem_value;
    sem_getvalue(sem, &current_sem_value);
    // We merely check if sem_getvalue() is now larger than it was when we started because:
    // a) The number of processors online could have changed whilst we were running, so checking it exactly matches isn't right
    // b) sem_unlink is safe whilst others still have the semaphore open
    // c) if our parent has exited it can't spawn us any others
    if (current_sem_value >= original_sem_value) {
      sem_unlink(sem_name(my_parent)); // Not much point error checking this because we could get scenarios where someone else already called unlink
    }
  }
}
