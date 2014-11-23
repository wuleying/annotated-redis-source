/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
 * 事件驱动库
 *
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
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

#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "ae.h"
#include "zmalloc.h"
#include "config.h"

/* Include the best multiplexing layer supported by this system.
 * The following should be ordered by performances, descending. */
#ifdef HAVE_EVPORT
// 包含事件端口模块(Illumos)
#include "ae_evport.c"
#else
    #ifdef HAVE_EPOLL
    // 包含epoll接口模块(Linux)
    #include "ae_epoll.c"
    #else
        #ifdef HAVE_KQUEUE
        // 包含kqueue接口模块(FreeBSD)
        #include "ae_kqueue.c"
        #else
        // 包含select接口模块(Windows)
        #include "ae_select.c"
        #endif
    #endif
#endif

/*
 * 创建事件处理器
 *
 * setsize 已追踪的最大文件描述符
 *
 */
aeEventLoop *aeCreateEventLoop(int setsize) {

    // 事件处理器
    aeEventLoop *eventLoop;
    // 循环计数器
    int i;

    // 创建事件处理器
    if ((eventLoop = zmalloc(sizeof(*eventLoop))) == NULL) goto err;

    // 初始化文件事件结构
    eventLoop->events = zmalloc(sizeof(aeFileEvent)*setsize);
    // 初始化已就绪事件结构
    eventLoop->fired = zmalloc(sizeof(aeFiredEvent)*setsize);

    // 检查文件事件与已就绪事件是否为NULL
    if (eventLoop->events == NULL || eventLoop->fired == NULL) goto err;

    // 设置已追踪的最大文件描述符
    eventLoop->setsize = setsize;
    // 设置最后一次执行事件的时间
    eventLoop->lastTime = time(NULL);
    // 设置时间事件头
    eventLoop->timeEventHead = NULL;
    // 设置下个事件ID
    eventLoop->timeEventNextId = 0;
    // 标记为启动事件
    eventLoop->stop = 0;
    // 设置当前已注册的最大文件描述符
    eventLoop->maxfd = -1;
    // 设置事件处理前的sleep方法
    eventLoop->beforesleep = NULL;

    // 创建事件API
    if (aeApiCreate(eventLoop) == -1) goto err;

    /* Events with mask == AE_NONE are not set. So let's initialize the
     * vector with it. */
    // 初始化事件状态类型为未设置
    for (i = 0; i < setsize; i++)
        eventLoop->events[i].mask = AE_NONE;

    // 返回事件处理器
    return eventLoop;

// 错误处理
err:
    if (eventLoop) {
        // 释放文件事件
        zfree(eventLoop->events);
        // 释放已就绪事件
        zfree(eventLoop->fired);
        // 释放事件处理器
        zfree(eventLoop);
    }
    // 返回NULL
    return NULL;
}

/* Return the current set size. */

/*
 * 获取当前已追踪的最大文件描述符
 *
 * eventLoop 事件处理器指针
 *
 */
int aeGetSetSize(aeEventLoop *eventLoop) {
    // 返回当前已追踪的最大文件描述符
    return eventLoop->setsize;
}

/* Resize the maximum set size of the event loop.
 * If the requested set size is smaller than the current set size, but
 * there is already a file descriptor in use that is >= the requested
 * set size minus one, AE_ERR is returned and the operation is not
 * performed at all.
 *
 * Otherwise AE_OK is returned and the operation is successful. */

/*
 * 重置事件处理器已追踪的最大文件描述符
 *
 * eventLoop 事件处理器指针
 * setsize 已追踪的最大文件描述符
 *
 */
int aeResizeSetSize(aeEventLoop *eventLoop, int setsize) {
    // 循环计数器
    int i;

    // 设置的最大文件描述符与当前事件处理器已追踪的最大文件描述符一致，返回成功状态
    if (setsize == eventLoop->setsize) return AE_OK;
    // 已注册的最大文件描述符大于或等于 设置的最大文件描述符，返回失败状态
    if (eventLoop->maxfd >= setsize) return AE_ERR;
    // 使用对应平台模块中的aeApiResize方法
    if (aeApiResize(eventLoop,setsize) == -1) return AE_ERR;

    // 为已注册的文件事件重新分配内存
    eventLoop->events = zrealloc(eventLoop->events,sizeof(aeFileEvent)*setsize);
    // 为已就绪的事件重新分配内存
    eventLoop->fired = zrealloc(eventLoop->fired,sizeof(aeFiredEvent)*setsize);
    // 设置当前已追踪的最大文件描述符
    eventLoop->setsize = setsize;

    /* Make sure that if we created new slots, they are initialized with
     * an AE_NONE mask. */
    // 初始化事件状态类型为未设置
    for (i = eventLoop->maxfd+1; i < setsize; i++)
        eventLoop->events[i].mask = AE_NONE;

    // 返回成功状态
    return AE_OK;
}

