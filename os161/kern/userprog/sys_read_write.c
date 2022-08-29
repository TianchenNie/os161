#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <machine/pcb.h>
#include <machine/spl.h>
#include <machine/trapframe.h>
#include <kern/callno.h>
#include <syscall.h>

/* Constants for read/write/etc: special file handles */
#define STDIN_FILENO  0      /* Standard input */
#define STDOUT_FILENO 1      /* Standard output */
#define STDERR_FILENO 2      /* Standard error */

int
sys_write(int fd, const void *buf, size_t nbytes, int32_t *retval){
	if(fd != STDOUT_FILENO && fd != STDERR_FILENO){
		return EBADF;
	}
	char *kernel_dest = kmalloc(nbytes);
	int err = copyin(buf, kernel_dest, nbytes);
	if(err != 0){
		return err;
	}
	unsigned int i = 0;
	unsigned int r = 0;
	for(i = 0; i < nbytes; i++){
		putch(kernel_dest[i]);
		r++;
	}
	*retval = r;
	kfree(kernel_dest);
	return 0;
}

int
sys_read(int fd, void *buf, size_t buflen){
	if(fd != STDIN_FILENO){
		return EBADF;
	}
	if(buflen != 1){
		// kprintf("RETURNING WITH BUFLEN NOT 1");
		return EUNIMP;
	}
	char *kernel_src = kmalloc(buflen);
	// unsigned i;
	// kprintf("KERNEL SRC: %c", *kernel_src);

 	kernel_src[0] = getch();
	// kprintf("KERNEL SRC: %c", kernel_src[0]);
	int err = copyout(kernel_src, buf, buflen);
    kfree(kernel_src);
	return err;
}


