#pragma once

#include "Common.hpp"
#include "ThreadCache.hpp"
#include "PageCache.hpp"

// 用户申请内存
void* ConcurrentAlloc(size_t size)
{
	// 申请的内存大于64K,就直接到PageCache中申请内存
	if (size > MAXBYTES)
	{
		// 按页的大小对齐,计算出申请的页的个数(1页=4k)
		size_t roundsize = ClassSize::_RoundUp(size, 1 << PAGE_SHIFT);
		size_t npage = roundsize >> PAGE_SHIFT;
		// 获得span对象
		Span* span = PageCache::GetInstance()->NewSpan(npage);
		void* ptr = (void*)(span->_pageid << PAGE_SHIFT);

		return ptr;
	}
	else // 在64K之内直接在线程缓存中申请内存
	{
		// 获取线程自己的tls(线程本地存储)
		// 遇到的问题:怎么让每个线程都有自己的一个线程缓存
		// 解决方法:
		// 方法一:可以设置一个全局访问表(线程id--线程缓存)
		// 问题:多个线程去全局访问表同时去拿的时候，需要给全局访问表加锁造成效率问题
		// 方法二:设置一个静态线程本地存储,每个线程都相当于是有自己的一份全局变量(实用)
		if (tls_threadcache == nullptr)
		{
			tls_threadcache = new ThreadCache;
		}
		// 返回获取的内存块的地址
		return tls_threadcache->Allocate(size);
	}
}

// 用户释放内存
void ConcurrentFree(void* ptr)
{
	// 获取页号到span的映射
	Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);
	// 项目中遇到的问题:free释放时不需要大小，怎么实现不需要大小
	// 解决方法: 申请大于64k,直接去PageCache,大小记录在objsize
	//			 释放大于64k的内存，直接归还给PageCache,objsize中存着大小
	size_t size = span->_objsize;

	// 是大于64K,解决归还给PageCache
	if (size > MAXBYTES)
	{
		PageCache::GetInstance()->RelaseToPageCache(span);
	}
	else
	{
		return tls_threadcache->Deallocate(ptr, size);
	}
}