#ifndef LST_TIMER_H
#define LST_TIMER_H

#include <unistd.h>
#include <signal.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <time.h>
#include "../log/log.h"

class util_timer;
     
/*
    客户数据主要包含：
        socket地址
        socket编号
        定时器
*/
struct client_data
{
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};

/*
    定时器类主要包含：
        当前定时器的超时时间
        超时需要执行的回调函数
        指向上一个定时器的指针
        指向下一个定时器的指针
*/
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    time_t expire; //任务超时时间
    void (*cb_func)(client_data *);//任务回调函数
    //回调函数处理的客户数据，由定时器的执行者传递给回调函数
    client_data *user_data;
    util_timer *prev;//指向前一个定时器
    util_timer *next;//指向下一个定时器
    
};

/*
    一个升序的定时器链表，主要包含以下方法：
        void add_timer(util_timer *timer, util_timer *lst_head)
            用于从定时器头开始添加链表
        void add_timer(util_timer *timer)
            先判断链表是否为空，为空先设为头节点，
                            不为空则调用 void add_timer(util_timer *timer, util_timer *lst_head) 
        void adjust_timer(util_timer *timer)
            用于调整定时器链表
        void del_timer(util_timer *timer)
            删除定时器
        void tick()
            用于从定时器链表头开始逐个调用每个已经超时的定时器中的回调函数，并将该定时器从链表中删除
*/
class sort_timer_lst
{
public:
    sort_timer_lst();
    ~sort_timer_lst();

    void add_timer(util_timer *timer);
    void adjust_timer(util_timer *timer);
    void del_timer(util_timer *timer);
    void tick();
private:
    void add_timer(util_timer *timer, util_timer *lst_head);
    util_timer *head;
    util_timer *tail;
};

/*
    
*/
class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    int setnonblocking(int fd);

    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    static void sig_handler(int sig);

    void addsig(int sig, void(handler)(int), bool restart = true);

    void timer_hanler();

    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;
    sort_timer_lst m_timer_lst; //顺序定时器链表
    static int u_epollfd;
    int m_TIMESLOT;
};

void cb_func(client_data *user_data);
#endif