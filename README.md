# 高并发内存池

### 一.模快划分

* 线程缓存(ThreadCache)
* 中心缓存(CentralCache)
* 页缓存(PageCache)

### 二.项目考虑的问题

* 高并发下的线程安全问题(通过ThreadCache解决)
* 效率问题(通过CentralCache中的均衡技术解决)
* 内存碎片问题(通过PageCache中的合并Span解决)

### 三.模快功能简介

* 线程缓存：每个线程获取自己的线程本地存储变量(TLS)后，当这个线程去申请小于64k的内存的时候，ThreadCache对象从相应的自由链表上取下给定数目的内存 对象返给用户。这里不需要加锁，通过线程本地存储，每个线程都拥有自己唯一的一个ThreadCache对象来维护自由链表。这是本项目中高效的一个地方
* 中心缓存：中心缓存是所有线程所共享的，ThreadCache是按需要从CentralCache中获取内存对象。 CentralCache条件性的回收ThreadCache中的内存对象，避免一个线程占用了太多的内存对象，而其他线程的内存对象不足，带来效率问题。本级缓存可以达到内存分配在多个线程中更均衡的按需调度的目的。CentralCache是存在竞争的，所以从这里取内存对象是需要加锁
* 页缓存是在CentralCache缓存上面加的一层缓存，存储的内存是以页为单位存储及分配的，CentralCache没有Span对象时，从PageCache分配出一定数量的page，并切割成定长大小的小块内存，分配给CentralCache。PageCache会回收CentralCache满足条件的span对象，并且合并相邻的页，组成更大的页，缓解内存碎片的问题 

### 四.模快接口

* 线程缓存：包括一个自由链表数组。ThreadCache提供的接口主要有:
  * Allocate：从ThreadCache分配内存
  * Deallocate：释放内存给ThreadCache
  * FetchFromCentralCache：向中心缓存区获取内存对象
  * ListTooLong：当自由链表中的对象个数超过一定数量，然后将内存对象归还给CentralCache,为了实现均衡技术，防止其他线程要获取该大小的内存对象还需要去PageCache中去获取，从而带来效率问题
* 中心缓存：包括一个Span对象的双向带头循环链表数组，每一个Span对象由多页(一页4k)组成，Span对象中也包含了一条自由链表。CentralCache提供的主要接口有：
  * GetInstance：为了保证全局只有唯一的一个CentralCache,本模快采用单例模式(饿汉模式)实现，此接口用来获取单例对象
  * FetchRangeObj：获取一定数量的内存对象给ThreadCache
  * GetOneSpan：从对应位置上获取一个Span对象，如果对象位置没有Span对象，则去向PageCache去申请Span对象，该接口主要是给FetchRangeObj使用
  * ReleaseListToSpans：将ThreadCache中过长的自由链表中的内存对象重新挂到对应的Span中，主要在ThreadCache中的ListTooLong中被调用，为了均衡个县城之间相同大小的内存对象数量
* 页缓存：包含一个由Span对象构成的SpanList数组，大小为128，如果申请的内存超过128页，则直接去向系统去申请。PageCache提供的接口主要有：
  * GetInstance：为了保证全局只有唯一的一个PageCache对象，本模快使用单例模式(饿汉模式)实现，此接口用来获取单例对象
  * NewSpan：如果用户申请超过128页的内存，则直接去向系统申请，如果申请的内存是64k-128页的内存，ThreadCache直接调用该接口获取到内存对象，如果申请的内存小于64k，首先去ThreadCache申请，如果没有则逐级向上申请，该接口主要用在CentralCache中的GetOneSpan中和申请内存范围在64k-128页中
  * MapObjectToSpan：当内存对象从ThreadCache归还给CentralCache时，每一个内存对象都需要知道自由是属于哪一个Span对象的，所以在PageCache合并Span或者是从系统申请出来的Span，都要建立一个页号到Span的映射，在归还的时候，可以通过查找这个映射关系来确定是在哪一个Span中，本接口就是为了查找映射关系
  * RelaseToPageCache：每一个Span对象中都存在着一个使用计数，当这个使用计数为0时说明该Span中的所有内存对象都是空闲的。此时，可以将这个Span对象归还给PageCache中进行合并来减少内存碎片。本接口主要在CentralCache中的ReleaseListToSpans被调用，用来归还Span对象和合并相邻空闲的Span来减少内存碎片

**注：本项目详细介绍位于我的CSDN博客，博客链接在下边。**

**https://blog.csdn.net/hansionz/article/details/87885229**

