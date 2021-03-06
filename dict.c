/* Hash Tables Implementation.
 * 哈希表(字典)
 *
 * This file implements in memory hash tables with insert/del/replace/find/
 * get-random-element operations. Hash tables will auto resize if needed
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

#include "fmacros.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <sys/time.h>
#include <ctype.h>

#include "dict.h"
#include "zmalloc.h"
#include "redisassert.h"

/* Using dictEnableResize() / dictDisableResize() we make possible to
 * enable/disable resizing of the hash table as needed. This is very important
 * for Redis, as we use copy-on-write and don't want to move too much memory
 * around when there is a child performing saving operations.
 *
 * Note that even when dict_can_resize is set to 0, not all resizes are
 * prevented: a hash table is still allowed to grow if the ratio between
 * the number of elements and the buckets > dict_force_resize_ratio. */
static int dict_can_resize = 1;
static unsigned int dict_force_resize_ratio = 5;

/* -------------------------- private prototypes ---------------------------- */

static int _dictExpandIfNeeded(dict *ht);
static unsigned long _dictNextPower(unsigned long size);
static int _dictKeyIndex(dict *ht, const void *key);
static int _dictInit(dict *ht, dictType *type, void *privDataPtr);

/* -------------------------- hash functions -------------------------------- */

/* Thomas Wang's 32 bit Mix Function */
unsigned int dictIntHashFunction(unsigned int key)
{
    key += ~(key << 15);
    key ^=  (key >> 10);
    key +=  (key << 3);
    key ^=  (key >> 6);
    key += ~(key << 11);
    key ^=  (key >> 16);
    return key;
}

static uint32_t dict_hash_function_seed = 5381;

void dictSetHashFunctionSeed(uint32_t seed) {
    dict_hash_function_seed = seed;
}

uint32_t dictGetHashFunctionSeed(void) {
    return dict_hash_function_seed;
}

/* MurmurHash2, by Austin Appleby
 * Note - This code makes a few assumptions about how your machine behaves -
 * 1. We can read a 4-byte value from any address without crashing
 * 2. sizeof(int) == 4
 *
 * And it has a few limitations -
 *
 * 1. It will not work incrementally.
 * 2. It will not produce the same results on little-endian and big-endian
 *    machines.
 */
unsigned int dictGenHashFunction(const void *key, int len) {
    /* 'm' and 'r' are mixing constants generated offline.
     They're not really 'magic', they just happen to work well.  */
    uint32_t seed = dict_hash_function_seed;
    const uint32_t m = 0x5bd1e995;
    const int r = 24;

    /* Initialize the hash to a 'random' value */
    uint32_t h = seed ^ len;

    /* Mix 4 bytes at a time into the hash */
    const unsigned char *data = (const unsigned char *)key;

    while(len >= 4) {
        uint32_t k = *(uint32_t*)data;

        k *= m;
        k ^= k >> r;
        k *= m;

        h *= m;
        h ^= k;

        data += 4;
        len -= 4;
    }

    /* Handle the last few bytes of the input array  */
    switch(len) {
    case 3: h ^= data[2] << 16;
    case 2: h ^= data[1] << 8;
    case 1: h ^= data[0]; h *= m;
    };

    /* Do a few final mixes of the hash to ensure the last few
     * bytes are well-incorporated. */
    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;

    return (unsigned int)h;
}

/* And a case insensitive hash function (based on djb hash) */
unsigned int dictGenCaseHashFunction(const unsigned char *buf, int len) {
    unsigned int hash = (unsigned int)dict_hash_function_seed;

    while (len--)
        hash = ((hash << 5) + hash) + (tolower(*buf++)); /* hash * 33 + c */
    return hash;
}

/* ----------------------------- API implementation ------------------------- */

/* Reset a hash table already initialized with ht_init().
 * NOTE: This function should only be called by ht_destroy(). */

/*
 * 重置字典哈希表属性
 *
 * ht 字典哈希表指针
 *
 */
static void _dictReset(dictht *ht)
{
    // Bucket置空
    ht->table = NULL;
    // 指针数组大小设为0
    ht->size = 0;
    // 指针数组长度设为0
    ht->sizemask = 0;
    // 已有的节点数量设为0
    ht->used = 0;
}

/* Create a new hash table */

/*
 * 创建一个字典
 * 
 * type 字典特定类型的处理函数
 * privDataPtr 处理函数的私有数据
 *
 */
dict *dictCreate(dictType *type,
        void *privDataPtr)
{
    // 分配空间
    dict *d = zmalloc(sizeof(*d));

    // 初始化字典
    _dictInit(d,type,privDataPtr);
    
    // 返回字典指针
    return d;
}

/* Initialize the hash table */

/*
 * 初始化字典
 *
 * d 字典指针
 * type 字典特定类型的处理函数
 * privDataPtr 处理函数的私有数据
 *
 */
int _dictInit(dict *d, dictType *type,
        void *privDataPtr)
{
    // 初始化ht[0]
    _dictReset(&d->ht[0]);
    // 初始化ht[1]
    _dictReset(&d->ht[1]);
    // 设置字典特定类型的处理函数
    d->type = type;
    // 设置处理函数的私有数据
    d->privdata = privDataPtr;
    // rehash进度设为未进行
    d->rehashidx = -1;
    // 设置正在运行的安全迭代器数量
    d->iterators = 0;
    // 返回成功状态
    return DICT_OK;
}

