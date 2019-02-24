#include "Common.hpp"

//����Page CacheҲҪ����Ϊ����������Central Cache��ȡspan��ʱ��
//ÿ�ζ��Ǵ�ͬһ��page�����л�ȡspan

class PageCache
{
public:
	static PageCache* GetInstance()
	{
		return &_inst;
	}

	//��ϵͳ����span���ߴ���Ҫ�����npage��Pagespan������
	Span* NewSpan(size_t npage);

	Span* _NewSpan(size_t npage);

	// ��ȡ�Ӷ���span��ӳ��
	Span* MapObjectToSpan(void* obj);

	//��CentralCache�黹span��Page
	void RelaseToPageCache(Span* span);

private:
	//NPAGES��129������ʹ��128������Ԫ�أ�Ҳ�����±��1��ʼ��128�ֱ�Ϊ1ҳ��128ҳ
	SpanList _pagelist[NPAGES];

private:
	PageCache() = default;
	PageCache(const PageCache&) = delete;
	PageCache& operator=(const PageCache&) = delete;
	static PageCache _inst;

	std::mutex _mtx;
	std::unordered_map<PageID, Span*> _id_span_map;
};