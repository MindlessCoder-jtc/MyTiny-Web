#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

/*
    线程池的实现方式：
        先初始化线程池类，建立一个线程池，里面初始化若干个子线程，每个子线程都一直运行一个work函数，
        work函数里面调用run方法，从工作队列里面取http连接的处理请求
*/
template <typename T>
class threadpool
{
public:
    /*
        thread_number是线程池中线程的数量，max_request是请求队列中最多允许的、等待处理的请求数量
    */
   threadpool(int actor_model, connection_pool *connPool, int thread_number= 8, int max_request = 10000);
   ~threadpool();
   bool append(T *request, int state);
   bool append_p(T *request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/

    /*工作函数为类的静态函数，若不是静态成员函数，则this指针会作为默认的参数被传进函数中，从而和线程函数参数(void*)arg不能匹配*/
    static void *worker(void *arg); 
    void run();

private:
    int m_thread_number; //线程池中的线程数
    int m_max_requests;  //请求队列中允许的最大请求数
    pthread_t *m_threads;   //描述线程池的数组，其大小为m_thread_number
    std::list<T* > m_workqueue; //请求队列
    locker m_queuelocker; //保护请求队列的互斥锁
    sem m_queuestat;    //是否用任务需要处理
    connection_pool *m_connPool; //数据库
    int m_actor_model;  //模型切换

};

template <typename T>
threadpool<T>::threadpool (int actor_model, connection_pool *connPool, int thread_number,
                        int max_requests): m_actor_model(actor_model), m_thread_number(thread_number),
                        m_max_requests(max_requests), m_threads(NULL), m_connPool(connPool)
{
    //构造函数：创建线程ID数组，为每一个ID创建一个线程，并完成线程分离
    if (thread_number <= 0 || max_requests <= 0)
    {
        throw std::exception();
    }
    //创建线程池数组
    m_threads = new pthread_t[m_thread_number]; 
    if (!m_threads)
        throw std::exception();
    for (int i = 0; i < thread_number; ++i)
    {
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete [] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i]))
        {
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool <T>::~threadpool()
{
    delete [] m_threads;
}

template <typename T>
bool threadpool <T>::append(T *request, int state)
{
    /*
        通过list容器来创建请求队列，向队列添加请求时，通过互斥锁保证线程安全，添加完成后，通过信号量提示有任务要处理，主要线程同步
    */
   m_queuelocker.lock(); //请求队列上锁
   if (m_workqueue.size() >= m_max_requests)
   {
       m_queuelocker.unlock(); //队列已满
       return false;
   }
   //添加请求任务
   request -> m_state = state;
   m_workqueue.push_back(request);
   m_queuelocker.unlock();
   m_queuestat.post();
   return true;
}

template <typename T>
bool threadpool<T>::append_p(T *request)
{
    //主要用于proactor模式的服务器添加读请求
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

//线程处理函数
template <typename T>
void *threadpool<T> :: worker(void *arg)
{
    //内部访问私有成员，完成线程处理要求
    //将参数转为线程池类，调用成员方法
    threadpool *pool = (threadpool *)arg;
    pool -> run();
    return pool;
}

//run 执行任务
//工作线程从请求队列中取出某个任务进行处理，注意线程同步
template <typename T>
void threadpool<T> :: run() 
{
    while (1)
    {
        m_queuestat.wait();//信号量减1，为0则阻塞
        m_queuelocker.lock();
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front(); //从请求队列取出第一个任务
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request){
            continue;
        }
        if (m_actor_model == 1) //reactor模式，每个线程需要自己处理读写
        {
            if (request -> m_state == 0) //0表示读，1表示写
            {
                // 读取浏览器端发送过来的请求报文并存放到m_read_buf数组中
                if (request -> read_once()) 
                {
                    request -> improv = 1;
                    //每个http链接都连接数据库
                    connectionRAII mysqlcon(&request -> mysql, m_connPool);
                    //处理请求
                    request -> process();//通过process函数完成对报文的解析和响应报文的生成
                }
                else
                {   //遇到客户端连接关闭
                    request -> improv = 1;
                    request -> timer_flag = 1;
                }
            }
            else
            {
                if (request -> write())
                {
                    request -> improv = 1;
                }
                else
                {   //如果数据发送失败或者当前的连接为短连接
                    request -> improv = 1;
                    request -> timer_flag = 1;
                }
            }
        }
        else
        {
            //每个http链接都连接数据库
            connectionRAII mysqlcon(&request -> mysql, m_connPool);
            //处理请求
            request -> process(); //通过process函数完成对报文的解析和响应报文的生成
        }
    }
}
#endif 