/* Resize the table to the minimal size that contains all the elements,
 * but with the invariant of a USED/BUCKETS ratio near to <= 1 */

/*
 * 调整字典大小，让已有节点数与Bucket比率尽量接近小于等于1
 *
 * d 字典指针
 *
 */
int dictResize(dict *d)
{
    // 字典最小大小
    int minimal;

    // 不能在dict_can_resize为假或是字典正rehash时调整字典大小
    if (!dict_can_resize || dictIsRehashing(d)) return DICT_ERR;
    
    // 字典最小为ht[0]已有的节点数量
    minimal = d->ht[0].used;
    
    // 最小小于哈希表的初始大小，将最小值设为哈希表的初始大小
    if (minimal < DICT_HT_INITIAL_SIZE)
        minimal = DICT_HT_INITIAL_SIZE;
    
    // 调整字典大小
    return dictExpand(d, minimal);
}

/* Expand or create the hash table */

/*
 * 扩展或创建字典
 *
 * d 字典指针
 * size 字典增加的节点数量
 *
 */
int dictExpand(dict *d, unsigned long size)
{
    // 一个新字典哈希表
    dictht n; /* the new hash table */
    
    // 计算字典的真实大小
    unsigned long realsize = _dictNextPower(size);

    /* the size is invalid if it is smaller than the number of
     * elements already inside the hash table */
    
    // 字典正在rehash或字典ht[0]已有的节点数量大于增加的节点数量，返回错误状态
    if (dictIsRehashing(d) || d->ht[0].used > size)
        return DICT_ERR;

    /* Allocate the new hash table and initialize all pointers to NULL */
    // 设置字典哈希表指针数组大小
    n.size = realsize;
    // 设置字典哈希表指针数组长度
    n.sizemask = realsize-1;
    // 为字典哈希表Bucket分配内存空间
    n.table = zcalloc(realsize*sizeof(dictEntry*));
    // 初始化字典哈希表已有节点数量
    n.used = 0;

    /* Is this the first initialization? If so it's not really a rehashing
     * we just set the first hash table so that it can accept keys. */
    
    // 如果ht[0]为空
    if (d->ht[0].table == NULL) {
        // 将新哈希表赋值给ht[0]
        d->ht[0] = n;
        // 返回成功状态
        return DICT_OK;
    }

    /* Prepare a second hash table for incremental rehashing */
    
    // ht[0]不为空，将新哈希表赋值给ht[1]
    d->ht[1] = n;
    // 打开rehash标识
    d->rehashidx = 0;
    // 返回成功状态
    return DICT_OK;
}

/* Performs N steps of incremental rehashing. Returns 1 if there are still
 * keys to move from the old to the new hash table, otherwise 0 is returned.
 * Note that a rehashing step consists in moving a bucket (that may have more
 * than one key as we use chaining) from the old to the new hash table. */

/*
 * 字典渐进式rehash操作
 *
 * d 字典指针
 * n 处理次数
 *
 */
int dictRehash(dict *d, int n) {
    
    // 字典没有处于rehash操作中，返回0
    if (!dictIsRehashing(d)) return 0;

    // 循环处理
    while(n--) {
        
        // 定义字典节点
        dictEntry *de, *nextde;

        /* Check if we already rehashed the whole table... */
        
        // 如果ht[0]已有的节点数量为0，表示已迁移完毕
        if (d->ht[0].used == 0) {
            // 释放ht[0]的Bucket
            zfree(d->ht[0].table);
            // 用ht[1]替换原来的ht[0]
            d->ht[0] = d->ht[1];
            // 清空ht[1]
            _dictReset(&d->ht[1]);
            // 标记rehash已完成
            d->rehashidx = -1;
            // 返回0
            return 0;
        }

        /* Note that rehashidx can't overflow as we are sure there are more
         * elements because ht[0].used != 0 */
        
        // ht[0]已有的节点数量必须大于rehash进度索引
        assert(d->ht[0].size > (unsigned long)d->rehashidx);
        
        // 移动到ht[0]的Bucket中首个不为空的链表索引上
        while(d->ht[0].table[d->rehashidx] == NULL) d->rehashidx++;
        
        // 指向链表头
        de = d->ht[0].table[d->rehashidx];
        
        /* Move all the keys in this bucket from the old to the new hash HT */
        
        // 将链表中的所有节点从h[0]迁移到h[1]
        while(de) {
            // 初始化节点哈希值
            unsigned int h;
            
            // 获取当前节点的后续节点
            nextde = de->next;
            
            /* Get the index in the new hash table */
            
            // 计算节点在ht[1]中的哈希值
            h = dictHashKey(d, de->key) & d->ht[1].sizemask;
            
            // 设置当前节点的后续节点为ht[1]中对应的节点
            de->next = d->ht[1].table[h];
            
            // 添加节点到ht[1]
            d->ht[1].table[h] = de;
            // ht[0]节点数减1
            d->ht[0].used--;
            // ht[1]节点数加1
            d->ht[1].used++;
            // 将当前节点设为它的后续节点，继续处理
            de = nextde;
        }
        
        // 设置指针为空，下次处理时直接跳过
        d->ht[0].table[d->rehashidx] = NULL;
        // 转到下一rehash索引
        d->rehashidx++;
    }
    
    // 返回1
    return 1;
}

/*
 * 返回当前时间，以毫秒为单位
 *
 */
