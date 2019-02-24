/*
	进行资源的均衡，对于ThreadCache的某个资源过剩的时候，可以回收ThreadCache内部的的内存
	从而可以分配给其他的ThreadCache
	只有一个中心缓存，对于所有的线程来获取内存的时候都应该是一个中心缓存
	所以对于中心缓存可以使用单例模式来进行创建中心缓存的类
	对于中心缓存来说要加锁
*/

#pragma once 

#include "Common.hpp"

class CentralCache
{
public:
	static CentralCache* GetInstance()
	{
		return &_inst;
	}

	//从中心缓存获取一定数量的内存给thread cache
	size_t FetchRangeObj(void*& start, void*& end, size_t num, size_t byte);

	//从span链表数组中拿出和bytes相等的span链表，并在该链表中查找一个还有内存块的span
	Span* GetOneSpan(SpanList* spanlist, size_t bytes);

	//将ThreadCache中的内存块归还给CentralCache
	void ReleaseListToSpans(void* start, size_t byte);

private:
	SpanList _spanlist[NLISTS];//中心缓存的span链表的数组，默认大小是	NLISTS : 240

private:
	//构造函数默认化，也就是无参无内容
	CentralCache() = default;
	CentralCache(const CentralCache&)  = delete;
	CentralCache& operator=(const CentralCache&)  = delete;

	//创建一个对象
	static CentralCache _inst;
};