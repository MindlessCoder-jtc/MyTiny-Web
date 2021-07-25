#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool(){
    m_CurConn = 0;
    m_FreeConn = 0;
}

connection_pool *connection_pool::GetInstance(){
    static connection_pool connPool;
    return &connPool;
}

void connection_pool::init(string Url, string User, string Password, string DBName, int Port, 
            int MaxConn, int Close_log)
{
    m_url = Url;
    m_Port = Port;
    m_User = User;
    m_Password = Password;
    m_DatabaseName = DBName;
    m_MaxConn = MaxConn;
    m_close_log = Close_log;//日志开关

    for (int i = 0; i < MaxConn; i++){
        MYSQL *con = NULL;
        /*
            MYSQL * STDCALL mysql_init(MYSQL *mysql);
            成功返回*mysql指针，失败返回NULL
        */
        con = mysql_init(con); //初始化mysql连接
        if(con == NULL)
        {
            LOG_ERROR ("MYSQL ERROR");
            exit(1);
        }

        con = mysql_real_connect(con, Url.c_str(), User.c_str(), Password.c_str(), DBName.c_str(), Port, NULL, 0);
        if (con == NULL)
        {
            LOG_ERROR("MYSQL ERROR");
            exit(1);
        }
        //连接池链表插入一个连接
        connList.push_back(con); 
        //当前空闲连接+1
        ++m_FreeConn; 
    }
    //将信号量初始化为最大连接次数
    reserve = sem(m_FreeConn);

    m_MaxConn = m_FreeConn; //最大连接次数等于空闲连接的数量
}

/*
    当前线程数量大于数据库的连接数量，使用信号量进行同步，每次取出连接，信号量减1,释放连接，信号量加1,若连接词没有连接了，则阻塞等待
    另外，由于多线程操作连接池，会造成竞争，这里使用互斥锁，具体的同步机制均使用lock.h中封装好的类
*/
//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
    MYSQL *con = NULL;

    //没有多余连接
    if (connList.size() == 0)
    {
        return NULL;
    }
    //取出连接，信号量减去1,为0则等待
    reserve.wait();
    lock.lock();
    con = connList.front();
    connList.pop_front();

    --m_FreeConn;
    ++m_CurConn;

    lock.unlock();
    return con;
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
    if (con == NULL)
        return false;
    lock.lock();

    connList.push_back(con);
    ++m_FreeConn;
    --m_CurConn;

    lock.unlock();
    
    reserve.post();
    return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool()
{
    lock.lock();
    if (connList.size() > 0)
    {
        list<MYSQL *>::iterator it;
        for (it == connList.begin(); it != connList.end(); ++it)
        {
            MYSQL *con = *it;
            //关闭MYSQL数据库的一条连接
            mysql_close(con);
        }
        m_CurConn = 0;
        m_FreeConn = 0;
        connList.clear();
    }
    lock.unlock();
}

//当前空闲的连接数
int connection_pool::GetFreeConn()
{
    return this -> m_FreeConn;
}

connection_pool::~connection_pool()
{
    DestroyPool();
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool)
{
    *SQL = connPool -> GetConnection();

    conRAII = *SQL;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII()
{
    poolRAII -> ReleaseConnection(conRAII);
}