/*
 * 删除事件处理器
 *
 * eventLoop 事件处理器指针
 *
 */
void aeDeleteEventLoop(aeEventLoop *eventLoop) {
    // 使用对应平台模块中的aeApiFree方法
    aeApiFree(eventLoop);
    // 释放已注册的文件事件
    zfree(eventLoop->events);
    // 释放已就绪的事件
    zfree(eventLoop->fired);
    // 释放整个事件处理器
    zfree(eventLoop);
}

/*
 * 停止事件处理器
 *
 * eventLoop 事件处理器指针
 *
 */
void aeStop(aeEventLoop *eventLoop) {
    // 标记为停止状态
    eventLoop->stop = 1;
}

/*
 * 创建文件事件
 *
 * eventLoop 事件处理器指针
 * fd 事件文件描述符
 * mask 事件类型掩码
 * proc 文件事件方法
 * clientData 复用库的私有数据
 *
 */
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask,
        aeFileProc *proc, void *clientData)
{
    // 事件文件描述符大于等于已追踪的最大文件描述符，返回错误号与错误状态
    if (fd >= eventLoop->setsize) {
        errno = ERANGE;
        return AE_ERR;
    }
    
    // 获取文件描述符对应的文件事件指针
    aeFileEvent *fe = &eventLoop->events[fd];

    // 使用对应平台模块中的aeApiAddEvent方法，如果出错返回错误状态
    if (aeApiAddEvent(eventLoop, fd, mask) == -1)
        return AE_ERR;
    
    // 设置事件类型掩码
    fe->mask |= mask;
    
    // 文件事件状态可读时设置读文件方法
    if (mask & AE_READABLE) fe->rfileProc = proc;
    // 文件事件状态可写时设置写文件方法
    if (mask & AE_WRITABLE) fe->wfileProc = proc;
    // 设置复用库的私有数据
    fe->clientData = clientData;
    
    // 事件文件描述符大于当前已注册的最大文件描述符
    if (fd > eventLoop->maxfd)
        // 将已注册的最大文件描述符设为当前事件文件描述符
        eventLoop->maxfd = fd;
    
    // 返回成功状态
    return AE_OK;
}

/*
 * 删除文件事件
 *
 * eventLoop 事件处理器指针
 * fd 事件文件描述符
 * mask 事件类型掩码
 *
 */
void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask)
{
    // 事件文件描述符大于等于已追踪的最大文件描述符，直接返回
    if (fd >= eventLoop->setsize) return;
    
    // 获取文件描述符对应的文件事件指针
    aeFileEvent *fe = &eventLoop->events[fd];
    
    // 事件类型掩码为未设置，直接返回
    if (fe->mask == AE_NONE) return;

    // 使用对应平台模块中的aeApiDelEvent方法
    aeApiDelEvent(eventLoop, fd, mask);
    
    // 设置事件类型掩码，事件类型掩码取反然后与当前事件类型进行按位与运算
    fe->mask = fe->mask & (~mask);
    
    // 当前事件文件描述符等于已注册的最大文件描述符并且当前文件事件状态为未设置
    if (fd == eventLoop->maxfd && fe->mask == AE_NONE) {
        /* Update the max fd */
        // 循环计数器
        int j;

        // 获取最大文件描述符
        for (j = eventLoop->maxfd-1; j >= 0; j--)
            if (eventLoop->events[j].mask != AE_NONE) break;
        
        // 更新已注册的最大文件描述符
        eventLoop->maxfd = j;
    }
}

/*
 * 获取文件事件
 * 
 * eventLoop 事件处理器指针
 * fd 事件文件描述符
 *
 */
int aeGetFileEvents(aeEventLoop *eventLoop, int fd) {
    
    // 事件文件描述符大于等于已追踪的最大文件描述符，直接返回0
    if (fd >= eventLoop->setsize) return 0;
    
    // 获取文件描述符对应的文件事件指针
    aeFileEvent *fe = &eventLoop->events[fd];

    // 返回文件事件类型掩码
    return fe->mask;
}

