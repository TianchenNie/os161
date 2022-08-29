/* 
 * stoplight.c
 *
 * You can use any synchronization primitives available to solve
 * the stoplight problem in this file.
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


/*
 * Number of cars created.
 */

#define NCARS 20


/*
 *
 * Function Definitions
 *
 */

/*
                | N | N |       
                | ↓   ↑ |       
                |   |   |
-------------------------------------------
      W<-----   | NW| NE|   <-----E
- - - - - - - - - - - - - - - - - - - - - -
      W----->   | SW| SE|   ----->E    
-------------------------------------------
                | ↓ | ↑ |
                |       |
                | S | S |

*/


static const char *directions[] = { "N", "E", "S", "W" };

struct cv *stoplight_cv;
struct lock *stoplight_lock;
// only one car should be checking car direction at once, as we will increment num_verical_cars accordingly
struct lock *car_direction_lock;
// wait for all threads to be assigned a direction
struct cv *wait_for_all_threads_cv;
struct lock *wait_for_all_threads_lock;

// wait for left turns to finish, one direction at a time, two directions could cause deadlocks
struct cv *wait_for_north_left_turn_cv;
struct lock *wait_for_north_left_turn_lock;
struct cv *wait_for_south_left_turn_cv;
struct lock *wait_for_south_left_turn_lock;
struct cv *wait_for_east_left_turn_cv;
struct lock *wait_for_east_left_turn_lock;
struct cv *wait_for_west_left_turn_cv;
struct lock *wait_for_west_left_turn_lock;

// if stoplight == 0, all cars on vertical directions (N, S) can drive, if stoplight == 1, all cars on horizontal directions can drive (W, E)
int stoplight;
int num_vertical_cars;
int num_north_left_turns;
int num_south_left_turns;
int num_east_left_turns;
int num_west_left_turns;
int num_checked_threads;
// one lock for each approaching direction, only 1 car should be approaching from a direction at a time
// if a car is approaching from east, lock E_lock_start so until this car is in a region
struct lock *N_lock_start;
struct lock *E_lock_start; 
struct lock *S_lock_start;
struct lock *W_lock_start;


// one lock for each leaving direction, only 1 car should be leaving from a direction at a time
// if a car is leaving at east, lock E_lock_leave so until this car leaves (thread terminates)
struct lock *N_lock_leave;
struct lock *E_lock_leave;
struct lock *S_lock_leave;
struct lock *W_lock_leave;

// static const char *portions[] = { "NW", "NE", "SE", "SW" };

// locks for the regions, if a car is in a region, no other car should be allowed to enter the region.
struct lock *NW_lock;
struct lock *NE_lock;
struct lock *SE_lock;
struct lock *SW_lock;

static const char *msgs[] = {
        "approaching:",
        "region1:    ",
        "region2:    ",
        "region3:    ",
        "leaving:    "
};

struct car {
        int number; // unique car number (0 - 19 for 20 cars)
        int startdirection; // 0 for north, 1 for east, 2 for south, 3 for west
        int destdirection; // 0 for north, 1 for east, 2 for south, 3 for west
        int status; // 0 for approaching, 1 for region1, 2 for region 2, 3 for region 3, 4 for leaving. Pass this into msg_nr.
};
/* use these constants for the first parameter of message */
enum { APPROACHING, REGION1, REGION2, REGION3, LEAVING };


// msg_nr: 0 for approaching, 1 for region1, 2 for region2, 3 for region3, 4 for leaving
// carnumber: number of car
// cardirection: 0 for N, 1 for E, 2 for S, 3 for W
// destdirection: 0 for N, 1 for E, 2 for S, 3 for W
static void
message(int msg_nr, int carnumber, int cardirection, int destdirection)
{
        kprintf("%s car = %2d, direction = %s, destination = %s\n",
                msgs[msg_nr], carnumber,
                directions[cardirection], directions[destdirection]);
}

static void
carmessage(struct car c){
        message(c.status, c.number, c.startdirection, c.destdirection);
}

// static
// int
// all_portions_empty(){
//         return (NW_lock->holding_thread == NULL && NE_lock->holding_thread == NULL && SW_lock->holding_thread == NULL && SE_lock->holding_thread == NULL)
//                 ? 1 : 0;
// }

