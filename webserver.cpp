#include "webserver.h"

WebServer::WebServer()
{
    //http_conn类对象
    users = new http_conn[MAX_FD];

    //root文件夹路径
    char server_path[200];
    //getcwd()会将当前的工作目录绝对路径复制到参数buf 所指的内存空间，参数size 为buf 的空间大小
    getcwd(server_path, 200); 
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    //定时器
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write, 
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

void WebServer::trig_mode()
{
    //LT + LT
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    //LT + ET
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    //ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    //ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void WebServer::log_write()
{
    if (0 == m_close_log)
    {
        //初始化日志,1表示设置异步日志
        if (1 == m_log_write)
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

void WebServer::sql_pool()
{
    //初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    //初始化数据库读取表
    users->initmysql_result(m_connPool);
}

void WebServer::thread_pool()
{
    //线程池
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

void WebServer::eventListen()
{
    //网络编程基础步骤
    /*
        1.创建一个监听套接字
        2.设置监听套接字的属性
        3.设置socket地址，并绑定到监听套接字
        4.IO复用为epoll方式
            
    */
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    /*
        关闭连接的三种方式：
            typedef struct linger{
                u_short l_onoff; //开关， 零或非零
                
                 l_linger; //优雅关闭的最长时限
            }
        l_onoff    l_linger     closesocket行为     发送队列        底层行为
            0       ignore         立即返回         保持直至发送完成    系统接管套接字并保证将数据发送到对端
            非0        0            立即返回         立即放弃          直接发送RST包，自身立即复位，不用经过2MSL状态，对端收到复位错误号
            非0       非0        阻塞直到l_linger    在超时时间段内保     超时同第二种情况，若发送完成则皆大欢喜
                                                持尝试发送，若超时立即放弃
    */

    //优雅关闭连接
    if (0 == m_OPT_LINGER)
    {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);/* INADDR_ANY：将套接字绑定到所有可用的接口 */
    address.sin_port = htons(m_port);

    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));/* SO_REUSEADDR 允许端口被重复使用 */
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    utils.init(TIMESLOT); //初始化定时器间隔

    //epoll创建内核事件表
    //epoll_event events[MAX_EVENT_NUMBER];/* 用于存储epoll事件表中就绪事件的event数组 */
    m_epollfd = epoll_create(5);/* 创建一个额外的文件描述符来唯一标识内核中的epoll事件表 */
    assert(m_epollfd != -1);
    
    /* 主线程往epoll内核事件表中注册监听socket事件，当listen到新的客户连接时，listenfd变为就绪事件 */
    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd; //将epollfd传给http_conn类

    //创建管道将信号传递给主循环
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    utils.addsig(SIGPIPE, SIG_IGN);//SIG_IGN表示忽略目标信号
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    alarm(TIMESLOT);

    //工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    /* 
        初始化连接状态
            注册sockfd到epollfd
            统计当前连接数量
            设置根目录位置
            设置http连接的触发模式
            该连接是都记录到日志

            设置该http连接连接数据库的mysql登录名，密码，
    */
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    //初始化client_data数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];  //指定当前连接的客户数据
    timer->cb_func = cb_func;
    time_t cur = time(NULL); //当前时间
    timer->expire = cur + 3 * TIMESLOT; //设置超时时间
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

/* 主要用于清楚该socket连接及定时器 */
void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
    {
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

/*
    Q：多路IO复用accept为什么应该工作在非阻塞模式？
    A：如果accept工作在阻塞模式，考虑这种情况： TCP 连接被客户端夭折，即在服务器调用 accept 之
        前（此时select等已经返回连接到达读就绪），客户端主动发送 RST 终止连接，导致刚刚建立的连接从
        就绪队列中移出，如果套接口被设置成阻塞模式，服务器就会一直阻塞在 accept 调用上，直到其他某个客户
        建立一个新的连接为止。但是在此期间，服务器单纯地阻塞在accept 调用上（实际应该阻塞在select上），
        就绪队列中的其他描述符都得不到处理。
      解决办法是把监听套接口设置为非阻塞， 当客户在服务器调用 accept 之前中止某个连接时，accept 调用可以
        立即返回 -1。这是源自 Berkeley 的实现会在内核中处理该事件，并不会将该事件通知给 epoll，而其他实现把 
        errno 设置为 ECONNABORTED 或者 EPROTO 错误，我们应该忽略这两个错误。

    Q：ET 模式下的读写注意事项
    A：我们可以知道，当epoll工作在ET模式下时，对于读操作，如果read一次没有读尽buffer中的数据，
        那么下次将得不到读就绪的通知，造成buffer中已有的数据无机会读出，除非有新的数据再次到达。对于写操作，
        主要是因为ET模式下fd通常为非阻塞造成的一个问题——如何保证将用户要求写的数据写完。
        要解决上述两个ET模式下的读写问题，我们必须实现：
        1.对于读，只要buffer中还有数据就一直读
            只要可读，就一直读，直到返回0或者errno==EAGAIN
        2.对于写，只要buffer还有空间，且用户请求写的数据还未写完，就一直写
            只要可写，就一直写，直到数据发送完或者errno==EAGAIN

        使用这种方式一定要使每个连接的套接字工作于非阻塞模式，因为读写需要一直读或者写直到出错（对于读，
        当读到的实际字节数小于请求字节数时就可以停止），而如果你的文件描述符如果不是非阻塞的，那这个一直读或
        一直写势必会在最后一次阻塞（最后一次read肯定要返回0，表示缓冲区没有数据可读了，因此最后一次read会阻塞）。
        这样就不能在阻塞在epoll_wait上了，造成其他文件描述符的任务饿死
    
*/
bool WebServer::dealclinetdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    /*
        LT:
            对于采用LT工作模式的文件描述符，当epoll_wait检测到其上有事件发生并将此事件通知应用程序后，
            应用程序可以不立即处理该事件。当应用程序下次调用epoll_wait的时候，epoll_wait还会向应用程序
            通知此事件，直到该事件被处理。
        ET:
            对于采用ET工作模式的文件描述符，当epoll_wait检测到其上有事件发生并通知应用程序后，
            应用程序必须立即处理该事件，因为后续的epoll_wait将不在向应用程序通知此事件。
            可见epoll_wait很大程度上降低了同一个epoll事件被触发的次数，因此效率较高

        LT 与ET 的区别可以从以下两点看出：
            当采用LT 模式时，当connfd接收到数据，但是超过了缓冲区的大小，此时无法一次性接收，所以要分两次进行
                接收，也就是两次监听到socket准备好再读取数据
            当采用ET 模式时， 当connfd接收到数据，但是超过了缓冲区的大小，此时也也无法一次性接收，所以要开启
                一个while循环，内部不断循环读缓冲区的数据，直到缓冲区数据全部被读完，此时结束循环，整个过程只
                有一次监听到socket准备好的过程
    */
    if (0 == m_LISTENTrigmode)//LT
    {
        /*
            采用LT模式下，如果accept调用有返回就可以麻灰色那个建立当前这个连接，再epoll_wait等待下次通知，
            和select一样
        */
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD)
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address);
    }

    else//ET
    {
        /*
            考虑这种情况：多个连接同时到达，服务器的 TCP 就绪队列瞬间积累多个就绪连接，由于是边缘触发模式，
            epoll 只会通知一次，accept 只处理一个连接，导致 TCP 就绪队列中剩下的连接都得不到处理

            解决办法是用 while 循环抱住 accept 调用，处理完 TCP 就绪队列中的所有连接后再退出循环。
            如何知道是否处理完就绪队列中的所有连接呢？accept 返回 -1 
                并且 errno 设置为 EAGAIN 就表示所有连接都处理完
        */
        while (1)
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        /* 因为每个信号只占一个字节，所以按字节来逐个接收信号 */
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
                case SIGALRM:
                {
                    timeout = true;
                    break;
                }
                case SIGTERM:
                {
                    stop_server = true;
                    break;
                }
            }
        }
    }
    return true;
}