/*
 * 获取当前时间 秒与毫秒
 *
 * seconds 秒
 * milliseconds 毫秒
 *
 */
static void aeGetTime(long *seconds, long *milliseconds)
{
    // 定义timeval结构体
    struct timeval tv;

    // 获得当前精确时间
    gettimeofday(&tv, NULL);
    // 秒
    *seconds = tv.tv_sec;
    // 毫秒
    *milliseconds = tv.tv_usec/1000;
}

/*
 * 给当前时间加上N毫秒
 *
 * milliseconds 毫秒
 * sec 秒
 * ms 毫秒
 *
 */
static void aeAddMillisecondsToNow(long long milliseconds, long *sec, long *ms) {
    
    // 初始化要用到的变量
    long cur_sec, cur_ms, when_sec, when_ms;

    // 获取当前时间
    aeGetTime(&cur_sec, &cur_ms);
    
    // 获取当前时间加上N毫秒后的秒数
    when_sec = cur_sec + milliseconds/1000;
    // 获取当前时间加上N毫秒后的毫秒数
    when_ms = cur_ms + milliseconds%1000;
    
    // 再对毫秒数进行一次处理，大于1000毫秒将秒数加1，毫秒数减1000
    if (when_ms >= 1000) {
        when_sec ++;
        when_ms -= 1000;
    }
    
    // 最终得到的秒与毫秒数
    *sec = when_sec;
    *ms = when_ms;
}

/*
 * 创建时间事件
 * 
 * eventLoop 事件处理器指针
 * milliseconds 毫秒数
 * proc 时间事件处理方法
 * clientData 复用库的私有数据
 * finalizerProc 时间事件释放方法
 *
 */
long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds,
        aeTimeProc *proc, void *clientData,
        aeEventFinalizerProc *finalizerProc)
{
    // 将时间事件ID加1
    long long id = eventLoop->timeEventNextId++;
    
    // 定义时间事件指针
    aeTimeEvent *te;

    // 分配内存
    te = zmalloc(sizeof(*te));
    
    // 分配内存失败返回错误状态
    if (te == NULL) return AE_ERR;
    
    // 设置时间事件ID
    te->id = id;
    // 设置时间事件到达的秒与毫秒数
    aeAddMillisecondsToNow(milliseconds,&te->when_sec,&te->when_ms);
    // 设置时间事件处理方法
    te->timeProc = proc;
    // 设置时间事件释放方法
    te->finalizerProc = finalizerProc;
    // 设置复用库的私有数据
    te->clientData = clientData;
    // 设置指向下个时间事件的指针
    te->next = eventLoop->timeEventHead;
    // 将事件处理器的时间事件头设为当前时间事件
    eventLoop->timeEventHead = te;
    // 返回时间事件ID
    return id;
}

/*
 * 删除时间事件
 *
 * eventLoop 事件处理器指针
 * id 时间事件ID
 *
 */
int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id)
{
    // 时间事件，前驱
    aeTimeEvent *te, *prev = NULL;

    // 获取事件处理器事件头事件
    te = eventLoop->timeEventHead;
    
    // 遍历时间事件
    while(te) {
        // 找到时间ID匹配的时间事件
        if (te->id == id) {
            // 前驱为空
            if (prev == NULL)
                // 将时间事件头设为下一个时间事件
                eventLoop->timeEventHead = te->next;
            else
                // 将前驱的后续设为此时间事件的后续
                prev->next = te->next;
            // 如果定义了时间事件释放方法
            if (te->finalizerProc)
                // 使用自定义方法释放事件事件复用库的私有数据
                te->finalizerProc(eventLoop, te->clientData);
            
            // 释放时间事件
            zfree(te);
            
            // 返回成功状态
            return AE_OK;
        }
        
        // 不匹配，将当前时间事件设为前驱
        prev = te;
        // 处理下一个事件事件
        te = te->next;
    }
    
    // 遍历所有时间事件，没有匹配，返回错误状态
    return AE_ERR; /* NO event with the specified ID found */
}

