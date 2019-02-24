#include "PageCache.hpp"

PageCache PageCache::_inst;

Span* PageCache::NewSpan(size_t npage)
{
	// ��������ֹ����߳�ͬʱ��PageCache������span
	// ��������Ǹ�ȫ�ּ��������ܵ����ĸ�ÿ��Ͱ����
	// �����ӦͰû��span,����Ҫ��ϵͳ�����
	// ���ܴ��ڶ���߳�ͬʱ��ϵͳ�����ڴ�Ŀ���
	std::unique_lock<std::mutex> lock(_mtx);
	// ������Ǵ���128��ҳ�Ĵ�С
	if (npage >= NPAGES)
	{
		void* ptr = SystemAlloc(npage);
		// ����spanȥ�����ϵͳ������ڴ�
		Span* span = new Span();
		span->_pageid = (PageID)ptr >> PAGE_SHIFT;
		span->_npage = npage;
		span->_objsize = npage << PAGE_SHIFT;

		// ������Ĵ���ڴ��ĵ�һ��ҳ�Ų����ȥ�Ϳ���
		// ��Ϊ������ڴ����64K,��ֱ�ӻ���PageCache,����Ҫ����ҳ�����span��ӳ��
		_id_span_map[span->_pageid] = span;

		return span;
	}

	// ����64k-128ҳ���ڴ��ʱ��
	// ��Ҫ��span��objsize����Ϊspan�Ĵ�С
	// ��Ϊ�����ϵͳ�黹���spanʱ��Ҫ��С
	Span* span = _NewSpan(npage);
	span->_objsize = span->_npage << PAGE_SHIFT;
	return span;
}
Span* PageCache::_NewSpan(size_t npage)
{
	// ���ڿ��е�span,ֱ�ӷ���һ��span
	if (!_pagelist[npage].Empty())
	{
		return _pagelist[npage].PopFront();
	}
	// ���npage��spanΪ�գ����������������span�ǲ���Ϊ�յģ�������ǿյľ��и����span
	for (size_t i = npage + 1; i < NPAGES; i++)
	{
		if (!_pagelist[i].Empty())
		{
			// �����и�
			Span* span = _pagelist[i].PopFront();
			Span* split = new Span();

			// ҳ��:��span�ĺ�������и�
			split->_pageid = span->_pageid + span->_npage - npage;
			// ҳ��
			split->_npage = npage;
			span->_npage = span->_npage - npage;

			// ���·ָ������ҳ��ӳ�䵽�µ�span��
			for (size_t i = 0; i < npage; i++)
			{
				_id_span_map[split->_pageid + i] = split;
			}

			_pagelist[span->_npage].PushFront(span);

			return split;
		}
	}
	// ������˵��SpanList��û�к��ʵ�span,ֻ����ϵͳ����128ҳ���ڴ�
	// void* ptr = SystemAlloc(npage);
	void* ptr = SystemAlloc(128);
	Span* largespan = new Span();
	largespan->_pageid = (PageID)(ptr) >> PAGE_SHIFT;
	largespan->_npage = NPAGES - 1;
	_pagelist[NPAGES - 1].PushFront(largespan);

	// ����ϵͳ�����ҳ��ӳ�䵽ͬһ��span
	for (size_t i = 0; i < largespan->_npage; i++)
	{
		_id_span_map[largespan->_pageid + i] = largespan;
	}

	// β�ݹ�һ��
	return _NewSpan(npage);
}

// ��map�ҵ���Ӧָ���span
Span* PageCache::MapObjectToSpan(void* obj)
{
	// ������ڴ��ҳ��
	PageID pageid = (PageID)(obj) >> PAGE_SHIFT;
	
	auto it = _id_span_map.find(pageid);

	assert(it != _id_span_map.end());

	return it->second;
}

// ��CentralCache��span�黹��PageCache���кϲ�(�����ڴ���Ƭ:����Ƭ)
void PageCache::RelaseToPageCache(Span* span)
{
	// ������ȫ����,���ܶ���߳�һ���ThreadCache�й黹����
	std::unique_lock<std::mutex> lock(_mtx);

	// ���ͷŵ��ڴ��Ǵ���128ҳ,ֱ�ӽ��ڴ�黹������ϵͳ,���ܺϲ�
	if (span->_npage >= NPAGES)
	{
		void* ptr = (void*)(span->_pageid << PAGE_SHIFT);
		// �黹֮ǰɾ����ҳ��span��ӳ��
		_id_span_map.erase(span->_pageid);
		SystemFree(ptr);
		delete span;
		return;
	}

	// �ϲ�����:����ǰ�ϲ���Ȼ��������span�ٴ����ϲ�
	// �ҵ����spanǰ��һ��span
	auto previt = _id_span_map.find(span->_pageid - 1);

	while (previt != _id_span_map.end())
	{
		Span* prevspan = previt->second;
		
		// �ж�ǰ���span�ļ����ǲ���0
		if (prevspan->_usecount != 0)
		{
			break;
		}
		
		// �ж�ǰ���span���Ϻ����span��û�г���NPAGES
		// ����128ҳ���ܺϲ�
		if (prevspan->_npage + span->_npage >= NPAGES)
		{
			break;
		}

		// ���кϲ�(������ҳ��ֻҪ��span�е�ҳ������һ�𼴿�)
		_pagelist[prevspan->_npage].Erase(prevspan);
		prevspan->_npage += span->_npage;
		delete(span);
		span = prevspan;
		
		previt = _id_span_map.find(span->_pageid - 1);
	}

	// �ҵ����span�����span(Ҫ������span)
	auto nextvit = _id_span_map.find(span->_pageid + span->_npage);

	while (nextvit != _id_span_map.end())
	{
		Span* nextspan = nextvit->second;

		// �жϺ��span�ļ����ǲ���0
		if (nextspan->_usecount != 0)
		{
			break;
		}

		// �ж�ǰ���span���Ϻ����span��û�г���NPAGES
		if (nextspan->_npage + span->_npage >= NPAGES)
		{
			break;
		}

		// ���кϲ�,�������span��span����ɾ��,�ϲ���ǰ���span��
		_pagelist[nextspan->_npage].Erase(nextspan);
		span->_npage += nextspan->_npage;
		delete(nextspan);

		nextvit = _id_span_map.find(span->_pageid + span->_npage);
	}

	// ���ϲ��õ�ҳ��ӳ�䵽�µ�span��
	for (size_t i = 0; i < span->_npage; i++)
	{
		_id_span_map[span->_pageid + i] = span;
	}

	// ��󽫺ϲ��õ�span���뵽span����
	_pagelist[span->_npage].PushFront(span);
}