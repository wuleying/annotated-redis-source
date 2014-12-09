/* Hash Tables Implementation.
 * 哈希表(字典)
 *
 * This file implements in-memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto-resize if needed
 * tables of power of two in size are used, collisions are handled by
 * chaining. See the source code for more information... :)
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

#include <stdint.h>

#ifndef __DICT_H
#define __DICT_H

// 操作返回状态 成功
#define DICT_OK 0
// 操作返回状态 失败
#define DICT_ERR 1

/* Unused arguments generate annoying warnings... */
// 编译时参数未使用产生了警告，直接忽略
#define DICT_NOTUSED(V) ((void) V)

// 字典节点结构体
typedef struct dictEntry {
    // 键
    void *key;
    // 值
    union {
        // 指针类型
        void *val;
        // 无符号整型
        uint64_t u64;
        // 有符号整型
        int64_t s64;
        // 浮点型
        double d;
    } v;
    // 后续节点
    struct dictEntry *next;
} dictEntry;

// 字典特定类型的一组处理函数
typedef struct dictType {
    // 计算键的哈希值函数，不同的字典可以有不同的hashFunction
    unsigned int (*hashFunction)(const void *key);
    // 复制键的函数
    void *(*keyDup)(void *privdata, const void *key);
    // 复制值的函数
    void *(*valDup)(void *privdata, const void *obj);
    // 对比两个键的函数
    int (*keyCompare)(void *privdata, const void *key1, const void *key2);
    // 键的析构函数
    void (*keyDestructor)(void *privdata, void *key);
    // 值的析构函数
    void (*valDestructor)(void *privdata, void *obj);
} dictType;

/* This is our hash table structure. Every dictionary has two of this as we
 * implement incremental rehashing, for the old to the new table. */

// 字典哈希表结构体
typedef struct dictht {
    // 哈希表节点指针数组(Bucket)
    dictEntry **table;
    // 指针数组的大小
    unsigned long size;
    // 指针数组的长度，用来计算索引值
    unsigned long sizemask;
    // 字典已有的节点数量
    unsigned long used;
} dictht;

// 字典结构体
typedef struct dict {
    // 特定类型的处理函数组
    dictType *type;
    // 处理函数的私有数据
    void *privdata;
    // 使用两个字典哈希表，用于实现渐进式rehash
    dictht ht[2];
    // rehash进度，-1表未未进行
    long rehashidx; /* rehashing not in progress if rehashidx == -1 */
    // 正在运行的安全迭代器数量
    int iterators; /* number of iterators currently running */
} dict;

/* If safe is set to 1 this is a safe iterator, that means, you can call
 * dictAdd, dictFind, and other functions against the dictionary even while
 * iterating. Otherwise it is a non safe iterator, and only dictNext()
 * should be called while iterating. */

// 字典迭代器
typedef struct dictIterator {
    // 字典
    dict *d;
    // 正在迭代的哈希表的数组索引
    long index;
    // table 正在迭代的哈希表的代码(0或1)
    // safe 是否是安全迭代器 不安全的迭代器只可调用dictNext方法
    int table, safe;
    // entry 当前哈希节点
    // nextEntry 后续哈希节点
    dictEntry *entry, *nextEntry;
    /* unsafe iterator fingerprint for misuse detection. */
    // 指纹标识，防止滥用非安全迭代器
    long long fingerprint;
} dictIterator;

// 字典的遍历方法
typedef void (dictScanFunction)(void *privdata, const dictEntry *de);

/* This is the initial size of every hash table */
// 哈希表的初始大小
#define DICT_HT_INITIAL_SIZE     4

/* ------------------------------- Macros ------------------------------------*/

/* 宏 */
// 释放字典节点值，如果定义了valDestructor函数指针，则执行此函数
#define dictFreeVal(d, entry) \
    if ((d)->type->valDestructor) \
        (d)->type->valDestructor((d)->privdata, (entry)->v.val)

// 设置字典节点值，如果定义了valDup函数值针，则执行此函数
#define dictSetVal(d, entry, _val_) do { \
    if ((d)->type->valDup) \
        entry->v.val = (d)->type->valDup((d)->privdata, _val_); \
    else \
        entry->v.val = (_val_); \
} while(0)

// 设置字典节点的整型值
#define dictSetSignedIntegerVal(entry, _val_) \
    do { entry->v.s64 = _val_; } while(0)

// 设置字典节点的无符号整型值
#define dictSetUnsignedIntegerVal(entry, _val_) \
    do { entry->v.u64 = _val_; } while(0)

