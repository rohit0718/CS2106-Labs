#include "restaurant.h"
#include <semaphore.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_GRPS 1000
#define MAX_SZ 5

typedef struct table {
    int id;
    bool is_avail;
    struct table* next;
} table_t;

typedef struct group {
    // int id;
    int queue_num;
    struct group* next;
} group_t;

sem_t global_lock;
sem_t my_lock[MAX_GRPS];
table_t* tables[MAX_SZ];
group_t* waiting[MAX_SZ];
group_t* to_free; // chain of waiting elements to free later
int queue_num = 0;

void add_to_tables(table_t* table, table_t* new_table) {
    while (table->next != NULL) {
        table = table->next;
    }
    table->next = new_table;
}

void add_to_groups(group_t* group, group_t* new_waiting_group) {
    while (group->next != NULL) {
        group = group->next;
    }
    group->next = new_waiting_group;
}

void add_to_free(group_t* to_free, group_t* new_to_free) {
    while (to_free->next != NULL) {
        to_free = to_free->next;
    }
    to_free->next = new_to_free;
}

// sets table with table_id = id as available
void release_table(int id, int num_ppl) {
    table_t* table = tables[num_ppl];
    while (table != NULL) {
        if (table->id == id) {
            table->is_avail = true;
            return;
        }
        table = table->next;
    }
}

// returns the size of table (0-indexed) with id = table_id
// table_id is guaranteed to exist
int get_size(int table_id) {
    for (int sz = 0; sz < MAX_SZ; ++sz) {
        table_t* table = tables[sz];
        while (table != NULL) {
            if (table->id == table_id) {
                return sz;
            }
            table = table->next;
        }
    }
    return -1;
}

// returns the queue number of next group to enter table_id, if any
// also helps to remove it from queue and free memory
// returns -1 if no such group is found
int has_waiting(int table_id) {
    int sz = get_size(table_id);
    // for ex4, grps must have size exactly sz
    if (waiting[sz] == NULL) {
        return -1;
    }
    // DEBUG
    printf("DEBUG WAITING LIST with size %d: ", sz);
    group_t* tmp = waiting[sz];
    while (tmp) {
        printf("%d, ", tmp->queue_num);
        tmp = tmp->next;
    }
    printf("\n");

    group_t* next_in_queue = waiting[sz];
    int next_num = next_in_queue->queue_num;
    waiting[sz] = next_in_queue->next;
    next_in_queue->next = NULL;
    if (to_free == NULL) {
        to_free = next_in_queue;
    } else {
        add_to_free(to_free, next_in_queue);
    }
    return next_num;
}

// returns pointer of table that can accomodate num_ppl
// returns NULL if no table is found
table_t* has_table(int num_ppl) {
    // for ex4, tables must be exactly num_ppl sized
    table_t* table = tables[num_ppl];
    while (table != NULL) {
        if (table->is_avail == true) {
            return table;
        }
        table = table->next;
    }
    return NULL;
}

void restaurant_init(int num_tables[5]) {
    // init semaphores
    sem_init(&global_lock, 0, 1);
    for (int i = 0; i < MAX_GRPS; ++i) {
        sem_init(&my_lock[i], 0, 1); // all unlocked at start
    }
    // init tables array
    int table_id = 0;
    for (int i = 0; i < MAX_SZ; ++i) {
        for (int j = 0; j < num_tables[i]; ++j) {
            // create new struct
            table_t* new_table = (table_t*) malloc(sizeof(table_t));
            new_table->id = table_id++;
            new_table->is_avail = true;
            new_table->next = NULL;
            // no table_t yet
            if (tables[i] == NULL) {
                tables[i] = new_table;
                continue;
            }
            add_to_tables(tables[i], new_table);
        }
    }
}

void restaurant_destroy(void) {
    // remove semaphores
    sem_destroy(&global_lock);
    for (int i = 0; i < MAX_GRPS; ++i) {
        sem_destroy(&my_lock[i]);
    }
    // remove tables
    for (int i = 0; i < MAX_SZ; ++i) {
        table_t* table = tables[i];
        while (table != NULL) {
            table_t* tmp = table;
            table = table->next;
            free(tmp);
        }
    }
    // remove waiting
    while (to_free) {
        group_t* tmp = to_free;
        to_free = to_free->next;
        free(tmp);
    }
}

