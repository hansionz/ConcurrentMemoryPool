#include "Common.hpp"
#include "ConcurrentAlloc.hpp"
#include <vector>

#define MAXSIZE 100000
/*
	tls(thread local storage)线程本地存储
*/
void TestThreadCache()
{
}
	//测试不同线程对于tls的效果，也就是对于tls每一个线程都有着自己一个
	//std::thread tl(ConcurrentAlloc, 10);
	//std::thread t2(ConcurrentAlloc, 10);
	//tl.join();
	//t2.join();

	//测试申请内存
	//ConcurrentAlloc(10);

	//size_t n = 10;
	//size_t byte = 6;
	//std::vector<void*> v;
	//for (size_t i = 0; i < n; ++i)
	//{
	//	v.push_back(ConcurrentAlloc(byte));
	//	std::cout << v.back() << std::endl;
	//}
	//std::cout << std::endl << std::endl;

	///*for (size_t i = 0; i < n; ++i)
	//{
	//	ConcurrentFree(v[i], 10);
	//}
	//v.clear();*/

	//for (int i = n - 1; i >= 0; --i)
	//{
	//	ConcurrentFree(v[i]);
	//}
	//v.clear();

	//for (size_t i = 0; i < n; ++i)
	//{
	//	v.push_back(ConcurrentAlloc(byte));
	//	std::cout << v.back() << std::endl;
	//}

	//for (size_t i = 0; i < n; ++i)
	//{
	//	ConcurrentFree(v[i]);


	//}
	//v.clear();
	/*void* ptr = ConcurrentAlloc(128 * 4096 - 500);
	std::cout << ptr << std::endl;
	ConcurrentFree(ptr);*/
	// 单轮次申请释放次数 线程数 轮次
void BenchmarkConcurrentMalloc(size_t ntimes, size_t nworks, size_t rounds)
{
	std::vector<std::thread> vthread(nworks);
	size_t malloc_costtime = 0;
	size_t free_costtime = 0;
	for (size_t k = 0; k < nworks; ++k)
	{
		vthread[k] = std::thread([&]() {
			std::vector<void*> v;
			v.reserve(ntimes);
			for (size_t j = 0; j < rounds; ++j)
			{
				size_t begin1 = clock();
				for (size_t i = 0; i < ntimes; i++)
				{
					v.push_back(ConcurrentAlloc(MAXSIZE));
				}
				size_t end1 = clock();
				size_t begin2 = clock();
				for (size_t i = 0; i < ntimes; i++)
				{
					ConcurrentFree(v[i]);
				}
				size_t end2 = clock();
				v.clear();

				malloc_costtime += end1 - begin1;
				free_costtime += end2 - begin2;
			}
		});
	}
	for (auto& t : vthread)
	{
		t.join();
	}
	printf("%u个线程并发执行%u轮次，每轮次concurrent alloc %u次: 花费：%u ms\n",nworks, rounds, ntimes, malloc_costtime);
	printf("%u个线程并发执行%u轮次，每轮次concurrent dealloc %u次: 花费：%u ms\n",nworks, rounds, ntimes, free_costtime);
	printf("%u个线程并发concurrent alloc&dealloc %u次，总计花费：%u ms\n", nworks, nworks*rounds*ntimes, malloc_costtime + free_costtime);
}

void BenchmarkMalloc(size_t ntimes, size_t nworks, size_t rounds)
{
	std::vector<std::thread> vthread(nworks);
	size_t malloc_costtime = 0;
	size_t free_costtime = 0;
	for (size_t k = 0; k < nworks; ++k)
	{
		vthread[k] = std::thread([&]() {
			std::vector<void*> v;
			v.reserve(ntimes);
			for (size_t j = 0; j < rounds; ++j)
			{
				size_t begin1 = clock();
				for (size_t i = 0; i < ntimes; i++)
				{
					v.push_back(malloc(MAXSIZE));
				}
				size_t end1 = clock();
				size_t begin2 = clock();
				for (size_t i = 0; i < ntimes; i++)
				{
					free(v[i]);
				}
				size_t end2 = clock();
				v.clear();

				malloc_costtime += end1 - begin1;
				free_costtime += end2 - begin2;
			}
		});
	}
	for (auto& t : vthread)
	{
		t.join();
	}
	printf("%u个线程并发执行%u轮次，每轮次malloc %u次: 花费：%u ms\n", nworks, rounds, ntimes, malloc_costtime);
	printf("%u个线程并发执行%u轮次，每轮次free %u次: 花费：%u ms\n", nworks, rounds, ntimes, free_costtime);
	printf("%u个线程并发concurrent malloc&free %u次，总计花费：%u ms\n", nworks, nworks*rounds*ntimes, malloc_costtime + free_costtime);
}

int main()
{
	BenchmarkMalloc(100, 10, 10);
	std::cout << "===========================================================================" << std::endl;
	std::cout << "===========================================================================" << std::endl;
	BenchmarkConcurrentMalloc(100, 10, 10);
	
	return 0;
}