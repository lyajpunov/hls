#ifndef _THREADPOOL_H
#define _THREADPOOL_H

#include <pthread.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include <queue>


// 定义任务结构体
using callback = void (*)(void *);
struct Task
{
    callback function;
    void *arg;
    Task() : function(nullptr), arg(nullptr){};
    Task(callback f, void *arg_f) : function(f), arg(arg_f){};
};


// 任务队列
class TaskQueue
{
public:
    TaskQueue()
    {
        pthread_mutex_init(&m_mutex, NULL);
    };
    ~TaskQueue()
    {
        pthread_mutex_destroy(&m_mutex);
    };

    // 添加任务
    inline void addTask(Task &task)
    {
        pthread_mutex_lock(&m_mutex);
        m_queue.push(task);
        pthread_mutex_unlock(&m_mutex);
    }

    // 添加任务
    inline void addTask(callback func, void *arg)
    {
        pthread_mutex_lock(&m_mutex);
        Task task(func, arg);
        m_queue.push(task);
        pthread_mutex_unlock(&m_mutex);
    };

    // 取出一个任务
    inline Task takeTask()
    {
        Task t;
        pthread_mutex_lock(&m_mutex);
        if (!m_queue.empty())
        {
            t = m_queue.front();
            m_queue.pop();
        }
        pthread_mutex_unlock(&m_mutex);
        return t;
    };

    // 获取当前队列中任务个数
    inline int taskNumber()
    {
        return m_queue.size();
    }

    inline bool empty() {
        return m_queue.empty();
    }

private:
    pthread_mutex_t m_mutex;  // 互斥锁
    std::queue<Task> m_queue; // 任务队列
};

class ThreadPool
{
public:
    ThreadPool(int min, int max);
    ThreadPool() : ThreadPool(5, 20) {}
    ~ThreadPool();

    // 添加任务
    void addTask(Task task);
    // 添加任务
    void addTask(callback func, void *arg);
    // 获取忙线程的个数
    int getBusyNumber();
    // 获取活着的线程个数
    int getAliveNumber();

private:
    // 工作的线程的任务函数
    static void *worker(void *arg);
    // 管理者线程的任务函数
    static void *manager(void *arg);
    void threadExit();

private:
    pthread_mutex_t m_lock;
    pthread_cond_t m_notEmpty;
    pthread_t *m_threadIDs;
    pthread_t m_managerID;
    TaskQueue *m_taskQ;
    int m_minNum;
    int m_maxNum;
    int m_busyNum;
    int m_aliveNum;
    int m_exitNum;
    bool m_shutdown;
};

ThreadPool::ThreadPool(int min, int max) : m_minNum(min), m_maxNum(max), m_busyNum(0), m_aliveNum(min), m_exitNum(0), m_shutdown(false)
{
    // 实例化任务队列
    m_taskQ = new TaskQueue;
    // 给线程数组分配内存
    m_threadIDs = new pthread_t[m_maxNum];
    memset(m_threadIDs, 0, sizeof(pthread_t) * m_maxNum);
    // 初始化锁和条件变量
    pthread_mutex_init(&m_lock, NULL);
    pthread_cond_init(&m_notEmpty,NULL);
    // 创建管理者线程
    pthread_create(&m_managerID, NULL, manager, this);
    // 创建工作者线程
    for (int i=0; i<min; i++) {
        pthread_create(&m_threadIDs[i], NULL, worker, this);
    }
}

ThreadPool::~ThreadPool() {
    this->m_shutdown = true;
    // 销毁管理者线程
    pthread_join(m_managerID, NULL);
    // 唤醒所有的消费者线程
    for (int i = 0; i < m_aliveNum; ++i) {
        pthread_cond_signal(&m_notEmpty);
    }
    // 销毁任务队列
    if (m_taskQ) delete m_taskQ;
    // 销毁保存消费者ID的数组
    if (m_threadIDs) delete[]m_threadIDs;
    // 销毁锁
    pthread_mutex_destroy(&m_lock);
    // 销毁条件变量
    pthread_cond_destroy(&m_notEmpty);
}

void ThreadPool::addTask(Task task)
{
    if (m_shutdown) return;
    // 添加任务
    m_taskQ->addTask(task);
    // 唤醒一个工作处理线程
    pthread_cond_signal(&m_notEmpty);
}

