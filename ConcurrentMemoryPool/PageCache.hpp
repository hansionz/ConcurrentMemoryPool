#include "Common.hpp"

//对于Page Cache也要设置为单例，对于Central Cache获取span的时候
//每次都是从同一个page数组中获取span

class PageCache
{
public:
	static PageCache* GetInstance()
	{
		return &_inst;
	}

	//从系统申请span或者大于要申请的npage的Pagespan中申请
	Span* NewSpan(size_t npage);

	Span* _NewSpan(size_t npage);

	// 获取从对象到span的映射
	Span* MapObjectToSpan(void* obj);

	//从CentralCache归还span到Page
	void RelaseToPageCache(Span* span);

private:
	//NPAGES是129，但是使用128个数据元素，也就是下标从1开始到128分别为1页到128页
	SpanList _pagelist[NPAGES];

private:
	PageCache() = default;
	PageCache(const PageCache&) = delete;
	PageCache& operator=(const PageCache&) = delete;
	static PageCache _inst;

	std::mutex _mtx;
	std::unordered_map<PageID, Span*> _id_span_map;
};