long long timeInMilliseconds(void) {
    // 初始化timeval结构体
    struct timeval tv;

    // 获取当前时间
    gettimeofday(&tv,NULL);
    
    // 返回当前时间
    return (((long long)tv.tv_sec)*1000)+(tv.tv_usec/1000);
}

/* Rehash for an amount of time between ms milliseconds and ms+1 milliseconds */

/*
 * 在指定时间内(毫秒)对字典进行rehash
 *
 * d 字典指针
 * ms 指定时间(毫秒)
 *
 */
int dictRehashMilliseconds(dict *d, int ms) {
    // 获取开始时间
    long long start = timeInMilliseconds();
    // 初始化rehash进度
    int rehashes = 0;

    // rehash字典，每次处理数量为100
    while(dictRehash(d,100)) {
        // 步长为100
        rehashes += 100;
        // 超出指定时间，跳出
        if (timeInMilliseconds()-start > ms) break;
    }
    // 返回处理数量
    return rehashes;
}

/* This function performs just a step of rehashing, and only if there are
 * no safe iterators bound to our hash table. When we have iterators in the
 * middle of a rehashing we can't mess with the two hash tables otherwise
 * some element can be missed or duplicated.
 *
 * This function is called by common lookup or update operations in the
 * dictionary so that the hash table automatically migrates from H1 to H2
 * while it is actively used. */

/*
 * 将一个节点从ht[0]迁移到ht[1]
 *
 * d 字典指针
 *
 */
static void _dictRehashStep(dict *d) {
    // 正在运行的安全迭代器数量为0时，迁移一个节点
    if (d->iterators == 0) dictRehash(d,1);
}

/* Add an element to the target hash table */

/*
 * 给字典添加一个节点
 * 
 * d 字典指针
 * key 键
 * val 值
 *
 */
int dictAdd(dict *d, void *key, void *val)
{
    // 定义一个字典节点，并设置键
    dictEntry *entry = dictAddRaw(d,key);

    // 字典节点定义失败，返回错误状态
    if (!entry) return DICT_ERR;
    
    // 设置值
    dictSetVal(d, entry, val);
    
    // 返回成功状态
    return DICT_OK;
}

/* Low level add. This function adds the entry but instead of setting
 * a value returns the dictEntry structure to the user, that will make
 * sure to fill the value field as he wishes.
 *
 * This function is also directly exposed to the user API to be called
 * mainly in order to store non-pointers inside the hash value, example:
 *
 * entry = dictAddRaw(dict,mykey);
 * if (entry != NULL) dictSetSignedIntegerVal(entry,1000);
 *
 * Return values:
 *
 * If key already exists NULL is returned.
 * If key was added, the hash entry is returned to be manipulated by the caller.
 */

/*
 * 给字典添加一个键
 *
 * d 字典指针
 * key 键
 *
 */
dictEntry *dictAddRaw(dict *d, void *key)
{
    // 索引位置
    int index;
    // 字典节点
    dictEntry *entry;
    // 字典哈希表
    dictht *ht;

    // 如果字典正在rehash中 尝试rehash一个节点
    if (dictIsRehashing(d)) _dictRehashStep(d);

    /* Get the index of the new element, or -1 if
     * the element already exists. */
    
    // 查找节点的索引位置 如果键已存在返加NULL
    if ((index = _dictKeyIndex(d, key)) == -1)
        return NULL;

    /* Allocate the memory and store the new entry */
    
    // 如果字典正在rehash中 将节点放到ht[1] 反之放入ht[0]
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];
    
    // 为节点分配内存
    entry = zmalloc(sizeof(*entry));
    // 新节点的后续指向旧的表头节点
    entry->next = ht->table[index];
    // 设置新节点为表头
    ht->table[index] = entry;
    // 已有节点数量加1
    ht->used++;

    /* Set the hash entry fields. */
    
    // 关联节点与键
    dictSetKey(d, entry, key);
    
    // 返回节点
    return entry;
}

/* Add an element, discarding the old if the key already exists.
 * Return 1 if the key was added from scratch, 0 if there was already an
 * element with such key and dictReplace() just performed a value update
 * operation. */

/*
 * 替换键对应的值
 *
 * d 字典指针
 * key 键
 * val 新值
 *
 */
int dictReplace(dict *d, void *key, void *val)
{
    // 节点
    dictEntry *entry, auxentry;

    /* Try to add the element. If the key
     * does not exists dictAdd will suceed. */
    
    // 尝试直接添加节点到字典 键不存在就会添加成功 直接并返回1
    if (dictAdd(d, key, val) == DICT_OK)
        return 1;
    
    /* It already exists, get the entry */
    
    // 添加失败表示节点存在 获取键对应的节点
    entry = dictFind(d, key);
    
    /* Set the new value and free the old one. Note that it is important
     * to do that in this order, as the value may just be exactly the same
     * as the previous one. In this context, think to reference counting,
     * you want to increment (set), and then decrement (free), and not the
     * reverse. */
    
    // 指向旧值
    auxentry = *entry;
    // 设置新值
    dictSetVal(d, entry, val);
    // 释放旧值
    dictFreeVal(d, &auxentry);
    // 返回0
    return 0;
}

/* dictReplaceRaw() is simply a version of dictAddRaw() that always
 * returns the hash entry of the specified key, even if the key already
 * exists and can't be added (in that case the entry of the already
 * existing key is returned.)
 *
 * See dictAddRaw() for more information. */

