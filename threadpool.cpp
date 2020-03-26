
#include "threadpool.h"

threadpool_descriptor* create_threadpool(int min_thr_num, int max_thr_num, int queue_max_size){
    int i;
    threadpool_descriptor* pool = nullptr;

    if((pool = new threadpool_descriptor) == nullptr){
        std::cout << "Failed to create thread pool." << std::endl;
        free_threadpool(pool);
        return nullptr;
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
        free_threadpool(pool);
        return nullptr;
    }

    if((pool->task_queue = new threadpool_task_t[queue_max_size]) == nullptr){
        std::cout << "Failed to malloc space for task queue." << std::endl;
        free_threadpool(pool);
        return nullptr;
    }

    if(!pthread_mutex_init(&pool->lock, nullptr)
        || !pthread_mutex_init(&pool->thread_counter, nullptr)
        || !pthread_cond_init(&pool->queue_not_full, nullptr)
        || !pthread_cond_init(&pool->queue_not_empty, nullptr)){
        std::cout << "Failed to initialize locks or conds." << std::endl;
        free_threadpool(pool);
        return nullptr;
    }

    for(i = 0; i < min_thr_num; ++i){
        pthread_create(&(pool->threads[i]), nullptr, working_thread, (void *) pool);
        std::cout << "Starts running a new tread." << std::endl;
    }

    pthread_create(&(pool->adjust_tid), nullptr, managing_thread, (void *) pool);

    return pool;

}

// Basic task for every single thread in the pool
void* working_thread(void* threadpool){
    auto* pool = (threadpool_descriptor *)threadpool;
    threadpool_task_t task{};

    while(true){
        pthread_mutex_lock(&pool->lock);

        // If the queue size is 0, wait
        while((pool->queue_size == 0) && (!pool->shutdown)){
            std::cout << "Thread " << pthread_self() << " is waiting..." << std::endl;

            // Wait for the releasing signal of the conditional lock of the adding_task action
            // or the exit commands from the management thread
            pthread_cond_wait(&pool->queue_not_empty, &pool->lock);

            // Current thread should exit when the exit waiting thread number > 0
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

        // Check if the thread pool will be shutdown
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

// For managing purpose, dynamically adjusting the total thread number according to the current busy level of the pool
// An independent thread
void* managing_thread(void* threadpool){
    int i;
    auto* pool = (threadpool_descriptor *)threadpool;

    while (!pool->shutdown){

        // Timer: check the status of the thread pool every 10 seconds
        sleep(DEFAULT_TIME);

        // Get status info from the thread pool
        pthread_mutex_lock(&pool->lock);
        int queue_size =  pool->queue_size;
        int live_thr_num = pool->live_thr_num;
        pthread_mutex_unlock(&pool->lock);

        pthread_mutex_lock(&pool->thread_counter);
        int busy_thr_num = pool->busy_thr_num;
        pthread_mutex_unlock(&pool->thread_counter);

        // If it is busy, create new threads for the thread pool
        if(queue_size >= MIN_WAIT_TASK_NUM && live_thr_num < pool->max_thr_num){
            pthread_mutex_lock(&pool->lock);
            int add = 0;

            for(i = 0; i < pool->max_thr_num && add < DEFAULT_THREAD_CHANGE_STEP
                        && pool->live_thr_num < pool->max_thr_num; ++i){
                if(pool->threads[i] == 0 || !is_thread_alive(pool->threads[i])){
                    pthread_create(&pool->threads[i], nullptr, working_thread, (void *) pool);
                    ++add;
                    ++pool->live_thr_num;
                }
            }
            pthread_mutex_unlock(&pool->lock);
        }

        // If there are too many idle threads, destroy some of them
        if((busy_thr_num * 2) < live_thr_num && live_thr_num > pool->min_thr_num){
            pthread_mutex_lock(&pool->lock);
            pool->wait_exit_thr_num = DEFAULT_THREAD_CHANGE_STEP;
            pthread_mutex_unlock(&pool->lock);

            for(i = 0; i < DEFAULT_THREAD_CHANGE_STEP; ++i){
                pthread_cond_signal(&pool->queue_not_empty);
            }
        }
    }

    return nullptr;
}

// Destroy the whole thread pool
int destroy_threadpool(threadpool_descriptor *pool){
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

    free_threadpool(pool);

    return 0;
}

// Release the space for the whole thread pool
int free_threadpool(threadpool_descriptor* pool){
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

// Check if the target thread still works
bool is_thread_alive(pthread_t tid){
    int kill_rc = pthread_kill(tid, 0);
    return kill_rc != ESRCH;
}

// Add a real task into the working queue from which the threads will get tasks
int threadpool_add_task(threadpool_descriptor* pool, void* (*function)(void * arg), void* arg){

    pthread_mutex_lock(&pool->lock);

    // Wait for the signal of the queue_not_full if the queue size reaches the max
    while((pool->queue_size == pool->queue_max_size) && (!pool->shutdown)){
        pthread_cond_wait(&pool->queue_not_full, &pool->lock);
    }

    // Check if the thread pool is going to be shutdown
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

    // Release the locks
    pthread_cond_signal(&pool->queue_not_empty);
    pthread_mutex_unlock(&pool->lock);

    return 0;
}


int threadpool_all_threadnum(threadpool_descriptor *pool)
{
    int all_threadnum = -1;
    pthread_mutex_lock(&(pool->lock));
    all_threadnum = pool->live_thr_num;
    pthread_mutex_unlock(&(pool->lock));
    return all_threadnum;
}

int threadpool_busy_threadnum(threadpool_descriptor *pool)
{
    int busy_threadnum = -1;
    pthread_mutex_lock(&(pool->thread_counter));
    busy_threadnum = pool->busy_thr_num;
    pthread_mutex_unlock(&(pool->thread_counter));
    return busy_threadnum;
}

// Test instance
void* test_process(void *arg)
{
    printf("thread 0x %lx working on task %d\n ", (unsigned long)pthread_self(), *(int *)arg);
    sleep(1);
    printf("task %d is end\n",*(int *)arg);

    return nullptr;
}

int main() {
    /*threadpool_descriptor *threadpool_create(int min_thr_num, int max_thr_num, int queue_max_size);*/

    threadpool_descriptor *thp = create_threadpool(3, 100, 100);/*创建线程池，池里最小3个线程，最大100，队列最大100*/
    printf("pool inited");

    //int *num = (int *)malloc(sizeof(int)*20);
    int num[20], i;
    for (i = 0; i < 20; i++) {
        num[i]=i;
        printf("add task %d\n",i);
        threadpool_add_task(thp, test_process, (void *) &num[i]);     /* 向线程池中添加任务 */
    }
    sleep(10);                                          /* 等子线程完成任务 */
    destroy_threadpool(thp);

    return 0;
}