// get the destination of the car (going straight) based on its starting direction
static
int
get_dest_straight(int startdirection){
        // If car is going straight from north, dest is south
        if(startdirection == 0){
                return 2;
                
        }
        // If car is going straight from east, dest is west
        else if(startdirection == 1){
                return 3;
        }
        // If car is going straight from south, dest is north
        else if(startdirection == 2){
                return 0;
        }
        // If car is going straight from west, dest is east
        else{
                return 1;
        }
}

// get the destination of the car (turning right) based on its starting direction
static
int
get_dest_right(int startdirection){
        // If car is turning right from north, dest is west
        if(startdirection == 0){
                return 3;
                
        }
        // If car is turning right from east, dest is north
        else if(startdirection == 1){
                return 0;
        }
        // If car is turning right from south, dest is east
        else if(startdirection == 2){
                return 1;
        }
        // If car is turning right from west, dest is south
        else{
                return 2;
        }
}

// get the destination of the car (turning left) based on its starting direction
static
int
get_dest_left(int startdirection){
        // If car is turning left from north, dest is east
        if(startdirection == 0){
                return 1;
                
        }
        // If car is turning left from east, dest is south
        else if(startdirection == 1){
                return 2;
        }
        // If car is turning left from south, dest is west
        else if(startdirection == 2){
                return 3;
        }
        // If car is turning left from west, dest is north
        else{
                return 0;
        }
}

// wrapper function, arguments: start direction and action (0 for straight, 1 for right, 2 for left), returns: dest direction
static
int
get_dest(int startdirection, int action){
        // car going straight
        if(action == 0){
                return get_dest_straight(startdirection);
        }
        // car turning right
        else if(action == 1){
                return get_dest_right(startdirection);
        }
        // car turning left
        else{
                return get_dest_left(startdirection);
        }
}
/*
 * gostraight()
 *
 * Arguments:
 *      unsigned long cardirection: the direction from which the car
 *              approaches the intersection.
 *      unsigned long carnumber: the car id number for printing purposes.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      This function should implement passing straight through the
 *      intersection from any direction.
 *      Write and comment this function.
 */

static
void
gostraight(unsigned long cardirection,
           unsigned long carnumber)
{
        struct car straight_car;
        straight_car.number = carnumber;
        // status is currenty approaching
        straight_car.status = 0;
        straight_car.startdirection = cardirection;
        straight_car.destdirection = get_dest_straight(cardirection);
        // If car is going straight from north, dest is south
        // it will go through N --> NW --> SW --> S
        if(cardirection == 0){
                // lock_release(stoplight_lock);
                lock_acquire(NW_lock);
                // now in region 1
                straight_car.status = 1;
                carmessage(straight_car);
                lock_release(N_lock_start);
                lock_acquire(SW_lock);
                // now in region 2
                straight_car.status = 2;
                carmessage(straight_car);
                lock_release(NW_lock);
                lock_acquire(S_lock_leave);
                // now leaving
                straight_car.status = 4;
                carmessage(straight_car);
                lock_release(SW_lock);
                lock_release(S_lock_leave);
                return;
                
        }
        // If car is going straight from east, dest is west
        // it will go through E --> NE --> NW --> W
        else if(cardirection == 1){
                // lock_release(stoplight_lock);
                lock_acquire(NE_lock);
                // now in region 1
                straight_car.status = 1;
                carmessage(straight_car);
                lock_release(E_lock_start);
                lock_acquire(NW_lock);
                // now in region 2
                straight_car.status = 2;
                carmessage(straight_car);
                lock_release(NE_lock);
                lock_acquire(W_lock_leave);
                // now leaving
                straight_car.status = 4;
                carmessage(straight_car);
                lock_release(NW_lock);
                lock_release(W_lock_leave);
                return;
        }
        // If car is going straight from south, dest is north
        // it will go through S --> SE --> NE --> N
        else if(cardirection == 2){
                // lock_release(stoplight_lock);
                lock_acquire(SE_lock);
                // now in region 1
                straight_car.status = 1;
                carmessage(straight_car);
                lock_release(S_lock_start);
                lock_acquire(NE_lock);
                // now in region 2
                straight_car.status = 2;
                carmessage(straight_car);
                lock_release(SE_lock);
                lock_acquire(N_lock_leave);
                // now leaving
                straight_car.status = 4;
                carmessage(straight_car);
                lock_release(NE_lock);
                lock_release(N_lock_leave);
                return;
        }
        // If car is going straight from west, dest is east
        // it will go through W --> SW --> SE --> E
        else if(cardirection == 3){
                // lock_release(stoplight_lock);
                lock_acquire(SW_lock);
                //now in region 1
                straight_car.status = 1;
                carmessage(straight_car);
                lock_release(W_lock_start);
                lock_acquire(SE_lock);
                // now in region 2
                straight_car.status = 2;
                carmessage(straight_car);
                lock_release(SW_lock);
                lock_acquire(E_lock_leave);
                // now leaving
                straight_car.status = 4;
                carmessage(straight_car);
                lock_release(SE_lock);
                lock_release(E_lock_leave);
                return;
        }
}


