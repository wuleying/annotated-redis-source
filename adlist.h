/* adlist.h - A generic doubly linked list implementation
 * 一个通用的双向链表实现
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

#ifndef __ADLIST_H__
#define __ADLIST_H__

/* Node, List, and Iterator are the only data structures used currently. */

// 双向链表节点
typedef struct listNode {
    // 前驱
    struct listNode *prev;
    // 后续
    struct listNode *next;
    // 值
    void *value;
} listNode;

// 链表迭代器
typedef struct listIter {
    // 后续
    listNode *next;
    // 迭代方向
    int direction;
} listIter;

// 双向链表
typedef struct list {
    // 头指针
    listNode *head;
    // 尾指针
    listNode *tail;
    // 复制方法
    void *(*dup)(void *ptr);
    // 释放方法
    void (*free)(void *ptr);
    // 查询匹配方法
    int (*match)(void *ptr, void *key);
    // 链表节点数量
    unsigned long len;
} list;

/* Functions implemented as macros */
// 返回链表的节点数量
#define listLength(l) ((l)->len)
// 返回链表的头节点
#define listFirst(l) ((l)->head)
// 返回链表的尾节点
#define listLast(l) ((l)->tail)
// 返回节点的前驱节点
#define listPrevNode(n) ((n)->prev)
// 返回节点的后续节点
#define listNextNode(n) ((n)->next)
// 返回节点的值
#define listNodeValue(n) ((n)->value)

// 设置链表的复制方法
#define listSetDupMethod(l,m) ((l)->dup = (m))
// 设置链表的释放方法
#define listSetFreeMethod(l,m) ((l)->free = (m))
// 设置链表的查询匹配方法
#define listSetMatchMethod(l,m) ((l)->match = (m))

// 返回链表的复制方法
#define listGetDupMethod(l) ((l)->dup)
// 返回链表的释放方法
#define listGetFree(l) ((l)->free)
// 返回链表的查询匹配方法
#define listGetMatchMethod(l) ((l)->match)

/* Prototypes */
// 创建链表
list *listCreate(void);
// 释放链表
void listRelease(list *list);
// 新建一个节点，值为value, 并添加到链表头
list *listAddNodeHead(list *list, void *value);
// 新建一个节点，值为value, 并添加到链表尾
list *listAddNodeTail(list *list, void *value);
// 在链表中插入节点
list *listInsertNode(list *list, listNode *old_node, void *value, int after);
// 在链表中删除节点
void listDelNode(list *list, listNode *node);
// 返回链表迭代器
listIter *listGetIterator(list *list, int direction);
// 使用链表迭代器访问下一个节点
listNode *listNext(listIter *iter);
// 释放链表迭代器
void listReleaseIterator(listIter *iter);
// 复制整个链表
list *listDup(list *orig);
// 按节点值搜索链表
listNode *listSearchKey(list *list, void *key);
// 按节点索引返回节点
listNode *listIndex(list *list, long index);
// 将迭代器的迭代指针倒回到链表头
void listRewind(list *list, listIter *li);
// 将迭代器的迭代指针倒回到链表尾
void listRewindTail(list *list, listIter *li);
// 分离链表的尾节点 将它设为新的头节点
void listRotate(list *list);

/* Directions for iterators */
// 迭代器迭代方向 从头到尾
#define AL_START_HEAD 0
// 迭代器迭代方向 从尾到头
#define AL_START_TAIL 1

#endif /* __ADLIST_H__ */