/*
 * 给字典添加一个键 类似dictAddRaw
 *
 * d 字典指针
 * key 键
 *
 */
dictEntry *dictReplaceRaw(dict *d, void *key) {
    
    // 获取键对应的节点
    dictEntry *entry = dictFind(d,key);

    // 如果找到对应的节点则直接返回 未找到则使用dictAddRaw添加键
    return entry ? entry : dictAddRaw(d,key);
}

/* Search and remove an element */

/*
 * 根据键查找并删除节点
 *
 * d 字典指针
 * key 键
 * nofree 不释放节点内存
 *
 */
static int dictGenericDelete(dict *d, const void *key, int nofree)
{
    // 节点哈希值
    // 节点索引
    unsigned int h, idx;
    // 字典节点
    dictEntry *he, *prevHe;
    // 字典哈希表计数器
    int table;

    // ht[0]是空表 返回错误状态
    if (d->ht[0].size == 0) return DICT_ERR; /* d->ht[0].table is NULL */
    
    // 如果字典正在rehash中 尝试rehash一个节点
    if (dictIsRehashing(d)) _dictRehashStep(d);
    // 获取节点哈希值
    h = dictHashKey(d, key);

    // 在两个哈希表中查找
    for (table = 0; table <= 1; table++) {
        
        // 获取索引值
        idx = h & d->ht[table].sizemask;
        // 获取索引在Bucket中对应的表头
        he = d->ht[table].table[idx];
        // 前驱表头设为NULL
        prevHe = NULL;
        
        // 遍历链表
        while(he) {
            // 比较两个键
            if (dictCompareKeys(d, key, he->key)) {
                /* Unlink the element from the list */
                // 断开获取链表关联
                // 存在前驱表头
                if (prevHe)
                    // 将前驱表头的后续设为当前节点的后续
                    prevHe->next = he->next;
                else
                    // Bucket表头设为当前节点的后续
                    d->ht[table].table[idx] = he->next;
                
                // 如果需要释放节点内存
                if (!nofree) {
                    // 释放键
                    dictFreeKey(d, he);
                    // 释放值
                    dictFreeVal(d, he);
                }
                // 释放节点
                zfree(he);
                // 已有节点数量减1
                d->ht[table].used--;
                // 返回成功状态
                return DICT_OK;
            }
            // 未找到键对应节点 将节点设为前驱节点
            prevHe = he;
            // 将节点设为当前节点后续 继续查找
            he = he->next;
        }
        
        // 如果未进行rehash 就不需要遍历ht[1]
        if (!dictIsRehashing(d)) break;
    }
    
    // 遍历完毕后未发现键对应节点 返回失败状态
    return DICT_ERR; /* not found */
}

/*
 * 删除键 并释放对应节点内存
 *
 * ht 字典指针
 * key 键
 *
 */
int dictDelete(dict *ht, const void *key) {
    // 调用dictGenericDelete删除键
    return dictGenericDelete(ht,key,0);
}

/*
 * 删除键 但不释放对应节点内存
 *
 * ht 字典指针
 * key 键
 *
 */
int dictDeleteNoFree(dict *ht, const void *key) {
    // 调用dictGenericDelete删除键
    return dictGenericDelete(ht,key,1);
}

/* Destroy an entire dictionary */

/*
 * 从字典中销毁指定哈希表
 *
 * d 字典指针
 * ht 要销毁的哈希表
 * callback 回调函数指针
 *
 */
int _dictClear(dict *d, dictht *ht, void(callback)(void *)) {
    
    // 遍历计数器
    unsigned long i;

    /* Free all the elements */
    // 遍历释放所有元素
    for (i = 0; i < ht->size && ht->used > 0; i++) {
        // 节点
        // 后续节点
        dictEntry *he, *nextHe;

        // 如果定义了回调函数 使用回调函数处理函数的私有数据
        if (callback && (i & 65535) == 0) callback(d->privdata);

        // 如果Bucket为空 跳过这次循环
        if ((he = ht->table[i]) == NULL) continue;
        
        // 遍历链表上的节点
        while(he) {
            // 获取后续节点
            nextHe = he->next;
            // 释放键
            dictFreeKey(d, he);
            // 释放值
            dictFreeVal(d, he);
            // 释放节点
            zfree(he);
            // 已有节点数量减1
            ht->used--;
            // 将后续节点设为当前 继续循环
            he = nextHe;
        }
    }
    /* Free the table and the allocated cache structure */
    
    // 释放哈希表
    zfree(ht->table);
    
    /* Re-initialize the table */
    
    // 重置字典哈希表属性
    _dictReset(ht);
    
    // 返回成功状态 永远都不会失败
    return DICT_OK; /* never fails */
}

/* Clear & Release the hash table */

/*
 * 清空并释放字典
 *
 * d 字典指针
 *
 */
void dictRelease(dict *d)
{
    // 销毁ht[0]
    _dictClear(d,&d->ht[0],NULL);
    // 销毁ht[1]
    _dictClear(d,&d->ht[1],NULL);
    // 释放字典
    zfree(d);
}

/*
 * 在字典中查找键对应的节点
 *
 * d 字典节点
 * key 键
 *
 */
