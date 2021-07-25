#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>


/*
    锁的机制：
        实现多线程同步，通过锁机制，确保任一时刻只能有一个线程能进入关键代码段
*/
class sem
{
    /*
        sem_init:用于初始化一个未命名的信号量
        sem_destory:用于销毁信量
        sem_wait:以原子操作的方式将信号量减1，信号量为0时,sem_wait阻塞
        sem_post:以原子操作的方式将信号两加1，信号量大于0时，唤醒调用sem_post的线程
        以上成功返回0，失败返回erron
    */
public:
    sem(){
        if (sem_init(&m_sem, 0, 0) != 0){
            throw std::exception();
        }
    }
    sem(int num){
        if (sem_init(&m_sem, 0, num) != 0){
            throw std::exception();
        }
    }
    ~sem(){
        sem_destroy(&m_sem);
    }
    bool wait(){
        return sem_wait(&m_sem) == 0;
    }
    bool post(){
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};

class locker
{
    /*
        pthread_mutex_init函数用于初始化互斥锁
        pthread_mutex_destory函数用于销毁互斥锁
        pthread_mutex_lock函数以原子的方式给互斥锁加锁
        pthread_mutex_unlock函数以原子的方式给互斥所解锁
    */
public:
    locker(){
        if (pthread_mutex_init(&m_mutex, NULL) != 0){
            throw std::exception();
        }
    }
    ~locker(){
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock(){
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    bool unlock(){
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    pthread_mutex_t *get(){
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;//互斥锁
};

class cond
{
    /*
        条件变量提供了一种线程间的通信机制，当某个共享数据达到某个值时，唤醒等待这个共享数据的线程
        pthread_cond_init函数用于初始化条件变量
        pthread_cond_destroy:
        pthread_cond_broadcast函数以广播的方式唤醒所有等待目标条件变量的线程
        pthread_cond_wait函数用于等待目标条件变量，该函数调用时传入mutex(加锁的互斥锁),函数执行
        时，先把调用线程放入条件变量的请求队列，然后将互斥锁mutex解锁，当函数成功返回0时，互斥锁会
        再次被锁上，也就是函数内部会有一次解锁和加锁的操作
    */
public:
    cond(){
        if (pthread_cond_init(&m_cond, NULL) != 0){
            throw std::exception();
        }
    }
    ~cond(){
        pthread_cond_destroy(&m_cond);
    }

    /*
        等待条件变量有两种方式：
            无条件等待pthread_cond_wait()
            计时等待pthread_cond_timewait()
        无论哪种等待方式，都必须和一个互斥锁配合，以防止多个线程
        同时请求pthread_cond_wait()或pthread_cond_timedwait()的竞争条件，mutex互斥锁必须是普通锁
        或者适应锁（PTHREAD_MUTEX_ADAPTIVE_NP），且在调用pthread_cond_wait()前必须由本线程加锁（pthread_mutex_lock()），
        而在更新条件等待队列以前，mutex保持锁定状态，并在线程挂起进入等待前解锁。
    */
    bool wait(pthread_mutex_t *m_mutex){
        int ret = 0;
        ret = pthread_cond_wait(&m_cond, m_mutex);
        return ret == 0;
    }

    bool timewait(pthread_mutex_t *m_mutex, struct timespec t){
        int ret = 0;
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        return ret == 0;
    }
    
    /*
        激活等待条件变量的线程有两种:
            pthread_cond_signal()激活一个等待该条件变量的线程
            pthread_cond_broadcast()激活所有等待线程
    */
    bool signal(){
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast(){
        return pthread_cond_broadcast(&m_cond) == 0;
    }
private:
    pthread_cond_t m_cond;
};

#endif