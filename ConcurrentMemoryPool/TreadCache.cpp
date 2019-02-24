#include "ThreadCache.hpp"
#include "CentralCache.hpp"

// �����Ļ�����ȡ����
// ÿһ��ȡ���������ݣ���Ϊÿ�ε�CentralCache�����ڴ��ʱ������Ҫ������
// ����һ�ξͶ�����һЩ�ڴ�飬��ֹÿ�ε�CentralCacheȥ�ڴ���ʱ��,��μ������Ч������
void* ThreadCache::FetchFromCentralCache(size_t index, size_t byte)
{
	FreeList* freelist = &_freelist[index];

	// ����ÿ������10�������ǽ����������Ĺ���
	// ��������ԽС�������ڴ�������Խ��
	// ��������Խ�������ڴ�������ԽС
	// �������Խ�࣬������
	// ������,������
	size_t num_to_move = min(ClassSize::NumMoveSize(byte), freelist->MaxSize());

	// start��end�ֱ��ʾȡ�������ڴ�Ŀ�ʼ��ַ�ͽ�����ַ
	// ȡ�������ڴ���һ������һ����ڴ������Ҫ��β��ʶ
	void* start, *end;

	// fetchnum��ʾʵ��ȡ�������ڴ�ĸ���
	// fetchnum�п���С��num����ʾ���Ļ���û����ô���С���ڴ��
	size_t fetchnum = CentralCache::GetInstance()->FetchRangeObj(start, end, num_to_move, byte);
	if (fetchnum > 1)
	{
		freelist->PushRange(NEXT_OBJ(start), end, fetchnum - 1);
	}
	// �ϴ�һ���ƶ����������ֵ����,����������1
	if (num_to_move == freelist->MaxSize())
	{
		freelist->SetMaxSize(num_to_move + 1);
	}
	return start;
}

// ���������������������������ȡ�ڴ����
void* ThreadCache::Allocate(size_t byte)
{
	assert(byte < MAXBYTES);
	// ����
	byte = ClassSize::RoundUp(byte);
	// ����λ��
	size_t index = ClassSize::Index(byte);
	FreeList* freelist = &_freelist[index];

	if (!freelist->Empty())
	{
		return freelist->Pop();
	}
	// ��������Ϊ�յ�Ҫȥ���Ļ�������ȡ�ڴ����һ��ȡ�����ֹ���ȥȡ�����������Ŀ��� 
	// �������:ÿ�����Ķѷ����ThreadCache����ĸ����Ǹ�����������
	//          ����ȡ�Ĵ������Ӷ��ڴ�����������,��ֹһ�θ������̷߳���̫�࣬����һЩ�߳�����
	//          �ڴ�����ʱ�����ȥPageCacheȥȡ������Ч������
	else 
	{
		return FetchFromCentralCache(index, byte);
	}
}

// ���ڴ���󻹸�threadCache�ж�Ӧ����������
void ThreadCache::Deallocate(void* ptr, size_t byte)
{
	assert(byte < MAXBYTES);
	size_t index = ClassSize::Index(byte);
	
	FreeList* freelist = &_freelist[index];

	freelist->Push(ptr);

	// ��Դ�������
	// �������������������һ�δ�CentralCache������ڴ�������ʱ
	// ��ʼ�����ڴ�鵽���Ļ���
	if (freelist->Size() >= freelist->MaxSize())
	{
		ListTooLong(freelist, byte);
	}
}

void ThreadCache::ListTooLong(FreeList* freelist, size_t byte)
{
	void* start = freelist->Clear();

	// ��start��ʼ���ڴ�黹�����Ļ���
	CentralCache::GetInstance()->ReleaseListToSpans(start, byte);
}



