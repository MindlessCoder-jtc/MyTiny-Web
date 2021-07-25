#include "config.h"

int main(int argc, char* argv[])
{   
    //数据库登陆信息
    string user = "root";
    string passwd = "mysqladmin";
    string databasename = "jtc_database";
    //命令行解析
    Config config;
    config.parse_arg(argc, argv);
    //cout << "parse arg ready ... " << endl;

    WebServer server;

    /*
        初始化Web服务器,包括监听端口号,数据库登录名/密码/数据库名,日志写入方式,
            关闭连接的方式,触发组合模式，数据库连接池数量，线程池线程数量，
            是否关闭连接，并发模型的选择
    */
    //初始化
    server.init(config.PORT, user, passwd, databasename, config.LOGWrite, 
                config.OPT_LINGER, config.TRIGMode,  config.sql_num,  config.thread_num, 
                config.close_log, config.actor_model);
    
    //日志
    server.log_write();

    //初始化数据库
    server.sql_pool();

    //初始化线程池
    server.thread_pool();

    //设置触发模式
    server.trig_mode();

    //监听
    server.eventListen();

    //运行
    server.eventLoop();

    return 0;
}