#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <machine/pcb.h>
#include <machine/spl.h>
#include <machine/trapframe.h>
#include <kern/callno.h>
#include <syscall.h>
#include <clock.h> // for time syscall


/*
 * System call handler.
 *
 * A pointer to the trapframe created during exception entry (in
 * exception.S) is passed in.
 *
 * The calling conventions for syscalls are as follows: Like ordinary
 * function calls, the first 4 32-bit arguments are passed in the 4
 * argument registers a0-a3. In addition, the system call number is
 * passed in the v0 register.
 *
 * On successful return, the return value is passed back in the v0
 * register, like an ordinary function call, and the a3 register is
 * also set to 0 to indicate success.
 *
 * On an error return, the error code is passed back in the v0
 * register, and the a3 register is set to 1 to indicate failure.
 * (Userlevel code takes care of storing the error code in errno and
 * returning the value -1 from the actual userlevel syscall function.
 * See src/lib/libc/syscalls.S and related files.)
 *
 * Upon syscall return the program counter stored in the trapframe
 * must be incremented by one instruction; otherwise the exception
 * return code will restart the "syscall" instruction and the system
 * call will repeat forever.
 *
 * Since none of the OS/161 system calls have more than 4 arguments,
 * there should be no need to fetch additional arguments from the
 * user-level stack.
 *
 * Watch out: if you make system calls that have 64-bit quantities as
 * arguments, they will get passed in pairs of registers, and not
 * necessarily in the way you expect. We recommend you don't do it.
 * (In fact, we recommend you don't use 64-bit quantities at all. See
 * arch/mips/include/types.h.)
 */

void
mips_syscall(struct trapframe *tf)
{
	// kprintf("FORK LOCK NAME: %s", fork_lock->name);
	// kprintf("FORK LOCK HOLDING THREAD: %p", fork_lock->holding_thread);
	// struct trapframe *tf_copy = kmalloc(sizeof *tf);
	// memcpy(tf_copy, tf, sizeof *tf);
	// kprintf("%d %d %d %d", tf->tf_v0 == tf_copy->tf_v0, tf->tf_a3 == tf_copy->tf_a3, tf->tf_a0 == tf_copy->tf_a0, tf->tf_epc == tf_copy->tf_epc);
	int callno;
	int32_t retval;
	int err = 0;
	assert(curspl==0);
	callno = tf->tf_v0;

	/*
	 * Initialize retval to 0. Many of the system calls don't
	 * really return a value, just 0 for success and -1 on
	 * error. Since retval is the value returned on success,
	 * initialize it to 0 by default; thus it's not necessary to
	 * deal with it except for calls that return other values, 
	 * like write.
	 */

	retval = 0;

	switch (callno) {
	    case SYS_reboot:
			err = sys_reboot(tf->tf_a0);
			break;

	    /* Add stuff here */      
		case SYS_write:
			err = sys_write(tf->tf_a0, (void *) tf->tf_a1, tf->tf_a2, &retval);
			// kprintf("ERR under sys write: %d\n", err);
			if(err != 0){
				retval = -1;
			}
			break;

		case SYS_read:
			err = sys_read(tf->tf_a0, (void *) tf->tf_a1, tf->tf_a2);
			if(err == 0){
				retval = 1;
			}
			else if(err != 0){
				retval = -1;
			}
			// kprintf("err under sys read: %d\n", err);
			break;

		case SYS___time:
			retval = sys_time((time_t *) tf->tf_a0, (unsigned long *) tf->tf_a1);
			if(retval == -1){
				err = EFAULT;
			}
			break;
		
		case SYS_sleep:
			err = sys_sleep(tf->tf_a0);
			break; 

		// TODO: Implement these
		case SYS_fork:
			retval = sys_fork(tf, &err);
			break;

		case SYS_getpid:
			retval = sys_getpid();
			break;

		case SYS_waitpid:
			retval = sys_waitpid((pid_t) tf->tf_a0, (int *) tf->tf_a1, (int) tf->tf_a2, &err);
			break;

		case SYS__exit:
			sys__exit(tf->tf_a0);
			break;

		case SYS_execv:
			retval = sys_execv((char *) tf->tf_a0, (char **) tf->tf_a1, &err);
			break;


	    default:
		kprintf("Unknown syscall %d\n", callno);
		err = ENOSYS;
		break;
	}


	if (err) {
		/*
		 * Return the error code. This gets converted at
		 * userlevel to a return value of -1 and the error
		 * code in errno.
		 */
		tf->tf_v0 = err;
		tf->tf_a3 = 1;      /* signal an error */
	}
	else {
		/* Success. */
		tf->tf_v0 = retval;
		tf->tf_a3 = 0;      /* signal no error */
	}
	
	/*
	 * Now, advance the program counter, to avoid restarting
	 * the syscall over and over again.
	 */
	
	tf->tf_epc += 4;

	/* Make sure the syscall code didn't forget to lower spl */
	assert(curspl==0);
}