// 设置字典节点的浮点型值
#define dictSetDoubleVal(entry, _val_) \
    do { entry->v.d = _val_; } while(0)

// 析构字典节点的key，如果定义了keyDestructor函数指针，则执行此函数
#define dictFreeKey(d, entry) \
    if ((d)->type->keyDestructor) \
        (d)->type->keyDestructor((d)->privdata, (entry)->key)

// 设置字典节点的key，如果定义了keyDup函数指针，则执行此函数
#define dictSetKey(d, entry, _key_) do { \
    if ((d)->type->keyDup) \
        entry->key = (d)->type->keyDup((d)->privdata, _key_); \
    else \
        entry->key = (_key_); \
} while(0)

// 比较两个键，如果定义了keyCompare函数指针，则执行此函数
#define dictCompareKeys(d, key1, key2) \
    (((d)->type->keyCompare) ? \
        (d)->type->keyCompare((d)->privdata, key1, key2) : \
        (key1) == (key2))

// 获取字典中key对应的节点哈希值
#define dictHashKey(d, key) (d)->type->hashFunction(key)
// 获取字典节点的key
#define dictGetKey(he) ((he)->key)
// 获取字典节点的值
#define dictGetVal(he) ((he)->v.val)
// 获取字典节点的整型值
#define dictGetSignedIntegerVal(he) ((he)->v.s64)
// 获取字典节点的无符号整型值
#define dictGetUnsignedIntegerVal(he) ((he)->v.u64)
// 获取字典节点的浮点型值
#define dictGetDoubleVal(he) ((he)->v.d)
// 获取字典分配的指针数组的大小总合
#define dictSlots(d) ((d)->ht[0].size+(d)->ht[1].size)
// 获取字典已有的节点数量总合
#define dictSize(d) ((d)->ht[0].used+(d)->ht[1].used)
// 字典是否正在rehash
#define dictIsRehashing(d) ((d)->rehashidx != -1)

/* API */
// 创建一个字典
dict *dictCreate(dictType *type, void *privDataPtr);
// 扩展或创建字典
int dictExpand(dict *d, unsigned long size);
// 给字典添加一个节点
int dictAdd(dict *d, void *key, void *val);
// 给字典添加一个键
dictEntry *dictAddRaw(dict *d, void *key);
// 替换键对应的值
int dictReplace(dict *d, void *key, void *val);
// 给字典添加一个键 类似dictAddRaw
dictEntry *dictReplaceRaw(dict *d, void *key);
// 删除键 并释放对应节点内存
int dictDelete(dict *d, const void *key);
// 删除键 但不释放对应节点内存
int dictDeleteNoFree(dict *d, const void *key);
// 清空并释放字典
void dictRelease(dict *d);
// 在字典中查找键对应的节点
dictEntry * dictFind(dict *d, const void *key);
// 在字典中查找键对应的值
void *dictFetchValue(dict *d, const void *key);
// 调整字典大小，让已有节点数与Bucket比率尽量接近小于等于1
int dictResize(dict *d);
// 给指定字典创建一个不安全迭代器
dictIterator *dictGetIterator(dict *d);
// 给指定字典创建一个安全迭代器
dictIterator *dictGetSafeIterator(dict *d);
// 返回迭代器指向的当前节点
dictEntry *dictNext(dictIterator *iter);
void dictReleaseIterator(dictIterator *iter);
dictEntry *dictGetRandomKey(dict *d);
unsigned int dictGetRandomKeys(dict *d, dictEntry **des, unsigned int count);
void dictPrintStats(dict *d);
unsigned int dictGenHashFunction(const void *key, int len);
unsigned int dictGenCaseHashFunction(const unsigned char *buf, int len);
void dictEmpty(dict *d, void(callback)(void*));
void dictEnableResize(void);
void dictDisableResize(void);
// 字典渐进式rehash操作
int dictRehash(dict *d, int n);
// 在指定时间内(毫秒)对字典进行rehash
int dictRehashMilliseconds(dict *d, int ms);
void dictSetHashFunctionSeed(unsigned int initval);
unsigned int dictGetHashFunctionSeed(void);
unsigned long dictScan(dict *d, unsigned long v, dictScanFunction *fn, void *privdata);

/* Hash table types */
extern dictType dictTypeHeapStringCopyKey;
extern dictType dictTypeHeapStrings;
extern dictType dictTypeHeapStringCopyKeyValue;

#endif /* __DICT_H */
