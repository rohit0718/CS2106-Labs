/*************************************
* Lab 1 Exercise 2
* Name: Rohit Rajesh Bhat
* Student No: A0214231Y
* Lab Group: 06
*************************************/

#include "node.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Add in your implementation below to the respective functions
// Feel free to add any headers you deem fit (although you do not need to)
int get_len(node *cur_node) {
    node *head = cur_node;
    int len = 1;
    while (cur_node->next != head) {
        cur_node = cur_node->next;
        len++;
    }
    return len;
}

node *get_node(node *node, int index) {
    while (index) {
        node = node->next;
        index--;
    }
    return node;
}

// Inserts a new node with data value at index (counting from head
// starting at 0).
// Note: index is guaranteed to be valid.
void insert_node_at(list *lst, int index, int data) {
    node *new_node = (node *) malloc(sizeof(node));
    new_node->data = data;
    if (!lst->head) {
        lst->head = new_node;
        lst->head->next = new_node;
        return;
    }
    int len = get_len(lst->head); // ideally this shd be in the struct.
    int prev_node_idx = ((index - 1) + len) % len;
    int next_node_idx = index % len;
    node *prev_node = get_node(lst->head, prev_node_idx);
    node *next_node = get_node(lst->head, next_node_idx);
    prev_node->next = new_node;
    new_node->next = next_node;
    // if new_node is first index, update head
    if (!index) {
        lst->head = new_node;
    }
}

// Deletes node at index (counting from head starting from 0).
// Note: index is guarenteed to be valid.
void delete_node_at(list *lst, int index) {
    if (!lst->head) {
        return;
    }
    int len = get_len(lst->head); // ideally this shd be in the struct.
    int prev_node_idx = ((index - 1) + len) % len;
    int next_node_idx = (index + 1) % len;
    node *prev_node = get_node(lst->head, prev_node_idx);
    node *next_node = get_node(lst->head, next_node_idx);
    node *del_node = prev_node->next;
    prev_node->next = next_node;
    // if index is 0, we need to update the head
    // set head to NULL if its the only node, or to
    // next_node if len is > 1
    if (!index) {
        lst->head = (len == 1) ? NULL : next_node;
    }
    free(del_node);
}

// Rotates list by the given offset.
// Note: offset is guarenteed to be non-negative.
void rotate_list(list *lst, int offset) {
    if (!lst->head) {
        return;
    }
    node *new_head = lst->head;
    while (offset) {
        new_head = new_head->next;
        offset--;
    }
    lst->head = new_head;
}

// Reverses the list, with the original "tail" node
// becoming the new head node.
void reverse_list(list *lst) {
    if (!lst->head) {
        return;
    }
    node *prev_node = lst->head;
    node *next_node = prev_node->next;
    while (next_node != lst->head) {
        node *tmp = next_node->next;
        next_node->next = prev_node;
        prev_node = next_node;
        next_node = tmp;
    }
    next_node->next = prev_node;
    lst->head = prev_node;
}

// Resets list to an empty state (no nodes) and frees
// any allocated memory in the process
void reset_list(list *lst) {
    if (!lst->head) {
        return;
    }
    node *cur_node = lst->head->next;
    while (cur_node != lst->head) {
        node *tmp = cur_node->next;
        free(cur_node);
        cur_node = tmp;
    }
    free(lst->head);
    lst->head = NULL;
}

