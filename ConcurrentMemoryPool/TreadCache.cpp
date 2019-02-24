#include "ThreadCache.hpp"
#include "CentralCache.hpp"

// 从中心缓存拿取数据
// 每一次取批量的数据，因为每次到CentralCache申请内存的时候是需要加锁的
// 所以一次就多申请一些内存块，防止每次到CentralCache去内存块的时候,多次加锁造成效率问题
void* ThreadCache::FetchFromCentralCache(size_t index, size_t byte)
{
	FreeList* freelist = &_freelist[index];

	// 不是每次申请10个，而是进行慢增长的过程
	// 单个对象越小，申请内存块的数量越多
	// 单个对象越大，申请内存块的数量越小
	// 申请次数越多，数量多
	// 次数少,数量少
	size_t num_to_move = min(ClassSize::NumMoveSize(byte), freelist->MaxSize());

	// start，end分别表示取出来的内存的开始地址和结束地址
	// 取出来的内存是一个链在一起的内存对象，需要守卫标识
	void* start, *end;

	// fetchnum表示实际取出来的内存的个数
	// fetchnum有可能小于num，表示中心缓存没有那么多大小的内存块
	size_t fetchnum = CentralCache::GetInstance()->FetchRangeObj(start, end, num_to_move, byte);
	if (fetchnum > 1)
	{
		freelist->PushRange(NEXT_OBJ(start), end, fetchnum - 1);
	}
	// 下一次申请相同位置的，申请数量加1
	if (num_to_move == freelist->MaxSize())
	{
		freelist->SetMaxSize(num_to_move + 1);
	}
	return start;
}

// 从自由链表数组的自由链表上拿取内存对象
void* ThreadCache::Allocate(size_t byte)
{
	assert(byte < MAXBYTES);
	// 对齐
	byte = ClassSize::RoundUp(byte);
	// 计算位置
	size_t index = ClassSize::Index(byte);
	FreeList* freelist = &_freelist[index];

	if (!freelist->Empty())
	{
		return freelist->Pop();
	}
	// 自由链表为空的要去中心缓存中拿取内存对象，一次取多个防止多次去取而加锁带来的开销 
	// 均衡策略:每次中心堆分配给ThreadCache对象的个数是个慢启动策略
	//          随着取的次数增加而内存对象个数增加,防止一次给其他线程分配太多，而另一些线程申请
	//          内存对象的时候必须去PageCache去取，带来效率问题
	else 
	{
		return FetchFromCentralCache(index, byte);
	}
}

// 将空闲的内存还给threadCache中对应的自由链表
void ThreadCache::Deallocate(void* ptr, size_t byte)
{
	assert(byte < MAXBYTES);
	size_t index = ClassSize::Index(byte);
	
	FreeList* freelist = &_freelist[index];

	freelist->Push(ptr);

	// 当自由链表的数量超过一次从Central cache申请的内存块的数量时
	// 开始回收内存块到中心缓存
	// 资源均衡策略
	if (freelist->Size() >= freelist->MaxSize())
	{
		ListTooLong(freelist, byte);
	}
}

void ThreadCache::ListTooLong(FreeList* freelist, size_t byte)
{
	void* start = freelist->Clear();

	// 从start开始的内存归还给中心缓存
	CentralCache::GetInstance()->ReleaseListToSpans(start, byte);
}



