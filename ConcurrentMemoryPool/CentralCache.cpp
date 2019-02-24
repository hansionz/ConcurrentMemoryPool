#include "CentralCache.hpp"
#include "PageCache.hpp"

CentralCache CentralCache::_inst;

// 当Central Cache中存在span的时候，直接返回span,否则要到PageCache中获取span
Span* CentralCache::GetOneSpan(SpanList* spanlist, size_t bytes)
{
	// CentralCache中存在span
	Span* span = spanlist->begin();
	while (span != spanlist->end())
	{
		if (span->_objlist != nullptr)
		{
			return span;
		}
		span = span->_next;
	}

	// CentralCache中的span为空
	// 需要计算要获取几页npage
	size_t npage = ClassSize::NumMovePage(bytes);
	Span* newSpan = PageCache::GetInstance()->NewSpan(npage);

	// 获取到了newSpan之后，这个newSpan是npage数量的页
	// 需要将这个span分割成一个个的bytes大小的内存块连接起来
	// 地址设置为char*是为了一会儿可以方便的挂链，每次加数字的时候，可以直接移动这么多的字节
	char* start = (char*)(newSpan->_pageid << PAGE_SHIFT);

	// end是当前内存块的最后一个字节地址的下一个字节地址
	char* end = start + (newSpan->_npage << PAGE_SHIFT);

	char* next = start + bytes;
	char* cur = start;
	while (next < end)
	{
		NEXT_OBJ(cur) = next;
		cur = next;
		next = cur + bytes;
	}
	NEXT_OBJ(cur) = nullptr;

	newSpan->_objlist = start;
	newSpan->_objsize = bytes;
	newSpan->_usecount = 0;

	spanlist->PushFront(newSpan);
	return newSpan;
}

// 从中心缓存获取内存块给ThreadCache
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t num, size_t bytes)
{
	//求出在span链表数组中的下标
	size_t index = ClassSize::Index(bytes);
	SpanList* spanlist = &_spanlist[index];

	// 对当前桶进行加锁,可能存在多个线程同时来同一个SpanList来获取内存对象
	std::unique_lock<std::mutex> lock(spanlist->_mtx);

	Span* span = GetOneSpan(spanlist, bytes);
	
	void* cur = span->_objlist;
	// prev记录最后一个内存对象
	void* prev= cur;
	size_t fetchnum = 0;
	// 可能该span中没有那么多的内存对象
	// 这种情况下有多少返给多少
	while (cur != nullptr && fetchnum < num)
	{
		prev = cur;
		cur = NEXT_OBJ(cur);
		fetchnum++;
	}

	start = span->_objlist;
	end = prev;

	NEXT_OBJ(end) = nullptr;

	// 将剩下来的内存对象再次接span的objlist上
	span->_objlist = cur;

	span->_usecount += fetchnum;

	// 每次将span中的内存块拿出来的时候,判断这个span中还有没有内存块
	// 没有就放到最后，这样做可以提高下一次的检索效率
	if (span->_objlist == nullptr)
	{
		spanlist->Erase(span);
		spanlist->PushBack(span);
	}

	return fetchnum;
}

void CentralCache::ReleaseListToSpans(void* start, size_t byte)
{
	// 找到对应的spanlist
	size_t index = ClassSize::Index(byte);
	SpanList* spanlist = &_spanlist[index];

	// CentralCache:对当前桶进行加锁(桶锁)
	// PageCache:必须对整个SpanList全局加锁
	// 因为可能存在多个线程同时去系统申请内存的情况
	std::unique_lock<std::mutex> lock(spanlist->_mtx);

	// 遍历start那条空闲链表，从新连到span的_objlist中
	while (start)
	{
		void* next = NEXT_OBJ(start);
		// 获取内存对象到span的映射
		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);

		// 当一个span为空将一个span移到尾上
		// 提高效率
		if (span->_objlist == nullptr)
		{
			spanlist->Erase(span);
			spanlist->PushBack(span);
		}

		// 将内存对象采用头插归还给CentralCache的span
		NEXT_OBJ(start) = span->_objlist;
		span->_objlist = start;

		// 使用计数变成0,说明这个span上的内存块都还回来
		// 那就将这个span归还给PageCache进行合并来减少内存碎片
		if (--span->_usecount == 0)
		{
			spanlist->Erase(span);

			span->_next = nullptr;
			span->_prev = nullptr;
			span->_objlist = nullptr;
			span->_objsize = 0;
			// 将一个span从CentralCache归还到PageCache的时候只需要页号和页数
			// 不需要其他的东西所以对于其他的数据进行赋空

			PageCache::GetInstance()->RelaseToPageCache(span);
		}
		start = next;
	}

}