dictEntry *dictFind(dict *d, const void *key)
{
    // 字典哈希表节点
    dictEntry *he;
    
    // 定义节点哈希值
    // 定义索引值
    // 定义字典哈希表计数器
    unsigned int h, idx, table;

    // 如果ht[0]大小为0 直接返回空
    if (d->ht[0].size == 0) return NULL; /* We don't have a table at all */
    
    // 如果字典正在rehash中 尝试rehash一个节点
    if (dictIsRehashing(d)) _dictRehashStep(d);
    
    // 获取节点哈希值
    h = dictHashKey(d, key);
    
    // 在两个哈希表中查找
    for (table = 0; table <= 1; table++) {
        // 获取索引值
        idx = h & d->ht[table].sizemask;
        // 获取索引在Bucket中对应的表头
        he = d->ht[table].table[idx];
        // 遍历链表
        while(he) {
            // 比较两个键是否相同
            if (dictCompareKeys(d, key, he->key))
                // 返回匹配的节点
                return he;
            
            // 不匹配 将后续节点设为当前节点 继续查找
            he = he->next;
        }
        
        // 如果未进行rehash 就不需要遍历ht[1]
        if (!dictIsRehashing(d)) return NULL;
    }
    
    // 遍历结束未找到则返回空
    return NULL;
}

/*
 * 在字典中查找键对应的值
 *
 * d 字典节点
 * key 键
 *
 */
void *dictFetchValue(dict *d, const void *key) {
    // 字典哈希表节点
    dictEntry *he;

    // 使用dictFind方法查找节点
    he = dictFind(d,key);
    
    // 如果查找到对应节点 返回节点的值 否则返回空
    return he ? dictGetVal(he) : NULL;
}

/* A fingerprint is a 64 bit number that represents the state of the dictionary
 * at a given time, it's just a few dict properties xored together.
 * When an unsafe iterator is initialized, we get the dict fingerprint, and check
 * the fingerprint again when the iterator is released.
 * If the two fingerprints are different it means that the user of the iterator
 * performed forbidden operations against the dictionary while iterating. */

/*
 * 通过指纹来禁止每个不安全的哈希迭代器的非法操作 每个不安全迭代器只能有一个指纹
 *
 * d 字典指针
 *
 */
long long dictFingerprint(dict *d) {
    long long integers[6], hash = 0;
    int j;

    integers[0] = (long) d->ht[0].table;
    integers[1] = d->ht[0].size;
    integers[2] = d->ht[0].used;
    integers[3] = (long) d->ht[1].table;
    integers[4] = d->ht[1].size;
    integers[5] = d->ht[1].used;

    /* We hash N integers by summing every successive integer with the integer
     * hashing of the previous sum. Basically:
     *
     * Result = hash(hash(hash(int1)+int2)+int3) ...
     *
     * This way the same set of integers in a different order will (likely) hash
     * to a different number. */
    for (j = 0; j < 6; j++) {
        hash += integers[j];
        /* For the hashing step we use Tomas Wang's 64 bit integer hash. */
        hash = (~hash) + (hash << 21); // hash = (hash << 21) - hash - 1;
        hash = hash ^ (hash >> 24);
        hash = (hash + (hash << 3)) + (hash << 8); // hash * 265
        hash = hash ^ (hash >> 14);
        hash = (hash + (hash << 2)) + (hash << 4); // hash * 21
        hash = hash ^ (hash >> 28);
        hash = hash + (hash << 31);
    }
    return hash;
}

/*
 * 给指定字典创建一个不安全迭代器
 *
 * d 字典指针
 *
 */
dictIterator *dictGetIterator(dict *d)
{
    // 分配迭代器内存
    dictIterator *iter = zmalloc(sizeof(*iter));

    // 设置字典
    iter->d = d;
    // 设置正在迭代的哈希表为ht[0]
    iter->table = 0;
    // 设置正在迭代的哈希表的数组索引
    iter->index = -1;
    // 标记为不安全迭代器
    iter->safe = 0;
    // 设置当前节点
    iter->entry = NULL;
    // 后续节点
    iter->nextEntry = NULL;
    // 返回迭代器
    return iter;
}

/*
 * 给指定字典创建一个安全迭代器
 *
 * d 字典指针
 *
 */
dictIterator *dictGetSafeIterator(dict *d) {
    // 先使用dictGetIterator创建一个不安全迭代器
    dictIterator *i = dictGetIterator(d);
    // 标记为安全迭代器
    i->safe = 1;
    // 返回迭代器
    return i;
}

/*
 * 返回迭代器指向的当前节点
 *
 * iter 迭代器指针
 *
 */
dictEntry *dictNext(dictIterator *iter)
{
    while (1) {
        if (iter->entry == NULL) {
            dictht *ht = &iter->d->ht[iter->table];
            if (iter->index == -1 && iter->table == 0) {
                if (iter->safe)
                    iter->d->iterators++;
                else
                    iter->fingerprint = dictFingerprint(iter->d);
            }
            iter->index++;
            if (iter->index >= (long) ht->size) {
                if (dictIsRehashing(iter->d) && iter->table == 0) {
                    iter->table++;
                    iter->index = 0;
                    ht = &iter->d->ht[1];
                } else {
                    break;
                }
            }
            iter->entry = ht->table[iter->index];
        } else {
            iter->entry = iter->nextEntry;
        }
        
        
        if (iter->entry) {
            /* We need to save the 'next' here, the iterator user
             * may delete the entry we are returning. */
            iter->nextEntry = iter->entry->next;
            return iter->entry;
        }
    }
    
    // 字典迭代完毕 返回空
    return NULL;
}

