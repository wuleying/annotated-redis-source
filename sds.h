/* SDSLib, A C dynamic strings library
 * 动态字符串库
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

#ifndef __SDS_H
#define __SDS_H

// 动态字符串最大的预分配长度1M
#define SDS_MAX_PREALLOC (1024*1024)

#include <sys/types.h>
#include <stdarg.h>

// 动态字符串类型，定义为char *，是后续绝大部分函数的参数
typedef char *sds;

// 动态字符串数据结构
struct sdshdr {
    // 已使用字节
    unsigned int len;
    // 空闲字节
    unsigned int free;
    // 指向实际存储数据buf的指针(C99 flexible array member)
    char buf[];
};

/*
 * 获取动态字符串buf已使用字节数
 *
 * s 动态字符串
 *
 */
static inline size_t sdslen(const sds s) {
    // 获取动态字符串指针
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    // 返回长度
    return sh->len;
}

/*
 * 获取动态字符串空闲字节
 *
 * s 动态字符串
 *
 */
static inline size_t sdsavail(const sds s) {
    // 获取动态字符串指针
    struct sdshdr *sh = (void*)(s-(sizeof(struct sdshdr)));
    // 返回空闲字节
    return sh->free;
}

// 创建一个指定长度的动态字符串
sds sdsnewlen(const void *init, size_t initlen);
// 根据给定值创建一个动态字符串
sds sdsnew(const char *init);
// 创建一个空动态字符串，长度为0
sds sdsempty(void);
// 获取动态字符串buf已使用字节数
size_t sdslen(const sds s);
// 复制给定的动态字符串
sds sdsdup(const sds s);
// 释放动态字符串
void sdsfree(sds s);
// 获取动态字符串空闲字节
size_t sdsavail(const sds s);
// 对动态字符串的buf进行扩展，扩展长度为len，无内容部分用\0填充
sds sdsgrowzero(sds s, size_t len);
// 按长度len扩展动态字符串，并将t拼接到末尾
sds sdscatlen(sds s, const void *t, size_t len);
// 将一个C字符串拼接到动态字符串末尾
sds sdscat(sds s, const char *t);
// 拼接两个动态字符串，t添加到s末尾
sds sdscatsds(sds s, const sds t);
// 将一个C字符串的前len个字节复制到动态字符串
sds sdscpylen(sds s, const char *t, size_t len);
// 将一个C字符串复制到动态字符串
sds sdscpy(sds s, const char *t);

sds sdscatvprintf(sds s, const char *fmt, va_list ap);
#ifdef __GNUC__
sds sdscatprintf(sds s, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
sds sdscatprintf(sds s, const char *fmt, ...);
#endif

// 格式化输出动态字符串
sds sdscatfmt(sds s, char const *fmt, ...);
sds sdstrim(sds s, const char *cset);
void sdsrange(sds s, int start, int end);
// 更新动态字符串长度
void sdsupdatelen(sds s);
void sdsclear(sds s);
int sdscmp(const sds s1, const sds s2);
sds *sdssplitlen(const char *s, int len, const char *sep, int seplen, int *count);
void sdsfreesplitres(sds *tokens, int count);
void sdstolower(sds s);
void sdstoupper(sds s);
sds sdsfromlonglong(long long value);
sds sdscatrepr(sds s, const char *p, size_t len);
sds *sdssplitargs(const char *line, int *argc);
sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen);
sds sdsjoin(char **argv, int argc, char *sep);

/* Low level functions exposed to the user API */

// 对动态字符串的buf进行扩展，扩展长度不小于addlen
sds sdsMakeRoomFor(sds s, size_t addlen);
// 在动态字符串buf右边加incr个位置，如果incr为负数，会截短bufs
void sdsIncrLen(sds s, int incr);
// 释放动态字符串buf的多余空间，并且不改动buf内容
sds sdsRemoveFreeSpace(sds s);
// 计算动态字符串buf占用的内存长度
size_t sdsAllocSize(sds s);

#endif
