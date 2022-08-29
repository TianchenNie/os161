#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <machine/pcb.h>
#include <machine/spl.h>
#include <machine/trapframe.h>
#include <kern/callno.h>
#include <syscall.h>
#include <clock.h> // for time syscall


unsigned int
sys_sleep(unsigned int seconds){
	clocksleep(seconds);
	return 0;
}

time_t
sys_time(time_t *seconds, unsigned long *nanoseconds){
	time_t curr_seconds;
	u_int32_t curr_nanoseconds;
	int error = 0;
	gettime(&curr_seconds, &curr_nanoseconds); 
	if(seconds != NULL){
		error = copyout(&curr_seconds, (userptr_t) seconds, sizeof(curr_seconds));
		if(error == EFAULT){
			return -1;
		}
	}
	if(nanoseconds != NULL){
		curr_nanoseconds = (unsigned long) curr_nanoseconds;
		error = copyout(&curr_nanoseconds, (userptr_t) nanoseconds, sizeof(curr_nanoseconds));
		if(error == EFAULT){
			return -1;
		}
	}
	return curr_seconds;
}