/* Search the first timer to fire.
 * This operation is useful to know how many time the select can be
 * put in sleep without to delay any event.
 * If there are no timers NULL is returned.
 *
 * Note that's O(N) since time events are unsorted.
 * Possible optimizations (not needed by Redis so far, but...):
 * 1) Insert the event in order, so that the nearest is just the head.
 *    Much better but still insertion or deletion of timers is O(N).
 * 2) Use a skiplist to have this operation as O(1) and insertion as O(log(N)).
 */

/*
 * 找到离当前最近的时间事件
 *
 * eventLoop 事件处理器指针
 *
 */
static aeTimeEvent *aeSearchNearestTimer(aeEventLoop *eventLoop)
{
    // 获取事件处理器事件头事件
    aeTimeEvent *te = eventLoop->timeEventHead;
    // 初始化离当前最近的时间事件，设为NULL
    aeTimeEvent *nearest = NULL;

    // 遍历时间事件
    while(te) {
        // 如果未找到最近的时间或者对比当前处理的时间事件到达时间比之前已获取的时间事件的到达时间小，则替换为新的离当前最近的时间事件
        if (!nearest || te->when_sec < nearest->when_sec ||
                (te->when_sec == nearest->when_sec &&
                 te->when_ms < nearest->when_ms))
            // 将离当前最近时间事件设为当前处理的时间事件
            nearest = te;
        
        // 将下一个时间事件设为当前处理的时间事件
        te = te->next;
    }
    
    // 返回最终获取的离当前最近的时间事件
    return nearest;
}

/* Process time events */
/*
 * 处理所有已到达的时间事件
 *
 * eventLoop 事件处理器指针
 *
 */
static int processTimeEvents(aeEventLoop *eventLoop) {
    
    // 初始化已处理的事件计数器
    int processed = 0;
    // 定义时间事件
    aeTimeEvent *te;
    // 定义最大的时间事件ID
    long long maxId;
    // 获取当前时间
    time_t now = time(NULL);

    /* If the system clock is moved to the future, and then set back to the
     * right value, time events may be delayed in a random way. Often this
     * means that scheduled operations will not be performed soon enough.
     *
     * Here we try to detect system clock skews, and force all the time
     * events to be processed ASAP when this happens: the idea is that
     * processing events earlier is less dangerous than delaying them
     * indefinitely, and practice suggests it is. */
    
    // 当前时间小于最后一次执行事件的时间，表示系统时间移动到了未来时间后又回调到了正确的时间，这里的处理是为了防止这种情况的出现
    if (now < eventLoop->lastTime) {
        // 获取事件处理器事件头事件
        te = eventLoop->timeEventHead;
        
        // 遍历事件处理器
        while(te) {
            // 将事件到达时间设为0
            te->when_sec = 0;
            // 继续处理下个事件
            te = te->next;
        }
    }
    // 将最后一次执行事件的时间设为当前时间
    eventLoop->lastTime = now;

    // 重新获取事件处理器事件头事件
    te = eventLoop->timeEventHead;
    // 将最大事件ID设为下个事件ID减1
    maxId = eventLoop->timeEventNextId-1;
    
    // 遍历事件处理器
    while(te) {
        // 时间事件到达的秒与微秒
        long now_sec, now_ms;
        // 时间事件ID
        long long id;

        // 当前时间事件的ID大于最大事件ID
        if (te->id > maxId) {
            // 处理下一个时间事件
            te = te->next;
            // 跳到下次循环
            continue;
        }
        
        // 调用对应复用库中的aeGetTime方法
        aeGetTime(&now_sec, &now_ms);
        
        // 当前时间大于时间事件到达时间，表示时间事件需要执行了
        if (now_sec > te->when_sec ||
            (now_sec == te->when_sec && now_ms >= te->when_ms))
        {
            // 定义retval变量
            int retval;

            // 获取当前处理的时间事件ID
            id = te->id;
            
            // 调用时间事件处理方法，返回-1或一个整数，大于-1时表示这个事件需要循环片理
            retval = te->timeProc(eventLoop, id, te->clientData);
            
            // 已处理的事件计数器加1
            processed++;
            /* After an event is processed our time event list may
             * no longer be the same, so we restart from head.
             * Still we make sure to don't process events registered
             * by event handlers itself in order to don't loop forever.
             * To do so we saved the max ID we want to handle.
             *
             * FUTURE OPTIMIZATIONS:
             * Note that this is NOT great algorithmically. Redis uses
             * a single time event so it's not a problem but the right
             * way to do this is to add the new elements on head, and
             * to flag deleted elements in a special way for later
             * deletion (putting references to the nodes to delete into
             * another linked list). */
            
            // 是否有需要循环执行这个事件时间
            if (retval != AE_NOMORE) {
                // retval毫秒之后继续执行这个时间事件
                aeAddMillisecondsToNow(retval,&te->when_sec,&te->when_ms);
            } else {
                // 删除时间事件
                aeDeleteTimeEvent(eventLoop, id);
            }
            // 前当前处理事件设为时间处理器的头事件，继续循环
            te = eventLoop->timeEventHead;
        } else {
            // 将当前处理事件设为下一个时间事件，继续循环
            te = te->next;
        }
    }
    
    // 返回已处理的时间事件数量
    return processed;
}