/*
 * turnright()
 *
 * Arguments:
 *      unsigned long cardirection: the direction from which the car
 *              approaches the intersection.
 *      unsigned long carnumber: the car id number for printing purposes.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      This function should implement making a right turn through the 
 *      intersection from any direction.
 *      Write and comment this function.
 */

static
void
turnright(unsigned long cardirection,
          unsigned long carnumber)
{
        struct car rightcar;
        rightcar.number = carnumber;
        // status is currently approaching
        rightcar.status = 0;
        rightcar.startdirection = cardirection;
        rightcar.destdirection = get_dest_right(cardirection);
        // If the car is turning right from north, dest is west
        // it will go through N --> NW --> W
        if(cardirection == 0){
                lock_acquire(NW_lock);
                // in region1
                rightcar.status = 1;
                carmessage(rightcar);
                lock_release(N_lock_start);
                lock_acquire(W_lock_leave);
                // in leave
                rightcar.status = 4;
                carmessage(rightcar);
                lock_release(NW_lock);
                lock_release(W_lock_leave);
                return;
        }
        // If the car is turning right from east, dest is north
        // it will go through E --> NE --> N
        else if(cardirection == 1){
                lock_acquire(NE_lock);
                // in region1
                rightcar.status = 1;
                carmessage(rightcar);
                lock_release(E_lock_start);
                lock_acquire(N_lock_leave);
                // in leave
                rightcar.status = 4;
                carmessage(rightcar);
                lock_release(NE_lock);
                lock_release(N_lock_leave);
                return;
        }
        // If the car is turning right from south, dest is east
        // it will go through S --> SE --> E
        else if(cardirection == 2){
                lock_acquire(SE_lock);
                // in region1
                rightcar.status = 1;
                carmessage(rightcar);
                lock_release(S_lock_start);

                lock_acquire(E_lock_leave);
                // in leave
                rightcar.status = 4;
                carmessage(rightcar);
                lock_release(SE_lock);
                lock_release(E_lock_leave);
                return;
        }
        // If the car is turning right from west, dest is south
        // it will go through W --> SW --> S
        else if(cardirection == 3){
                lock_acquire(SW_lock);
                // in region1
                rightcar.status = 1;
                carmessage(rightcar);
                lock_release(W_lock_start);
                lock_acquire(S_lock_leave);
                // in leave
                rightcar.status = 4;
                carmessage(rightcar);
                lock_release(SW_lock);
                lock_release(S_lock_leave);
                return;
        }
}


/*
 * turnleft()
 *
 * Arguments:
 *      unsigned long cardirection: the direction from which the car
 *              approaches the intersection.
 *      unsigned long carnumber: the car id number for printing purposes.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      This function should implement making a left turn through the 
 *      intersection from any direction.
 *      Write and comment this function.
 */

