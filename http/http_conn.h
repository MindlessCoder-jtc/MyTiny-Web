#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <map>
#include <errno.h>
#include <sys/uio.h>
#include <mysql/mysql.h>
#include <fstream>
#include <iostream>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../log/log.h"
#include "../timer/lst_timer.h"

/*
    http报文处理流程
        浏览器端发出http连接请求，主线程创建http对象接收请求并将所有数据读入对应buffer，将该对象插入任务队列，工作线程从任务队列中取出一个任务进行处理
        工作线程取出任务后，调用process_read函数，通过主从状态机对请求报文进行解析
        解析完后，跳转do_request函数生成响应报文，通过process_write写入buffer，返回给浏览器端
*/
class http_conn
{
public:
    //要读取文件的名称大小
    static const int FILENAME_LEN = 200;
    //设置读缓冲区m_read_buf大小
    static const int READ_BUFFER_SIZE = 2048;
    //设置写缓冲区m_write_buf大小
    static const int WRITE_BUFFER_SIZE = 1024;
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };
public:
    http_conn() {}
    ~http_conn() {}

public:
    //初始化套接字地址，函数内部会调用私有方法init
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    //关闭http连接
    void close_conn(bool real_close = true);
    void process();
    //读取浏览器发来的全部数据
    bool read_once();
    //响应报文写入函数
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    //同步线程初始化数据库读取表
    void initmysql_result(connection_pool *connPool);
    int timer_flag;
    int improv;

private:
    void init();
    //从m_read_buf读取，并处理请求报文
    HTTP_CODE process_read();
    //向m_write_buf写入数据
    bool process_write(HTTP_CODE ret);
    //主状态机解析报文中的请求数据行
    HTTP_CODE parse_request_line(char *text);
    //主状态机解析报文中的请求头数据
    HTTP_CODE parse_headers(char *text);
    //主状态机解析报文中的请求内容
    HTTP_CODE parse_content(char *text);
    //生成响应报文
    HTTP_CODE do_request();
    //m_start_line是已经解析的字符，
    //get_line用于将指针向后偏移，指向未处理的字符
    char *get_line() 
    {
        return m_read_buf + m_start_line;
    }
    //从状态机读取一行，分析是请求报文中的哪一部分
    LINE_STATUS parse_line();
    void unmap();
    //根据响应报文格式，生产对应8个部分，以下函数均由do_request()调用
    bool add_response(const char* format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;
    int m_state; //read == 0, write == 1

private:
    int m_sockfd;
    sockaddr_in m_address;
    //存储读取的请求报文数据
    char m_read_buf[READ_BUFFER_SIZE];
    //缓冲区中m_read_buf中数据的最后一个字节的下一个位置
    int m_read_idx;
    //m_read_buf读取的位置m_checked_idx
    int m_checked_idx;
    //m_read_buf中已经解析的字符的个数
    int m_start_line;
    //存储发出的响应报文的数据
    char m_write_buf[WRITE_BUFFER_SIZE];
    //要发送的响应报文的字节数
    int m_write_idx;
    //主状态机的状态
    CHECK_STATE m_check_state;
    //请求方法
    METHOD m_method;
    //存储读取文件的名称
    char m_real_file[FILENAME_LEN];
    char *m_url;
    char *m_version;
    char *m_host;
    int m_content_length;
    bool m_linger;
    //读取服务器上文件地址
    char *m_file_address;
    struct stat m_file_stat;
    //io向量机制iovc
    struct iovec m_iv[2];
    int m_iv_count;
    //是否启用cgi
    int cgi;
    //存储请求头数据
    char *m_string;
    //剩余发送字节数
    int bytes_to_send;
    //已发送字节数
    int bytes_have_send;
    char *doc_root;

    map<string, string> m_user;
    int m_TRIGMode;
    int m_close_log;
    
    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};


#endif