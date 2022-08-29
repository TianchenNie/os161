/*
 * Synchronization primitives.
 * See synch.h for specifications of the functions.
 */

#include <types.h>
#include <lib.h>
#include <synch.h>
#include <thread.h>
#include <curthread.h>
#include <machine/spl.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *namearg, int initial_count)
{
	struct semaphore *sem;

	assert(initial_count >= 0);

	sem = kmalloc(sizeof(struct semaphore));
	if (sem == NULL) {
		return NULL;
	}

	sem->name = kstrdup(namearg);
	if (sem->name == NULL) {
		kfree(sem);
		return NULL;
	}

	sem->count = initial_count;
	return sem;
}

void
sem_destroy(struct semaphore *sem)
{
	int spl;
	assert(sem != NULL);

	spl = splhigh();
	assert(thread_hassleepers(sem)==0);
	splx(spl);

	/*
	 * Note: while someone could theoretically start sleeping on
	 * the semaphore after the above test but before we free it,
	 * if they're going to do that, they can just as easily wait
	 * a bit and start sleeping on the semaphore after it's been
	 * freed. Consequently, there's not a whole lot of point in 
	 * including the kfrees in the splhigh block, so we don't.
	 */

	kfree(sem->name);
	kfree(sem);
}

void 
P(struct semaphore *sem)
{
	int spl;
	assert(sem != NULL);

	/*
	 * May not block in an interrupt handler.
	 *
	 * For robustness, always check, even if we can actually
	 * complete the P without blocking.
	 */
	assert(in_interrupt==0);

	spl = splhigh();
	while (sem->count==0) {
		thread_sleep(sem);
	}
	assert(sem->count>0);
	sem->count--;
	splx(spl);
}

void
V(struct semaphore *sem)
{
	int spl;
	assert(sem != NULL);
	spl = splhigh();
	sem->count++;
	assert(sem->count>0);
	thread_wakeup(sem);
	splx(spl);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
	struct lock *lock;

	lock = kmalloc(sizeof(struct lock));
	if (lock == NULL) {
		return NULL;
	}

	lock->name = kstrdup(name);
	if (lock->name == NULL) {
		kfree(lock);
		return NULL;
	}
	
	// add stuff here as needed
	// when the lock is created, no thread should be holding it.
	lock->holding_thread = NULL;
	
	return lock;
}

void
lock_destroy(struct lock *lock)
{
	int spl;
	assert(lock != NULL);
	// add stuff here as needed
	// when the lock is about to be destroyed, no thread should be holding it.
	assert(lock->holding_thread == NULL);
	// when the lock is about to be destroyed, no thread should be waiting to hold it.
	spl = splhigh();
	assert(thread_hassleepers(lock) == 0);
	splx(spl);
	kfree(lock->name);
	kfree(lock);
}

void
lock_acquire(struct lock *lock)
{
	assert(lock != NULL);
	assert(in_interrupt == 0);

	int spl;
	spl = splhigh();

	// sleep threads trying to acquire the lock when the lock is being held by a thread
	while (lock->holding_thread != NULL) {
		thread_sleep(lock);
	}
	// there should be no thread holding the lock and let the current thread hold the lock
	assert(lock->holding_thread == NULL);
	lock->holding_thread = curthread;
	splx(spl);
}

void
lock_release(struct lock *lock)
{
	assert(lock != NULL);
	// only the thread holding the lock can release
	if(curthread != lock->holding_thread){
		kprintf("ERROR, thread trying to release a lock that does not belong to it, lock-name: %s\n", lock->name);
	}
	assert(curthread == lock->holding_thread);
	int spl;
	spl = splhigh();
	// unlock the lock (no thread is holding it anymore)
	lock->holding_thread = NULL;
	// wakeup all threads waiting for the lock to unlock
	thread_wakeup(lock);
	splx(spl);
}

int
lock_do_i_hold(struct lock *lock)
{
	assert(lock != NULL);
	return curthread == lock->holding_thread ? 1 : 0;  
}

////////////////////////////////////////////////////////////
//
// CV


struct cv *
cv_create(const char *name)
{
	struct cv *cv;

	cv = kmalloc(sizeof(struct cv));
	if (cv == NULL) {
		return NULL;
	}

	cv->name = kstrdup(name);
	if (cv->name==NULL) {
		kfree(cv);
		return NULL;
	}
	
	// add stuff here as needed
	return cv;
}

void
cv_destroy(struct cv *cv)
{
        int spl;
	assert(cv != NULL);
	
	// add stuff here as needed
	// no thread should be waiting for this cv when this cv is about to be detroyed.
	spl = splhigh();
	assert(thread_hassleepers(cv) == 0);
	kfree(cv->name);
	kfree(cv);
    splx(spl);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	// Write this
	// current thread must be holding the lock
	assert(curthread == lock->holding_thread);
	int spl;
	spl = splhigh();

	// release lock, sleep until woken up by signal or broadcast.
	// once the thread is woken up by signal or broadcast, it should
	// not longer be sleeping on the cv address, but on the lock address
	// if it could not acquire the lock 
	// (shouldn't be waiting for anymore cv signals, should be waiting for lock releases)
	lock_release(lock);
	thread_sleep(cv);
	lock_acquire(lock);
	splx(spl);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	// Write this
	// current thread must be holding the lock
	assert(curthread == lock->holding_thread);	
	int spl;
	spl = splhigh();
	// wake up a single thread waiting on this cv
	thread_single_wakeup(cv);
	splx(spl);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	// current thread must be holding the lock
	assert(curthread == lock->holding_thread);
	int spl;
	spl = splhigh();
	// wakeup all threads waiting on this cv
	thread_wakeup(cv);
	splx(spl);
	// Write this
}
