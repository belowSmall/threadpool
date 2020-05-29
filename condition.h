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