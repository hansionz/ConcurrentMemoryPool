#pragma once
#include <iostream>
#include <assert.h>
#include <thread>
#include <mutex>
#include <vector>
#include <unordered_map>


#ifdef _WIN32
#include <windows.h>
#endif // _WIN32


//管理自由链表数组的长度
const size_t NLISTS = 240;
//最大可以一次分配多大的内存64K
const size_t MAXBYTES = 64 * 1024;
//对于一页是4096byte，12就是2的次方
const size_t PAGE_SHIFT = 12;
//对于PageCache的最大可以存放NPAGES页
const size_t NPAGES = 129;

static inline void* SystemAlloc(size_t npage)
{
#ifdef _WIN32
	//到这里也就是，PageCache里面也没有大于申请的npage的页，要去系统申请内存
	//对于从系统申请内存，一次申请128页的内存，这样的话，提高效率，一次申请够不需要频繁申请
	void* ptr = VirtualAlloc(NULL, (NPAGES - 1) << PAGE_SHIFT, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (ptr == nullptr)
	{
		throw std::bad_alloc();
	}

	return ptr;
#else 
#endif //_WIN32
}

static inline void SystemFree(void* ptr)
{
#ifdef _WIN32
	//到这里也就是，PageCache里面也没有大于申请的npage的页，要去系统申请内存
	//对于从系统申请内存，一次申请128页的内存，这样的话，提高效率，一次申请够不需要频繁申请
	VirtualFree(ptr, 0, MEM_RELEASE);
	if (ptr == nullptr)
	{
		throw std::bad_alloc();
	}
#else 
#endif //_WIN32
}

static inline void*& NEXT_OBJ(void* obj)
{
	return *((void**)obj);
}

//自由链表
class FreeList
{
public:
	bool Empty()
	{
		return _list == nullptr;
	}

	void PushRange(void* start, void* end, size_t num)
	{
		//_list->start->end->_list
		//将这一段内存添加到自由链表当中，并且对于该段自由链表的内存块数量进行增加
		NEXT_OBJ(end) = _list;
		_list = start;
		_size += num;
	}

	//头删
	void* Pop()
	{
		void* obj = _list;
		_list = NEXT_OBJ(obj);
		_size--;

		return obj;
	}

	//头插
	void Push(void* ptr)
	{
		NEXT_OBJ(ptr) = _list;
		_list = ptr;
		_size++;
	}

	size_t MaxSize()
	{
		return _maxsize;
	}

	void SetMaxSize(size_t num)
	{
		_maxsize = num;
	}

	size_t Size()
	{
		return _size;
	}

	void* Clear()
	{
		_size = 0;
		void* list = _list;
		_list = nullptr;

		return list;
	}

private:
	void* _list = nullptr;//形成一个自由链表
	size_t _size = 0;     //有多少个内存结点
	size_t _maxsize = 1;  //最多有多少个内存结点，作用：水位线，自由链表当中现在有多少内存块
};

//对于span是为了对于thread cache还回来的内存进行管理，
//一个span中包含了内存块
typedef size_t PageID;
struct Span
{
	PageID _pageid = 0; //页号
	size_t _npage = 0; //页的数量
	Span* _next = nullptr;
	Span* _prev = nullptr;

	void* _objlist = nullptr; //对象自由链表
	size_t _objsize = 0;	//记录该span上的内存块的大小,作用：用来对于使用的时候计算，内存块的大小
	size_t _usecount = 0; //使用计数，计算使用了多少内存块
};

//跨度链表
class SpanList
{
public:
	//双向循环带头结点链表
	SpanList()
	{
		_head = new Span;
		_head->_next = _head;
		_head->_prev = _head;
	}

	Span* begin()
	{
		return _head->_next;
	}
	
	Span* end()
	{
		return _head;
	}

	bool Empty()
	{
		return _head == _head->_next;
	}

	void Insert(Span* cur, Span* newspan)
	{
		assert(cur);
		Span* prev = cur->_prev;
		
		//prev newspan cur
		prev->_next = newspan;
		newspan->_prev = prev;
		newspan->_next = cur;
		cur->_prev = newspan;
	}

	void Erase(Span* cur)
	{
		assert(cur != nullptr && cur != _head);

		Span* prev = cur->_prev;
		Span* next = cur->_next;

		prev->_next = next;
		next->_prev = prev;
	}

	void PushBack(Span* cur)
	{
		Insert(end(), cur);
	}

	void PopBack()
	{
		Span* span = end();
		Erase(span);
	}

	void PushFront(Span* cur)
	{
		Insert(begin(), cur);
	}

	Span* PopFront()
	{
		//必须要使用sapn来接收一下开始，因为删除之后再次使用begin()返回的话，就会返回刚才删除的下一个
		//会出错，不是删除的那个一个了
		Span* span = begin();
		Erase(span);

		return span;
	}

	//为了给每一个桶加锁
	std::mutex _mtx;
private:
	Span * _head = nullptr;
};

class ClassSize
{
public:
	// align是对齐数
	static inline size_t _RoundUp(size_t size, size_t align)
	{
		// 比如size是15 < 128,对齐数align是8，那么要进行向上取整，
		// ((15 + 7) / 8) * 8就可以了
		// 这个式子就是将(align - 1)加上去
		// 然后再将加上去的二进制的低三位设置为0
		// 15 + 7 = 22 : 10110 
		// 7 : 111 ~7 : 000
		// 22 & ~7 : 10000 (16)就达到了向上取整的效果
		return (size + align - 1) & ~(align - 1);
	}

	//向上取整
	static inline size_t RoundUp(size_t size)
	{
		assert(size <= MAXBYTES);
		
		if (size <= 128)
		{
			return _RoundUp(size, 8);
		}
		if (size <= 8 * 128)
		{
			return _RoundUp(size, 16);
		}
		if (size <= 8 * 1024)
		{
			return _RoundUp(size, 128);
		}
		if (size <= 64 * 1024)
		{
			return _RoundUp(size, 512);
		}
		else
		{
			return -1;
		}
	}

	//控制内碎片在12%左右的浪费
	//[1, 128]						8byte对齐		freelist[0,16)
	//[129, 1024]					16byte对齐		freelist[17, 72)
	//[1025, 8 * 1024]				64byte对齐		freelist[72, 128)
	//[8 * 1024 + 1, 64 * 1024]		512byte对齐		freelist[128, 240)
	//也就是说对于自由链表数组只需要开辟240个空间就可以了

	//求出在该区间的第几个
	static size_t _Index(size_t bytes, size_t align_shift)
	{
		//对于(1 << align_sjift)相当于求出对齐数
		//给bytes加上对齐数减一也就是，让其可以跨越到下一个自由链表的数组的元素中
		return ((bytes + (1 << align_shift) - 1) >> align_shift) - 1;
	}

	//获取自由链表的下标
	static inline size_t Index(size_t bytes)
	{
		//开辟的字节数，必须小于可以开辟的最大的字节数
		assert(bytes < MAXBYTES);

		//每个对齐区间中，有着多少条自由链表
		static int group_array[4] = { 16, 56, 56, 112 };

		if (bytes <= 128)
		{
			return _Index(bytes, 3);
		}
		else if (bytes <= 1024) //(8 * 128)
		{
			return _Index(bytes - 128, 4) + group_array[0];
		}
		else if (bytes <= 4096) //(8 * 8 * 128)
		{
			return _Index(bytes - 1024, 7) + group_array[1] + group_array[0];
		}
		else if (bytes <= 8 * 128)
		{
			return _Index(bytes - 4096, 9) + group_array[2] + group_array[1] + group_array[0];
		}
		else
		{
			return -1;
		}
	}

	//对于不同的byte获取不一样数量的内存
	static inline size_t NumMoveSize(size_t size)
	{
		if (size == 0)
		{
			return 0;
		}

		int num = (int)(MAXBYTES / size);
		//当申请的size是64K的时候，就一次申请64K * 2
		if (num < 2)
		{
			num = 2;
		}
		//当申请的size是8byte的时候，就一次申请512 * 8byte
		if (num >= 512)
		{
			num = 512;
		}

		return num;
	}

	//计算要获取几页
	static inline size_t NumMovePage(size_t size)
	{
		size_t num = NumMoveSize(size);
		size_t npage = (num * size) >> PAGE_SHIFT;

		//如果申请的内存是0byte的话，计算下来就会申请0页
		//我们对其进行处理当申请的内存是0byte的时候，我们就申请一页的内存
		if (npage == 0)
		{
			npage = 1;
		}

		return npage;
	}

};