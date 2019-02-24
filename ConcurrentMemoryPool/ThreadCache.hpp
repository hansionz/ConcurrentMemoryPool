#pragma once
#include "Common.hpp"
#include "CentralCache.hpp"
#include <iostream>
#include <stdlib.h>

class ThreadCache
{
public:
	//给线程分配内存
	void* Allocate(size_t size);

	//释放内存
	void Deallocate(void* ptr, size_t size);

	// 从中心缓存中拿取内存
	void* FetchFromCentralCache(size_t index, size_t size);

	//当链表中的对象太多的时候开始回收到中心缓存
	void ListTooLong(FreeList* freelist, size_t byte);

private:
	// 创建了一个自由链表数组，长度为NLISTS是240
	// 长度计算时根据对齐规则得来的
	FreeList _freelist[NLISTS];
};

// 静态的tls变量，每一个ThreadCache对象都有着自己的一个tls_threadcache
// _declspec(thread)相当于每一个线程都有一个属于自己的全局变量
static _declspec(thread) ThreadCache* tls_threadcache = nullptr;