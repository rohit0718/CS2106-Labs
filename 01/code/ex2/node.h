/*
    You can modify this file for your testing purpose but note
    that we will use our own copies of this file for testing
    during grading so any changes in this file will be overwritten
*/

typedef struct NODE {
    int data;
    struct NODE *next;
} node;

typedef struct {
    node *head;
} list;

void insert_node_at(list *lst, int index, int data);
void delete_node_at(list *lst, int index);
void rotate_list(list *lst, int offset);
void reverse_list(list *lst);
void reset_list(list *lst);
