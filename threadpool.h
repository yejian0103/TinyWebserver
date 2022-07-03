#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "locker.h"

template<typename T>
class threadpool{
private:
    //线程的数量
    int m_thread_number;

    //线程池的实体容器
    pthread_t *m_thread;

    //请求队列的大小上限
    int m_max_requests;

    //请求队列的实体容器
    std::list<T *> m_workqueue;

    //访问工作队列的互斥锁
    muxlock m_queue_lock;

    //表示任务数量的信号量
    sem m_queue_stat;

    //线程是否结束
    bool m_stop;
public:
    threadpool(int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    bool append(T * request);
private:
    //工作线程运行的函数，以轮询的方式从请求队列中取出任务执行
    static void* worker(void* arg);
    void run();
};

template <typename T>
threadpool<T>::threadpool(int thread_number, int max_requests):m_thread_number(thread_number), m_max_requests(max_requests),
    m_stop(false), m_thread(nullptr){
        if(thread_number <= 0 || max_requests <= 0){
            throw std::exception();
        }

        m_thread = new pthread_t[m_thread_number];

        if(!m_thread){
            throw std::exception();
        }

        //创建thread_number个线程，并设置成脱离
        for(int i = 0; i < m_thread_number; i++){
            printf("create the %dth thread", i);
            if(pthread_create(m_thread + i, NULL, worker, this) != 0){
                delete [] m_thread;
                throw std::exception();
            }

            if(pthread_detach(m_thread[i])){
                delete [] m_thread;
                throw std::exception();
            }
        }
}

template< typename T >
threadpool< T >::~threadpool() {
    delete [] m_thread;
    m_stop = true;
}

template <typename T>
bool threadpool<T>::append(T* request){
    m_queue_lock.lock();
    if(m_workqueue.size() >= m_max_requests){
        m_queue_lock.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queue_lock.unlock();
    m_queue_stat.post();
    return true;
}

template <typename T>
void * threadpool<T>::worker(void* arg){
    threadpool* pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run(){
    //每个线程轮询工作队列是否有任务
    while(!m_stop){
        m_queue_stat.wait();
        m_queue_lock.lock();
        if(m_workqueue.empty()){
            m_queue_lock.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queue_lock.unlock();
        if(!request){
            continue;
        }
        request->process();
    }
}

#endif