static
void
turnleft(unsigned long cardirection,
         unsigned long carnumber)
{
        struct car leftcar;
        leftcar.number = carnumber;
        // currently status is approaching
        leftcar.status = 0;
        leftcar.startdirection = cardirection;
        leftcar.destdirection = get_dest_left(cardirection);

        // If the car is turning left from north, dest is east
        // it will go through N --> NW --> SW --> SE --> E
        if(cardirection == 0){
                // lock_release(stoplight_lock);
                lock_acquire(NW_lock);
                // in region1
                leftcar.status = 1;
                carmessage(leftcar);
                lock_release(N_lock_start);
                lock_acquire(SW_lock);
                // in region2
                leftcar.status = 2;
                carmessage(leftcar);
                lock_release(NW_lock);
                lock_acquire(SE_lock);
                // in region3
                leftcar.status = 3;
                carmessage(leftcar);
                lock_release(SW_lock);
                lock_acquire(E_lock_leave);
                // in leave
                leftcar.status = 4;
                carmessage(leftcar);
                lock_release(SE_lock);
                lock_release(E_lock_leave);
                return;

        }

        // If the car is turning left from east, dest is south
        // it will go through E --> NE --> NW --> SW --> S
        else if(cardirection == 1){
                // lock_release(stoplight_lock);
                lock_acquire(NE_lock);
                // in region1
                leftcar.status = 1;
                carmessage(leftcar);
                lock_release(E_lock_start);
                lock_acquire(NW_lock);
                // in region2
                leftcar.status = 2;
                carmessage(leftcar);
                lock_release(NE_lock);
                lock_acquire(SW_lock);
                // in region3
                leftcar.status = 3;
                carmessage(leftcar);
                lock_release(NW_lock);
                lock_acquire(S_lock_leave);
                // in leave
                leftcar.status = 4;
                carmessage(leftcar);
                lock_release(SW_lock);
                lock_release(S_lock_leave);
                return;
        } 
        // If the car is turning left from south, dest is west
        // it will go through S --> SE --> NE --> NW --> W
        else if(cardirection == 2){
                // lock_release(stoplight_lock);
                lock_acquire(SE_lock);
                // in region1
                leftcar.status = 1;
                carmessage(leftcar);
                lock_release(S_lock_start);
                lock_acquire(NE_lock);
                // in region2
                leftcar.status = 2;
                carmessage(leftcar);
                lock_release(SE_lock);
                lock_acquire(NW_lock);
                // in region3
                leftcar.status = 3;
                carmessage(leftcar);
                lock_release(NE_lock);
                lock_acquire(W_lock_leave);
                // in leave
                leftcar.status = 4;
                carmessage(leftcar);
                lock_release(NW_lock);
                lock_release(W_lock_leave);
                return;
        }
        // If the car is turning left from west, dest is north
        // it will go through W --> SW --> SE --> NE --> N
        else if(cardirection == 3){
                // lock_release(stoplight_lock);
                lock_acquire(SW_lock);
                // in region1
                leftcar.status = 1;
                carmessage(leftcar);
                lock_release(W_lock_start);
                lock_acquire(SE_lock);
                // in region2
                leftcar.status = 2;
                carmessage(leftcar);
                lock_release(SW_lock);
                lock_acquire(NE_lock);
                // in region3
                leftcar.status = 3;
                carmessage(leftcar);
                lock_release(SE_lock);
                lock_acquire(N_lock_leave);
                // in leave
                leftcar.status = 4;
                carmessage(leftcar);
                lock_release(NE_lock);
                lock_release(N_lock_leave);
                return;
        }



}

/*
 * approachintersection()
 *
 * Arguments: 
 *      void * unusedpointer: currently unused.
 *      unsigned long carnumber: holds car id number.
 *
 * Returns:
 *      nothing.
 *
 * Notes:
 *      Change this function as necessary to implement your solution. These
 *      threads are created by createcars().  Each one must choose a direction
 *      randomly, approach the intersection, choose a turn randomly, and then
 *      complete that turn.  The code to choose a direction randomly is
 *      provided, the rest is left to you to implement.  Making a turn
 *      or going straight should be done by calling one of the functions
 *      above.
 */
 
