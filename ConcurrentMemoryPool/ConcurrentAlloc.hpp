#pragma once

#include "Common.hpp"
#include "ThreadCache.hpp"
#include "PageCache.hpp"

// �û������ڴ�
void* ConcurrentAlloc(size_t size)
{
	// ������ڴ����64K,��ֱ�ӵ�PageCache�������ڴ�
	if (size > MAXBYTES)
	{
		// ��ҳ�Ĵ�С����,����������ҳ�ĸ���(1ҳ=4k)
		size_t roundsize = ClassSize::_RoundUp(size, 1 << PAGE_SHIFT);
		size_t npage = roundsize >> PAGE_SHIFT;
		// ���span����
		Span* span = PageCache::GetInstance()->NewSpan(npage);
		void* ptr = (void*)(span->_pageid << PAGE_SHIFT);

		return ptr;
	}
	else // ��64K֮��ֱ�����̻߳����������ڴ�
	{
		// ��ȡ�߳��Լ���tls(�̱߳��ش洢)
		// ����������:��ô��ÿ���̶߳����Լ���һ���̻߳���
		// �������:
		// ����һ:��������һ��ȫ�ַ��ʱ�(�߳�id--�̻߳���)
		// ����:����߳�ȥȫ�ַ��ʱ�ͬʱȥ�õ�ʱ����Ҫ��ȫ�ַ��ʱ�������Ч������
		// ������:����һ����̬�̱߳��ش洢,ÿ���̶߳��൱�������Լ���һ��ȫ�ֱ���(ʵ��)
		if (tls_threadcache == nullptr)
		{
			tls_threadcache = new ThreadCache;
		}
		// ���ػ�ȡ���ڴ��ĵ�ַ
		return tls_threadcache->Allocate(size);
	}
}

// �û��ͷ��ڴ�
void ConcurrentFree(void* ptr)
{
	// ��ȡҳ�ŵ�span��ӳ��
	Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);
	// ��Ŀ������������:free�ͷ�ʱ����Ҫ��С����ôʵ�ֲ���Ҫ��С
	// �������: �������64k,ֱ��ȥPageCache,��С��¼��objsize
	//			 �ͷŴ���64k���ڴ棬ֱ�ӹ黹��PageCache,objsize�д��Ŵ�С
	size_t size = span->_objsize;

	// �Ǵ���64K,����黹��PageCache
	if (size > MAXBYTES)
	{
		PageCache::GetInstance()->RelaseToPageCache(span);
	}
	else
	{
		return tls_threadcache->Deallocate(ptr, size);
	}
}