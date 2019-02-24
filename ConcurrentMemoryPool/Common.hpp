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

const size_t NLISTS = 240;   //����������������ĳ���,���ݶ��������������

const size_t MAXBYTES = 64 * 1024; //ThreadCache������һ�η�������ڴ�64K

const size_t PAGE_SHIFT = 12;//һҳ��4096�ֽ�,2��12�η�=4096

const size_t NPAGES = 129;   //PageCache�������Դ��NPAGES-1ҳ

// ��ϵͳ�����ڴ�
static inline void* SystemAlloc(size_t npage)
{
#ifdef _WIN32
	void* ptr = VirtualAlloc(NULL, (npage) << PAGE_SHIFT, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

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
	VirtualFree(ptr, 0, MEM_RELEASE);
	if (ptr == nullptr)
	{
		throw std::bad_alloc();
	}
#else 
#endif //_WIN32
}

// ȡ����ָ���ǰ4����ǰ8���ֽ�
static inline void*& NEXT_OBJ(void* obj)
{
	return *((void**)obj);
}

// ����������
class FreeList
{
public:
	bool Empty()
	{
		return _list == nullptr;
	}

	// ��һ���ڴ������ӵ�����������
	void PushRange(void* start, void* end, size_t num)
	{
		//_list->start->end->_list
		NEXT_OBJ(end) = _list;
		_list = start;
		_size += num;
	}

	//ͷɾ
	void* Pop()
	{
		void* obj = _list;
		_list = NEXT_OBJ(obj);
		_size--;

		return obj;
	}

	//ͷ��
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
	void* _list = nullptr;// һ����������
	size_t _size = 0;     // �ڴ������
	size_t _maxsize = 1;  // ����ж��ٸ��ڴ��� ����:ˮλ��
};

// ʹ��span���ݽṹ�����ڴ�
// һ��span������ҳ
typedef size_t PageID;
struct Span
{
	PageID _pageid = 0;  //ҳ��
	size_t _npage = 0;   //ҳ������

	// ά��һ��span�����˫���ͷѭ������
	Span* _next = nullptr;
	Span* _prev = nullptr;

	void* _objlist = nullptr; // �ڴ������������
	size_t _objsize = 0;	  // ��¼��span�ϵ��ڴ��Ĵ�С
	size_t _usecount = 0;     // ʹ�ü���
};

// ���������
class SpanList
{
public:
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
		Span* span = begin();
		Erase(span);

		return span;
	}

	// Ϊ�˸�ÿһ��Ͱ����
	std::mutex _mtx;
private:
	Span * _head = nullptr;
};

// ��С��
class ClassSize
{
public:
	// align�Ƕ�����
	static inline size_t _RoundUp(size_t size, size_t align)
	{
		// ����size��15 < 128,������align��8����ôҪ��������ȡ����
		// ((15 + 7) / 8) * 8�Ϳ�����
		// ���ʽ�Ӿ��ǽ�(align - 1)����ȥ
		// Ȼ���ٽ�����ȥ�Ķ����Ƶĵ���λ����Ϊ0
		// 15 + 7 = 22 : 10110 
		// 7 : 111 ~7 : 000
		// 22 & ~7 : 10000 (16)�ʹﵽ������ȡ����Ч��
		return (size + align - 1) & ~(align - 1);
	}
	// ����ȡ��
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

	//��������Ƭ��12%���ҵ��˷�
	//[1, 128]						8byte����		freelist[0,16)
	//[129, 1024]					16byte����		freelist[17, 72)
	//[1025, 8 * 1024]				64byte����		freelist[72, 128)
	//[8 * 1024 + 1, 64 * 1024]		512byte����		freelist[128, 240)
	//Ҳ����˵������������ֻ��Ҫ����240���ռ�Ϳ�����

	//����ڸ�����ĵڼ���
	static size_t _Index(size_t bytes, size_t align_shift)
	{
		// ��bytes���϶�������һҲ����
		// ������Կ�Խ����һ����������������Ԫ����
		return ((bytes + (1 << align_shift) - 1) >> align_shift) - 1;
	}

	//��ȡ����������±�
	static inline size_t Index(size_t bytes)
	{
		assert(bytes < MAXBYTES);

		// ��¼ÿ���������������Ŷ�������������
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

	// ����һ�δ����Ļ������ƶ����ٸ��ڴ����ThreadCache��
	static inline size_t NumMoveSize(size_t size)
	{
		if (size == 0)
		{
			return 0;
		}

		int num = (int)(MAXBYTES / size);

		if (num < 2)
		{
			num = 2;
		}
		if (num >= 512)
		{
			num = 512;
		}

		return num;
	}

	// ����size�������Ļ���Ҫ��ҳ������ȡ����span����
	static inline size_t NumMovePage(size_t size)
	{
		size_t num = NumMoveSize(size);
		size_t npage = (num * size) >> PAGE_SHIFT;

		if (npage == 0)
		{
			npage = 1;
		}

		return npage;
	}

};