今天在公司摸鱼的时候，想起之前没有看的线程池，然后赶紧把它捡起来。

## 介绍
**线程池**：有一堆已经创建好的线程，初始化时处于空闲态，当有新的任务进来的时候，分配一个线程去处理任务。然任务处理完之后，线程放回到线程池中。当线程池中的线程都在忙碌，此时新的任务将放到任务队列中等待空闲线程。

**为什么要有线程池**：不能一直开关线程，这样会给系统产生大量的消耗。线程是可重用的资源，不需要每次使用时都进行初始化，因为可以采用有限的线程处理无限的任务。

## 线程池实现
#### 1.封装条件变量
**为什么要使用条件变量**：死锁
死锁：
int n = 0;
消费者A：等待n > 0
生产者B：更改n, 使得n > 0

1.当消费者A进入临界区时，其他线程不能进入临界区，意味着生产者B没有机会去修改n，n的值一直为0，不满足消费者A继续执行的条件（n > 0），消费者A只能一直等待。
2.消费者进程A拿到互斥锁 --> 进入临界区 --> 发现共享资源 n 不满足继续执行的条件（n > 0） --> 等待 n > 0
3.消费者进程A占有互斥锁 --> 生产者进程B无法进入临界区 --> 无法修改 n 的值 --> 生产者B等待消费者A释放互斥锁
**解决死锁的方案就是采用条件变量。**

下面有详细的注释

```c
#ifndef _CONDITION_H_
#define _CONDITION_H_

// 线程池：有一堆已经创建好的线程，初始状态是都处于空闲状态，当有新的任务进来，从线程池中取出一个空闲的线程处理任务，然后当任务处理完之后，该线程会放回到线程池中
// 当线程池中的线程都在处理任务，没有空闲的线程。若有新的任务产生，只能等待线程池中有归还的线程

// 为什么要有线程池：不能一直开关线程，这样会给系统产生大量的消耗。线程是可重用的资源，不需要每次使用时都进行初始化，因为可以采用有限的线程处理无限的任务

#include <pthread.h>

// 封装一个互斥量和条件变量作为状态
typedef struct condition {
	// 互斥量 锁住变量
    pthread_mutex_t pmutex; // 在操作一个共享变量时，先用锁把变量锁住，然后再操作，操作完了之后再释放掉锁
    
    /*
    死锁：
    int n = 0;
    消费者A：等待n > 0
    生产者B：更改n, 使得n > 0
    当消费者A进入临界区时，其他线程不能进入临界区，意味着生产者B没有机会去修改n，n的值一直为0，不满足消费者A继续执行的条件（n > 0），消费者A只能一直等待。
    消费者进程A拿到互斥锁 --> 进入临界区 --> 发现共享资源 n 不满足继续执行的条件（n > 0） --> 等待 n > 0
    消费者进程A占有互斥锁 --> 生产者进程B无法进入临界区 --> 无法修改 n 的值 --> 生产者B等待消费者A释放互斥锁
    解决死锁的方案就是采用条件变量。
    */
    pthread_cond_t pcond;
} condition_t;

// 对状态的操作函数
int condition_init(condition_t *cond);
int condition_lock(condition_t *cond);
int condition_unlock(condition_t *cond);
int condition_wait(condition_t *cond);
int condition_timedwait(condition_t *cond, const struct timespec *abstime);
int condition_signal(condition_t* cond);
int condition_broadcast(condition_t *cond);
int condition_destroy(condition_t *cond);

#endif
```
```c
#include "condition.h"

//初始化
int condition_init(condition_t *cond) {
    int status;
    if((status = pthread_mutex_init(&cond->pmutex, NULL))) //初始化互斥锁
        return status;
    
    if((status = pthread_cond_init(&cond->pcond, NULL))) //初始化条件变量
        return status;
    
    return 0;
}

//加锁
int condition_lock(condition_t *cond) {
    return pthread_mutex_lock(&cond->pmutex); //拿到互斥锁
}

//解锁
int condition_unlock(condition_t *cond) { //释放互斥锁
    return pthread_mutex_unlock(&cond->pmutex);
}

//等待
int condition_wait(condition_t *cond) { //等待在条件变量上
    return pthread_cond_wait(&cond->pcond, &cond->pmutex);
}

//固定时间等待
int condition_timedwait(condition_t *cond, const struct timespec *abstime) {
    return pthread_cond_timedwait(&cond->pcond, &cond->pmutex, abstime);
}

//唤醒一个睡眠线程
int condition_signal(condition_t* cond) {
    return pthread_cond_signal(&cond->pcond); //通知等待在条件变量上的消费者
}

//唤醒所有睡眠线程
int condition_broadcast(condition_t *cond) {
    return pthread_cond_broadcast(&cond->pcond);
}

//释放
int condition_destroy(condition_t *cond) {
    int status;
    if ((status = pthread_mutex_destroy(&cond->pmutex))) //销毁互斥锁
        return status;
    
    if ((status = pthread_cond_destroy(&cond->pcond))) //销毁条件变量
        return status;
        
    return 0;
}
```

#### 2.任务、线程池的封装
任务：
run 执行的任务
arg 任务参数
next 值向下一个任务
线程池：
ready 操作时要锁住线程池
first 任务队列的第一个任务
last 任务队列的最后一个任务
counter 线程池中已有的线程数
idle 线程池中空闲线程数
max_threads 线程池中最大线程数
quit 是否退出的标志

```c
#ifndef _THREAD_POOL_H_
#define _THREAD_POOL_H_

// 线程池头文件
#include "condition.h"

// 封装线程池中的对象需要执行的任务对象
typedef struct task {
    void *(*run)(void *);     // 需要执行的任务
    void *arg;                // 参数
    struct task *next;        // 任务队列中下一个任务
} task_t;

// 线程池结构体
typedef struct threadpool {
    condition_t ready; // 状态量
    task_t *first;     // 任务队列中第一个任务
    task_t *last;      // 任务队列中最后一个任务
    int counter;       // 线程池中已有线程数
    int idle;          // 线程池中空闲线程数
    int max_threads;   // 线程池最大线程数
    int quit;          // 是否退出的标志
} threadpool_t;

// 线程池初始化
void threadpool_init(threadpool_t *pool, int threads);

// 往线程池中加入任务
void threadpool_add_task(threadpool_t *pool, void *(*run)(void *arg), void *arg);

// 摧毁线程池
void threadpool_destroy(threadpool_t *pool);

#endif
```

**1.线程池初始化**
```c
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
```
**2.增加一个任务到线程池**
```c
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
```

**3.重点：创建的线程**

每次操作线程池之前必须加锁！！
while (pool->first == NULL && !pool->quit)这个是用来判断超时的
然后在任务队列中取出第一个任务，执行任务处理函数。（由于执行任务可能耗时较多，这里需要释放锁）

```c
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
```
**4.线程池销毁**
```c
// 线程池销毁
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
```

代码转载于[https://www.cnblogs.com/yangang92/p/5485868.html](https://www.cnblogs.com/yangang92/p/5485868.html)

---
2020.5.29  15:17  深圳
