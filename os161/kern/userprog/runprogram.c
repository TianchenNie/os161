/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <kern/callno.h>
#include <lib.h>
#include <addrspace.h>
#include <array.h>
#include <thread.h>
#include <curthread.h>
#include <syscall.h>
#include <vm.h>
#include <vfs.h>
#include <test.h>
#include <syscall.h>

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */


int
runprogram_without_args(char *progname)
{
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, &v);
	if (result) {
		return result;
	}

	/* We should be a new thread. */
	assert(curthread->t_vmspace == NULL);

	/* Create a new address space. */
	curthread->t_vmspace = as_create();
	if (curthread->t_vmspace==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Activate it. */
	as_activate(curthread->t_vmspace);

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* thread_exit destroys curthread->t_vmspace */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(curthread->t_vmspace, &stackptr);
	if (result) {
		/* thread_exit destroys curthread->t_vmspace */
		return result;
	}

	/* Warp to user mode. */
	md_usermode(0 /*argc*/, NULL /*userspace addr of argv*/,
		    stackptr, entrypoint);
	
	/* md_usermode does not return */
	panic("md_usermode returned\n");
	return EINVAL;
}

int
runprogram_with_args(char *program, char ** args, int nargs, int *err)
{
	// kprintf("IN RUN PROGRAM WITH ARGS");
	char * program_copy = kmalloc(strlen(program) + 1);
	memcpy(program_copy, program, strlen(program) + 1);
    int result = 0;
    // struct vnode *v;
    int i;
    int k;
    int argc = 0;
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
    int prog_len = strlen(program) + 1;
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
    char *prog_copy = program + prog_len;
    for (k = 0; k < padding; k++) {
        *prog_copy++ = '\0';
    }
    array_add(args_kernel, program);


    struct array *arglens = array_create();
    result = array_preallocate(arglens, 1);
    if (result) {
        *err = ENOMEM;
        return -1;
    }
    array_add(arglens, prog_total_len_ptr);

	i = 0;
    while (i < nargs) {
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
        memcpy(arg, args[i], arg_len);
        char * arg_copy = arg + arg_len;
        // kprintf("Got arg %s\n", arg);
        for (k = 0; k < padding; k++) {
            *arg_copy++ = '\0';
            // kprintf("PRINTING IN PREVIOUS LOOP: \n");
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
	result = vfs_open(program_copy, O_RDONLY, &v);
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


    /* copy args onto stack */
    for (i = array_getnum(args_kernel) - 1; i >= 0; i--) {
        int arg_len = * (int *)array_getguy(arglens, i);
        stackptr -= arg_len;
        char * arg = (char *) array_getguy(args_kernel, i);
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

	panic("Should not get here in run program with args");
	return EINVAL;
}

int 
runprogram(char *progname, char ** args, int nargs) {
	int result;
	as_destroy(curthread->t_vmspace);
	if (args == NULL) {
		result = runprogram_without_args(progname);
		return result;
	}
	int err = 0;
	result = runprogram_with_args(progname, args, nargs, &err);
	return result;
}
