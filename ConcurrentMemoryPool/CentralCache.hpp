/*
	������Դ�ľ��⣬����ThreadCache��ĳ����Դ��ʣ��ʱ�򣬿��Ի���ThreadCache�ڲ��ĵ��ڴ�
	�Ӷ����Է����������ThreadCache
	ֻ��һ�����Ļ��棬�������е��߳�����ȡ�ڴ��ʱ��Ӧ����һ�����Ļ���
	���Զ������Ļ������ʹ�õ���ģʽ�����д������Ļ������
	�������Ļ�����˵Ҫ����
*/
#pragma once 

#include "Common.hpp"

class CentralCache
{
public:
	static CentralCache* GetInstance()
	{
		return &_inst;
	}

	//�����Ļ����ȡһ���������ڴ��thread cache
	size_t FetchRangeObj(void*& start, void*& end, size_t num, size_t byte);

	// ��span�����������ó���bytes��ȵ�span����
	Span* GetOneSpan(SpanList* spanlist, size_t bytes);

	//��ThreadCache�е��ڴ��黹��CentralCache
	void ReleaseListToSpans(void* start, size_t byte);

private:
	SpanList _spanlist[NLISTS];//���Ļ����span��������飬Ĭ�ϴ�С��	NLISTS : 240

private:
	//���캯��Ĭ�ϻ���Ҳ�����޲�������
	CentralCache() = default;
	CentralCache(const CentralCache&)  = delete;
	CentralCache& operator=(const CentralCache&)  = delete;

	//����һ������
	static CentralCache _inst;
};