#include "rtt_syscalls.h"

#include <errno.h>
#include <sys/stat.h>
#include <sys/times.h>

#include "SEGGER_RTT.h"

void RttSyscallsInit() {
  SEGGER_RTT_Init();
}

extern "C" {

char* __env[1] = {nullptr};
char** environ = __env;

void initialise_monitor_handles() {}

int _getpid() {
  return 1;
}

int _kill(int pid, int sig) {
  (void)pid;
  (void)sig;
  errno = EINVAL;
  return -1;
}

void _exit(int status) {
  _kill(status, -1);
  while (true) {
  }
}

int _read(int file, char* ptr, int len) {
  (void)file;
  if (len <= 0) {
    return 0;
  }
  unsigned read_bytes = 0;
  while (read_bytes == 0U) {
    read_bytes = SEGGER_RTT_Read(0U, ptr, static_cast<unsigned>(len));
  }
  return static_cast<int>(read_bytes);
}

int _write(int file, const char* ptr, int len) {
  (void)file;
  if (len <= 0) {
    return 0;
  }
  SEGGER_RTT_Write(0U, ptr, static_cast<unsigned>(len));
  return len;
}

int _close(int file) {
  (void)file;
  return -1;
}

int _fstat(int file, struct stat* st) {
  (void)file;
  st->st_mode = S_IFCHR;
  return 0;
}

int _isatty(int file) {
  (void)file;
  return 1;
}

int _lseek(int file, int ptr, int dir) {
  (void)file;
  (void)ptr;
  (void)dir;
  return 0;
}

int _open(char* path, int flags, ...) {
  (void)path;
  (void)flags;
  return -1;
}

int _wait(int* status) {
  (void)status;
  errno = ECHILD;
  return -1;
}

int _unlink(char* name) {
  (void)name;
  errno = ENOENT;
  return -1;
}

clock_t _times(struct tms* buf) {
  (void)buf;
  return static_cast<clock_t>(-1);
}

int _stat(const char* file, struct stat* st) {
  (void)file;
  st->st_mode = S_IFCHR;
  return 0;
}

int _link(char* old_path, char* new_path) {
  (void)old_path;
  (void)new_path;
  errno = EMLINK;
  return -1;
}

int _fork() {
  errno = EAGAIN;
  return -1;
}

int _execve(char* name, char** argv, char** env) {
  (void)name;
  (void)argv;
  (void)env;
  errno = ENOMEM;
  return -1;
}

}  // extern "C"
