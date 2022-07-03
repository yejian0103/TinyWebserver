#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include "locker.h"
#include "threadpool.h"
#include "httpconn.h"

#define MAX_FD 65535    //最大的文件描述符数
#define MAX_EVENT_NUMBER 10000  //监听的最大事件数

extern void addfd(int epollfd, int fd, bool one_shot);
extern void removefd(int epollfd, int fd);

int httpconn::m_epollfd ;
int httpconn::m_user_count ;

//添加信号捕捉函数
void addsig(int sig, void(handler)(int)){
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

int main (int argc, char* argv[]){
    if(argc <= 1){
        printf("usage: %s port number\n", basename(argv[0]));
        return 1;
    }

    int port = atoi(argv[1]);
    addsig(SIGPIPE, SIG_IGN);

    //创建线程池
    threadpool<httpconn>* pool = nullptr;
    try
    {
        pool = new threadpool<httpconn>;
    }
    catch(...)
    {
        return 1;
    }

    //连接的用户数组
    httpconn* users = new httpconn[MAX_FD];

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);

    struct sockaddr_in seraddr;
    seraddr.sin_family = AF_INET;
    seraddr.sin_addr.s_addr = INADDR_ANY;
    seraddr.sin_port = htons(port);

    //端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    int ret = bind(listenfd, (struct sockaddr*)&seraddr, sizeof(seraddr));
    assert(ret >= 0);

    ret = listen(listenfd, 5);

    //创建epoll
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    
    //添加监听连接的文件描述符到epoll中
    addfd(epollfd, listenfd, false);
    httpconn::m_epollfd = epollfd;

    while(true){
        int count = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);

        if((count < 0) && (errno != EINTR)){
            printf("epoll failure\n");
            break;
        }

        for(int i = 0; i < count; i++){
            int sockfd = events[i].data.fd;

            if(sockfd == listenfd){
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );

                if ( connfd < 0 ) {
                    printf( "errno is: %d\n", errno );
                    continue;
                }

                if( httpconn::m_user_count >= MAX_FD ) {
                    close(connfd);
                    continue;
                }
                users[connfd].init( connfd, client_address);
            }else if(events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR )){

                users[sockfd].close_conn();

            }else if(events[i].events & EPOLLIN){

                if(users[sockfd].read()) {
                    pool->append(users + sockfd);
                } else {
                    users[sockfd].close_conn();
                }

            }else if(events[i].events & EPOLLOUT){

                if( !users[sockfd].write() ) {
                    users[sockfd].close_conn();
                }

            }
        }
    }

    close( epollfd );
    close( listenfd );
    delete [] users;
    delete pool;
    return 0;

}