// returns the id of the table group sits at
int request_for_table(group_state *state, int num_ppl, int grp_id) {
    num_ppl--;
    int my_num = -1;
    int table_id = -1;
    /* bool has_enqueued = false; */

    sem_wait(&global_lock);
    // get queue number
    my_num = queue_num++;
    on_enqueue(); // group has locked global_lock and gotten a number!
    while (1) {
        // wait on my own number
        sem_wait(&my_lock[my_num]);
        // is there a table for me?
        table_t* my_table = has_table(num_ppl);
        if (my_table != NULL) {
            // take table
            my_table->is_avail = false;
            table_id = my_table->id;
            // update state
            state->table_id = table_id;
            state->num_ppl = num_ppl; // this is 1 lesser than the true num_ppl to make it 0 indexed
            /* // if on_enqueue hasnt been called, call it */
            /* if (!has_enqueued) { */
            /*     on_enqueue(); */
            /* } */
            break;
        }
        // no table for me yet. joining waiting queue
        /// add myself to waiting array
        printf("[GrpId:%d,QueueNum:%d] added to queue\n", grp_id, my_num);
        group_t* new_waiting_group = (group_t*) malloc(sizeof(group_t));
        new_waiting_group->queue_num = my_num;
        // no group_t yet
        if (waiting[num_ppl] == NULL) {
            waiting[num_ppl] = new_waiting_group;
        } else {
            add_to_groups(waiting[num_ppl], new_waiting_group);
        }
        /* on_enqueue(); // group has been added to the queue! */
        /* has_enqueued = true; */
        /// unlock lock to process the next enter or leave
        sem_post(&global_lock);
    }
    sem_post(&global_lock);

    return table_id;
}

void leave_table(group_state *state) {
    sem_wait(&global_lock);
    release_table(state->table_id, state->num_ppl);
    int next_in_queue = has_waiting(state->table_id);
    if (next_in_queue == -1) {
        // no one in queue can fit in this table / queue empty
        sem_post(&global_lock);
        return;
    }
    // signal next_in_queue to allow it take this current table
    sem_post(&my_lock[next_in_queue]);
}

/* typedef struct { */
/*     int id; */
/*     int seatsLeft; */
/* } table; */

/*

    seats_left[sum(num_tables)] = { 1,1,2,2,2,... }
    semaph turn[sum(num_tables)] = { 1,0,0,0,0,... }

    mutex_lock
    mutex_unlock



    availIdx[5] = { 0,0,0,0,0 } // represents the next id avail
    curTableIdx[5] = { -1,-1,-1,-1,-1 } // represents the highest idx at table
        if idx == curTableIdx + 1, then we increment the curTableIdx and enter table
    int seatsLeft[5] = { init0,init1,... }


    [[(1,1),(2,1)],[(3,1)],[(4,1),(5,1)],[(6,1)],[(7,1),(8,1)]]
    
    mutex_lock
    take availIdx[num_ppl], increment availIdx[num_ppl]

    if seatsLeft[num_ppl] > 0:
        // take a seat
        seatsLeft[num_ppl]--
        mutex_unlock
    else:
        // i need to start waiting
        mutex_unlock
        wait(hasSpace[num_ppl])


    wait(idx == curTableIdx[num_ppl] + 1)
    // take a table
    seatsLeft[num_ppl]--


    leave_table:
        curTableIdx[num_ppl]++


*/



/*

    queue_numbers[5] = { 0,0,0,0,0 }
    tables_left[5] = { 0,0,0,0,0 }

    enter:
        mutex_lock
        seats_left[num_ppl]--
        if seats_left[num_ppl] > 0:
            unlock_next_in_queue
        mutex_unlock

    leave:
        mutex_lock
        seats_left[num_ppl]++
        if seats_left[num_ppl] == 1:
            unlock_next_in_queue
        mutex_unlock

*/


/*

    semaph lock[1000][5] = { 1,0,0,... } // 5 of these for each of the tables
    int cur_queue[5] = { 0,0,0,0,0 } // represents next queue number allowed to get table

    int size_left[5] = { init0,init1,... }


    // get a queue number
    int my_num;
    wait_mutex
    my_num = cur_queue[num_ppl]
    cur_queue[num_ppl]++
    signal_mutex

    // wait for my turn
    wait(lock[num_ppl][my_num])
    // loop through the size_left array and see if a match is found
    if a match is found:
        // we can 
    else:
        // 





    cur_num = 1 // global queue num
    typedef struct {
        int id;
        int seats;
    } table;
    table* states;
    int waiting_for_turn[5][1000] = { 0 }

ENTER
    while (1) {
        wait my_num
        if no_space:
            add my_num to waiting_for_turn
            signal next_num
            continue // wait on myself
        if has_waiting:
            
        res_id = table_with_space
        signal next_num
    }

LEAVE
    wait lock
    release table with id table_id
    wake up a waiting thread with num_ppl <= table_size (if any) with signal

*/

