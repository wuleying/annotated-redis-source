/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
 * 事件驱动库
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
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

#ifndef __AE_H__
#define __AE_H__

// 事件执行状态 成功
#define AE_OK 0
// 事件执行状态 失败
#define AE_ERR -1

// 文件事件状态 未设置
#define AE_NONE 0
// 文件事件状态 可读
#define AE_READABLE 1
// 文件事件状态 可写
#define AE_WRITABLE 2

#define AE_FILE_EVENTS 1
#define AE_TIME_EVENTS 2
#define AE_ALL_EVENTS (AE_FILE_EVENTS|AE_TIME_EVENTS)
#define AE_DONT_WAIT 4

// 事件是否持续执行
#define AE_NOMORE -1

/* Macros */
/* 宏 */
// ? 貌似项目中未使用此宏
#define AE_NOTUSED(V) ((void) V)

// 事件处理器结构体
struct aeEventLoop;

/* Types and data structures */
/* 事件类型与数据结构体 */
// 文件事件方法
typedef void aeFileProc(struct aeEventLoop *eventLoop, int fd, void *clientData, int mask);
// 时间事件方法
typedef int aeTimeProc(struct aeEventLoop *eventLoop, long long id, void *clientData);
// 时间事件释放方法
typedef void aeEventFinalizerProc(struct aeEventLoop *eventLoop, void *clientData);
// 事件处理前的sleep方法
typedef void aeBeforeSleepProc(struct aeEventLoop *eventLoop);

/* File event structure */
// 文件事件结构体
typedef struct aeFileEvent {
    // 事件类型掩码 值为READABLE、WRITABLE，或两者按位或
    int mask; /* one of AE_(READABLE|WRITABLE) */
    // 读文件方法
    aeFileProc *rfileProc;
    // 写文件方法
    aeFileProc *wfileProc;
    // 复用库的私有数据
    void *clientData;
} aeFileEvent;

/* Time event structure */
// 时间事件结构体
typedef struct aeTimeEvent {
    // 时间事件的唯一标识符
    long long id; /* time event identifier. */
    // 事件到达时间 秒
    long when_sec; /* seconds */
    // 事件到达时间 毫秒
    long when_ms; /* milliseconds */
    // 时间事件处理方法
    aeTimeProc *timeProc;
    // 时间事件释放方法
    aeEventFinalizerProc *finalizerProc;
    // 复用库的私有数据
    void *clientData;
    // 指向下个时间事件的指针 形成单链表
    struct aeTimeEvent *next;
} aeTimeEvent;

/* A fired event */
// 已就绪事件
typedef struct aeFiredEvent {
    // 已就绪事件文件描述符
    int fd;
    // 事件类型掩码 值为READABLE、WRITABLE，或两者按位或
    int mask;
} aeFiredEvent;

/* State of an event based program */
// 定义事件处理器结构体
typedef struct aeEventLoop {
    // 当前已注册的最大文件描述符
    int maxfd;   /* highest file descriptor currently registered */
    // 当前已追踪的最大文件描述符
    int setsize; /* max number of file descriptors tracked */
    // 生成下个事件ID
    long long timeEventNextId;
    // 最后一次执行事件的时间
    time_t lastTime;     /* Used to detect system clock skew */
    // 已注册的文件事件
    aeFileEvent *events; /* Registered events */
    // 已就绪的事件
    aeFiredEvent *fired; /* Fired events */
    // 时间事件头
    aeTimeEvent *timeEventHead;
    // 终止/启动事件
    int stop;
    // 轮询API数据
    void *apidata; /* This is used for polling API specific data */
    // 事件处理前的sleep方法
    aeBeforeSleepProc *beforesleep;
} aeEventLoop;

/* Prototypes */
// 创建事件处理器
aeEventLoop *aeCreateEventLoop(int setsize);
// 删除事件处理器
void aeDeleteEventLoop(aeEventLoop *eventLoop);
// 停止事件处理器
void aeStop(aeEventLoop *eventLoop);
// 创建文件事件
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask,
        aeFileProc *proc, void *clientData);
// 删除文件事件
void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask);
// 获取文件事件
int aeGetFileEvents(aeEventLoop *eventLoop, int fd);
// 创建时间事件
long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds,
        aeTimeProc *proc, void *clientData,
        aeEventFinalizerProc *finalizerProc);
// 删除时间事件
int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id);
// 处理所有已到达的时间事件以及所有已就绪的文件事件
int aeProcessEvents(aeEventLoop *eventLoop, int flags);
// 等待milliseconds毫秒
int aeWait(int fd, int mask, long long milliseconds);
// 事件处理器主循环
void aeMain(aeEventLoop *eventLoop);
// 返回所使用复用库的名称
char *aeGetApiName(void);
// 设置处理事件前需要被执行的sleep函数
void aeSetBeforeSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *beforesleep);
// 返回事件处理器已追踪的最大文件描述符
int aeGetSetSize(aeEventLoop *eventLoop);
// 重置事件处理器已追踪的最大文件描述符
int aeResizeSetSize(aeEventLoop *eventLoop, int setsize);

#endif
