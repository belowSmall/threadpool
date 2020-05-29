#include "threadpool.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>

// 创建的线程执行
void *thread_routine(void *arg) {
    struct timespec abstime;
    int timeout;
    printf("thread %d is starting\n", (int)pthread_self());
    threadpool_t *pool = (threadpool_t *)arg;
    while (1) {
        timeout = 0;
        condition_lock(&pool->ready);                                 // 访问线程池之前需要加锁
        pool->idle++; //空闲
        while (pool->first == NULL && !pool->quit) {                  // 等待队列有任务到来 或者 收到线程池销毁通知，否则线程阻塞等待
            printf("thread %d is waiting\n", (int)pthread_self());
            clock_gettime(CLOCK_REALTIME, &abstime);                  // 获取从当前时间，并加上等待时间，设置进程的超时睡眠时间
            abstime.tv_sec += 2;                                      // 2秒超时
            int status = condition_timedwait(&pool->ready, &abstime); // 该函数会解锁，允许其他线程访问，当被唤醒时，加锁
            if (status == ETIMEDOUT) {
                printf("thread %d wait timed out\n", (int)pthread_self());
                timeout = 1;
                break;
            }
        }

        pool->idle--;
        if (pool->first != NULL) {
            task_t *t = pool->first;
            pool->first = t->next;          // 取出等待队列最前的任务，移除任务，并执行任务
            condition_unlock(&pool->ready); // 由于任务执行需要消耗时间，先解锁让其他线程访问线程池
            t->run(t->arg);                 // 执行任务
            free(t);                        // 执行完任务释放内存
            condition_lock(&pool->ready);   // 重新加锁
        }

        // 退出线程池 执行threadpool_destroy才会到这里
        if (pool->quit && pool->first == NULL) {
            pool->counter--;                // 当前工作的线程数-1
            if (pool->counter == 0) {       // 若线程池中没有线程，通知等待线程（主线程）全部任务已经完成
                condition_signal(&pool->ready);
            }
            condition_unlock(&pool->ready);
            break;
        }

        // 超时，跳出销毁线程 只有当condition_timedwait超时时，才会到这里
        if (timeout == 1) {
            pool->counter--;                // 当前工作的线程数-1
            condition_unlock(&pool->ready);
            break;
        }

        condition_unlock(&pool->ready);
    }
    
    printf("thread %d is exiting\n", (int)pthread_self());
    return NULL;
}

/*
typedef struct condition {
    pthread_mutex_t pmutex;
    pthread_cond_t pcond;
} condition_t;
*/

// @param1 线程池 @param2 最大线程数
void threadpool_init(threadpool_t *pool, int threads) {
    condition_init(&pool->ready); // type condition_t
    pool->first = NULL;           // 任务队列中第一个任务
    pool->last = NULL;            // 任务队列中最后一个任务
    pool->counter = 0;            // 线程池中已有线程数(最大值为pool->max_threads)
    pool->idle = 0;               // 线程池中空闲线程数
    pool->max_threads = threads;  // 线程池最大线程数
    pool->quit = 0;               // 是否退出的标志
}

// 增加一个任务到线程池
// @param2 任务  @param3 任务参数
void threadpool_add_task(threadpool_t *pool, void *(*run)(void *), void *arg) {
    // 产生一个新的任务
    task_t *newtask = (task_t *)malloc(sizeof(task_t));
    newtask->run = run;
    newtask->arg = arg;
    newtask->next = NULL;          // 新加的任务放在队列尾端

    condition_lock(&pool->ready);  // 线程池的状态被多个线程共享，操作前需要加锁

    if (pool->first == NULL) {     // 第一个任务加入
        pool->first = newtask;
    } else {
        pool->last->next = newtask;
    }
    pool->last = newtask;          // 队列尾指向新加入的线程

    // 线程池中有线程空闲，唤醒
    if (pool->idle > 0) {
        condition_signal(&pool->ready);
    } else if(pool->counter < pool->max_threads) { // 当前线程池中线程个数没有达到设定的最大值，创建一个新的线性
        pthread_t tid;
        pthread_create(&tid, NULL, thread_routine, pool); // 创建一个线程
        pool->counter++;
    }
    //结束，访问
    condition_unlock(&pool->ready);
}

//线程池销毁
void threadpool_destroy(threadpool_t *pool) {
    if (pool->quit) return;        // 如果已经调用销毁，直接返回

    condition_lock(&pool->ready);  // 加锁
    pool->quit = 1;                // 设置销毁标记为1
    if (pool->counter > 0) {       // 线程池中工作线程个数大于0
        if (pool->idle > 0) {      // 对于等待的线程(空闲)，发送信号唤醒
            condition_broadcast(&pool->ready);
        }
        while (pool->counter) {    // 正在执行任务的线程，等待他们结束任务
            condition_wait(&pool->ready);
        }
    }
    condition_unlock(&pool->ready);
    condition_destroy(&pool->ready);
}