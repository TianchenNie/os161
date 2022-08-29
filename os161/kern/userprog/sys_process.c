#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <lib.h>
#include <machine/pcb.h>
#include <machine/spl.h>
#include <machine/trapframe.h>
#include <kern/callno.h>
#include <syscall.h>
#include <curthread.h>
#include <thread.h>
#include <synch.h>
#include <addrspace.h>
#include <array.h>
#include <vm.h>
#include <vfs.h>
#include <test.h>


int debug = 0;
/*
FORK
****Description****
fork duplicates the currently running process. The two copies are identical, except that one (the "new" one, or "child"), 
has a new, unique process id, and in the other (the "parent") the process id is unchanged.
The process id must be greater than 0.

The two processes do not share memory or open file tables; this state is copied into the new process, 
and subsequent modification in one process does not affect the other.

However, the file handle objects the file tables point to are shared, so, for instance, 
calls to lseek in one process can affect the other.

****Return Values****
On success, fork returns twice, once in the parent process and once in the child process. In the child process, 0 is returned. 
In the parent process, the process id of the new child process is returned.
On error, no new process is created, fork only returns once, returning -1, and errno is set according to the error encountered.

****Errors****
The following error codes should be returned under the conditions given. Other error codes may be returned for other errors not mentioned here.
 	 
EAGAIN	Too many processes already exist.
ENOMEM	Sufficient virtual memory for the new process was not available.
*/

/* function that the child calls, should be passed into thread fork */
void
md_forkentry(void *tf, unsigned long addrspace)
{
	/*
	 * This function is provided as a reminder. You need to write
	 * both it and the code that calls it.
	 *
	 * Thus, you can trash it and do things another way if you prefer.
	 */

    // kprintf("IN MD FORK ENTRY");
	
    struct trapframe *tf_child = (struct trapframe *)tf;
	struct addrspace *addr_child = (struct addrspace *) addrspace;

    // struct addrspace child_addrspace = *child_addrspace_ptr;

    // kprintf("child address space pointer: %p", child_addrspace_ptr);
    // struct addrspace *addr_child2 = kmalloc(sizeof (struct addrspace));
    // *addr_child2 = *addr_child;
    curthread->t_vmspace = addr_child;
    as_activate(curthread->t_vmspace);
    
    /* It's necessary for the trap frame used here to be on the
	 * current thread's own stack. It cannot correctly be on either
	 * another thread's stack or in the kernel heap. (Why?)
     * Create tf that exists on threads own stack;
	 */
    struct trapframe tf_child_stack;

    // kprintf("BEFORE MEM CPY");
    tf_child_stack = *tf_child;
    // kprintf("TRAP FRAME CHILD STACK PC: %d\n", tf_child_stack.tf_epc);

    /* set v0 (return 0), set a3 (signal no error), set epc (increment pc by 4) */
    tf_child_stack.tf_v0 = 0;
    tf_child_stack.tf_a3 = 0;
    tf_child_stack.tf_epc += 4;

    // kprintf("TRAP FRAME CHILD STACK PC AFTER INCREMENT: %d\n", tf_child_stack.tf_epc);

    mips_usermode(&tf_child_stack);

    kprintf("CHILD THREAD WITH PID: %d FINISHED EXECUTING MIPS USER MODE AND RETURNED TO MD FORK ENTRY!!", curthread->pid);
    assert(0);
}


pid_t 
sys_fork(struct trapframe *tf, int *err) {
    assert(tf != NULL);
    /* The trap frame is supposed to be 37 registers long. */
	assert(sizeof *tf == (37*4));

    if(thread_count() > MAX_THREADS) {
        // lock_release(fork_lock);
        *err = EAGAIN;
        return -1;
    }

    struct trapframe *tf_child;
    struct addrspace *addr_child;
    struct thread *thread_child;
     


    /* make a copy of the parent trapframe to be used by the child */
    tf_child = kmalloc(sizeof (struct trapframe));
    if(tf_child == NULL) {
        *err = ENOMEM;
        return -1;
    }
    memmove(tf_child, tf, sizeof (struct trapframe));

    /* make a copy of the parent address space to be used by the child */
    addr_child = kmalloc(sizeof (struct addrspace));
    if (addr_child == NULL){
        kfree(tf_child);
        *err = ENOMEM;
        return -1;
    }
    as_copy(curthread->t_vmspace, &addr_child);
    if (addr_child == NULL) {
        kfree(tf_child);
        *err = ENOMEM;
        return -1;
    }
    
    /* pass tf, addrspace, md_forkentry into thread_fork which creates a thread and calls md_forkentry
        with tf as first argument, addrspace as second argument */
    thread_fork("User Thread Fork", (void *) tf_child, (unsigned long) addr_child, md_forkentry, &thread_child);
    if (thread_child == NULL) {
        kfree(tf_child);
        kfree(addr_child);
        *err = ENOMEM;
        return -1;
    }
    return thread_child->pid;
}