/* Process every pending time event, then every pending file event
 * (that may be registered by time event callbacks just processed).
 * Without special flags the function sleeps until some file event
 * fires, or when the next time event occurs (if any).
 *
 * If flags is 0, the function does nothing and returns.
 * if flags has AE_ALL_EVENTS set, all the kind of events are processed.
 * if flags has AE_FILE_EVENTS set, file events are processed.
 * if flags has AE_TIME_EVENTS set, time events are processed.
 * if flags has AE_DONT_WAIT set the function returns ASAP until all
 * the events that's possible to process without to wait are processed.
 *
 * The function returns the number of events processed. */

/*
 * 处理所有已到达的时间事件以及所有已就绪的文件事件
 *
 * eventLoop 事件处理器指针
 * flags 事件处理旗标
 *       0 事件不处理直接返回
 *       AE_ALL_EVENTS 所有类型的事件都会被处理
 *       AE_FILE_EVENTS 处理文件事件
 *       AE_TIME_EVENTS 处理时间事件
 *       AE_DONT_WAIT 处理完所有不许阻塞的事件之后立即返回
 *
 */
int aeProcessEvents(aeEventLoop *eventLoop, int flags)
{
    // 初始化已处理的事件计数器，定义事件数量变量
    int processed = 0, numevents;

    /* Nothing to do? return ASAP */
    // 什么也不做，直接返回0
    if (!(flags & AE_TIME_EVENTS) && !(flags & AE_FILE_EVENTS)) return 0;

    /* Note that we want call select() even if there are no
     * file events to process as long as we want to process time
     * events, in order to sleep until the next time event is ready
     * to fire. */
    // 当前已注册的最大文件描述符不为-1或旗标为处理时间事件并且不为不许阻塞的事件
    if (eventLoop->maxfd != -1 ||
        ((flags & AE_TIME_EVENTS) && !(flags & AE_DONT_WAIT))) {
        
        // 定义循环计数器
        int j;
        // 定义时间事件指针
        aeTimeEvent *shortest = NULL;
        // 定义timeval结构体与指针
        struct timeval tv, *tvp;

        // 旗标为处理时间事件并且不为不许阻塞的事件
        if (flags & AE_TIME_EVENTS && !(flags & AE_DONT_WAIT))
            // 获取离当前最近需要处理的时间事件
            shortest = aeSearchNearestTimer(eventLoop);
        
        // 如果存在需要处理的时间事件
        if (shortest) {
            // 定义当前时间秒与微秒
            long now_sec, now_ms;

            /* Calculate the time missing for the nearest
             * timer to fire. */
            // 获取当前时间
            aeGetTime(&now_sec, &now_ms);
            
            // 设置timeval结构体指针
            tvp = &tv;
            
            // 计算距今最近的时间事件还要多久才能达到
            tvp->tv_sec = shortest->when_sec - now_sec;
            
            // 最近的时间事件到达时间小于当前时间
            if (shortest->when_ms < now_ms) {
                // 获取时间差
                tvp->tv_usec = ((shortest->when_ms+1000) - now_ms)*1000;
                tvp->tv_sec --;
            } else {
                // 获取时间差
                tvp->tv_usec = (shortest->when_ms - now_ms)*1000;
            }
            
            // 时间差小于0，表示事件可以执行，并将时间设置为0
            if (tvp->tv_sec < 0) tvp->tv_sec = 0;
            if (tvp->tv_usec < 0) tvp->tv_usec = 0;
        } else {
            /* If we have to check for events but need to return
             * ASAP because of AE_DONT_WAIT we need to set the timeout
             * to zero */
            
            // 没有要处理的时间事件
            
            // 根据AE_DONT_WAIT是否设置来决定是否阻塞，以及阻塞的时间长度
            if (flags & AE_DONT_WAIT) {
                // 设置文件事件不阻塞
                tv.tv_sec = tv.tv_usec = 0;
                tvp = &tv;
            } else {
                /* Otherwise we can block */
                // 文件事件可以阻塞直到有事件到达为止
                tvp = NULL; /* wait forever */
            }
        }

        // 获取已就绪事件数量
        numevents = aeApiPoll(eventLoop, tvp);
        
        // 遍历事件
        for (j = 0; j < numevents; j++) {
            // 从已就绪数组中获取事件
            aeFileEvent *fe = &eventLoop->events[eventLoop->fired[j].fd];
            // 获取已就绪事件类型
            int mask = eventLoop->fired[j].mask;
            // 获取已就绪事件文件描述符
            int fd = eventLoop->fired[j].fd;
            // 是否为读事件
            int rfired = 0;

	    /* note the fe->mask & mask & ... code: maybe an already processed
             * event removed an element that fired and we still didn't
             * processed, so we check if the event is still valid. */
            // 读事件
            if (fe->mask & mask & AE_READABLE) {
                // 标记为读事件，确保读与写事件只能执行其中一个
                rfired = 1;
                // 调用读文件方法
                fe->rfileProc(eventLoop,fd,fe->clientData,mask);
            }
            
            // 写事件
            if (fe->mask & mask & AE_WRITABLE) {
                if (!rfired || fe->wfileProc != fe->rfileProc)
                    // 调用写文件方法
                    fe->wfileProc(eventLoop,fd,fe->clientData,mask);
            }
            
            // 已处理事件加1
            processed++;
        }
    }
    
    /* Check time events */
    // 处理时间事件
    if (flags & AE_TIME_EVENTS)
        // 使用processTimeEvents函数执行时间事件，并将已处理的事件数量累加
        processed += processTimeEvents(eventLoop);

    // 返回已处理的事件数量
    return processed; /* return the number of processed file/time events */
}

