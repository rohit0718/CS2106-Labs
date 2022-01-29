/*
    You can modify this file for your testing purpose but note
    that we will use our own copies of this file for testing
    during grading so any changes in this file will be overwritten
*/

// General purpose standard C lib
#include <stdio.h>   // stdio includes printf
#include <stdlib.h>  // stdlib includes malloc() and free()

// User-defined header files
#include "node.h"

// Macros
#define PRINT_LIST 0
#define INSERT_AT 1
#define DELETE_AT 2
#define ROTATE_LIST 3
#define REVERSE_LIST 4
#define RESET_LIST 5

void run_instruction(list *lst, int instr);
void print_list(list *lst);

int main() {
    list *lst = (list *)malloc(sizeof(list));
    lst->head = NULL;

    int instr;
    while (scanf("%d", &instr) == 1) {
        run_instruction(lst, instr);
    }

    reset_list(lst);
    free(lst);
}

// Takes an instruction enum and runs the corresponding function
// We assume input always has the right format (no input validation on runner)
void run_instruction(list *lst, int instr) {
    int index, data, offset;
    switch (instr) {
        case PRINT_LIST:
            print_list(lst);
            break;
        case INSERT_AT:
            scanf("%d %d", &index, &data);
            insert_node_at(lst, index, data);
            break;
        case DELETE_AT:
            scanf("%d", &index);
            delete_node_at(lst, index);
            break;
        case ROTATE_LIST:
            scanf("%d", &offset);
            rotate_list(lst, offset);
            break;
        case REVERSE_LIST:
            reverse_list(lst);
            break;
        case RESET_LIST:
            reset_list(lst);
    }
}

// Prints out the whole list in a single line
void print_list(list *lst) {
    if (lst->head == NULL) {
        printf("[ ]\n");
        return;
    }

    printf("[ ");
    node *curr = lst->head;
    do {
        printf("%d ", curr->data);
        curr = curr->next;
    } while (curr != lst->head);
    printf("]\n");
}
