/*
 * catlock.c
 *
 * Please use LOCKS/CV'S to solve the cat syncronization problem in 
 * this file.
 */


/*
 * 
 * Includes
 *
 */

#include <types.h>
#include <lib.h>
#include <test.h>
#include <thread.h>
#include <synch.h>
#include "catmouse.h"


struct lock *lock;

// bowl values: 0 (non-occupied), 1 (mouse eating here), 2 (cat eating here)
int bowl1;
int bowl2;
/*
 * 
 * Function Definitions
 * 
 */

/*
 * catlock()
 *
 * Arguments:
 *      void * unusedpointer: currently unused.
 *      unsigned long catnumber: holds the cat identifier from 0 to NCATS -
 *      1.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      Write and comment this function using locks/cv's.
 *
 */

static
int
cat_can_eat(void) 
{
        // if nobody is eating on either bowls, return 1
        if(bowl1 == 0 && bowl2 == 0){
                return 1;
        }

        // if a bowl is non-occupied, check what animal is eating on the other (occupied) bowl.
        // if the animal eating on the other (occupied) bowl is a mouse, return 0, else return 1.
        if(bowl1 == 0 || bowl2 == 0){
                // if bowl1 is un-occupied, check what animal is eating on bowl2
                if(bowl1 == 0){
                        return bowl2 == 2 ? 1 : 0;
                }
                // if bowl2 is un-occupied, check what animal is eating on bowl1
                else if(bowl2 == 0){
                        return bowl1 == 2 ? 1 : 0;
                }
        }
        return 0;
}

static
void
catlock(void * unusedpointer, 
        unsigned long catnumber)
{
        /*
         * Avoid unused variable warnings.
         */
        (void) unusedpointer;


        int iteration = 0;
        int can_eat;
        int my_bowl;
        while(iteration < NMEALS){
                lock_acquire(lock);
                // check if this cat can eat
                can_eat = cat_can_eat();
                if(!can_eat){
                        lock_release(lock);
                        // this cat cannot eat, gracefully let other animals do their thing.
                        thread_yield();
                        continue;
                }
                // this cat can eat, assign a bowl to him/her.
                my_bowl = bowl1 == 0 ? 1 : 2;
                if(my_bowl == 1){
                        bowl1 = 2;
                }
                else if(my_bowl == 2){
                        bowl2 = 2;
                }
                lock_release(lock);
                catmouse_eat("cat", catnumber, my_bowl, iteration);
                // this cat finished eating, mark bowl as un-occupied.
                if(my_bowl == 1){
                        bowl1 = 0;
                }
                else if(my_bowl == 2){
                        bowl2 = 0;
                }
                iteration++;
        }
}

static
int
mouse_can_eat(void) {
        // if nobody is eating on either bowls, return 1
        if(bowl1 == 0 && bowl2 == 0){
                return 1;
        }

        // if a bowl is un-occupied, check what animal is eating on the other bowl.
        // if the animal eating on the other (occupied) bowl is a cat, return 0, else return 1.
        if(bowl1 == 0 || bowl2 == 0){
                // if bowl1 is un-occupied, check what animal is eating on bowl2.
                if(bowl1 == 0){
                        return bowl2 == 1 ? 1 : 0;
                }
                // if bowl2 is un-occupied, check what animal is eating on bowl1.
                else if(bowl2 == 0){
                        return bowl1 == 1 ? 1 : 0;
                }
        }
        // if no bowls are available (both bowls have value greater than 0), return 0
        return 0;
}

/*
 * mouselock()
 *
 * Arguments:
 *      void * unusedpointer: currently unused.
 *      unsigned long mousenumber: holds the mouse identifier from 0 to 
 *              NMICE - 1.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      Write and comment this function using locks/cv's.
 *
 */

static
void
mouselock(void * unusedpointer,
          unsigned long mousenumber)
{
        
        /*
         * Avoid unused variable warnings.
         */    
        (void) unusedpointer;


        int iteration = 0;
        int can_eat;
        int my_bowl;
        while(iteration < NMEALS){
                lock_acquire(lock);
                // check if this mouse can eat.
                can_eat = mouse_can_eat();
                if(!can_eat){
                        lock_release(lock);
                        // this mouse cannot eat, gracefully let other animals do their thing.
                        thread_yield();
                        continue;
                }
                // this mouse can eat, occupy a bowl for him/her.
                my_bowl = bowl1 == 0 ? 1 : 2;
                if(my_bowl == 1){
                        bowl1 = 1;
                }
                else if(my_bowl == 2){
                        bowl2 = 1;
                }
                lock_release(lock);
                catmouse_eat("mouse", mousenumber, my_bowl, iteration);
                // this mouse finished eating, mark bowl as non-occupied.
                if(my_bowl == 1){
                        bowl1 = 0;
                }
                else if(my_bowl == 2){
                        bowl2 = 0;
                }
                iteration++;
        }
}


/*
 * catmouselock()
 *
 * Arguments:
 *      int nargs: unused.
 *      char ** args: unused.
 *
 * Returns:
 *      0 on success.
 *
 * Notes:
 *      Driver code to start up catlock() and mouselock() threads.  Change
 *      this code as necessary for your solution.
 */

int
catmouselock(int nargs,
             char ** args)
{
        int index, error;
        lock = lock_create("Cat Mouse Lock");
        bowl1 = 0;
        bowl2 = 0;
        
        /*
         * Start NCATS catlock() threads.
         */

        for (index = 0; index < NCATS; index++) {
           
                error = thread_fork("catlock thread", 
                                    NULL, 
                                    index, 
                                    catlock, 
                                    NULL
                                    );
                
                /*
                 * panic() on error.
                 */

                if (error) {
                 
                        panic("catlock: thread_fork failed: %s\n", 
                              strerror(error)
                              );
                }
        }

        /*
         * Start NMICE mouselock() threads.
         */

        for (index = 0; index < NMICE; index++) {
   
                error = thread_fork("mouselock thread", 
                                    NULL, 
                                    index, 
                                    mouselock, 
                                    NULL
                                    );
      
                /*
                 * panic() on error.
                 */

                if (error) {
         
                        panic("mouselock: thread_fork failed: %s\n", 
                              strerror(error)
                              );
                }
        }
        
        /*
         * wait until all other threads finish
         */
        
        while (thread_count() > 1)
                thread_yield();

        (void)nargs;
        (void)args;
        lock_destroy(lock);
        kprintf("catlock test done\n");

        return 0;
}

