#include "packer.h"
#include <semaphore.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_BALL 3
#define N 2

typedef struct {
    int* ids;
    int size; // release all if size == N
} ball;

ball balls[MAX_BALL];
sem_t ball_locks[MAX_BALL];
sem_t increment_lock;
sem_t is_printing;


/*
 *      int thread_num[MAX_NUM] = { 0,0,0 };
 *      int* thread_ids[MAX_NUM];
 *      mutex(1)[MAX_NUM], semaph(0)[MAX_NUM], isPrinting(1)[MAX_NUM]
 *
 *      // below, each semaphore is the [id] semaphore
 *
 * */

void packer_init(void) {
    // init semaphores
    sem_init(&increment_lock, 0, 1);
    sem_init(&is_printing, 0, 1);
    for (int i = 0; i < MAX_BALL; ++i) {
        sem_init(&ball_locks[i], 0, 0);
    }
    // init ball id sizes
    for (int i = 0; i < MAX_BALL; ++i) {
        balls[i].ids = (int*) malloc(N * sizeof(int));
    }
}

void packer_destroy(void) {
    // destroy semaphores
    sem_destroy(&increment_lock);
    sem_destroy(&is_printing);
    for (int i = 0; i < MAX_BALL; ++i) {
        sem_destroy(&ball_locks[i]);
    }
    // free ball ids
    for (int i = 0; i < MAX_BALL; ++i) {
        free(balls[i].ids);
    }
}

int pack_ball(int colour, int id) {
    colour--;

    sem_wait(&increment_lock);

    sem_wait(&is_printing);
    sem_post(&is_printing);


    int cur_size = balls[colour].size;
    balls[colour].ids[cur_size] = id;
    balls[colour].size++;
 
    if (balls[colour].size == N) {
        sem_wait(&is_printing);
        sem_post(&ball_locks[colour]);
    }

    sem_post(&increment_lock);

    int res_id = -1;
    sem_wait(&ball_locks[colour]);
    // res_id = id in thread_ids[id] where thread_ids[id] != id
    for (int i = 0; i < N; ++i) {
        if (balls[colour].ids[i] != id) {
            res_id = balls[colour].ids[i];
            break;
        }
    }
    balls[colour].size--;
    if (balls[colour].size == 0) {
        sem_post(&is_printing);
    } else {
        sem_post(&ball_locks[colour]);
    }
 
    return res_id;
}

