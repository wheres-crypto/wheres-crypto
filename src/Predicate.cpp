#include <sstream>
#include <idp.hpp>

#include "common.hpp"
#include "Predicate.hpp"
#include "Condition.hpp"
#include "DFGNode.hpp"
#include "DFGraph.hpp"
#include "Broker.hpp"

PredicateImpl::PredicateImpl(
	DFGNode & oNode1a, operator_t eOperatora, DFGNode & eNode2a,
	Broker &oBuilder
) {
	MergeCondition(Condition::create(oNode1a, eOperatora, eNode2a), oBuilder);
}
PredicateImpl::PredicateImpl(Condition & oCondition, Broker &oBuilder) {
	MergeCondition(oCondition, oBuilder);
}
merge_status_t PredicateImpl::MergeCondition(Condition & oCondition, Broker &oBuilder) {
	std::list<Condition>::iterator it;
	oCondition->Normalize(oBuilder);
	if (oCondition->eSpecial == SPECIAL_COND_TRUE) {
		/* A /\ true -> A */
		return MERGE_STATUS_OK;
	} else if (oCondition->eSpecial == SPECIAL_COND_FALSE) {
		/* A /\ false -> false */
		aConditions.clear();
		aConditions.insert(aConditions.end(), oCondition);
		return MERGE_STATUS_OK;
	}

	it = aConditions.begin();
	while(it != aConditions.end()) {
		if ((*it)->eSpecial == SPECIAL_COND_FALSE) {
			/* current state is false -> no point in doing anything at this point */
			return MERGE_STATUS_OK;
		}

		if ((*it)->oExpression1 == oCondition->oExpression1) {
			Condition oMergedCondition;
			merge_result_t eMergeResult = CompareNormalized(&oMergedCondition, oCondition, (*it), &oBuilder);
			switch (eMergeResult) {
			case MERGE_RESULT_NEVER_SATISFIED:
				/* A /\ current state == false */
				aConditions.clear();
				aConditions.insert(aConditions.end(), Condition::create(false));
				return MERGE_STATUS_OK;
			case MERGE_RESULT_MERGABLE:
				aConditions.erase(it);
				/* merged condition is stronger so we use it instead */
				oCondition = oMergedCondition;
				/* new merged condition may merge with previous entries -> start over */
				it = aConditions.begin();
				break;
			case MERGE_RESULT_NOT_MERGABLE:
				it++;
				break;
			case MERGE_RESULT_ALWAYS_SATISFIED:
				/* nothing to do */
				return MERGE_STATUS_OK;
			case MERGE_RESULT_INTERNAL_ERROR:
				return MERGE_STATUS_INTERNAL_ERROR;
			}
		} else {
			it++;
		}
	}

	aConditions.insert(aConditions.end(), oCondition);
	return MERGE_STATUS_OK;
}

satisfied_t PredicateImpl::IsSatisfied(Condition & oCondition, Broker &oBuilder) {
	std::list<Condition>::iterator it;
	oCondition->Normalize(oBuilder);
	if (oCondition->eSpecial == SPECIAL_COND_TRUE) {
		return SATISFIED_ALWAYS;
	} else if (oCondition->eSpecial == SPECIAL_COND_FALSE) {
		return SATISFIED_NEVER;
	}

	for (it = aConditions.begin(); it != aConditions.end(); it++) {
		if ((*it)->oExpression1 == oCondition->oExpression1) {
			merge_result_t eMergeResult = CompareNormalized(NULL, oCondition, (*it), NULL);
			switch (eMergeResult) {
			case MERGE_RESULT_NEVER_SATISFIED:
				return SATISFIED_NEVER;
			case MERGE_RESULT_MERGABLE:
			case MERGE_RESULT_NOT_MERGABLE:
				break;
			case MERGE_RESULT_ALWAYS_SATISFIED:
				return SATISFIED_ALWAYS;
			}
		}
	}
	return SATISFIED_SOMETIMES;
}

