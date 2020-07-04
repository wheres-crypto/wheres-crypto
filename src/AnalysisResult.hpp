#pragma once
#include "types.hpp"
#include "Broker.hpp"

#define THREAD_RESULT_TYPE_EVALUATION_RESULT 0x690798c9

typedef enum {
	EVALUATION_RESULT_MATCH_FOUND,
	EVALUATION_RESULT_NO_MATCH_FOUND
} evaluation_status_t;

class AbstractEvaluationResultImpl : virtual public ReferenceCounted, public ThreadTaskResultImpl {
public:
	inline AbstractEvaluationResultImpl(AbstractEvaluator oEvaluator, Broker oCodeGraph, evaluation_status_t eStatus)
	  : oEvaluator(oEvaluator), oCodeGraph(oCodeGraph), eStatus(eStatus), ThreadTaskResultImpl(THREAD_RESULT_TYPE_EVALUATION_RESULT) { }
	inline ~AbstractEvaluationResultImpl() { }
	inline AbstractEvaluationResult toAbstract() { return AbstractEvaluationResult::typecast(this); }

	virtual bool Mark(DFGNode &oCodeNode) = 0;
	virtual std::string Label() = 0;

	AbstractEvaluator oEvaluator;
	Broker oCodeGraph;
	evaluation_status_t eStatus;
};

class AbstractEvaluatorImpl : virtual public ReferenceCounted, public ThreadTaskImpl {
public:
	inline AbstractEvaluatorImpl() { }
	inline ~AbstractEvaluatorImpl() { }

	virtual bool Evaluate(AbstractEvaluationResult *lpOutput) = 0;
	inline unsigned long Execute(void* lpPrivate) {
		AbstractEvaluationResult oResult;
		bool bEvaluationResult = Evaluate(&oResult);
		oThreadPool->YieldResult(oResult->toGenericResult());
		return 0;
	}
	inline AbstractEvaluator toAbstract() { return AbstractEvaluator::typecast(this); }
	template<class T, typename... Args> static inline AbstractEvaluator ScheduleEvaluate(
		ThreadPool oThreadPool,
		Broker oCodeGraph,
		int dwMaxEvaluationTime,
		Args&&... args
	) {
		T oEvaluator(T::create(std::forward<Args>(args)...));
		oEvaluator->oThreadPool = oThreadPool;
		oEvaluator->oCodeGraph = oCodeGraph;
		oEvaluator->dwMaxEvaluationTime = dwMaxEvaluationTime;
		oThreadPool->Schedule(oEvaluator->toThreadTask(), NULL);
		return oEvaluator->toAbstract();
	}

	ThreadPool oThreadPool;
	Broker oCodeGraph;
	int dwMaxEvaluationTime;
};

class AnalysisResultImpl
	: virtual public ReferenceCounted, public std::unordered_map<AbstractEvaluator, AbstractEvaluationResult> {
public:
	AnalysisResultImpl(Broker &oCodeGraph) : oCodeGraph(oCodeGraph) { }
	inline void SetNull(AbstractEvaluator oEvaluator) {
		iterator it = find(oEvaluator);
		if (it == end()) {
			insert(std::pair<AbstractEvaluator, AbstractEvaluationResult>(oEvaluator, nullptr));
		} else {
			it->second = nullptr;
		}
	}
	inline void SetResult(AbstractEvaluationResult oResult) {
		iterator it = find(oResult->oEvaluator);
		if (it == end()) {
			insert(std::pair<AbstractEvaluator, AbstractEvaluationResult>(oResult->oEvaluator, oResult));
		} else {
			it->second = oResult;
		}
	}
	inline bool AllResultsSet() {
		iterator it;
		for (it = begin(); it != end(); it++) {
			if (it->second == nullptr) {
				return false;
			}
		}
		return true;
	}

	Broker oCodeGraph;
};
