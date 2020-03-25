#include <iostream>
#include <cstdlib>
#include <pthread.h>
#include <unistd.h>
#include <cassert>
#include <cstring>
#include <csignal>
#include <cerrno>
#include "threadpool.h"

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

void* adjust_thread(void* threadpool);
bool is_thread_alive(pthread_t tid);
int threadpool_free(threadpool_t* pool);
void* threadpool_thread(void* threadpool);

threadpool_t* create_threadpool(int min_thr_num, int max_thr_num, int queue_max_size){
    int i;
    threadpool_t* pool = nullptr;
    do{
        if((pool = new threadpool_t) == nullptr){
            std::cout << "Failed to create thread pool." << std::endl;
            break;
        }
        pool->max_thr_num = max_thr_num;
        pool->min_thr_num = min_thr_num;
        pool->queue_max_size = queue_max_size;
        pool->queue_size = 0;
        pool->queue_front = 0;
        pool->queue_rear = 0;
        pool->shutdown = false;
        pool->live_thr_num = 0;
        pool->busy_thr_num = 0;
        pool->wait_exit_thr_num = 0;

        if((pool->threads = new pthread_t[max_thr_num]) == nullptr){
            std::cout << "Failed to malloc space for threads." << std::endl;
            break;
        }

        if((pool->task_queue = new threadpool_task_t[queue_max_size]) == nullptr){
            std::cout << "Failed to malloc space for task queue." << std::endl;
            break;
        }

        if(!pthread_mutex_init(&pool->lock, nullptr)
            || !pthread_mutex_init(&pool->thread_counter, nullptr)
            || !pthread_cond_init(&pool->queue_not_full, nullptr)
            || !pthread_cond_init(&pool->queue_not_empty, nullptr)){
            std::cout << "Failed to initialize locks or conds." << std::endl;
            break;
        }

        for(i = 0; i < min_thr_num; ++i){
            pthread_create(&(pool->threads[i]), nullptr, threadpool_thread, (void *)pool);
            std::cout << "Starts running a new tread." << std::endl;
        }
        pthread_create(&(pool->adjust_tid), nullptr, adjust_thread, (void *)pool);

        return pool;

    } while(false);

    threadpool_free(pool);

    return nullptr;
}

void* threadpool_thread(void* threadpool){
    auto* pool = (threadpool_t *)threadpool;
    threadpool_task_t task{};

    while(true){
        pthread_mutex_lock(&pool->lock);

        while((pool->queue_size == 0) && (!pool->shutdown)){
            std::cout << "Thread " << pthread_self() << " is waiting..." << std::endl;
            pthread_cond_wait(&pool->queue_not_empty, &pool->lock);

            if(pool->wait_exit_thr_num > 0){
                --pool->wait_exit_thr_num;

                if(pool->live_thr_num > pool->min_thr_num){
                    std::cout << "Thread " << pthread_self() << " is existing." << std::endl;

                    --pool->live_thr_num;
                    pthread_mutex_unlock(&pool->lock);
                    pthread_exit(nullptr);
                }
            }
        }

        if(pool->shutdown){
            pthread_mutex_unlock(&pool->lock);
            std::cout << "Thread " << pthread_self() << " is existing." << std::endl;
            pthread_exit(nullptr);
        }

        // Prepare for executing task
        // Get the function pointer and arguments from the head of task queue
        task.function = pool->task_queue[pool->queue_front].function;
        task.arg = pool->task_queue[pool->queue_front].arg;

        // Pop from current task queue
        pool->queue_front = (pool->queue_front + 1) % pool->queue_max_size;
        --pool->queue_size;

        // Inform that a new task from client can be added to the task queue
        pthread_cond_signal(&pool->queue_not_full);

        // Release the pool descriptor lock
        pthread_mutex_unlock(&pool->lock);

        // Start running the task
        std::cout << "Thread " << pthread_self() << " starts working" << std::endl;
        pthread_mutex_lock(&pool->thread_counter);
        ++pool->busy_thr_num;
        pthread_mutex_unlock(&pool->thread_counter);
        // Execute the callback function of task
        (*(task.function))(task.arg);

        // Task finishes
        std::cout << "Thread " << pthread_self() << " ends the task from client." << std::endl;
        pthread_mutex_lock(&pool->thread_counter);
        --pool->busy_thr_num;
        pthread_mutex_unlock(&pool->thread_counter);
    }

    pthread_exit(nullptr);
}