static
void
approachintersection(void * unusedpointer,
                     unsigned long carnumber)
{
        int cardirection;
        int car_action;

        /*
         * Avoid unused variable and function warnings.
         */

        (void) unusedpointer;

        /*
         * cardirection is set randomly.
         * 0 for N, 1 for E, 2 for S, 3 for W.
         */

        cardirection = random() % 4;
        car_action = random() % 3;
        // check if the car is going vertically, also increment the number of checked threads
        // lock ensures no races of incrementation
        lock_acquire(car_direction_lock);
        // increment number of vertical cars accordingly, don't increment for right turn (right turn can happen on red light).
        if((cardirection == 0 || cardirection == 2) && car_action != 1){
                num_vertical_cars++;
                // if this car will perform a left turn
                if(car_action == 2){
                        if(cardirection == 0){
                                num_north_left_turns++;
                        }
                        else if(cardirection == 2){
                                num_south_left_turns++;
                        }
                }
        }
        // if a car is driving horizontally
        else if(cardirection == 1 || cardirection == 3){
                // if this car will perform a left turn
                if(car_action == 2){
                        if(cardirection == 1){
                                num_east_left_turns++;
                        }
                        else if(cardirection == 3){
                                num_west_left_turns++;
                        }
                }
        }
        // increment total number of threads checked
        num_checked_threads++;
        lock_release(car_direction_lock);

        // wait for all threads to be checked
        lock_acquire(wait_for_all_threads_lock);
        while(num_checked_threads < NCARS){
                cv_wait(wait_for_all_threads_cv, wait_for_all_threads_lock);
        }
        // the last checked thread should wake up all threads sleeping on the cv.
        cv_broadcast(wait_for_all_threads_cv, wait_for_all_threads_lock);
        lock_release(wait_for_all_threads_lock);

        int destdirection = get_dest(cardirection, car_action);
        // if a car is starting from north and is going straight, check if it's a left turn, if it is, go ahead, if not wait for all left turns
        if(cardirection == 0){
                if(car_action == 0){
                        lock_acquire(wait_for_north_left_turn_lock);
                        while(num_north_left_turns > 0){
                                cv_wait(wait_for_north_left_turn_cv, wait_for_north_left_turn_lock);
                        }
                        lock_release(wait_for_north_left_turn_lock);
                        lock_acquire(wait_for_south_left_turn_lock);
                        while(num_south_left_turns > 0){
                                cv_wait(wait_for_south_left_turn_cv, wait_for_south_left_turn_lock);
                        }
                        lock_release(wait_for_south_left_turn_lock);
                }

                // block other cars from approaching at north, message this car as approaching
                lock_acquire(N_lock_start);
                message(0, carnumber, cardirection, destdirection);
        }
        // if a car is starting from east, if it's going straight, wait for horizontal left turns (and stoplight later), 
        // if it's turning left, wait for go ahead (may need to wait for stoplight later)
        else if(cardirection == 1){
                if(car_action == 0){
                        // wait for east left turns
                        lock_acquire(wait_for_east_left_turn_lock);
                        while(num_east_left_turns > 0){
                                cv_wait(wait_for_east_left_turn_cv, wait_for_east_left_turn_lock);
                        }
                        lock_release(wait_for_east_left_turn_lock);
                        // wait for west left turns
                        lock_acquire(wait_for_west_left_turn_lock);
                        while(num_west_left_turns > 0){
                                cv_wait(wait_for_west_left_turn_cv, wait_for_west_left_turn_lock);
                        }
                        lock_release(wait_for_west_left_turn_lock);   
                }
                // set the first car reaching here as approaching, may need to stay as approaching until stoplight changes if it's not a right turn
                lock_acquire(E_lock_start);
                message(0, carnumber, cardirection, destdirection);
        }
        // if a car is starting from south, if it's going straight, wait for vertical left turns, if it's going left, wait for north left turns.
        else if(cardirection == 2){
                if(car_action == 0){
                        lock_acquire(wait_for_north_left_turn_lock);
                        while(num_north_left_turns > 0){
                                cv_wait(wait_for_north_left_turn_cv, wait_for_north_left_turn_lock);
                        }
                        lock_release(wait_for_north_left_turn_lock);
                        lock_acquire(wait_for_south_left_turn_lock);
                        while(num_south_left_turns > 0){
                                cv_wait(wait_for_south_left_turn_cv, wait_for_south_left_turn_lock);
                        }
                        lock_release(wait_for_south_left_turn_lock);
                }
                if(car_action == 2){
                        lock_acquire(wait_for_north_left_turn_lock);
                        while(num_north_left_turns > 0){
                                cv_wait(wait_for_north_left_turn_cv, wait_for_north_left_turn_lock);
                        }
                        lock_release(wait_for_north_left_turn_lock);
                }
                lock_acquire(S_lock_start);
                message(0, carnumber, cardirection, destdirection);
        }
        // if a car is starting from west, if it's going straight, wait for horizontal left turns (and potentially stoplight later)
        // if it's turning left wait for east left turns.
        else if(cardirection == 3){
                if(car_action == 0){
                        // wait for east left turns
                        lock_acquire(wait_for_east_left_turn_lock);
                        while(num_east_left_turns > 0){
                                cv_wait(wait_for_east_left_turn_cv, wait_for_east_left_turn_lock);
                        }
                        lock_release(wait_for_east_left_turn_lock);
                        // wait for west left turns
                        lock_acquire(wait_for_west_left_turn_lock);
                        while(num_west_left_turns > 0){
                                cv_wait(wait_for_west_left_turn_cv, wait_for_west_left_turn_lock);
                        }
                        lock_release(wait_for_west_left_turn_lock);   
                }
                if(car_action == 2){
                        // wait for east left turns
                        lock_acquire(wait_for_east_left_turn_lock);
                        while(num_east_left_turns > 0){
                                cv_wait(wait_for_east_left_turn_cv, wait_for_east_left_turn_lock);
                        }
                        lock_release(wait_for_east_left_turn_lock);
                }
                lock_acquire(W_lock_start);
                message(0, carnumber, cardirection, destdirection);
        }

        if(car_action == 0){
                // go straight
                lock_acquire(stoplight_lock);
                // if the car is on a vertical direction
                if(cardirection == 1 || cardirection == 3){
                        while(stoplight == 0){
                                cv_wait(stoplight_cv, stoplight_lock);
                        }
                }
                lock_release(stoplight_lock);

                gostraight(cardirection, carnumber);
                // finished trip, decrement num vertical cars as needed
                if(cardirection == 0 || cardirection == 2){
                        lock_acquire(car_direction_lock);
                        num_vertical_cars--;
                        lock_release(car_direction_lock);
                }

                // if that was the last vertical car, acquire the stoplight lock, change the stoplight to horizontal green vertical red.
                // wakeup all horizontal cars.
                if(num_vertical_cars == 0){
                        lock_acquire(stoplight_lock);
                        stoplight = 1;
                        cv_broadcast(stoplight_cv, stoplight_lock);
                        lock_release(stoplight_lock);
                }
                return;
        }
        else if(car_action == 1){
                // turn right
                turnright(cardirection, carnumber);
                return;
        }
        else if(car_action == 2){
                // turn left
                // if the car is on a horizontal direction, wait for stoplight
                lock_acquire(stoplight_lock);
                if(cardirection == 1 || cardirection == 3){
                        while(stoplight == 0){
                                cv_wait(stoplight_cv, stoplight_lock);
                        }
                }
                lock_release(stoplight_lock);
                turnleft(cardirection, carnumber);
                // finished trip, decrement num left turns on direction and maybe num vertical cars
                if(cardirection == 0){
                        lock_acquire(car_direction_lock);
                        num_vertical_cars--;
                        num_north_left_turns--;
                        if(num_north_left_turns == 0){
                                lock_acquire(wait_for_north_left_turn_lock);
                                cv_broadcast(wait_for_north_left_turn_cv, wait_for_north_left_turn_lock);
                                lock_release(wait_for_north_left_turn_lock);
                        }
                        lock_release(car_direction_lock);
                }
                else if(cardirection == 1){
                        lock_acquire(car_direction_lock);
                        num_east_left_turns--;
                        if(num_east_left_turns == 0){
                                lock_acquire(wait_for_east_left_turn_lock);
                                cv_broadcast(wait_for_east_left_turn_cv, wait_for_east_left_turn_lock);
                                lock_release(wait_for_east_left_turn_lock);
                        }
                        lock_release(car_direction_lock);
                }
                else if(cardirection == 2){
                        lock_acquire(car_direction_lock);
                        num_vertical_cars--;
                        num_south_left_turns--;
                        if(num_south_left_turns == 0){
                                lock_acquire(wait_for_south_left_turn_lock);
                                cv_broadcast(wait_for_south_left_turn_cv, wait_for_south_left_turn_lock);
                                lock_release(wait_for_south_left_turn_lock);
                        }
                        lock_release(car_direction_lock);
                }
                else if(cardirection == 3){
                        lock_acquire(car_direction_lock);
                        num_west_left_turns--;
                        if(num_west_left_turns == 0){
                                lock_acquire(wait_for_west_left_turn_lock);
                                cv_broadcast(wait_for_west_left_turn_cv, wait_for_west_left_turn_lock);
                                lock_release(wait_for_west_left_turn_lock);
                        }
                        lock_release(car_direction_lock);
                }
                // if that was the last vertical car, acquire the stoplight lock, change the stoplight to horozontal green vertical red.
                // wakeup all horizontal cars.
                if(num_vertical_cars == 0){
                        lock_acquire(stoplight_lock);
                        stoplight = 1;
                        cv_broadcast(stoplight_cv, stoplight_lock);
                        lock_release(stoplight_lock);
                }
                return;
        }
}


