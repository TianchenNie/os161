#ifndef _SYSCALL_H_
#define _SYSCALL_H_

/*
 * Prototypes for IN-KERNEL entry points for system call implementations.
 */

int sys_reboot(int code);
int sys_write(int fd, const void *buf, size_t nbytes, int32_t *retval);
int sys_read(int fd, void *buf, size_t buflen);
unsigned int sys_sleep(unsigned int seconds);
time_t sys_time(time_t *seconds, unsigned long *nanoseconds);
pid_t sys_fork(struct trapframe *tf, int *err);
pid_t sys_getpid();
pid_t sys_waitpid(pid_t pid, int *status, int options, int *err);
void sys__exit(int exitcode);
int sys_execv(const char *program, char ** args, int *err);
int next_multiple_of_4(int num);

#endif /* _SYSCALL_H_ */
