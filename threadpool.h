//
// Created by Xiaoru_Zhu on 2020/3/20.
//

#ifndef THREADPOOLTEMPLATE_THREADPOOL_H
#define THREADPOOLTEMPLATE_THREADPOOL_H

#include <iostream>
#include <cstdlib>
#include <pthread.h>
#include <unistd.h>
#include <cassert>
#include <cstring>
#include <csignal>
#include <cerrno>

//typedef struct threadpool_t threadpool_t;

const int DEFAULT_TIME = 10; // check status of the pool every 10 seconds
const int MIN_WAIT_TASK_NUM = 10; //
const int DEFAULT_THREAD_VARY = 10;

typedef struct {
    void* (*function) (void *);
    void* arg;
} threadpool_task_t;

// thread pool descriptor
typedef struct {
    pthread_mutex_t lock;
    pthread_mutex_t thread_counter;
    pthread_cond_t queue_not_full;
    pthread_cond_t queue_not_empty;

    pthread_t* threads;
    pthread_t adjust_tid;
    threadpool_task_t* task_queue;

    int min_thr_num;
    int max_thr_num;
    int live_thr_num;
    int busy_thr_num;
    int wait_exit_thr_num;

    int queue_front;
    int queue_rear;
    int queue_size;
    int queue_max_size;

    bool shutdown;
} threadpool_t;


/**
 * @function threadpool_create
 * @descCreates a threadpool_t object.
 * @param thr_num  thread num
 * @param max_thr_num  max thread size
 * @param queue_max_size   size of the queue.
 * @return a newly created thread pool or NULL
 */
threadpool_t *threadpool_create(int min_thr_num, int max_thr_num, int queue_max_size);

/**
 * @function threadpool_add
 * @desc add a new task in the queue of a thread pool
 * @param pool     Thread pool to which add the task.
 * @param function Pointer to the function that will perform the task.
 * @param argument Argument to be passed to the function.
 * @return 0 if all goes well,else -1
 */
int threadpool_add(threadpool_t *pool, void*(*function)(void *arg), void *arg);

/**
 * @function threadpool_destroy
 * @desc Stops and destroys a thread pool.
 * @param pool  Thread pool to destroy.
 * @return 0 if destory success else -1
 */
int threadpool_destroy(threadpool_t *pool);

/**
 * @desc get the thread num
 * @pool pool threadpool
 * @return # of the thread
 */
int threadpool_all_threadnum(threadpool_t *pool);

/**
 * desc get the busy thread num
 * @param pool threadpool
 * return # of the busy thread
 */
int threadpool_busy_threadnum(threadpool_t *pool);

int threadpool_free(threadpool_t* pool);

bool is_thread_alive(pthread_t tid);

void* threadpool_thread(void* threadpool);

void* adjust_thread(void* threadpool);

#endif //THREADPOOLTEMPLATE_THREADPOOL_H
