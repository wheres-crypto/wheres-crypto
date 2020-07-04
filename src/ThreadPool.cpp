#include "ThreadPool.hpp"

std::mutex ThreadPoolImpl::stApiMutex;

ThreadTask ThreadTaskImpl::FromFunctionPointer(unsigned long(*lpFunction)(void *)) {
	return ThreadTask::typecast(rfc_ptr<ThreadTaskLambda>::create(lpFunction));
}

ThreadPoolImpl::ThreadPoolImpl(int dwNumThreads): dwNumThreads(dwNumThreads), dwNumActive(0) {
	if (this->dwNumThreads == 0) {
		this->dwNumThreads = std::thread::hardware_concurrency();
		if (this->dwNumThreads == 0) {
			this->dwNumThreads = 1;
		}
	}
}

void ThreadPoolImpl::Schedule(ThreadTask &oTask, void *lpPrivate) {
	std::unique_lock<std::mutex> mLock(stMutex);

	InitThreads();

	aTasks.insert(aTasks.end(), task_t(TASK_SPECIAL_NORMAL, oTask, lpPrivate));
	stCondition.notify_one();
}

bool ThreadPoolImpl::ScheduleIfResourceAvailable(ThreadTask & oTask, void* lpPrivate) {
	std::unique_lock<std::mutex> mLock(stMutex);

	InitThreads();

	if (aTasks.size() < dwNumThreads) {
		aTasks.insert(aTasks.end(), task_t(TASK_SPECIAL_NORMAL, oTask, lpPrivate));
		return true;
	}

	return false;
}

void ThreadPoolImpl::Synchronize() {
	std::unique_lock<std::mutex> mLock(stMutex);
	dwNumSynchronized = 0;
	aTasks.insert(aTasks.end(), task_t(TASK_SPECIAL_SYNCHRONIZE, ThreadTask(), NULL));
	stCondition.notify_all();

	while (dwNumSynchronized != dwNumThreads) {
		stSynchronize.wait(mLock);
	}
}

void ThreadPoolImpl::YieldResult(ThreadTaskResult &oResult) {
	std::unique_lock<std::mutex> mLock(stMutex);
	aResults.push_back(oResult);

	stYield.notify_all();
}

ThreadTaskResult ThreadPoolImpl::WaitForResult() {
	std::unique_lock<std::mutex> mLock(stMutex);

	while (
		!( /* continue down below if: */
			aResults.begin() != aResults.end() || ( // a result is yielded, or:
				dwNumActive == 0 && // all threads are idle, and
				aTasks.begin() == aTasks.end() // no tasks are currently scheduled
			)
		)
	) {
		stYield.wait(mLock);
	}

	if (aResults.begin() != aResults.end()) {
		ThreadTaskResult oResult = *aResults.begin();
		aResults.pop_front();
		return oResult;
	} else {
		return nullptr;
	}
}

ThreadPoolImpl::~ThreadPoolImpl() {
	{
		std::unique_lock<std::mutex> mLock(stMutex);
		aTasks.insert(aTasks.end(), task_t(TASK_SPECIAL_EXIT, ThreadTask(), NULL));
		stCondition.notify_all();
	}

	std::list<std::thread>::iterator it;
	for (it = aThreads.begin(); it != aThreads.end(); it++) {
		it->join();
	}
}

void ThreadPoolImpl::Worker() {
	for (;;) {
		task_t stTask;
		{
			std::unique_lock<std::mutex> mLock(stMutex);
			while (aTasks.begin() == aTasks.end()) {
				dwNumActive--;
				if (dwNumActive == 0) {
					stYield.notify_all();
				}
				stCondition.wait(mLock);
				dwNumActive++;
			}
			stTask = *aTasks.begin();

			if (stTask.eSpecial == TASK_SPECIAL_SYNCHRONIZE) {
				dwNumSynchronized++;

				if (dwNumSynchronized == dwNumThreads) {
					/* we're the last thread that hits this code -> notify others and clean */
					stSynchronize.notify_all();
					aTasks.pop_front();
				}
				while(dwNumSynchronized != dwNumThreads) {
					stSynchronize.wait(mLock);
				}
				continue;
			} else if (stTask.eSpecial == TASK_SPECIAL_EXIT) {
				break;
			} else {
				aTasks.pop_front();
				/* fall through */
			}
		}

		stTask.oTask->Execute(stTask.lpPrivate);
	}
}

void ThreadPoolImpl::InitThreads() {
	if (aThreads.begin() == aThreads.end()) {
		int i;
		dwNumActive = dwNumThreads;
		for (i = 0; i < dwNumThreads; i++) {
			aThreads.insert(aThreads.end(), std::thread(&ThreadPoolImpl::Worker, this));
		}
	}
}

inline void *ThreadTaskResultImpl::toVoidPointer() {
	return ((ThreadTaskResultVoidPointerImpl *)this)->lpResult;
}

ThreadTaskResult ThreadTaskResultImpl::FromVoidPointer(void *lpResult) {
	return ThreadTaskResult::typecast(rfc_ptr<ThreadTaskResultVoidPointerImpl>::create(lpResult));
}