/* Wait for milliseconds until the given file descriptor becomes
 * writable/readable/exception */

/*
 * 等待milliseconds毫秒
 * 
 * fd 已就绪事件文件描述符
 * mask 事件类型掩码
 *
 */
int aeWait(int fd, int mask, long long milliseconds) {
    // 定义pollfd
    struct pollfd pfd;
    // 初始化返回的事件类型掩码
    int retmask = 0, retval;

    // 初始化内存空间
    memset(&pfd, 0, sizeof(pfd));
    
    // 设置已就绪事件文件描述符
    pfd.fd = fd;
    
    // 文件事件状态可读
    if (mask & AE_READABLE) pfd.events |= POLLIN;
    // 文件事件状态可写
    if (mask & AE_WRITABLE) pfd.events |= POLLOUT;

    if ((retval = poll(&pfd, 1, milliseconds))== 1) {
        if (pfd.revents & POLLIN) retmask |= AE_READABLE;
        if (pfd.revents & POLLOUT) retmask |= AE_WRITABLE;
	if (pfd.revents & POLLERR) retmask |= AE_WRITABLE;
        if (pfd.revents & POLLHUP) retmask |= AE_WRITABLE;
        return retmask;
    } else {
        return retval;
    }
}

/*
 * 事件处理器主循环
 *
 * eventLoop 事件处理器指针
 *
 */
void aeMain(aeEventLoop *eventLoop) {
    // 设置事件处理器为启动状态
    eventLoop->stop = 0;
    
    // 遍历事件处理器
    while (!eventLoop->stop) {
        // 事件处理前的sleep方法不为空
        if (eventLoop->beforesleep != NULL)
            // 执行sleep方法
            eventLoop->beforesleep(eventLoop);
        
        // 调用对应复用库中的aeProcessEvents方法
        aeProcessEvents(eventLoop, AE_ALL_EVENTS);
    }
}

/*
 * 返回所使用的复用库的名称
 *
 */
char *aeGetApiName(void) {
    // 返回所使用的复用库的名称 MAC系统对应的是kqueue
    return aeApiName();
}

/*
 * 设置处理事件前需要被执行的sleep函数
 * 
 * eventLoop 事件处理器指针
 * beforesleep sleep函数指针
 *
 */
void aeSetBeforeSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *beforesleep) {
    // 设置处理事件前需要被执行的sleep函数
    eventLoop->beforesleep = beforesleep;
}
