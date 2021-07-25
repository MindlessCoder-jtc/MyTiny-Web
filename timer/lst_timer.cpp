#include "lst_timer.h"
#include "../http/http_conn.h"


sort_timer_lst::sort_timer_lst()
{
    head = NULL;
    tail = NULL;
}

sort_timer_lst::~sort_timer_lst()
{
    util_timer *tmp = head;
    while (tmp)
    {
        head = tmp -> next;
        delete tmp;
        tmp = head;
    }
}

void sort_timer_lst::add_timer(util_timer *timer)
{
    if (!timer)
    {
        return ;
    }
    if (!head)
    {
        head = tail = timer;
        return;
    }
    //如果当前定时器的超时时间小于第一个定时器，就插在链表的头节点
    if (timer -> expire < head -> expire)
    {
        timer -> next = head;
        head -> prev = timer;
        head = timer;
        return;
    }
    add_timer(timer, head);
}

void sort_timer_lst::adjust_timer(util_timer *timer)
{
    if (!timer)
        return ;
    util_timer *tmp = timer -> next;
    //判断是否是链表结尾或者当前定时器的超时时间仍然小于后面的定时器的超时时间
    if (!tmp || (timer -> expire < tmp -> expire))
    {
        return ;
    }
    //如果目标定时器事链表的头节点，则将该定时从链表中取出，再重新插入链表中
    if (timer == head)
    {
        head = head -> next;
        head -> prev = NULL;
        timer -> next = NULL;
        add_timer(timer, head);
    }
    else //如果目标定时器不是链表的头节点，则将该定时从链表中取出，再重新插入其原来位置之后的链表中
    {
        timer -> prev -> next = timer -> next;
        timer -> next -> prev = timer -> prev;
        add_timer(timer, timer -> next);
    }
}
void sort_timer_lst::del_timer(util_timer *timer)
{
    if (!timer)
    {
        return ;
    }
    if ((timer == head) && (timer == tail))
    {
        delete timer;
        head = NULL;
        tail = NULL;
        return ;
    }
    if (timer == head)
    {
        head = head -> next;
        head -> prev = NULL;
        delete timer;
        return ;
    }
    if (timer == tail)
    {
        tail = tail -> prev;
        tail -> next = NULL;
        delete timer;
        return ;
    }
    timer -> prev -> next = timer -> next;
    timer -> next -> prev = timer -> prev;
    delete timer;
}

/*
    SIGALRM信号每次触发就在其信号处理函数（如果使用统一事件源，则是主函数）中执行以此tick函数，
    以处理链表上到期的任务
*/
void sort_timer_lst::tick()
{
    if (!head)
        return;
    time_t cur = time(NULL);//获得系统当前时间
    util_timer *tmp = head;
    //从节点头依次处理每个定时器，直到遇到一个尚未到期的定时器
    while (tmp)
    {
        if (cur < tmp -> expire)
            break;
        tmp -> cb_func(tmp -> user_data);
        head = tmp -> next;
        if (head)
            head -> prev = NULL;
        delete tmp;
        tmp = head;
    }
}

void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head)
{
    util_timer *prev = lst_head;
    util_timer *tmp = prev -> next;
    while (tmp)
    {
        //如果要插入的定时器的超时时间小于当前节点的超时时间
        if (timer -> expire < tmp -> expire)
        {
            prev -> next = timer;
            timer -> next = tmp;
            timer -> prev = prev;
            tmp -> prev = timer;
            break;
        }
        prev = tmp;
        tmp = tmp -> next;
    }
    /*
        如果遍历完lst_head节点之后的部分链表，仍未找到超时时间大于要插入的目标定时器，则将
        目标定时器插入链表尾部，并把它设置未链表的新节点
    */
    if (!tmp)
    {
        prev -> next = timer;
        timer -> prev = prev;
        timer -> next = NULL;
        tail = timer;
    }
}

void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}


void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;
    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else 
        event.events = EPOLLIN | EPOLLRDHUP;
    
    /*
        对于注册了EPOLLONESHOT事件的文件描述符，操作系统最多触发其上注册的一个可读可写或者异常事件，且只触发一次，除非我们使用epoll_ctl函数
        重置该文件描述符上的注册的EPOLLONESHOT事件
    */
    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//信号处理函数
void Utils::sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void Utils::timer_hanler()
{
    m_timer_lst.tick();//定时处理任务,实际上就是调用tick()
    //因为一次alarm只会调用一次SIGALARM信号,所以我们要重新定时,以不断触发SIGALRM信号
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;

/*
    回调函数主要用于将当前定时器所管理连接的socket从epollfd中删除并关闭这个socket，并将连接数量减1
*/
void cb_func(client_data *user_data)
{
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data -> sockfd, 0);
    assert(user_data);
    close(user_data -> sockfd);
    http_conn::m_user_count--;
}