void * adjust_thread(void* threadpool){
    int i;
    auto* pool = (threadpool_t *)threadpool;
    while (!pool->shutdown){
        // Timer
        sleep(DEFAULT_TIME);

        pthread_mutex_lock(&pool->lock);
        int queue_size =  pool->queue_size;
        int live_thr_num = pool->live_thr_num;
        pthread_mutex_unlock(&pool->lock);

        pthread_mutex_lock(&pool->thread_counter);
        int busy_thr_num = pool->busy_thr_num;
        pthread_mutex_unlock(&pool->thread_counter);

        if(queue_size >= MIN_WAIT_TASK_NUM && live_thr_num < pool->max_thr_num){
            pthread_mutex_lock(&pool->lock);
            int add = 0;

            for(i = 0; i < pool->max_thr_num && add < DEFAULT_THREAD_VARY
                        && pool->live_thr_num < pool->max_thr_num; ++i){
                if(pool->threads[i] == 0 || !is_thread_alive(pool->threads[i])){
                    pthread_create(&pool->threads[i], nullptr, threadpool_thread, (void *)pool);
                    ++add;
                    ++pool->live_thr_num;
                }
            }
            pthread_mutex_unlock(&pool->lock);
        }

        if((busy_thr_num * 2) < live_thr_num && live_thr_num > pool->min_thr_num){
            pthread_mutex_lock(&pool->lock);
            pool->wait_exit_thr_num = DEFAULT_THREAD_VARY;
            pthread_mutex_unlock(&pool->lock);

            for(i = 0; i < DEFAULT_THREAD_VARY; ++i){
                pthread_cond_signal(&pool->queue_not_empty);
            }
        }
    }
    return nullptr;
}

int threadpool_destroy(threadpool_t *pool){
    int i;
    if (pool == nullptr) return -1;
    pool->shutdown = true;

    pthread_join(pool->adjust_tid, nullptr);

    for(i = 0; i < pool->live_thr_num; ++i){
        pthread_cond_broadcast(&pool->queue_not_empty);
    }

    for(i = 0; i < pool->live_thr_num; ++i){
        pthread_join(pool->threads[i], nullptr);
    }

    threadpool_free(pool);

    return 0;
}

int threadpool_free(threadpool_t* pool){
    if (pool == nullptr) return -1;
    if(pool->task_queue) delete [] (pool->task_queue);
    if(pool->threads){
        delete [] (pool->threads);
        pthread_mutex_lock(&pool->lock);
        pthread_mutex_destroy(&pool->lock);
        pthread_mutex_lock(&pool->thread_counter);
        pthread_mutex_destroy(&pool->thread_counter);
        pthread_cond_destroy(&pool->queue_not_empty);
        pthread_cond_destroy(&pool->queue_not_full);
    }
    delete pool; pool = nullptr;

    return 0;
}

bool is_thread_alive(pthread_t tid){
    int kill_rc = pthread_kill(tid, 0);
    return kill_rc != ESRCH;
}

// Add task
int threadpool_add(threadpool_t* pool, void* (*function)(void * arg), void* arg){
    pthread_mutex_lock(&pool->lock);

    while((pool->queue_size == pool->queue_max_size) && (!pool->shutdown)){
        pthread_cond_wait(&pool->queue_not_full, &pool->lock);
    }

    if(pool->shutdown){
        pthread_mutex_unlock(&pool->lock);
    }

    // Free up the target element of the task queue
    if(pool->task_queue[pool->queue_rear].arg != nullptr){
        free(pool->task_queue[pool->queue_rear].arg);
        pool->task_queue[pool->queue_rear].arg = nullptr;
    }

    // Append the new task to the rear of the circular queue
    pool->task_queue[pool->queue_rear].function = function;
    pool->task_queue[pool->queue_rear].arg = arg;
    pool->queue_rear = (pool->queue_rear + 1) % pool->queue_max_size;
    ++pool->queue_size;

    pthread_cond_signal(&pool->queue_not_empty);
    pthread_mutex_unlock(&pool->lock);

    return 0;
}

void* process(void *arg)
{
    printf("thread 0x %lx working on task %d\n ", (unsigned long)pthread_self(), *(int *)arg);
    sleep(1);
    printf("task %d is end\n",*(int *)arg);

    return nullptr;
}

int main() {
    /*threadpool_t *threadpool_create(int min_thr_num, int max_thr_num, int queue_max_size);*/

    threadpool_t *thp = create_threadpool(3, 100, 100);/*创建线程池，池里最小3个线程，最大100，队列最大100*/
    printf("pool inited");

    //int *num = (int *)malloc(sizeof(int)*20);
    int num[20], i;
    for (i = 0; i < 20; i++) {
        num[i]=i;
        printf("add task %d\n",i);
        threadpool_add(thp, process, (void*)&num[i]);     /* 向线程池中添加任务 */
    }
    sleep(10);                                          /* 等子线程完成任务 */
    threadpool_destroy(thp);

    return 0;
}