/*
    同步阻塞IO：
        在此种方式下，用户进程在发起一个IO操作以后，必须等待IO操作的完成，只有当真正完成了IO操作以后，用户进程才能运行。
    同步非阻塞IO：
        在此种方式下，用户进程发起一个IO操作以后边可返回做其它事情，但是用户进程需要时不时的询问IO操作是否就绪，
        这就要求用户进程不停的去询问，从而引入不必要的CPU资源浪费。
    异步阻塞IO：
        此种方式下是指应用发起一个IO操作以后，不等待内核IO操作的完成，等内核完成IO操作以后会通知应用程序，这其实就是同步和异步最关键的
        区别，同步必须等待或者主动的去询问IO是否完成，那么为什么说是阻塞的呢？因为此时是通过select系统调用来完成的，
        而select函数本身的实现方式是阻塞的，而采用select函数有个好处就是它可以同时监听多个文件句柄，从而提高系统的并发性！
    异步非阻塞IO：
        在此种模式下，用户进程只需要发起一个IO操作然后立即返回，等IO操作真正的完成以后，应用程序会得到IO操作完成的通知，此时用户进程只需要对数据进行处理就好了，不需要进行实际的IO读写操作，因为真正的IO读取或者写入操作已经由内核完成了。
*/

void WebServer::dealwithread(int sockfd)
{
    //处理读请求
    /*
        两种情况：
            reactor：
                将请求加入线程池，标识为0（读请求）,由线程的工作函数来完成对客户端数据的读取，数据库连接，请求报文的解析和反应报文的生成
            proactor:
                先完成对该连接的客户端数据的读取，然后将该连接请求加入到线程池，由工作函数完成对剩余操作（数据库连接，请求报文的解析和反应报文的生成）
            
            每次处理一个连接的读请求，需要将该连接的定时器超时时间向后调整3个单位，并调整其在定时器链表中的位置
    */
    util_timer *timer = users_timer[sockfd].timer;

    //reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer); //有数据处理，将定时器的超时向后延迟三个单位，并调整在定时器链表上的位置
        }

        //若监测到读事件，将该事件放入请求队列， 0表示读请求
        m_pool->append(users + sockfd, 0);

        while (true)
        {
            // ？？？
            if (1 == users[sockfd].improv) //应该是是否处理该请求
            {
                if (1 == users[sockfd].timer_flag) //读客户端数据出错
                {
                    deal_timer(timer, sockfd); //清除这个http链接
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor
        if (users[sockfd].read_once())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            //若监测到读事件，将该事件放入请求队列
            m_pool->append_p(users + sockfd);

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd); //清楚该链接
        }
    }
}