pid_t
sys_getpid() {
    return curthread->pid;
}


pid_t
sys_waitpid(pid_t pid, int *status, int options, int *err) {
    int spl = splhigh();
    int i;
    int *kernel_src = kmalloc(sizeof (int));
    int copy_err = 0;
    struct thread *child = get_thread_from_array(pid);
    if (pid <= 0) {
        *err = EINVAL;
        splx(spl);
        return -1;
    }
    if (child != NULL && child->ppid != curthread->pid) {
        *err = EINVAL;
        splx(spl);
        return -1;
    }
    if (options != 0) {
        *err = EINVAL;
        splx(spl);
        return -1;
    }

    /* check exit codes to see if child exitted already, if so, copy code, remove code to avoid waiting for the same child again 
    (case 3 of wait.c) and return pid */
    for (i = 0; i < array_getnum(curthread->child_exit_codes); i++) {
        struct exitted_thread *e = array_getguy(curthread->child_exit_codes, i);
        if (e->pid == pid) {
            *kernel_src = e->exitcode;
            /* remove this exit code */
            array_remove(curthread->child_exit_codes, i);
            copy_err = copyout(kernel_src, (userptr_t) status, sizeof (int));
            if (copy_err) {
                *err = copy_err;
                splx(spl);
                return -1;
            }
            splx(spl);
            return pid;
        } 
    }
    
    /* if we could not find child exit code and could not find child pid in thread array,
    then it means we are waiting for a non-existant thread (in wait test, this is case 3
    where the parent is waiting for the child twice), return an error */
    if (child == NULL) {
        *err = EINVAL;
        splx(spl);
        return -1;
    }

    thread_sleep(child);

    for (i = 0; i < array_getnum(curthread->child_exit_codes); i++) {
        struct exitted_thread *e = array_getguy(curthread->child_exit_codes, i);
        if (e->pid == pid) {
            *kernel_src = e->exitcode;
            copy_err = copyout(kernel_src, (userptr_t) status, sizeof (int));
            /* remove this exit code */
            array_remove(curthread->child_exit_codes, i);
            if (copy_err) {
                *err = copy_err;
                splx(spl);
                return -1;
            }
            splx(spl);
            return pid;
        } 
    }
    panic("Should not get here in sys__waitpid, debug please!!");
    return 0;
}



void
sys__exit(int exitcode) {
    thread_exit_with_code(exitcode);
}


void printProgram(const char *program) {
    if (debug) {
        kprintf("Program name in exec: %s\n", program);
    }
}

void printArgs(char ** args) {
    int i = 0;
    if (debug) {
        while (args[i] != NULL) {
            kprintf("Arg %d is %s\n", i, args[i]);
            i++;
        }
    }
}

void printKernelArgsArray(struct array * args) {
    int i = 0;
    if (debug) {
        while (i < array_getnum(args)) {
            kprintf("Kernel Buffer Arg %d is %s\n", i, (char *)array_getguy(args, i));
            i++;
        }
    }
}

void printArgTotalLen(struct array * lens) {
    int i = 0;
    if (debug) {
        while (i < array_getnum(lens)) {
            kprintf("Kernel Buffer Arg %d has length %d\n", i, *(int *)array_getguy(lens, i));
            i++;
        }
    }
}


void printArgv(char ** argv, int len) {
    int i = 0;
    if (debug) {
        while (i < len) {
            kprintf("argv %d has value 0x%x\n", i, (int) argv[i]);
            i++;
        }
    }
}

void printPaddedArg(char * arg, int len) {
    int i = 0;
    if (debug) {
        kprintf("Print padded arg %s\n", arg);
        while (i < len) {
            if (arg[i] == 0) {
                kprintf("NULL");
                i++;
                continue;
            }
            kprintf("%c", arg[i]);
            i++;
        }
    }
}
int next_multiple_of_4(int num) {
    int counter = 0;
    while (1) {
        if (num <= counter) {
            return counter;
        }
        counter += 4;
    }
}