/*
 * 释放迭代器
 *
 * iter 迭代器指针
 *
 */
void dictReleaseIterator(dictIterator *iter)
{
    if (!(iter->index == -1 && iter->table == 0)) {
        if (iter->safe)
            iter->d->iterators--;
        else
            assert(iter->fingerprint == dictFingerprint(iter->d));
    }
    zfree(iter);
}

/* Return a random entry from the hash table. Useful to
 * implement randomized algorithms */

/*
 * 从字典中返回一个随机节点
 *
 * d 字典指针
 *
 */
dictEntry *dictGetRandomKey(dict *d)
{
    dictEntry *he, *orighe;
    unsigned int h;
    int listlen, listele;

    if (dictSize(d) == 0) return NULL;
    if (dictIsRehashing(d)) _dictRehashStep(d);
    if (dictIsRehashing(d)) {
        do {
            h = random() % (d->ht[0].size+d->ht[1].size);
            he = (h >= d->ht[0].size) ? d->ht[1].table[h - d->ht[0].size] :
                                      d->ht[0].table[h];
        } while(he == NULL);
    } else {
        do {
            h = random() & d->ht[0].sizemask;
            he = d->ht[0].table[h];
        } while(he == NULL);
    }

    /* Now we found a non empty bucket, but it is a linked
     * list and we need to get a random element from the list.
     * The only sane way to do so is counting the elements and
     * select a random index. */
    listlen = 0;
    orighe = he;
    while(he) {
        he = he->next;
        listlen++;
    }
    listele = random() % listlen;
    he = orighe;
    while(listele--) he = he->next;
    return he;
}

/* This is a version of dictGetRandomKey() that is modified in order to
 * return multiple entries by jumping at a random place of the hash table
 * and scanning linearly for entries.
 *
 * Returned pointers to hash table entries are stored into 'des' that
 * points to an array of dictEntry pointers. The array must have room for
 * at least 'count' elements, that is the argument we pass to the function
 * to tell how many random elements we need.
 *
 * The function returns the number of items stored into 'des', that may
 * be less than 'count' if the hash table has less than 'count' elements
 * inside.
 *
 * Note that this function is not suitable when you need a good distribution
 * of the returned items, but only when you need to "sample" a given number
 * of continuous elements to run some kind of algorithm or to produce
 * statistics. However the function is much faster than dictGetRandomKey()
 * at producing N elements, and the elements are guaranteed to be non
 * repeating. */
unsigned int dictGetRandomKeys(dict *d, dictEntry **des, unsigned int count) {
    int j; /* internal hash table id, 0 or 1. */
    unsigned int stored = 0;

    if (dictSize(d) < count) count = dictSize(d);
    while(stored < count) {
        for (j = 0; j < 2; j++) {
            /* Pick a random point inside the hash table 0 or 1. */
            unsigned int i = random() & d->ht[j].sizemask;
            int size = d->ht[j].size;

            /* Make sure to visit every bucket by iterating 'size' times. */
            while(size--) {
                dictEntry *he = d->ht[j].table[i];
                while (he) {
                    /* Collect all the elements of the buckets found non
                     * empty while iterating. */
                    *des = he;
                    des++;
                    he = he->next;
                    stored++;
                    if (stored == count) return stored;
                }
                i = (i+1) & d->ht[j].sizemask;
            }
            /* If there is only one table and we iterated it all, we should
             * already have 'count' elements. Assert this condition. */
            assert(dictIsRehashing(d) != 0);
        }
    }
    return stored; /* Never reached. */
}

/* Function to reverse bits. Algorithm from:
 * http://graphics.stanford.edu/~seander/bithacks.html#ReverseParallel */
static unsigned long rev(unsigned long v) {
    unsigned long s = 8 * sizeof(v); // bit size; must be power of 2
    unsigned long mask = ~0;
    while ((s >>= 1) > 0) {
        mask ^= (mask << s);
        v = ((v >> s) & mask) | ((v << s) & ~mask);
    }
    return v;
}

