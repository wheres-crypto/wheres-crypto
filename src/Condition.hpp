#pragma once

#include "types.hpp"

typedef enum {
	OPERATOR_EQ = 0,
	OPERATOR_NEQ,
	OPERATOR_UGE, // unsigned greater or equal
	OPERATOR_ULT, // unsigned less than
	OPERATOR_UGT,
	OPERATOR_ULE,
	OPERATOR_GE,
	OPERATOR_LT,
	OPERATOR_GT,
	OPERATOR_LE
} operator_t;

typedef enum {
	SPECIAL_COND_NORMAL = 0,
	SPECIAL_COND_TRUE,
	SPECIAL_COND_FALSE
} special_cond_t;

class ConditionImpl : virtual public ReferenceCounted {
public:
	special_cond_t eSpecial;
	DFGNode oExpression1;
	operator_t eOperator;
	DFGNode oExpression2;

	std::string expression(int dwMaxDepth = -1) const;
	inline ConditionImpl(bool bValue) : eSpecial(bValue ? SPECIAL_COND_TRUE : SPECIAL_COND_FALSE) { }
	inline ConditionImpl(DFGNode oExpression1, operator_t eOperator, DFGNode oExpression2)
		: eSpecial(SPECIAL_COND_NORMAL), oExpression1(oExpression1), eOperator(eOperator), oExpression2(oExpression2) { }
	ConditionImpl(const ConditionImpl &) = default;
	void Normalize(Broker &oBuilder);
	Condition Negate();

private:
	Condition Migrate(DFGraph oGraph);

friend class PredicateImpl;
friend class CodeBrokerImpl;
};