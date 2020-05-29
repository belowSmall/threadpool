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