/* try to copy a character from program, if we fail, that means invalid pointer */
int try_copy_program(const char *program, int *err) {
    int result;
    char *copy = kmalloc(1);
    result = copyin((userptr_t) program, copy, 1);
    kfree(copy);
    if (result) {
        *err = result;
        return -1;
    }
    return 0;
}

/* try to copy args array */
int try_copy_args(char ** args, int *err) {
    int result;
    char **copy = kmalloc(sizeof (char *));
    result = copyin((userptr_t) args, copy, sizeof (char *));
    if (result) {
        *err = result;
        return -1;
    }
    return 0;
}

/* try to copy each arg in args array */
int try_copy_each_arg(char ** args, int *err) {
    int result;
    int i = 0;
    while(args[i] != NULL) {
        char * copy = kmalloc(1);
        result = copyin((userptr_t) args[i], copy, 1);
        kfree(copy);
        if (result) {
            *err = result;
            return -1;
        }
        i++;
    }
    return 0;
}

int
sys_execv(const char *program, char ** args, int *err) {
    // kprintf("IN SYS EXECV");
    int result = 0;
    // struct vnode *v;
    int i;
    int k;
    int argc = 0;
    printProgram(program);
    printArgs(args);
    // kprintf("Before comparison");
    if (program == NULL || args == NULL) {
        *err = EFAULT;
        return -1;
    }
    result = try_copy_program(program, err);
    if (result) {
        return -1;
    }
    if (strlen(program) == 0) {
        *err = EINVAL;
        return -1;
    }
    result = try_copy_args(args, err);
    if (result) {
        return -1;
    }

    result = try_copy_each_arg(args, err);
    if (result) {
        return -1;
    }
    // kprintf("After comparison");
    /* copy program name into kernel space, make 2 copies in case vfs_open destroys one */
    char * program_kernel = kmalloc(strlen(program) + 1);
    char * program_kernel_2 = kmalloc(strlen(program) + 1);
    result = copyin((userptr_t) program, program_kernel, strlen(program) + 1);
    // kprintf("Pointer is 0x%x", (int) program);
    // kprintf("Result is: %d", result);
    if (result) {
        *err = result;
        return -1;
    }
    result = copyin((userptr_t) program, program_kernel_2, strlen(program) + 1);
    if (result) {
        *err = result;
        return -1;
    }
    // kprintf("copied program name: %s", program_kernel);
    struct array *args_kernel = array_create();
    result = array_preallocate(args_kernel, 1);
    if (result) {
        *err = ENOMEM;
        return -1;
    }
    /* add the program name as the first argument */
    int prog_len = strlen(program_kernel_2) + 1;
    int padding = next_multiple_of_4(prog_len) - prog_len;
    int prog_total_len = prog_len + padding;
    int *prog_total_len_ptr = kmalloc(sizeof (int));
    if (prog_total_len_ptr == NULL) {
        *err = ENOMEM;
        return -1;
    }
    *prog_total_len_ptr = prog_total_len;
    assert(padding < 4);
    assert(prog_total_len % 4 == 0);
    char *prog_copy = program_kernel_2 + prog_len;
    for (k = 0; k < padding; k++) {
        *prog_copy++ = '\0';
    }
    array_add(args_kernel, program_kernel_2);


    struct array *arglens = array_create();
    result = array_preallocate(arglens, 1);
    if (result) {
        *err = ENOMEM;
        return -1;
    }
    array_add(arglens, prog_total_len_ptr);
    i = 0;
    while (args[i] != NULL) {
        /* don't load program name, loaded before */
        if (i == 0) {
            i++;
            continue;
        }
        if (args[i] == NULL || strlen(args[i]) == 0) {
            *err = EFAULT;
            return -1;
        }
        int arg_len = strlen(args[i]) + 1;
        int padding = next_multiple_of_4(arg_len) - arg_len;
        assert(padding < 4);

        char *arg = kmalloc(arg_len + padding);
        result = copyin((userptr_t) args[i], arg, arg_len);
        if (result) {
            *err = result;
            return -1;
        }
        char * arg_copy = arg + arg_len;
        // kprintf("Got arg %s\n", arg);
        for (k = 0; k < padding; k++) {
            *arg_copy++ = '\0';
            // kprintf("PRINTING IN PREVIOUS LOOP: \n");
            printPaddedArg(arg, arg_len + k + 1);
        }
        // kprintf("Arg length after padding: %d\n", arg_len + padding);
        // kprintf("Arg after padding: %s", arg);
        /* append padded string to array */
        int array_len = array_getnum(args_kernel);
        result = array_preallocate(args_kernel, array_len + 1);
        if (result) {
            *err = ENOMEM;
            return -1;
        }
        array_add(args_kernel, arg);

        /* keep in track of how long each argument was */
        result = array_preallocate(arglens, array_len + 1);
        if (result) {
            *err = ENOMEM;
            return -1;
        }

        /* push total length of this argument into arglens */
        int arg_total_len = arg_len + padding;
        assert(arg_total_len % 4 == 0);
        int *arg_total_len_ptr = kmalloc(sizeof (int));
        *arg_total_len_ptr = arg_total_len;
        array_add(arglens, arg_total_len_ptr);
        i++;
    }
    printKernelArgsArray(args_kernel);
    printArgTotalLen(arglens);
    argc = array_getnum(args_kernel);

    /* we should have n arguments and n lengths that represent length of each argument */
    assert(array_getnum(args_kernel) == array_getnum(arglens));
    

    /* Code from run program */

    /* destroy memory space of curthread */
    as_destroy(curthread->t_vmspace);
    curthread->t_vmspace = NULL;

    struct vnode *v;
	vaddr_t entrypoint, stackptr;

	/* Open the file. */
	result = vfs_open(program_kernel, O_RDONLY, &v);
	if (result) {
        *err = EINVAL;
		return -1;
	}

	/* We should be a new thread. */
	assert(curthread->t_vmspace == NULL);

	/* Create a new address space. */
	curthread->t_vmspace = as_create();
	if (curthread->t_vmspace==NULL) {
		vfs_close(v);
        *err = ENOMEM;
		return -1;
	}

	/* Activate it. */
	as_activate(curthread->t_vmspace);

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
        *err = EINVAL;
		/* thread_exit destroys curthread->t_vmspace */
		vfs_close(v);
		return -1;
	}

	/* Done with the file now. */
	vfs_close(v);
	/* Define the user stack in the address space */
	result = as_define_stack(curthread->t_vmspace, &stackptr);
	if (result) {
		/* thread_exit destroys curthread->t_vmspace */
        *err = result;
		return -1;
	}
    
    /* get the stack pointer addresses of args and store in argv */
    char * argv[argc + 1];
    
    argv[argc] = NULL;
    vaddr_t stackptr_copy = stackptr;
    for (i = array_getnum(args_kernel) - 1; i >= 0; i--) {
        // kprintf("Old value of stackptr: 0x%x\n", stackptr);
        int decrement = * (int *)array_getguy(arglens, i);
        stackptr_copy -= decrement;
        argv[i] = (char *) stackptr_copy;
        // kprintf("New value of stackptr: 0x%x\n", stackptr);
    };


    printArgv(argv, argc+1);

    /* copy args onto stack */
    for (i = array_getnum(args_kernel) - 1; i >= 0; i--) {
        int arg_len = * (int *)array_getguy(arglens, i);
        stackptr -= arg_len;
        char * arg = (char *) array_getguy(args_kernel, i);
        printPaddedArg(arg, arg_len);
        result = copyout(arg, (userptr_t) stackptr, arg_len);
        if (result) {
            panic("copy out error when copying args");
        }
    }
     
    /* copy arg addresses onto stack */
    for (i = argc; i >= 0; i--) {
        stackptr -= 4;
        char * address = argv[i];
        if (address == NULL) {
            result = copyout(&argv[i], (userptr_t) stackptr, 4);
            if (result) {
                panic("copy out error when copying arg addresses");
            }
            continue;
        }
        result = copyout(&address, (userptr_t) stackptr, 4);
        if (result) {
            panic("copy out error when copying arg addresses");
        }
    }

    // kprintf("final stack pointer: 0x%x\n", stackptr);
    // kprintf("final arg c: %d\n", argc);
    /* copy arguments and addresses to stack */
	/* Warp to user mode. argv is the same as stack ptr (from piazza) */
	md_usermode(argc /*argc*/, (userptr_t) stackptr /*userspace addr of argv*/,
		    stackptr, entrypoint);

	/* md_usermode does not return */
	panic("md_usermode returned\n");
	return EINVAL;

    // kprintf("Got number of args: %d", array_getnum(args_kernel));

    // /* Open the file. */
	// result = vfs_open(progname, O_RDONLY, &v);
	// if (result) {
	// 	return result;
	// }
    return -1;
}