/* dictScan() is used to iterate over the elements of a dictionary.
 *
 * Iterating works the following way:
 *
 * 1) Initially you call the function using a cursor (v) value of 0.
 * 2) The function performs one step of the iteration, and returns the
 *    new cursor value you must use in the next call.
 * 3) When the returned cursor is 0, the iteration is complete.
 *
 * The function guarantees all elements present in the
 * dictionary get returned between the start and end of the iteration.
 * However it is possible some elements get returned multiple times.
 *
 * For every element returned, the callback argument 'fn' is
 * called with 'privdata' as first argument and the dictionary entry
 * 'de' as second argument.
 *
 * HOW IT WORKS.
 *
 * The iteration algorithm was designed by Pieter Noordhuis.
 * The main idea is to increment a cursor starting from the higher order
 * bits. That is, instead of incrementing the cursor normally, the bits
 * of the cursor are reversed, then the cursor is incremented, and finally
 * the bits are reversed again.
 *
 * This strategy is needed because the hash table may be resized between
 * iteration calls.
 *
 * dict.c hash tables are always power of two in size, and they
 * use chaining, so the position of an element in a given table is given
 * by computing the bitwise AND between Hash(key) and SIZE-1
 * (where SIZE-1 is always the mask that is equivalent to taking the rest
 *  of the division between the Hash of the key and SIZE).
 *
 * For example if the current hash table size is 16, the mask is
 * (in binary) 1111. The position of a key in the hash table will always be
 * the last four bits of the hash output, and so forth.
 *
 * WHAT HAPPENS IF THE TABLE CHANGES IN SIZE?
 *
 * If the hash table grows, elements can go anywhere in one multiple of
 * the old bucket: for example let's say we already iterated with
 * a 4 bit cursor 1100 (the mask is 1111 because hash table size = 16).
 *
 * If the hash table will be resized to 64 elements, then the new mask will
 * be 111111. The new buckets you obtain by substituting in ??1100
 * with either 0 or 1 can be targeted only by keys we already visited
 * when scanning the bucket 1100 in the smaller hash table.
 *
 * By iterating the higher bits first, because of the inverted counter, the
 * cursor does not need to restart if the table size gets bigger. It will
 * continue iterating using cursors without '1100' at the end, and also
 * without any other combination of the final 4 bits already explored.
 *
 * Similarly when the table size shrinks over time, for example going from
 * 16 to 8, if a combination of the lower three bits (the mask for size 8
 * is 111) were already completely explored, it would not be visited again
 * because we are sure we tried, for example, both 0111 and 1111 (all the
 * variations of the higher bit) so we don't need to test it again.
 *
 * WAIT... YOU HAVE *TWO* TABLES DURING REHASHING!
 *
 * Yes, this is true, but we always iterate the smaller table first, then
 * we test all the expansions of the current cursor into the larger
 * table. For example if the current cursor is 101 and we also have a
 * larger table of size 16, we also test (0)101 and (1)101 inside the larger
 * table. This reduces the problem back to having only one table, where
 * the larger one, if it exists, is just an expansion of the smaller one.
 *
 * LIMITATIONS
 *
 * This iterator is completely stateless, and this is a huge advantage,
 * including no additional memory used.
 *
 * The disadvantages resulting from this design are:
 *
 * 1) It is possible we return elements more than once. However this is usually
 *    easy to deal with in the application level.
 * 2) The iterator must return multiple elements per call, as it needs to always
 *    return all the keys chained in a given bucket, and all the expansions, so
 *    we are sure we don't miss keys moving during rehashing.
 * 3) The reverse cursor is somewhat hard to understand at first, but this
 *    comment is supposed to help.
 */
unsigned long dictScan(dict *d,
                       unsigned long v,
                       dictScanFunction *fn,
                       void *privdata)
{
    dictht *t0, *t1;
    const dictEntry *de;
    unsigned long m0, m1;

    if (dictSize(d) == 0) return 0;

    if (!dictIsRehashing(d)) {
        t0 = &(d->ht[0]);
        m0 = t0->sizemask;

        /* Emit entries at cursor */
        de = t0->table[v & m0];
        while (de) {
            fn(privdata, de);
            de = de->next;
        }

    } else {
        t0 = &d->ht[0];
        t1 = &d->ht[1];

        /* Make sure t0 is the smaller and t1 is the bigger table */
        if (t0->size > t1->size) {
            t0 = &d->ht[1];
            t1 = &d->ht[0];
        }

        m0 = t0->sizemask;
        m1 = t1->sizemask;

        /* Emit entries at cursor */
        de = t0->table[v & m0];
        while (de) {
            fn(privdata, de);
            de = de->next;
        }

        /* Iterate over indices in larger table that are the expansion
         * of the index pointed to by the cursor in the smaller table */
        do {
            /* Emit entries at cursor */
            de = t1->table[v & m1];
            while (de) {
                fn(privdata, de);
                de = de->next;
            }

            /* Increment bits not covered by the smaller mask */
            v = (((v | m0) + 1) & ~m0) | (v & m0);

            /* Continue while bits covered by mask difference is non-zero */
        } while (v & (m0 ^ m1));
    }

    /* Set unmasked bits so incrementing the reversed cursor
     * operates on the masked bits of the smaller table */
    v |= ~m0;

    /* Increment the reverse cursor */
    v = rev(v);
    v++;
    v = rev(v);

    return v;
}

/* ------------------------- private functions ------------------------------ */

/* Expand the hash table if needed */
static int _dictExpandIfNeeded(dict *d)
{
    /* Incremental rehashing already in progress. Return. */
    if (dictIsRehashing(d)) return DICT_OK;

    /* If the hash table is empty expand it to the initial size. */
    if (d->ht[0].size == 0) return dictExpand(d, DICT_HT_INITIAL_SIZE);

    /* If we reached the 1:1 ratio, and we are allowed to resize the hash
     * table (global setting) or we should avoid it but the ratio between
     * elements/buckets is over the "safe" threshold, we resize doubling
     * the number of buckets. */
    if (d->ht[0].used >= d->ht[0].size &&
        (dict_can_resize ||
         d->ht[0].used/d->ht[0].size > dict_force_resize_ratio))
    {
        return dictExpand(d, d->ht[0].used*2);
    }
    return DICT_OK;
}

/* Our hash table capability is a power of two */
static unsigned long _dictNextPower(unsigned long size)
{
    unsigned long i = DICT_HT_INITIAL_SIZE;

    if (size >= LONG_MAX) return LONG_MAX;
    while(1) {
        if (i >= size)
            return i;
        i *= 2;
    }
}

/* Returns the index of a free slot that can be populated with
 * a hash entry for the given 'key'.
 * If the key already exists, -1 is returned.
 *
 * Note that if we are in the process of rehashing the hash table, the
 * index is always returned in the context of the second (new) hash table. */