void ThreadPool::addTask(callback func, void *arg)
{
    if (m_shutdown) return;
    // 添加任务
    m_taskQ->addTask(func,arg);
    // 唤醒一个工作处理线程
    pthread_cond_signal(&m_notEmpty);
}

// 工作线程任务函数
void* ThreadPool::worker(void* arg) {
    // 将传入的参数强制转换为ThreadPool*指针类型
    ThreadPool* pool = static_cast<ThreadPool*>(arg);
    while (true) {
        // 访问任务队列先要加锁
        pthread_mutex_lock(&pool->m_lock);
        // 任务为空则线程阻塞
        while (pool->m_taskQ->empty() && !pool->m_shutdown) {
            // 阻塞线程在信号量m_notEmpty上
            pthread_cond_wait(&pool->m_notEmpty, &pool->m_lock);
            // 解除阻塞之后判断是否要销毁线程
            if (pool->m_exitNum > 0) {
                pool->m_exitNum--;
                if (pool->m_aliveNum > pool->m_minNum) {
                    pool->m_aliveNum--;
                    pthread_mutex_unlock(&pool->m_lock);
                    pool->threadExit();
                }
            }
        }
        // 如果线程池要结束了
        if (pool->m_shutdown) {
            // 解锁
            pthread_mutex_unlock(&pool->m_lock);
            // 销毁线程
            pool->threadExit();
        }

        // 从任务队列中取出一个任务
        Task task = pool->m_taskQ->takeTask();
        // 工作的线程加1
        pool->m_busyNum++;
        // 解锁，下面要开始执行了
        pthread_mutex_unlock(&pool->m_lock);
        
        // 执行任务
        task.function(task.arg);
        // 销毁参数指针
        free(task.arg);
        task.arg = nullptr;
        
        // 工作的线程减1
        pthread_mutex_lock(&pool->m_lock);
        pool->m_busyNum--;
        pthread_mutex_unlock(&pool->m_lock);
    }
    return nullptr;
}

// 管理者线程任务函数
void* ThreadPool::manager(void* arg)
{
    ThreadPool* pool = static_cast<ThreadPool*>(arg);

    // 如果线程池没有关闭就一直检测
    while (!pool->m_shutdown) {
        // 每5s监控一次线程池状态
        sleep(5);
        // 取出任务数量和线程数量
        pthread_mutex_lock(&pool->m_lock);
        int queuesize = pool->m_taskQ->taskNumber();
        int liveNum = pool->m_aliveNum;
        int busyNum = pool->m_busyNum;
        pthread_mutex_unlock(&pool->m_lock);

        // 创建线程
        const int NUMBER = 2;
        // 当前任务太多了，需要增加线程处理
        if (queuesize > liveNum && liveNum < pool->m_maxNum) {
            // 线程池加锁
            pthread_mutex_lock(&pool->m_lock);
            int num = 0;
            for (int i = 0; i < pool->m_maxNum && num < NUMBER && pool->m_aliveNum < pool->m_maxNum; ++i)
            {
                if (pool->m_threadIDs[i] == 0)
                {
                    pthread_create(&pool->m_threadIDs[i], NULL, worker, pool);
                    num++;
                    pool->m_aliveNum++;
                }
            }
            // 线程池解锁
            pthread_mutex_unlock(&pool->m_lock);
        }
        // 当前任务太少了，需要减少线程，减轻系统负担
        if (busyNum * 2 < liveNum && liveNum > pool->m_minNum + NUMBER) {
            // 线程池加锁
            pthread_mutex_lock(&pool->m_lock);
            pool->m_exitNum = NUMBER;
            // 线程池解锁
            pthread_mutex_unlock(&pool->m_lock);
            
            // 唤醒线程，自动删除自己
            for (int i = 0; i < NUMBER; ++i) {
                pthread_cond_signal(&pool->m_notEmpty);
            }
        }
    }
    return nullptr;
}

void ThreadPool::threadExit()
{
    pthread_t tid = pthread_self();
    for (int i = 0; i < m_maxNum; ++i)
    {
        if (m_threadIDs[i] == tid) {
            m_threadIDs[i] = 0;
            break;
        }
    }
    pthread_exit(NULL);
}


#endif