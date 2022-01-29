#ifndef RESTAURANT_H
#define RESTAURANT_H

// You may add fields to this struct, but do not modify the name of this struct:
typedef struct {
    int table_id;
} group_state;


// DO NOT MODIFY THE FUNCTION DECLARATIONS BELOW THIS LINE

// Initialization function called once when the program starts.
// num_tables[i] == number of tables that have (i+1) seats.
// Table ids 0 to (num_tables[0]-1) are tables with 1 seat.
// Table ids num_tables[0] to (num_tables[1]-1) are tables with 2 seats.
// etc.
void restaurant_init(int num_tables[5]);

// Initialization function called once when the program ends.
void restaurant_destroy(void);

// This function is called by the main driver when a new group
// queues up at your restaurant.  This function should block
// until there is an available table, and return the table id
// that this group should sit at.
int request_for_table(group_state *state, int num_people);

// **This function is implemented for you in the main driver.**
// request_for_table() should call this function exactly once,
// when the group has been placed into the queue, but _before_ blocking.
// Your implementation must guarantee that, given two groups (i.e. threads), A and B:
// - If A calls on_enqueue() before B calls request_for_table(),
//   then A must queue up before B in the queue.
// (Note: When we talk about the time a group calls a function,
// we mean the time where control flow _enters_ the function.)
void on_enqueue(void);

// This function is called by the main driver when a group that is
// currently seated leaves the restaurant.  The pointer `state`
// points to the same object given to `request_for_table()`,
// so you may use this object to retrieve any information you require.
void leave_table(group_state *state);

#endif