merge_result_t PredicateImpl::CompareNormalized(Condition *lpMergedOutput, Condition & oCondition1, Condition & oCondition2, Broker *lpBuilder) {
	unsigned int dwValue1;
	unsigned int dwValue2;
	merge_result_t eResult;

	if (oCondition1->eSpecial != SPECIAL_COND_NORMAL || 
		oCondition1->oExpression1 != oCondition2->oExpression1) {

		return MERGE_RESULT_NOT_MERGABLE;
	}

	dwValue1 = oCondition1->oExpression2->toConstant()->dwValue;
	dwValue2 = oCondition2->oExpression2->toConstant()->dwValue;

	switch (oCondition1->eOperator) {
	case OPERATOR_EQ:
		switch (oCondition2->eOperator) {
		case OPERATOR_EQ:
			/* A == v2 | A == v1 */
			eResult = dwValue1 == dwValue2 ? MERGE_RESULT_ALWAYS_SATISFIED : MERGE_RESULT_NEVER_SATISFIED;
			break;
		case OPERATOR_NEQ:
			/* A != v2 | A == v1 */
			eResult = dwValue1 == dwValue2 ? MERGE_RESULT_NEVER_SATISFIED : MERGE_RESULT_MERGABLE;
			if (lpMergedOutput != NULL && eResult == MERGE_RESULT_MERGABLE) {
				*lpMergedOutput = oCondition1;
			}
			break;
		case OPERATOR_ULE:
			eResult = (unsigned int)dwValue1 <= (unsigned int)dwValue2 ? MERGE_RESULT_MERGABLE : MERGE_RESULT_NEVER_SATISFIED;
			break;
		case OPERATOR_UGE:
			eResult = (unsigned int)dwValue1 >= (unsigned int)dwValue2 ? MERGE_RESULT_MERGABLE : MERGE_RESULT_NEVER_SATISFIED;
			break;
		case OPERATOR_LE:
			eResult = (signed int)dwValue1 <= (signed int)dwValue2 ? MERGE_RESULT_MERGABLE : MERGE_RESULT_NEVER_SATISFIED;
			break;
		case OPERATOR_GE:
			eResult = (signed int)dwValue1 >= (signed int)dwValue2 ? MERGE_RESULT_MERGABLE : MERGE_RESULT_NEVER_SATISFIED;
			break;
		}
		if (lpMergedOutput != NULL && eResult == MERGE_RESULT_MERGABLE) {
			*lpMergedOutput = oCondition1;
		}
		return eResult;
	case OPERATOR_NEQ:
		switch (oCondition2->eOperator) {
		case OPERATOR_EQ:
			/* A == v2 | A != v1 */
			eResult = dwValue1 == dwValue2 ? MERGE_RESULT_NEVER_SATISFIED : MERGE_RESULT_ALWAYS_SATISFIED;
			break;
		case OPERATOR_NEQ:
			/* A != v2 | A != v1 */
			eResult = dwValue1 == dwValue2 ? MERGE_RESULT_ALWAYS_SATISFIED : MERGE_RESULT_NOT_MERGABLE;
			break;
		case OPERATOR_UGE:
		case OPERATOR_GE:
			if (dwValue1 == dwValue2) {
				if (lpMergedOutput != NULL) {
					/* N >= x /\ N != x -> N >= x+1 */
					*lpMergedOutput = Condition::create(oCondition2->oExpression1, oCondition2->eOperator, (*lpBuilder)->NewConstant(dwValue1 + 1));
				}
				eResult = MERGE_RESULT_MERGABLE;
			} else if (oCondition2->eOperator == OPERATOR_UGE && ((unsigned int)dwValue1 < (unsigned int)dwValue2) ||
				oCondition2->eOperator == OPERATOR_GE && ((signed int)dwValue1 < (signed int)dwValue2)) {
				/* N >= x /\ N != y {y < x} -> N >= x */
				eResult = MERGE_RESULT_ALWAYS_SATISFIED;
			} else {
				/* conditions cannot be merged */
				eResult = MERGE_RESULT_NOT_MERGABLE;
			}
			break;
		case OPERATOR_ULE:
		case OPERATOR_LE:
			if (dwValue1 == dwValue2) {
				if (lpMergedOutput != NULL) {
					/* N <= x /\ N != x -> N <= x-1 */
					*lpMergedOutput = Condition::create(oCondition2->oExpression1, oCondition2->eOperator, (*lpBuilder)->NewConstant(dwValue1 - 1));
				}
				eResult = MERGE_RESULT_MERGABLE;
			} else if (oCondition2->eOperator == OPERATOR_ULE && ((unsigned int)dwValue1 > (unsigned int)dwValue2) ||
				oCondition2->eOperator == OPERATOR_LE && ((signed int)dwValue1 > (signed int)dwValue2)) {
				eResult = MERGE_RESULT_ALWAYS_SATISFIED;
			} else {
				eResult = MERGE_RESULT_NOT_MERGABLE;
			}
			break;
		}
		return eResult;

	case OPERATOR_ULE:
		switch (oCondition2->eOperator) {
		case OPERATOR_EQ:
			eResult = dwValue1 >= dwValue2 ? MERGE_RESULT_ALWAYS_SATISFIED : MERGE_RESULT_NEVER_SATISFIED;
			break;
		case OPERATOR_NEQ:
			if (dwValue1 == dwValue2) {
				if (lpMergedOutput != NULL) {
					*lpMergedOutput = Condition::create(oCondition1->oExpression1, oCondition1->eOperator, (*lpBuilder)->NewConstant(dwValue1 - 1));
				}
				eResult = MERGE_RESULT_MERGABLE;
			} else if (dwValue1 < dwValue2) {
				if (lpMergedOutput != NULL) {
					*lpMergedOutput = oCondition1;
				}
				eResult = MERGE_RESULT_MERGABLE;
			} else {
				eResult = MERGE_RESULT_NOT_MERGABLE;
			}
			break;
		case OPERATOR_ULE:
			if (dwValue1 < dwValue2) {
				if (lpMergedOutput != NULL) { *lpMergedOutput = oCondition1; }
				eResult = MERGE_RESULT_MERGABLE;
			} else {
				eResult = MERGE_RESULT_ALWAYS_SATISFIED;
			}
			break;
		case OPERATOR_UGE:
			if (dwValue1 == dwValue2) {
				if (lpMergedOutput != NULL) {
					*lpMergedOutput = Condition::create(oCondition1->oExpression1, OPERATOR_EQ, oCondition1->oExpression2);
				}
				eResult = MERGE_RESULT_MERGABLE;
			} else if (dwValue1 < dwValue2) {
				eResult = MERGE_RESULT_NEVER_SATISFIED;
			} else {
				eResult = MERGE_RESULT_NOT_MERGABLE;
			}
			break;
		case OPERATOR_LE:
		case OPERATOR_GE:
			eResult = MERGE_RESULT_NOT_MERGABLE;
			break;
		}
		return eResult;

	case OPERATOR_UGE:
		switch (oCondition2->eOperator) {
		case OPERATOR_EQ:
			eResult = dwValue1 <= dwValue2 ? MERGE_RESULT_ALWAYS_SATISFIED : MERGE_RESULT_NEVER_SATISFIED;
			break;
		case OPERATOR_NEQ:
			if (dwValue1 == dwValue2) {
				if (lpMergedOutput != NULL) {
					*lpMergedOutput = Condition::create(oCondition1->oExpression1, oCondition1->eOperator, (*lpBuilder)->NewConstant(dwValue1 + 1));
				}
				eResult = MERGE_RESULT_MERGABLE;
			} else if (dwValue1 > dwValue2) {
				if (lpMergedOutput != NULL) { *lpMergedOutput = oCondition1; }
				eResult = MERGE_RESULT_MERGABLE;
			} else {
				eResult = MERGE_RESULT_NOT_MERGABLE;
			}
			break;
		case OPERATOR_UGE:
			if (dwValue1 > dwValue2) {
				if (lpMergedOutput != NULL) { *lpMergedOutput = oCondition1; }
				eResult = MERGE_RESULT_MERGABLE;
			} else {
				eResult = MERGE_RESULT_ALWAYS_SATISFIED;
			}
			break;
		case OPERATOR_ULE:
			if (dwValue1 == dwValue2) {
				if (lpMergedOutput != NULL) {
					*lpMergedOutput = Condition::create(oCondition1->oExpression1, OPERATOR_EQ, oCondition1->oExpression2);
				}
				eResult = MERGE_RESULT_MERGABLE;
			} else if (dwValue1 > dwValue2) {
				eResult = MERGE_RESULT_NEVER_SATISFIED;
			} else {
				eResult = MERGE_RESULT_NOT_MERGABLE;
			}
			break;
		case OPERATOR_LE:
		case OPERATOR_GE:
			eResult = MERGE_RESULT_NOT_MERGABLE;
			break;
		}
		return eResult;

	case OPERATOR_LE:
		switch (oCondition2->eOperator) {
		case OPERATOR_EQ:
			eResult = (signed int)dwValue1 >= (signed int)dwValue2 ? MERGE_RESULT_ALWAYS_SATISFIED : MERGE_RESULT_NEVER_SATISFIED;
			break;
		case OPERATOR_NEQ:
			if ((signed int)dwValue1 == (signed int)dwValue2) {
				if (lpMergedOutput != NULL) {
					*lpMergedOutput = Condition::create(oCondition1->oExpression1, oCondition1->eOperator, (*lpBuilder)->NewConstant(dwValue1 - 1));
				}
				eResult = MERGE_RESULT_MERGABLE;
			} else if ((signed int)dwValue1 < (signed int)dwValue2) {
				if (lpMergedOutput != NULL) { *lpMergedOutput = oCondition1; }
				eResult = MERGE_RESULT_MERGABLE;
			} else {
				eResult = MERGE_RESULT_NOT_MERGABLE;
			}
			break;
		case OPERATOR_LE:
			if ((signed int)dwValue1 <= (signed int)dwValue2) {
				if (lpMergedOutput != NULL) { *lpMergedOutput = oCondition1; }
				eResult = MERGE_RESULT_MERGABLE;
			} else {
				eResult = MERGE_RESULT_ALWAYS_SATISFIED;
			}
			break;
		case OPERATOR_GE:
			if ((signed int)dwValue1 == (signed int)dwValue2) {
				if (lpMergedOutput != NULL) {
					*lpMergedOutput = Condition::create(oCondition1->oExpression1, OPERATOR_EQ, oCondition1->oExpression2);
				}
				eResult = MERGE_RESULT_MERGABLE;
			} else if ((signed int)dwValue1 < (signed int)dwValue2) {
				eResult = MERGE_RESULT_NEVER_SATISFIED;
			} else {
				eResult = MERGE_RESULT_NOT_MERGABLE;
			}
			break;
		case OPERATOR_ULE:
		case OPERATOR_UGE:
			eResult = MERGE_RESULT_NOT_MERGABLE;
			break;
		}
		return eResult;

	case OPERATOR_GE:
		switch (oCondition2->eOperator) {
		case OPERATOR_EQ:
			eResult = (signed int)dwValue1 <= (signed int)dwValue2 ? MERGE_RESULT_ALWAYS_SATISFIED : MERGE_RESULT_NEVER_SATISFIED;
			break;
		case OPERATOR_NEQ:
			if ((signed int)dwValue1 == (signed int)dwValue2) {
				if (lpMergedOutput != NULL) {
					*lpMergedOutput = Condition::create(oCondition1->oExpression1, oCondition1->eOperator, (*lpBuilder)->NewConstant(dwValue1 + 1));
				}
				eResult = MERGE_RESULT_MERGABLE;
			} else if ((signed int)dwValue1 > (signed int)dwValue2) {
				if (lpMergedOutput != NULL) { *lpMergedOutput = oCondition1; }
				eResult = MERGE_RESULT_MERGABLE;
			} else {
				eResult = MERGE_RESULT_NOT_MERGABLE;
			}
			break;
		case OPERATOR_GE:
			if ((signed int)dwValue1 > (signed int)dwValue2) {
				if (lpMergedOutput != NULL) { *lpMergedOutput = oCondition1; }
				eResult = MERGE_RESULT_MERGABLE;
			} else {
				eResult = MERGE_RESULT_ALWAYS_SATISFIED;
			}
			break;
		case OPERATOR_LE:
			if ((signed int)dwValue1 == (signed int)dwValue2) {
				if (lpMergedOutput != NULL) {
					*lpMergedOutput = Condition::create(oCondition1->oExpression1, OPERATOR_EQ, oCondition1->oExpression2);
				}
				eResult = MERGE_RESULT_MERGABLE;
			} else if ((signed int)dwValue1 > (signed int)dwValue2) {
				eResult = MERGE_RESULT_NEVER_SATISFIED;
			}
			else {
				eResult = MERGE_RESULT_NOT_MERGABLE;
			}
			break;
		case OPERATOR_ULE:
		case OPERATOR_UGE:
			eResult = MERGE_RESULT_NOT_MERGABLE;
			break;
		}
		return eResult;
	}
	wc_debug("[-] should never get here [Predicate::CompareNormalized(%s, %s)]\n", oCondition1->expression(2).c_str(), oCondition2->expression(2).c_str());
	return MERGE_RESULT_INTERNAL_ERROR;
}

Predicate PredicateImpl::Migrate(DFGraph oGraph) {
	Predicate oFork = Predicate::create();
	std::list<Condition>::iterator it;

	for (it = aConditions.begin(); it != aConditions.end(); it++) {
		oFork->aConditions.push_back((*it)->Migrate(oGraph));
	}

	return oFork;
}

std::string PredicateImpl::expression(int dwMaxDepth) const {
	std::stringstream oStringStream;

	std::list<Condition>::const_iterator it;

	for (it = aConditions.begin(); it != aConditions.end(); it++) {
		if (it != aConditions.begin()) {
			oStringStream << " /\\ ";
		}
		oStringStream << (*it)->expression(dwMaxDepth);
	}
	return oStringStream.str();
}