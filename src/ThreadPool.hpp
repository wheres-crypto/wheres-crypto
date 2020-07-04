#pragma once

#include <list>
#include <thread>
#include <mutex>

#include "types.hpp"

#define API_LOCK() ThreadPoolImpl::stApiMutex.lock()
#define API_UNLOCK() ThreadPoolImpl::stApiMutex.unlock()

#define THREAD_RESULT_TYPE_VOID 0x210cba4b

typedef enum {
	TASK_SPECIAL_NORMAL = 0,
	TASK_SPECIAL_SYNCHRONIZE,
	TASK_SPECIAL_EXIT
} task_special_t;

class task_t {
public:
	task_special_t eSpecial;
	ThreadTask oTask;
	void *lpPrivate;

	inline task_t(task_special_t eSpecial, ThreadTask &oTask, void *lpPrivate)
		: eSpecial(eSpecial), oTask(oTask), lpPrivate(lpPrivate) { }
	inline task_t() { }
};

class ThreadTaskResultImpl : virtual public ReferenceCounted {
public:
	inline ThreadTaskResultImpl(unsigned int dwType): dwType(dwType) { }
	inline ThreadTaskResult toGenericResult() { return ThreadTaskResult::typecast(this); };
	inline void *toVoidPointer();
	static ThreadTaskResult FromVoidPointer(void *lpResult);

	unsigned int dwType;
};

class ThreadTaskResultVoidPointerImpl : public ThreadTaskResultImpl {
public:
	ThreadTaskResultVoidPointerImpl(void *lpResult)
		: ThreadTaskResultImpl(THREAD_RESULT_TYPE_VOID), lpResult(lpResult) { }
	void *lpResult;
};

class ThreadTaskImpl: virtual public ReferenceCounted {
public:
	inline ThreadTaskImpl() { }
	static ThreadTask FromFunctionPointer(unsigned long(*lpFunction)(void *));
	inline ThreadTask toThreadTask() { return ThreadTask::typecast(this); };

protected:
	virtual unsigned long Execute(void *lpPrivate) = 0;

friend class ThreadPoolImpl;
};

class ThreadTaskLambda : public ThreadTaskImpl {
public:
	ThreadTaskLambda(unsigned long(*lpFunction)(void *)) : lpFunction(lpFunction) { }

protected:
	unsigned long(*lpFunction)(void *);
	unsigned long Execute(void *lpPrivate) { return lpFunction(lpPrivate); }
};

class ThreadPoolImpl: virtual public ReferenceCounted {
public:
	ThreadPoolImpl(int dwNumThreads = 0);
	~ThreadPoolImpl();
	void Schedule(ThreadTask &, void *lpPrivate);
	bool ScheduleIfResourceAvailable(ThreadTask&, void* lpPrivate);
	void Synchronize();
	void YieldResult(ThreadTaskResult &oResult);
	ThreadTaskResult WaitForResult();

	static std::mutex stApiMutex;
	int dwNumThreads;
private:
	void Worker();
	void InitThreads();
	std::list<task_t> aTasks;
	std::list<ThreadTaskResult> aResults;
	std::list<std::thread> aThreads;
	std::mutex stMutex;
	std::condition_variable stCondition;
	std::condition_variable stSynchronize;
	std::condition_variable stYield;
	int dwNumSynchronized;
	int dwNumActive;
};