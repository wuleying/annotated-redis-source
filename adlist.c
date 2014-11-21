/* adlist.c - A generic doubly linked list implementation
 * 一个通用的双向链表实现
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


#include <stdlib.h>
#include "adlist.h"
#include "zmalloc.h"

/* Create a new list. The created list can be freed with
 * AlFreeList(), but private value of every node need to be freed
 * by the user before to call AlFreeList().
 *
 * On error, NULL is returned. Otherwise the pointer to the new list. */

/*
 * 创建一个新链表
 *
 * 失败返回 NULL，成功返回一个新链表
 *
 */
list *listCreate(void)
{
    struct list *list;

    // 分配内存
    if ((list = zmalloc(sizeof(*list))) == NULL)
        return NULL;

    // 初始化头节点与尾节点，设为NULL
    list->head = list->tail = NULL;
    // 链表长度为0
    list->len = 0;
    // 复制方法为NULL
    list->dup = NULL;
    // 释放方法为NULL
    list->free = NULL;
    // 查找匹配方法为NULL
    list->match = NULL;

    // 返回链表
    return list;
}

/* Free the whole list.
 *
 * This function can't fail. */

/*
 * 释放链表
 *
 * list 链表指针
 *
 */
void listRelease(list *list)
{
    // 用来保存链表节点数量
    unsigned long len;
    // 当前节点与后续节点
    listNode *current, *next;

    // 将链表头设为当前节点
    current = list->head;
    // 保存链表节点数量
    len = list->len;
    // 遍历链表
    while(len--) {
        // 获取后续节点
        next = current->next;
        // 如果设置了释放方法，使用释放方法释放当前节点的值
        if (list->free) list->free(current->value);
        // 释放当前节点
        zfree(current);
        // 将后续节点设为当前节点
        current = next;
    }
    // 释放整个链表
    zfree(list);
}

/* Add a new node to the list, to head, containing the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */

/*
 * 新建一个节点，值为value, 并添加到链表头
 *
 * list 链表指针
 * value 值指针
 *
 */
list *listAddNodeHead(list *list, void *value)
{
    // 链表节点
    listNode *node;

    // 为节点分配内存
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;

    // 设置节点的值
    node->value = value;

    if (list->len == 0) {
        /* 空链表 */
        // 链表头与链表尾设为当前节点
        list->head = list->tail = node;
        // 当前节点的前驱和后续都设为NULL
        node->prev = node->next = NULL;
    } else {
        /* 非空链表 */
        // 当前节点前驱设为NULL
        node->prev = NULL;
        // 当前节点后续设为链表头
        node->next = list->head;
        // 链表头前驱设为当前节点
        list->head->prev = node;
        // 链表头设为当前节点
        list->head = node;
    }
    // 链表节点数量加1
    list->len++;
    // 返回链表
    return list;
}

/* Add a new node to the list, to tail, containing the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */

/*
 * 新建一个节点，值为value, 并添加到链表尾
 *
 * list 链表指针
 * value 值指针
 *
 */
list *listAddNodeTail(list *list, void *value)
{
    // 链表节点
    listNode *node;

    // 为节点分配内存
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;

    // 设置节点的值
    node->value = value;

    if (list->len == 0) {
        /* 空链表 */
        // 链表头与链表尾设为当前节点
        list->head = list->tail = node;
        // 当前节点的前驱和后续都设为NULL
        node->prev = node->next = NULL;
    } else {
        /* 非空链表 */
        // 当前节点的前驱为链表尾
        node->prev = list->tail;
        // 当前节点的后续为NULL
        node->next = NULL;
        // 链表尾的后续为当前节点
        list->tail->next = node;
        // 链表尾设为当前节点
        list->tail = node;
    }
    // 链表节点数量加1
    list->len++;
    // 返回链表
    return list;
}

/*
 * 在链表中插入节点
 *
 * list 链表指针
 * old_node 目标节点指针
 * value 值指针
 * after 插入到目标节点之后，默认为目标节点之前
 *
 */
list *listInsertNode(list *list, listNode *old_node, void *value, int after) {
    // 链表节点
    listNode *node;

    // 为节点分配内存
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;

    // 设置节点的值
    node->value = value;

    if (after) {
        /* 插入到目标节点之后 */
        // 当前节点的前驱设为目标节点
        node->prev = old_node;
        // 当前节点的后续设为目标节点的后续
        node->next = old_node->next;
        // 目标节点为链表尾时
        if (list->tail == old_node) {
            // 将当前节点设为新的链表尾
            list->tail = node;
        }
    } else {
        /* 插入到目标节点之前 */
        // 当前节点的后续设为目标节点
        node->next = old_node;
        // 当前节点的前驱设为目标节点的前驱
        node->prev = old_node->prev;
        // 目标节点为链表头时
        if (list->head == old_node) {
            // 将当前节点设为新的链表头
            list->head = node;
        }
    }
    // 当前节点的前驱不为NULL时
    if (node->prev != NULL) {
        // 当前节点前驱的后续设为当前节点
        node->prev->next = node;
    }
    // 当前节点的后续不为NULL时
    if (node->next != NULL) {
        // 当前节点的后续的前驱设为当前节点
        node->next->prev = node;
    }
    // 链表节点数量加1
    list->len++;
    // 返回链表
    return list;
}

