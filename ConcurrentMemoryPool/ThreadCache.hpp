#pragma once
#include "Common.hpp"
#include "CentralCache.hpp"
#include <iostream>
#include <stdlib.h>

class ThreadCache
{
public:
	// �����ڴ�
	void* Allocate(size_t size);

	//�ͷ��ڴ�
	void Deallocate(void* ptr, size_t size);

	// �����Ļ�������ȡ�ڴ�
	void* FetchFromCentralCache(size_t index, size_t size);

	//�������еĶ���̫���ʱ��ʼ���յ����Ļ���
	void ListTooLong(FreeList* freelist, size_t byte);

private:
	// ������һ�������������飬����ΪNLISTS��240
	// ���ȼ���ʱ���ݶ�����������
	FreeList _freelist[NLISTS];
};

// ��̬��tls������ÿһ��ThreadCache���������Լ���һ��tls_threadcache
// _declspec(thread)�൱��ÿһ���̶߳���һ�������Լ���ȫ�ֱ���
static _declspec(thread) ThreadCache* tls_threadcache = nullptr;