/*
 * createcars()
 *
 * Arguments:
 *      int nargs: unused.
 *      char ** args: unused.
 *
 * Returns:
 *      0 on success.
 *
 * Notes:
 *      Driver code to start up the approachintersection() threads.  You are
 *      free to modiy this code as necessary for your solution.
 */

int
createcars(int nargs,
           char ** args)
{
        int index, error;
        stoplight = 0;
        num_vertical_cars = 0;
        num_north_left_turns = 0;
        num_south_left_turns = 0;
        num_east_left_turns = 0;
        num_west_left_turns = 0;
        num_checked_threads = 0;
        stoplight_cv = cv_create("Stoplight CV");
        wait_for_all_threads_cv = cv_create("Wait For All Threads To Be Assigned a Direction CV");
        stoplight_lock = lock_create("Stoplight Lock");
        wait_for_all_threads_lock = lock_create("Wait For All Threads To Be Assigned a Direction Lock");
        wait_for_north_left_turn_cv = cv_create("Wait For All Left Turns Starting From North CV");
        wait_for_north_left_turn_lock = lock_create("Wait For All Left Turns Starting From North Lock");
        wait_for_south_left_turn_cv = cv_create("Wait For All Left Turns Starting From South CV");
        wait_for_south_left_turn_lock = lock_create("Wait For All Left Turns Starting From South Lock");
        wait_for_east_left_turn_cv = cv_create("Wait For All Left Turns Starting From East CV");
        wait_for_east_left_turn_lock = lock_create("Wait For All Left Turns Starting From East Lock");
        wait_for_west_left_turn_cv = cv_create("Wait For All Left Turns Starting From West CV");
        wait_for_west_left_turn_lock = lock_create("Wait For All Left Turns Starting From West Lock");
        car_direction_lock = lock_create("Car Direction Lock");
        N_lock_start = lock_create("Lock N Direction Start");
        E_lock_start = lock_create("Lock E Direction Start");
        S_lock_start = lock_create("Lock S Direction Start");
        W_lock_start = lock_create("Lock W Direction Start");
        N_lock_leave = lock_create("Lock N Direction Leave");
        E_lock_leave = lock_create("Lock E Direction Leave");
        S_lock_leave = lock_create("Lock S Direction Leave");
        W_lock_leave = lock_create("Lock W Direction Leave");
        NE_lock = lock_create("Lock NE portion");
        NW_lock = lock_create("Lock NW portion");
        SW_lock = lock_create("Lock SW portion");
        SE_lock = lock_create("Lock SE portion");
        /*
         * Start NCARS approachintersection() threads.
         */

        for (index = 0; index < NCARS; index++) {
                error = thread_fork("approachintersection thread",
                                    NULL, index, approachintersection, NULL);

                /*
                * panic() on error.
                */

                if (error) {         
                        panic("approachintersection: thread_fork failed: %s\n",
                              strerror(error));
                }
        }
        
        /*
         * wait until all other threads finish
         */

        while (thread_count() > 1)
                thread_yield();

        (void)nargs;
        (void)args;
        cv_destroy(stoplight_cv);
        lock_destroy(stoplight_lock);
        lock_destroy(car_direction_lock);
        cv_destroy(wait_for_all_threads_cv);
        lock_destroy(wait_for_all_threads_lock);
        cv_destroy(wait_for_north_left_turn_cv);
        lock_destroy(wait_for_north_left_turn_lock);
        cv_destroy(wait_for_south_left_turn_cv);
        lock_destroy(wait_for_south_left_turn_lock);
        cv_destroy(wait_for_east_left_turn_cv);
        lock_destroy(wait_for_east_left_turn_lock);
        cv_destroy(wait_for_west_left_turn_cv);
        lock_destroy(wait_for_west_left_turn_lock);
        lock_destroy(N_lock_start);
        lock_destroy(E_lock_start);
        lock_destroy(S_lock_start);
        lock_destroy(W_lock_start);
        lock_destroy(N_lock_leave);
        lock_destroy(E_lock_leave);
        lock_destroy(S_lock_leave);
        lock_destroy(W_lock_leave);
        lock_destroy(NE_lock);
        lock_destroy(NW_lock);
        lock_destroy(SW_lock);
        lock_destroy(SE_lock);
        kprintf("stoplight test done\n");
        return 0;
}

