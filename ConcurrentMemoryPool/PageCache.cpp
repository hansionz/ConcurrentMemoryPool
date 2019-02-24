#include "PageCache.hpp"

PageCache PageCache::_inst;

Span* PageCache::NewSpan(size_t npage)
{
	// 加锁，防止多个线程同时到PageCache中申请span
	// 这里必须是给全局加锁，不能单独的给每个桶加锁
	// 如果对应桶没有span,是需要向系统申请的
	// 可能存在多个线程同时向系统申请内存的可能
	std::unique_lock<std::mutex> lock(_mtx);
	// 申请的是大于128的页的大小
	if (npage >= NPAGES)
	{
		void* ptr = SystemAlloc(npage);
		// 构造span去管理从系统申请的内存
		Span* span = new Span();
		span->_pageid = (PageID)ptr >> PAGE_SHIFT;
		span->_npage = npage;
		span->_objsize = npage << PAGE_SHIFT;

		// 将申请的大块内存块的第一个页号插入进去就可以
		// 因为申请的内存大于64K,是直接还给PageCache,不需要其他页到这个span的映射
		_id_span_map[span->_pageid] = span;

		return span;
	}

	// 申请64k-128页的内存的时候
	// 需要将span的objsize设置为span的大小
	// 因为向操作系统归还这个span时需要大小
	Span* span = _NewSpan(npage);
	span->_objsize = span->_npage << PAGE_SHIFT;
	return span;
}
Span* PageCache::_NewSpan(size_t npage)
{
	// 存在空闲的span,直接返回一个span
	if (!_pagelist[npage].Empty())
	{
		return _pagelist[npage].PopFront();
	}
	// 如果npage的span为空，接下来检查比他大的span是不是为空的，如果不是空的就切割这个span
	for (size_t i = npage + 1; i < NPAGES; i++)
	{
		if (!_pagelist[i].Empty())
		{
			// 进行切割
			Span* span = _pagelist[i].PopFront();
			Span* split = new Span();

			// 页号:从span的后面进行切割
			split->_pageid = span->_pageid + span->_npage - npage;
			// 页数
			split->_npage = npage;
			span->_npage = span->_npage - npage;

			// 将新分割出来的页都映射到新的span上
			for (size_t i = 0; i < npage; i++)
			{
				_id_span_map[split->_pageid + i] = split;
			}

			_pagelist[span->_npage].PushFront(span);

			return split;
		}
	}
	// 到这里说明SpanList中没有合适的span,只能向系统申请128页的内存
	// void* ptr = SystemAlloc(npage);
	void* ptr = SystemAlloc(128);
	Span* largespan = new Span();
	largespan->_pageid = (PageID)(ptr) >> PAGE_SHIFT;
	largespan->_npage = NPAGES - 1;
	_pagelist[NPAGES - 1].PushFront(largespan);

	// 将从系统申请的页都映射到同一个span
	for (size_t i = 0; i < largespan->_npage; i++)
	{
		_id_span_map[largespan->_pageid + i] = largespan;
	}

	// 尾递归一次
	return _NewSpan(npage);
}

// 查map找到对应指针的span
Span* PageCache::MapObjectToSpan(void* obj)
{
	// 计算该内存的页号
	PageID pageid = (PageID)(obj) >> PAGE_SHIFT;
	
	auto it = _id_span_map.find(pageid);

	assert(it != _id_span_map.end());

	return it->second;
}

// 将CentralCache的span归还给PageCache进行合并(减少内存碎片:外碎片)
void PageCache::RelaseToPageCache(Span* span)
{
	// 必须上全局锁,可能多个线程一起从ThreadCache中归还数据
	std::unique_lock<std::mutex> lock(_mtx);

	// 当释放的内存是大于128页,直接将内存归还给操作系统,不能合并
	if (span->_npage >= NPAGES)
	{
		void* ptr = (void*)(span->_pageid << PAGE_SHIFT);
		// 归还之前删除掉页到span的映射
		_id_span_map.erase(span->_pageid);
		SystemFree(ptr);
		delete span;
		return;
	}

	// 合并过程:先向前合并，然后跳过该span再次向后合并
	// 找到这个span前面一个span
	auto previt = _id_span_map.find(span->_pageid - 1);

	while (previt != _id_span_map.end())
	{
		Span* prevspan = previt->second;
		
		// 判断前面的span的计数是不是0
		if (prevspan->_usecount != 0)
		{
			break;
		}
		
		// 判断前面的span加上后面的span有没有超出NPAGES
		// 超过128页不能合并
		if (prevspan->_npage + span->_npage >= NPAGES)
		{
			break;
		}

		// 进行合并(连续的页，只要将span中的页数加在一起即可)
		_pagelist[prevspan->_npage].Erase(prevspan);
		prevspan->_npage += span->_npage;
		delete(span);
		span = prevspan;
		
		previt = _id_span_map.find(span->_pageid - 1);
	}

	// 找到这个span后面的span(要跳过该span)
	auto nextvit = _id_span_map.find(span->_pageid + span->_npage);

	while (nextvit != _id_span_map.end())
	{
		Span* nextspan = nextvit->second;

		// 判断后边span的计数是不是0
		if (nextspan->_usecount != 0)
		{
			break;
		}

		// 判断前面的span加上后面的span有没有超出NPAGES
		if (nextspan->_npage + span->_npage >= NPAGES)
		{
			break;
		}

		// 进行合并,将后面的span从span链中删除,合并到前面的span上
		_pagelist[nextspan->_npage].Erase(nextspan);
		span->_npage += nextspan->_npage;
		delete(nextspan);

		nextvit = _id_span_map.find(span->_pageid + span->_npage);
	}

	// 将合并好的页都映射到新的span上
	for (size_t i = 0; i < span->_npage; i++)
	{
		_id_span_map[span->_pageid + i] = span;
	}

	// 最后将合并好的span插入到span链中
	_pagelist[span->_npage].PushFront(span);
}