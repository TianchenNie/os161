/* pid array containing all pids, as well as a lock to protect it */
static pid_t pid_list[MAX_THREADS];
/* methods to manipulate pid array
 * get_smallest_available_pid() returns smallest available pid
 * recycle_pid(pid) recycles the pid passed in, so that it can be reused by new processes
 * initialize_pid_list() initializes the pid list
 */
pid_t get_smallest_available_pid() {
	int i;
	for (i = 0; i < MAX_THREADS; i++) {
		if (pid_list[i] > 0) {
			pid_t pid = pid_list[i];
			pid_list[i] = -1;
			return pid;
		}
	}
	kprintf("No PID available, shouldn't happen, debug please!");
	// if no pid available, return -1
	return -1;
}

void recycle_pid(pid_t pid) {
	if (pid_list[pid-1] != -1) {
		kprintf("Unknown behaviour in pid list, pid_list[%d] = %d instead of -1", pid-1, pid_list[pid-1]);
	}
	if (pid == -1){
		kprintf("Cannot recycle pid -1");
		return;
	}
	pid_list[pid-1] = pid;
}

void initialize_pid_list() {
	pid_t i;
	for(i = 1; i <= MAX_THREADS; i++) {
		pid_list[i-1] = i;
	}
}

void print_pid_list() {
	int i;
	for(i = 0; i < MAX_THREADS; i++) {
		kprintf("pid_list[%d] = %d\n", i, pid_list[i]);
	}
}

/* sys wait pid code with debug messages printed */
pid_t sys_waitpid(pid_t pid, int *status, int options, int *err) {
    // kprintf("NUM THREADS %d", thread_count());
    int spl = splhigh();
    int print_debug = 0;

    if(print_debug) print_sleepers();
    if(print_debug) kprintf("Thread %d scheduled to wait for child %d\n", curthread->pid, pid);
    // if(print_debug) kprintf("NDEXT POSSIBLE ID: %d", nextpid);
    // if(print_debug) kprintf("pid array: \n");
    // print_pid_list();
    int i;
    int *kernel_src = kmalloc(sizeof (int));
    int copy_err = 0;
    struct thread *child = get_thread_from_array(pid);
    if (child != NULL && child->ppid != curthread->pid) {
        if(print_debug) kprintf("Thread %d was not the parent of child %d!!\n", curthread->pid, pid);
        splx(spl);
        *err = EINVAL;
        return -1;
    }
    if (options != 0) {
        splx(spl);
        *err = EINVAL;
        return -1;
    }

    /* check exit codes to see if child exitted already, if so, copy code and return pid */
    for (i = 0; i < array_getnum(curthread->child_exit_codes); i++) {
        struct exitted_thread *e = array_getguy(curthread->child_exit_codes, i);
        if (e->pid == pid) {
            *kernel_src = e->exitcode;
            copy_err = copyout(kernel_src, (userptr_t) status, sizeof (int));
            if (copy_err) {
                *err = EFAULT;
                if(print_debug) kprintf("Before sleep, copy error while thread %d was retrieving exit code of %d", curthread->pid, pid);
                splx(spl);
                return -1;
            }
            if(print_debug) kprintf("Before sleep, thread %d successfully got child %d exit code", curthread->pid, pid);
            splx(spl);
            return pid;
        } 
    }
    
    /* if we could not find child exit code and could not find child pid in thread array,
    then it means we are waiting for a non-existant thread, return an error */
    if (child == NULL) {
        *err = EINVAL;
        splx(spl)
        return -1;
    }
    if(print_debug) kprintf("About to sleep, thread %d is about to sleep on child %d", curthread->pid, pid);
    thread_sleep(child);
    if(print_debug) kprintf("Woke Up, thread %d was woken up by child %d", curthread->pid, pid);

    for (i = 0; i < array_getnum(curthread->child_exit_codes); i++) {
        struct exitted_thread *e = array_getguy(curthread->child_exit_codes, i);
        if (e->pid == pid) {
            *kernel_src = e->exitcode;
            copy_err = copyout(kernel_src, (userptr_t) status, sizeof (int));
            if (copy_err) {
                *err = EFAULT;
                if(print_debug) kprintf("After sleep, copy error while thread %d was retrieving exit code of %d", curthread->pid, pid);
                splx(spl);
                return -1;
            }
            splx(spl);
            if(print_debug) kprintf("After sleep, thread %d successfully got child %d exit code", curthread->pid, pid);
            return pid;
        } 
    }
    // kprintf("child pid: %d", child->pid);
    // kprintf("child parent pid: %d", child->ppid);
    // kprintf("NUM EXIT CODES IN THREAD %d is %d", curthread->pid, array_getnum(curthread->child_exit_codes));
    // for (i = 0; i < array_getnum(curthread->child_exit_codes); i++) {
    //     struct exitted_thread *e = array_getguy(curthread->child_exit_codes, i);
    //     i
    // }

    panic("Should not get here in sys__waitpid, debug please!!");
    return 0;
}

/* working version of sys_fork that had debug codes commented out */
pid_t 
sys_fork(struct trapframe *tf, int32_t *retval) {
    // kprintf("IN INTERRUPT: %d", in_interrupt);
    // struct lock *f_lock = fork_lock;
    // lock_acquire(fork_lock);
    assert(tf != NULL);
    /* The trap frame is supposed to be 37 registers long. */
	assert(sizeof *tf == (37*4));
    // struct array *a = array_create();
    // array_preallocate(a, 1);
    // array_add(a, fork_lock);
    // array_destroy(a);
    // a = NULL;
    // kprintf("LOCK EQUALS? %d", f_lock == fork_lock);

    if(thread_count() > MAX_THREADS) {
        // lock_release(fork_lock);
        return EAGAIN;
    }

    struct trapframe *tf_child;
    struct addrspace *addr_child;
    struct thread *thread_child;
    // (void) retval;
     


    /* make a copy of the parent trapframe to be used by the child */
    tf_child = kmalloc(sizeof (struct trapframe));
    if(tf_child == NULL) {
        // lock_release(fork_lock);
        return ENOMEM;
    }
    memmove(tf_child, tf, sizeof (struct trapframe));
    // kprintf("PC EQUAL: %d", tf_child->tf_epc == tf->tf_epc);
    // kprintf("TRAP FRAME PARENT PC: %d\n", tf->tf_epc);


    /* make a copy of the parent address space to be used by the child */
    addr_child = kmalloc(sizeof (struct addrspace));
    if (addr_child == NULL){
        kfree(tf_child);
        // lock_release(fork_lock);
        return ENOMEM;
    }
    as_copy(curthread->t_vmspace, &addr_child);
    if (addr_child == NULL) {
        kfree(tf_child);
        // lock_release(fork_lock);
        return ENOMEM;
    }

    // kprintf("CUR THREAD VM SPACE: %p", curthread->t_vmspace);
    // kprintf("COPIED CHILD ADDRESS SPACE: %p", addr_child);
    

    thread_fork("User Thread Fork", (void *) tf_child, (unsigned long) addr_child, md_forkentry, &thread_child);
    if (thread_child == NULL) {
        kfree(tf_child);
        kfree(addr_child);
        return ENOMEM;
    }
    // kprintf("CHILD PID: %d", thread_child->pid);
    *retval = thread_child->pid;
    // lock_release(fork_lock);
    return 0;
}