void WebServer::dealwithwrite(int sockfd)
{
    /*
        处理写请求
        分为两种情况：
            reactor:
                先将该连接的请求加入线程池，标识为1（写请求），由线程池的线程的工作函数函数来执行写操作，之后的while（true）用来判断写请求的处理结果
            proactor:
                直接由主线程来完成对该连接的写请求操作，不调用其他线程

        每次处理一个连接的写请求，需要将该连接的定时器超时时间向后调整3个单位，并调整其在定时器链表中的位置
    */
    util_timer *timer = users_timer[sockfd].timer;
    //reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        m_pool->append(users + sockfd, 1);

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor
        if (users[sockfd].write())  
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {   //如果数据发送失败或者当前的连接为短连接，就清空该连接对应的定时器
            deal_timer(timer, sockfd);
        }
    }
}

/*
    EPOLLIN ： 表示对应的文件描述符可以读（包括对端SOCKET正常关闭）
    EPOLLOUT： 表示对应的文件描述符可以写；
    EPOLLPRI： 表示对应的文件描述符有紧急的数据可读（这里应该表示有带外数据到来）；
    EPOLLERR： 表示对应的文件描述符发生错误；
    EPOLLHUP： 表示对应的文件描述符被挂断；
    EPOLLET： 将 EPOLL设为边缘触发(Edge Triggered)模式（默认为水平触发），这是相对于水平触发(Level Triggered)来说的。
    EPOLLONESHOT： 只监听一次事件，当监听完这次事件之后，如果还需要继续监听这个socket的话，需要再次把这个socket加入到EPOLL队列里
*/
void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server)
    {
        //处理端口的读写或信号事件
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            //处理新到的客户连接
            if (sockfd == m_listenfd)
            {
                bool flag = dealclinetdata();  //接收连接，并将新连接加入到定时器链表中
                if (false == flag)  //成功接收新链接并将其加入到定时器链表中返回true，其余返回false
                    continue;
            }
            /*
                EPOLLHUP： 表示对应的文件描述符被挂断；
            */
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            //处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                //信号两种处理方式，一种是查看是否超时，一种是是否停止服务器；成功接收到信号返回true
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            else if (events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }
        if (timeout)
        {
            utils.timer_hanler();

            LOG_INFO("%s", "timer tick");

            timeout = false;
        }
    }
}