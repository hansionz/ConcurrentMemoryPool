#include "CentralCache.hpp"
#include "PageCache.hpp"

CentralCache CentralCache::_inst;

// ��Central Cache�д���span��ʱ��ֱ�ӷ���span,����Ҫ��PageCache�л�ȡspan
Span* CentralCache::GetOneSpan(SpanList* spanlist, size_t bytes)
{
	// CentralCache�д���span
	Span* span = spanlist->begin();
	while (span != spanlist->end())
	{
		if (span->_objlist != nullptr)
		{
			return span;
		}
		span = span->_next;
	}

	// CentralCache�е�spanΪ��
	// ��Ҫ����Ҫ��ȡ��ҳnpage
	size_t npage = ClassSize::NumMovePage(bytes);
	Span* newSpan = PageCache::GetInstance()->NewSpan(npage);

	// ��ȡ����newSpan֮�����newSpan��npage������ҳ
	// ��Ҫ�����span�ָ��һ������bytes��С���ڴ����������
	// ��ַ����Ϊchar*��Ϊ��һ������Է���Ĺ�����ÿ�μ����ֵ�ʱ�򣬿���ֱ���ƶ���ô����ֽ�
	char* start = (char*)(newSpan->_pageid << PAGE_SHIFT);

	// end�ǵ�ǰ�ڴ������һ���ֽڵ�ַ����һ���ֽڵ�ַ
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

// �����Ļ����ȡ�ڴ���ThreadCache
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t num, size_t bytes)
{
	//�����span���������е��±�
	size_t index = ClassSize::Index(bytes);
	SpanList* spanlist = &_spanlist[index];

	// �Ե�ǰͰ���м���,���ܴ��ڶ���߳�ͬʱ��ͬһ��SpanList����ȡ�ڴ����
	std::unique_lock<std::mutex> lock(spanlist->_mtx);

	Span* span = GetOneSpan(spanlist, bytes);
	
	void* cur = span->_objlist;
	// prev��¼���һ���ڴ����
	void* prev= cur;
	size_t fetchnum = 0;
	// ���ܸ�span��û����ô����ڴ����
	// ����������ж��ٷ�������
	while (cur != nullptr && fetchnum < num)
	{
		prev = cur;
		cur = NEXT_OBJ(cur);
		fetchnum++;
	}

	start = span->_objlist;
	end = prev;

	NEXT_OBJ(end) = nullptr;

	// ��ʣ�������ڴ�����ٴν�span��objlist��
	span->_objlist = cur;

	span->_usecount += fetchnum;

	// ÿ�ν�span�е��ڴ���ó�����ʱ��,�ж����span�л���û���ڴ��
	// û�оͷŵ�������������������һ�εļ���Ч��
	if (span->_objlist == nullptr)
	{
		spanlist->Erase(span);
		spanlist->PushBack(span);
	}

	return fetchnum;
}

void CentralCache::ReleaseListToSpans(void* start, size_t byte)
{
	// �ҵ���Ӧ��spanlist
	size_t index = ClassSize::Index(byte);
	SpanList* spanlist = &_spanlist[index];

	// CentralCache:�Ե�ǰͰ���м���(Ͱ��)
	// PageCache:���������SpanListȫ�ּ���
	// ��Ϊ���ܴ��ڶ���߳�ͬʱȥϵͳ�����ڴ�����
	std::unique_lock<std::mutex> lock(spanlist->_mtx);

	// ����start��������������������span��_objlist��
	while (start)
	{
		void* next = NEXT_OBJ(start);
		// ��ȡ�ڴ����span��ӳ��
		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);

		// ��һ��spanΪ�ս�һ��span�Ƶ�β��
		// ���Ч��
		if (span->_objlist == nullptr)
		{
			spanlist->Erase(span);
			spanlist->PushBack(span);
		}

		// ���ڴ�������ͷ��黹��CentralCache��span
		NEXT_OBJ(start) = span->_objlist;
		span->_objlist = start;

		// ʹ�ü������0,˵�����span�ϵ��ڴ�鶼������
		// �Ǿͽ����span�黹��PageCache���кϲ��������ڴ���Ƭ
		if (--span->_usecount == 0)
		{
			spanlist->Erase(span);

			span->_next = nullptr;
			span->_prev = nullptr;
			span->_objlist = nullptr;
			span->_objsize = 0;
			// ��һ��span��CentralCache�黹��PageCache��ʱ��ֻ��Ҫҳ�ź�ҳ��
			// ����Ҫ�����Ķ������Զ������������ݽ��и���

			PageCache::GetInstance()->RelaseToPageCache(span);
		}
		start = next;
	}

}
