#pragma once

#include <list>

#include "types.hpp"
#include "Condition.hpp"

typedef enum {
	SATISFIED_SOMETIMES = 0,
	SATISFIED_NEVER,
	SATISFIED_ALWAYS,
} satisfied_t;

typedef enum {
	MERGE_RESULT_INTERNAL_ERROR = 0,
	MERGE_RESULT_NOT_MERGABLE, // no merge possible
	MERGE_RESULT_MERGABLE, // merge possible, lcond narrows down rcond
	MERGE_RESULT_ALWAYS_SATISFIED, // lcond is already satisfied under rcond
	MERGE_RESULT_NEVER_SATISFIED, // lcond cannot be true under rcond
} merge_result_t;

typedef enum {
	MERGE_STATUS_INTERNAL_ERROR = 0,
	MERGE_STATUS_OK
} merge_status_t;

class PredicateImpl : virtual public ReferenceCounted {
public:
	inline PredicateImpl() { }
	PredicateImpl(const PredicateImpl &) = default;
	PredicateImpl(
		DFGNode &oNode1, operator_t eOperator, DFGNode &eNode2,
		Broker &oBuilder
	);
	PredicateImpl(
		Condition &oCondition,
		Broker &oBuilder
	);
	merge_status_t MergeCondition(Condition &oCondition, Broker &oBuilder);
	satisfied_t IsSatisfied(Condition &oCondition, Broker &oBuilder);
	std::string expression(int dwMaxDepth = -1) const;
	inline bool IsEmpty() const { return aConditions.size() == 0; }

private:
	std::list<Condition> aConditions;
	merge_result_t CompareNormalized(Condition *lpMergedOutput, Condition &oCondition1, Condition &oCondition2, Broker *lpBuilder);
	Predicate Migrate(DFGraph oGraph);

friend class CodeBrokerImpl;
};