/* Remove the specified node from the specified list.
 * It's up to the caller to free the private value of the node.
 *
 * This function can't fail. */

/*
 * 在链表中删除节点
 *
 * list 链表指针
 * node 节点指针
 *
 */
void listDelNode(list *list, listNode *node)
{
    if (node->prev)
        /* 待删节点存在前驱 */
        // 将待删节点前驱的后续设为待删节点的后续
        node->prev->next = node->next;
    else
        /* 待删节点不存在前驱，表示待删节点为链表头 */
        // 将链表头设为待删节点的后续
        list->head = node->next;

    if (node->next)
        /* 待删节点存在后续 */
        // 待删节点后续的前驱设为待删节点的前驱
        node->next->prev = node->prev;
    else
        /* 待删节点不存在后续，表示待删节点为链表尾 */
        // 将链表尾设为待删节点的前驱
        list->tail = node->prev;

    // 如果存在释放方法，使用该方法释放待删节点的值
    if (list->free) list->free(node->value);
    // 释放待删节点
    zfree(node);
    // 链表节点数量减1
    list->len--;
}

/* Returns a list iterator 'iter'. After the initialization every
 * call to listNext() will return the next element of the list.
 *
 * This function can't fail. */

/*
 * 返回链表迭代器
 *
 * list 链表指针
 * direction 迭代方向
 *
 */
listIter *listGetIterator(list *list, int direction)
{
    // 链表迭代器
    listIter *iter;

    // 为链表迭代器分配内存
    if ((iter = zmalloc(sizeof(*iter))) == NULL) return NULL;

    if (direction == AL_START_HEAD)
        /* 迭代方向为从头到尾 */
        // 迭代器后续为链表头
        iter->next = list->head;
    else
        /* 迭代方向为从尾到头 */
        // 迭代器后续为链表尾
        iter->next = list->tail;

    // 设置迭代器迭代方向
    iter->direction = direction;
    // 返回迭代器
    return iter;
}

/* Release the iterator memory */

/*
 * 释放链表迭代器
 *
 * iter 链表迭代器指针
 *
 */
void listReleaseIterator(listIter *iter) {
    // 释放链表迭代器
    zfree(iter);
}

/* Create an iterator in the list private iterator structure */

/*
 * 将迭代器的迭代指针倒回到链表头
 *
 * list 链表指针
 * li 链表迭代器指针
 *
 */
void listRewind(list *list, listIter *li) {
    // 将迭代器后续设为链表头
    li->next = list->head;
    // 将迭代方向设为从头到尾
    li->direction = AL_START_HEAD;
}

/*
 * 将迭代器的迭代指针倒回到链表尾
 *
 * list 链表指针
 * li 链表迭代器指针
 *
 */
void listRewindTail(list *list, listIter *li) {
    // 将迭代器后续设为链表尾
    li->next = list->tail;
    // 将迭代方向为从尾到头
    li->direction = AL_START_TAIL;
}

/* Return the next element of an iterator.
 * It's valid to remove the currently returned element using
 * listDelNode(), but not to remove other elements.
 *
 * The function returns a pointer to the next element of the list,
 * or NULL if there are no more elements, so the classical usage patter
 * is:
 *
 * iter = listGetIterator(list,<direction>);
 * while ((node = listNext(iter)) != NULL) {
 *     doSomethingWith(listNodeValue(node));
 * }
 *
 * */

/*
 * 使用链表迭代器访问下一个节点
 *
 * iter 迭代器指针
 *
 */
listNode *listNext(listIter *iter)
{
    // 当前节点设为迭代器的后续节点
    listNode *current = iter->next;

    if (current != NULL) {
        /* 当前节点不为空 */
        if (iter->direction == AL_START_HEAD)
            /* 迭代方向为从头到尾 */
            // 迭代器后续设为当前节点后续
            iter->next = current->next;
        else
            /* 迭代方向为从尾到头 */
            // 迭代器后续设为当前节点前驱
            iter->next = current->prev;
    }
    // 返回当前节点
    return current;
}

/* Duplicate the whole list. On out of memory NULL is returned.
 * On success a copy of the original list is returned.
 *
 * The 'Dup' method set with listSetDupMethod() function is used
 * to copy the node value. Otherwise the same pointer value of
 * the original node is used as value of the copied node.
 *
 * The original list both on success or error is never modified. */

