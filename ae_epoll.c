/* Linux epoll(2) based ae.c module
 * Linux平台事件触发库底层实现
 *
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include <sys/epoll.h>

// 作为事件处理器的apidata存储结构
typedef struct aeApiState {
    // epoll事件描述符
    int epfd;
    // 存储epoll_wait返回后得到的事件列表
    struct epoll_event *events;
} aeApiState;

/*
 * 创建epoll实例
 * 
 * eventLoop 事件处理器指针
 *
 */
static int aeApiCreate(aeEventLoop *eventLoop) {
    // 分配state内存空间
    aeApiState *state = zmalloc(sizeof(aeApiState));

    // 分配内存失败，返回-1
    if (!state) return -1;
    
    // 创建事件列表
    state->events = zmalloc(sizeof(struct epoll_event)*eventLoop->setsize);
    
    // 分配内存失败
    if (!state->events) {
        // 释放state
        zfree(state);
        // 返回-1
        return -1;
    }
    
    // 创建epoll实例
    state->epfd = epoll_create(1024); /* 1024 is just a hint for the kernel */
    
    // 分配内存失败
    if (state->epfd == -1) {
        // 释放事件列表
        zfree(state->events);
        // 释放state
        zfree(state);
        // 返回-1
        return -1;
    }
    
    // 设置事件处理器的apidata数据
    eventLoop->apidata = state;
    // 返回0，表示成功
    return 0;
}

/*
 * 重置事件处理器已追踪的最大文件描述符
 *
 * eventLoop 事件处理器指针
 * setsize 
 *
 */
static int aeApiResize(aeEventLoop *eventLoop, int setsize) {
    // 获取事件处理器事件轮询API数据
    aeApiState *state = eventLoop->apidata;

    // 重新分配事件轮询API数据内存大小, 大小为epoll实例大小乘以setsize
    state->events = zrealloc(state->events, sizeof(struct epoll_event)*setsize);
    
    // 返回0，表示成功
    return 0;
}

/*
 * 释放epoll实例与事件列表
 * 
 * eventLoop 事件处理器指针
 *
 */
static void aeApiFree(aeEventLoop *eventLoop) {
    // 获取事件处理器事件轮询API数据
    aeApiState *state = eventLoop->apidata;
    
    // 关闭epoll事件描述符
    close(state->epfd);
    // 释放事件列表
    zfree(state->events);
    // 释放state
    zfree(state);
}

/*
 * 添加文件事件
 *
 * eventLoop 事件处理器指针
 * fd 已就绪事件文件描述符
 * mask 事件类型掩码
 *
 */
static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask) {
    // 获取事件处理器事件轮询API数据
    aeApiState *state = eventLoop->apidata;
    
    // 定义事件列表结构体
    struct epoll_event ee;
    
    /* If the fd was already monitored for some event, we need a MOD
     * operation. Otherwise we need an ADD operation. */
    // fd不存在，模式为EPOLL_CTL_ADD
    // fd存在，模式为EPOLL_CTL_MOD
    int op = eventLoop->events[fd].mask == AE_NONE ?
            EPOLL_CTL_ADD : EPOLL_CTL_MOD;

    // 初始化事件列表
    ee.events = 0;
    // 合并之前的事件列表
    mask |= eventLoop->events[fd].mask; /* Merge old events */
    // 如果事件类型为读，将事件设为EPOLLIN
    if (mask & AE_READABLE) ee.events |= EPOLLIN;
    // 如果事件类型为写，将事件设为EPOLLOUT
    if (mask & AE_WRITABLE) ee.events |= EPOLLOUT;
    // 避免堆内存警告
    ee.data.u64 = 0; /* avoid valgrind warning */
    // 设置事件列表的已就绪事件文件描述符
    ee.data.fd = fd;
    // 操作epoll实例，添加事件文件描述符，失败返回-1
    if (epoll_ctl(state->epfd,op,fd,&ee) == -1) return -1;
    // 成功返回0
    return 0;
}

/*
 * 删除事件
 *
 * eventLoop 事件处理器指针
 * fd 已就绪事件文件描述符
 * delmask 删除事件类型掩码
 *
 */
static void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int delmask) {
    // 获取事件处理器事件轮询API数据
    aeApiState *state = eventLoop->apidata;
    
    // 定义事件列表结构体
    struct epoll_event ee;
    
    // 获取待删除事件的类型掩码
    int mask = eventLoop->events[fd].mask & (~delmask);

    // 初始化事件列表
    ee.events = 0;
    
    // 如果事件类型为读，将事件设为EPOLLIN
    if (mask & AE_READABLE) ee.events |= EPOLLIN;
    // 如果事件类型为写，将事件设为EPOLLOUT
    if (mask & AE_WRITABLE) ee.events |= EPOLLOUT;
    // 避免堆内存警告
    ee.data.u64 = 0; /* avoid valgrind warning */
    // 设置事件列表的已就绪事件文件描述符
    ee.data.fd = fd;
    
    // 事件掩码不为未设置
    if (mask != AE_NONE) {
        // 操作epoll实例，更改事件文件描述符
        epoll_ctl(state->epfd,EPOLL_CTL_MOD,fd,&ee);
    } else {
        /* Note, Kernel < 2.6.9 requires a non null event pointer even for
         * EPOLL_CTL_DEL. */
        // 操作epoll实例，删除事件文件描述符
        epoll_ctl(state->epfd,EPOLL_CTL_DEL,fd,&ee);
    }
}

/*
 * 获取可执行的事件
 *
 * eventLoop 事件处理器指针
 * tvp timeval时间指针
 *
 */
static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    // 获取事件处理器事件轮询API数据
    aeApiState *state = eventLoop->apidata;
    
    // 初始化返回时间与事件数量
    int retval, numevents = 0;

    // 调用epoll_wait方法，返回-1或一个整数，大于-1时表示需要循环处理的事件数量
    retval = epoll_wait(state->epfd,state->events,eventLoop->setsize,
            tvp ? (tvp->tv_sec*1000 + tvp->tv_usec/1000) : -1);
    
    // 存在需要循环处理的事件
    if (retval > 0) {
        // 初始化循环变量
        int j;
        
        // 事件量数等于retval
        numevents = retval;
        
        // 遍历事件
        for (j = 0; j < numevents; j++) {
            // 初始化事件类型掩码
            int mask = 0;
            // 事件列表
            struct epoll_event *e = state->events+j;

            // 设置事件类型掩码
            // 如果事件类型为EPOLLIN，将事件设为读
            if (e->events & EPOLLIN) mask |= AE_READABLE;
            // 如果事件类型为EPOLLOUT，将事件设为写
            if (e->events & EPOLLOUT) mask |= AE_WRITABLE;
            // 如果事件类型为EPOLLERR，将事件设为写
            if (e->events & EPOLLERR) mask |= AE_WRITABLE;
            // 如果事件类型为EPOLLHUP，将事件设为写
            if (e->events & EPOLLHUP) mask |= AE_WRITABLE;
            // 设置事件列表的已就绪事件文件描述符
            eventLoop->fired[j].fd = e->data.fd;
            // 设置事件列表的已就绪事件文件类型掩码
            eventLoop->fired[j].mask = mask;
        }
    }
    
    // 返回待处理事件数量
    return numevents;
}

/*
 * 返回所使用的复用库的名称
 *
 */
static char *aeApiName(void) {
    return "epoll";
}