static int _dictKeyIndex(dict *d, const void *key)
{
    unsigned int h, idx, table;
    dictEntry *he;

    /* Expand the hash table if needed */
    if (_dictExpandIfNeeded(d) == DICT_ERR)
        return -1;
    /* Compute the key hash value */
    h = dictHashKey(d, key);
    for (table = 0; table <= 1; table++) {
        idx = h & d->ht[table].sizemask;
        /* Search if this slot does not already contain the given key */
        he = d->ht[table].table[idx];
        while(he) {
            if (dictCompareKeys(d, key, he->key))
                return -1;
            he = he->next;
        }
        if (!dictIsRehashing(d)) break;
    }
    return idx;
}

void dictEmpty(dict *d, void(callback)(void*)) {
    _dictClear(d,&d->ht[0],callback);
    _dictClear(d,&d->ht[1],callback);
    d->rehashidx = -1;
    d->iterators = 0;
}

void dictEnableResize(void) {
    dict_can_resize = 1;
}

void dictDisableResize(void) {
    dict_can_resize = 0;
}

#if 0

/* The following is code that we don't use for Redis currently, but that is part
of the library. */

/* ----------------------- Debugging ------------------------*/

#define DICT_STATS_VECTLEN 50
static void _dictPrintStatsHt(dictht *ht) {
    unsigned long i, slots = 0, chainlen, maxchainlen = 0;
    unsigned long totchainlen = 0;
    unsigned long clvector[DICT_STATS_VECTLEN];

    if (ht->used == 0) {
        printf("No stats available for empty dictionaries\n");
        return;
    }

    for (i = 0; i < DICT_STATS_VECTLEN; i++) clvector[i] = 0;
    for (i = 0; i < ht->size; i++) {
        dictEntry *he;

        if (ht->table[i] == NULL) {
            clvector[0]++;
            continue;
        }
        slots++;
        /* For each hash entry on this slot... */
        chainlen = 0;
        he = ht->table[i];
        while(he) {
            chainlen++;
            he = he->next;
        }
        clvector[(chainlen < DICT_STATS_VECTLEN) ? chainlen : (DICT_STATS_VECTLEN-1)]++;
        if (chainlen > maxchainlen) maxchainlen = chainlen;
        totchainlen += chainlen;
    }
    printf("Hash table stats:\n");
    printf(" table size: %ld\n", ht->size);
    printf(" number of elements: %ld\n", ht->used);
    printf(" different slots: %ld\n", slots);
    printf(" max chain length: %ld\n", maxchainlen);
    printf(" avg chain length (counted): %.02f\n", (float)totchainlen/slots);
    printf(" avg chain length (computed): %.02f\n", (float)ht->used/slots);
    printf(" Chain length distribution:\n");
    for (i = 0; i < DICT_STATS_VECTLEN-1; i++) {
        if (clvector[i] == 0) continue;
        printf("   %s%ld: %ld (%.02f%%)\n",(i == DICT_STATS_VECTLEN-1)?">= ":"", i, clvector[i], ((float)clvector[i]/ht->size)*100);
    }
}

void dictPrintStats(dict *d) {
    _dictPrintStatsHt(&d->ht[0]);
    if (dictIsRehashing(d)) {
        printf("-- Rehashing into ht[1]:\n");
        _dictPrintStatsHt(&d->ht[1]);
    }
}

/* ----------------------- StringCopy Hash Table Type ------------------------*/

static unsigned int _dictStringCopyHTHashFunction(const void *key)
{
    return dictGenHashFunction(key, strlen(key));
}

static void *_dictStringDup(void *privdata, const void *key)
{
    int len = strlen(key);
    char *copy = zmalloc(len+1);
    DICT_NOTUSED(privdata);

    memcpy(copy, key, len);
    copy[len] = '\0';
    return copy;
}

static int _dictStringCopyHTKeyCompare(void *privdata, const void *key1,
        const void *key2)
{
    DICT_NOTUSED(privdata);

    return strcmp(key1, key2) == 0;
}

static void _dictStringDestructor(void *privdata, void *key)
{
    DICT_NOTUSED(privdata);

    zfree(key);
}

dictType dictTypeHeapStringCopyKey = {
    _dictStringCopyHTHashFunction, /* hash function */
    _dictStringDup,                /* key dup */
    NULL,                          /* val dup */
    _dictStringCopyHTKeyCompare,   /* key compare */
    _dictStringDestructor,         /* key destructor */
    NULL                           /* val destructor */
};

/* This is like StringCopy but does not auto-duplicate the key.
 * It's used for intepreter's shared strings. */
dictType dictTypeHeapStrings = {
    _dictStringCopyHTHashFunction, /* hash function */
    NULL,                          /* key dup */
    NULL,                          /* val dup */
    _dictStringCopyHTKeyCompare,   /* key compare */
    _dictStringDestructor,         /* key destructor */
    NULL                           /* val destructor */
};

/* This is like StringCopy but also automatically handle dynamic
 * allocated C strings as values. */
dictType dictTypeHeapStringCopyKeyValue = {
    _dictStringCopyHTHashFunction, /* hash function */
    _dictStringDup,                /* key dup */
    _dictStringDup,                /* val dup */
    _dictStringCopyHTKeyCompare,   /* key compare */
    _dictStringDestructor,         /* key destructor */
    _dictStringDestructor,         /* val destructor */
};
#endif