/*
 * 复制整个链表
 *
 * orig 目标链表
 *
 */
list *listDup(list *orig)
{
    // 新链表
    list *copy;
    // 链表迭代器
    listIter *iter;
    // 链表节点
    listNode *node;

    // 初始化空链表
    if ((copy = listCreate()) == NULL)
        return NULL;

    // 复制方法
    copy->dup = orig->dup;
    // 释放方法
    copy->free = orig->free;
    // 查询匹配方法
    copy->match = orig->match;
    // 获取源链表的迭代器，从头到尾迭代
    iter = listGetIterator(orig, AL_START_HEAD);

    // 使用迭代器遍历源链表
    while((node = listNext(iter)) != NULL) {
        // 定义一个空的值指针
        void *value;

        if (copy->dup) {
            /* 定义了复制方法 */
            // 使用复制方法复制源节点的值
            value = copy->dup(node->value);
            if (value == NULL) {
                /* 如果值为空 */
                // 释放链表
                listRelease(copy);
                // 释放迭代器
                listReleaseIterator(iter);
                // 返回NULL
                return NULL;
            }
        } else
            /* 未定义复制方法 */
            // 将值设为源节点的值
            value = node->value;

        if (listAddNodeTail(copy, value) == NULL) {
            /* 将节点添加到链表失败 */
            // 释放链表
            listRelease(copy);
            // 释放迭代器
            listReleaseIterator(iter);
            // 返回NULL
            return NULL;
        }
    }
    // 释放迭代器
    listReleaseIterator(iter);
    // 返回新链表
    return copy;
}

/* Search the list for a node matching a given key.
 * The match is performed using the 'match' method
 * set with listSetMatchMethod(). If no 'match' method
 * is set, the 'value' pointer of every node is directly
 * compared with the 'key' pointer.
 *
 * On success the first matching node pointer is returned
 * (search starts from head). If no matching node exists
 * NULL is returned. */

/*
 * 按节点值搜索链表
 *
 * list 链表指针
 * key 值指针
 *
 */
listNode *listSearchKey(list *list, void *key)
{
    // 迭代器
    listIter *iter;
    // 链表节点
    listNode *node;

    // 获取链表的迭代器，从头到尾迭代
    iter = listGetIterator(list, AL_START_HEAD);

    // 使用迭代器遍历链表
    while((node = listNext(iter)) != NULL) {
        if (list->match) {
            /* 定义了查找匹配方法 */
            if (list->match(node->value, key)) {
                /* 使用查找匹配方法查找值对应的节点值 */
                // 释放迭代器
                listReleaseIterator(iter);
                // 返回匹配到的节点
                return node;
            }
        } else {
            if (key == node->value) {
                /* 查找的值等于节点的值 */
                // 释放迭代器
                listReleaseIterator(iter);
                // 返回匹配到的节点
                return node;
            }
        }
    }

    // 释放迭代器
    listReleaseIterator(iter);
    // 未有匹配 返回NULL
    return NULL;
}

/* Return the element at the specified zero-based index
 * where 0 is the head, 1 is the element next to head
 * and so on. Negative integers are used in order to count
 * from the tail, -1 is the last element, -2 the penultimate
 * and so on. If the index is out of range NULL is returned. */

/*
 * 按节点索引返回节点
 *
 * list 链表指针
 * index 节点索引
 *
 */
listNode *listIndex(list *list, long index) {
    // 链表节点
    listNode *n;

    if (index < 0) {
        /* 节点索引小于0 */
        // 重置节点索引为取反-1
        index = (-index)-1;
        // 将当前节点设为链表尾
        n = list->tail;
        // 从链表尾向头遍历 直到碰到节点索引对应节点的前驱
        while(index-- && n) n = n->prev;
    } else {
        // 将当前节点设为链表头
        n = list->head;
        // 从链表头向尾遍历 直到碰到节点索引对应节点的后续
        while(index-- && n) n = n->next;
    }

    // 返回对应节点
    return n;
}

/* Rotate the list removing the tail node and inserting it to the head. */

/*
 * 分离链表的尾节点 将它设为新的头节点
 *
 * list 链表指针
 *
 */
void listRotate(list *list) {
    // 获取链表尾节点
    listNode *tail = list->tail;

    // 空链表或只有一个节点的链表，直接返回
    if (listLength(list) <= 1) return;

    /* Detach current tail */
    // 分离尾节点，将链表尾设为原尾节点的前驱
    list->tail = tail->prev;
    // 链表尾的后续设为NULL
    list->tail->next = NULL;
    /* Move it as head */
    // 将链表头的前驱设为原尾节点
    list->head->prev = tail;
    // 原尾节点的前驱设为NULL
    tail->prev = NULL;
    // 原尾节点的后续设为原链表头
    tail->next = list->head;
    // 将原尾节点设为新的链表头
    list->head = tail;
}
