#include "config.h"

Config::Config()
{
    //端口号：默认9006
    PORT = 9006;

    //日志写入方式, 默认同步
    LOGWrite = 0;

    //触发模式组合， 默认listenfd LT + connfd LT 
    TRIGMode = 0;

    LISTENTrigmode = 0;

    CONNTrigmode = 0;

    //优雅关闭连接
    OPT_LINGER = 0;

    //数据库连接池数量，默认为8
    sql_num = 8;

    //线程池内线程的数量，默认为8
    thread_num = 8;

    //关闭日志，默认不关闭
    close_log = 0;

    //并发模型，默认是proactor
    actor_model = 1;

}

void Config::parse_arg(int argc, char* argv[])
{   
    /*
     atoi (表示 ascii to integer)是把字符串转换成整型数的一个函数
     int atoi(const char *nptr) 
    */
    int opt;
    const char *str = "p:l:m:o:s:t:c:a:";
    while ((opt = getopt(argc, argv, str)) != -1)
    {
        switch (opt)
        {
        case 'p':
        {
            PORT = atoi(optarg);
            break;
        }
        case 'l':
        {
            LOGWrite = atoi(optarg);
            break;
        }
        case 'm':
        {
            TRIGMode = atoi(optarg);
            break;
        }
        case 'o':
        {
            OPT_LINGER = atoi(optarg);
            break;
        }
        case 's':
        {
            sql_num = atoi(optarg);
            break;
        }
        case 't':
        {
            thread_num = atoi(optarg);
            break;
        }
        case 'c':
        {
            close_log = atoi(optarg);
            break;
        }
        case 'a':
        {
            actor_model = atoi(optarg);
            break;
        }
        
        default:
            break;
        }
    }
}