#include <sstream>
#include <idp.hpp>

#include "common.hpp"
#include "Condition.hpp"
#include "DFGNode.hpp"
#include "DFGraph.hpp"
#include "Broker.hpp"

const char *aStrings[] = { "=", "!=", ">=u", "<u", ">u", "<=u", ">=", "<", ">", "<=" };

std::string ConditionImpl::expression(int dwMaxDepth) const {
	switch (eSpecial) {
	case SPECIAL_COND_TRUE:
		return std::string("true");
	case SPECIAL_COND_FALSE:
		return std::string("false");
	}

	std::stringstream oStream;
	oStream << oExpression1->expression(dwMaxDepth) << " " << aStrings[eOperator] << " " << oExpression2->expression(dwMaxDepth);
	return oStream.str();
}

static operator_t FlipOperator(operator_t eOperator) {
	switch (eOperator) {
	case OPERATOR_EQ:
		return OPERATOR_EQ;
	case OPERATOR_NEQ:
		return OPERATOR_NEQ;
	case OPERATOR_UGE: // unsigned greater or equal
		return OPERATOR_ULE;
	case OPERATOR_ULT:
		return OPERATOR_UGT;
	case OPERATOR_UGT:
		return OPERATOR_ULT;
	case OPERATOR_ULE:
		return OPERATOR_UGE;
	case OPERATOR_GE:
		return OPERATOR_LE;
	case OPERATOR_LT:
		return OPERATOR_GT;
	case OPERATOR_GT:
		return OPERATOR_LT;
	case OPERATOR_LE:
		return OPERATOR_GE;
	default:
		return eOperator;
	}
}

Condition ConditionImpl::Negate() {
	return Condition::create(oExpression1, (operator_t)((int)eOperator ^ 1), oExpression2);
}

Condition ConditionImpl::Migrate(DFGraph oGraph) {
	Condition oFork = Condition::create(*this);

	if (oFork->eSpecial == SPECIAL_COND_NORMAL) {
		oFork->oExpression1 = oGraph->FindNode(oFork->oExpression1->dwNodeId);
		oFork->oExpression2 = oGraph->FindNode(oFork->oExpression2->dwNodeId);
	}
	return oFork;
}

void ConditionImpl::Normalize(Broker & oBuilder) {
	if (eSpecial == SPECIAL_COND_NORMAL) {
		bool bAgain;

		if (!NODE_IS_CONSTANT(oExpression2)) {
			if (NODE_IS_CONSTANT(oExpression1)) {
				DFGNode oTemp = oExpression1;
				oExpression1 = oExpression2;
				eOperator = FlipOperator(eOperator);
				oExpression2 = oTemp;
			} else {
				oExpression1 = oBuilder->NewAdd(
					oExpression1,
					oBuilder->NewMult(oExpression2, oBuilder->NewConstant(-1))
				);
				oExpression2 = oBuilder->NewConstant(0);
			}
		}

		/* At this point oExpression2 is guaranteed to be of type constant */
		do {
			//wc_debug("[*] normalization step : %s\n", expression(2).c_str());
			bAgain = false;
			if (NODE_IS_ADD(oExpression1) || NODE_IS_MULT(oExpression1) || NODE_IS_XOR(oExpression1)) {
				std::list<DFGNode>::iterator itConst(oExpression1->aInputNodes.end());
				std::list<DFGNode>::iterator it;
				DFGNode oNode1(nullptr), oNode2(nullptr);

				for (it = oExpression1->aInputNodes.begin(); it != oExpression1->aInputNodes.end(); it++) {
					if (NODE_IS_CONSTANT(*it)) {
						itConst = it;
					} else if (oNode1 == nullptr) {
						oNode1 = *it;
					} else if (oNode2 == nullptr) {
						oNode2 = *it;
					} else {
						break;
					}
				}

				if (itConst != oExpression1->aInputNodes.end()) {
					if (NODE_IS_ADD(oExpression1)) {
						// ((a+CONST:x) == CONST:b) -> (a == CONST:b-x)
						DFGNode oReplacement;
						if (oNode1 == nullptr) {
							oReplacement = oBuilder->NewConstant(0);
						} else if (oNode2 == nullptr) {
							oReplacement = oNode1;
						} else {
							oReplacement = oBuilder->NewAdd(oNode1, oNode2);
						}
						for (; it != oExpression1->aInputNodes.end(); it++) {
							oReplacement = oBuilder->NewAdd(oReplacement, *it);
						}
						oExpression1 = oReplacement;
						oExpression2 = oBuilder->NewConstant(oExpression2->toConstant()->dwValue - (*itConst)->toConstant()->dwValue);
						bAgain = true;
					} else if (NODE_IS_MULT(oExpression1)) {
						// ((a*CONST:x) == CONST:b) -> (a == CONST:b/x)
						/* make sure x divides b */
						if ((oExpression2->toConstant()->dwValue % abs((int)(*itConst)->toConstant()->dwValue)) == 0) {
							DFGNode oReplacement;
							if (oNode1 == nullptr) {
								oReplacement = oBuilder->NewConstant(0);
							} else if (oNode2 == nullptr) {
								oReplacement = oNode1;
							} else {
								oReplacement = oBuilder->NewMult(oNode1, oNode2);
							}
							/* fix special case x=-1, b=-MAXINT */
							//long long int qwNewValue = (long long int)oExpression2->toConstant()->dwValue / (int)(*itConst)->toConstant()->dwValue;
							//if ((unsigned long long)qwNewValue >= 0x100000000) {
							if ((oExpression2->toConstant()->dwValue == 0x80000000) && (*itConst)->toConstant()->dwValue == -1) {
								eSpecial = SPECIAL_COND_FALSE;
								oExpression1 = nullptr;
								oExpression2 = nullptr;
								return;
							} else {
								for (; it != oExpression1->aInputNodes.end(); it++) {
									oReplacement = oBuilder->NewMult(oReplacement, *it);
								}
								oExpression1 = oReplacement;
								oExpression2 = oBuilder->NewConstant((int)oExpression2->toConstant()->dwValue / (int)(*itConst)->toConstant()->dwValue);
								if ((int)(*itConst)->toConstant()->dwValue < 0) {
									eOperator = FlipOperator(eOperator);
								}
							}
							bAgain = true;
						}
					} else if (NODE_IS_XOR(oExpression1)) {
						// ((a^CONST:x) == CONST:b) -> (a == CONST:b^x)
						DFGNode oReplacement;
						if (oNode1 == nullptr) {
							oReplacement = oBuilder->NewConstant(0);
						} else if (oNode2 == nullptr) {
							oReplacement = oNode1;
						} else {
							oReplacement = oBuilder->NewXor(oNode1, oNode2);
						}
						for (; it != oExpression1->aInputNodes.end(); it++) {
							oReplacement = oBuilder->NewXor(oReplacement, *it);
						}
						oExpression1 = oReplacement;
						oExpression2 = oBuilder->NewConstant(oExpression2->toConstant()->dwValue ^ (*itConst)->toConstant()->dwValue);
						bAgain = true;
					}
				}
			} else if (NODE_IS_SHIFT(oExpression1) || NODE_IS_ROTATE(oExpression1)) {
				// ((a<<CONST:x) == CONST:b) -> (a == CONST:b>>x)
				/* check if shift amount is constant */
				if (NODE_IS_CONSTANT(*++oExpression1->aInputNodes.begin())) {
					int dwAmount = (*++oExpression1->aInputNodes.begin())->toConstant()->dwValue;
					unsigned int dwValue = oExpression2->toConstant()->dwValue;
					if (dwAmount < 0) {
						oExpression1 = *oExpression1->aInputNodes.begin();
						oExpression2 = NODE_IS_SHIFT(oExpression1) ?
							oBuilder->NewConstant(dwValue << abs(dwAmount)) : // shift
							oBuilder->NewConstant((dwValue << abs(dwAmount)) | (dwValue >> (32 - abs(dwAmount)))); // rotate
						bAgain = true;
					} /*else {
						if (NODE_IS_SHIFT(oExpression1) && (eOperator == OPERATOR_EQ || eOperator == OPERATOR_NEQ) && (dwValue & ((1 << dwAmount) - 1)) != 0) {
							eSpecial = (eOperator == OPERATOR_EQ) ? SPECIAL_COND_FALSE : SPECIAL_COND_TRUE;
							oExpression1 = nullptr;
							oExpression2 = nullptr;
							return;
						} else {
							oExpression1 = *oExpression1->aInputNodes.begin();
							oExpression2 = NODE_IS_SHIFT(oExpression1) ?
								oBuilder->NewConstant(dwValue >> dwAmount) : // shift
								oBuilder->NewConstant((dwValue >> dwAmount) | (dwValue << (32 - dwAmount))); // rotate
							bAgain = true;
						}
					}*/
				}
			}
		} while (bAgain);

		//wc_debug("[*] normalization step : %s\n", expression(2).c_str());

		/* We substitute > and < with >= and <=, respectively, so that we don't have to handle both */
		switch (eOperator) {
		case OPERATOR_UGT:
			if (oExpression2->toConstant()->dwValue == 0xffffffff) {
_false:
				eSpecial = SPECIAL_COND_FALSE;
				oExpression1 = nullptr;
				oExpression2 = nullptr;
				return;
			}
			oExpression2 = oBuilder->NewConstant(oExpression2->toConstant()->dwValue + 1);
			eOperator = OPERATOR_UGE;
			break;
		case OPERATOR_UGE:
			if (oExpression2->toConstant()->dwValue == 0) {
_true:
				eSpecial = SPECIAL_COND_TRUE;
				oExpression1 = nullptr;
				oExpression2 = nullptr;
				return;
			}
			break;
		case OPERATOR_ULT:
			if (oExpression2->toConstant()->dwValue == 0) {
				goto _false;
			}
			oExpression2 = oBuilder->NewConstant(oExpression2->toConstant()->dwValue - 1);
			eOperator = OPERATOR_ULE;
			break;
		case OPERATOR_ULE:
			if (oExpression2->toConstant()->dwValue == 0xffffffff) {
				goto _true;
			}
			break;
		case OPERATOR_GT:
			if (oExpression2->toConstant()->dwValue == 0x7fffffff) {
				goto _false;
			}
			oExpression2 = oBuilder->NewConstant(oExpression2->toConstant()->dwValue + 1);
			eOperator = OPERATOR_GE;
			break;
		case OPERATOR_GE:
			if (oExpression2->toConstant()->dwValue == 0x80000000) {
				goto _true;
			}
			break;
		case OPERATOR_LT:
			if (oExpression2->toConstant()->dwValue == 0x80000000) {
				goto _false;
			}
			oExpression2 = oBuilder->NewConstant(oExpression2->toConstant()->dwValue - 1);
			eOperator = OPERATOR_LE;
			break;
		case OPERATOR_LE:
			if (oExpression2->toConstant()->dwValue == 0x7fffffff) {
				goto _true;
			}
			break;
		}

		//wc_debug("[*] normalization step : %s\n", expression(2).c_str());
		/* both expressions are constant -> result can thus be computed */
		if (NODE_IS_CONSTANT(oExpression1) && NODE_IS_CONSTANT(oExpression2)) {
			switch (eOperator) {
			case OPERATOR_EQ:
				eSpecial = oExpression1->toConstant()->dwValue == oExpression2->toConstant()->dwValue ? SPECIAL_COND_TRUE : SPECIAL_COND_FALSE; break;
			case OPERATOR_NEQ:
				eSpecial = oExpression1->toConstant()->dwValue != oExpression2->toConstant()->dwValue ? SPECIAL_COND_TRUE : SPECIAL_COND_FALSE; break;
			case OPERATOR_UGE: // unsigned greater or equal
				eSpecial = (unsigned int)oExpression1->toConstant()->dwValue >= (unsigned int)oExpression2->toConstant()->dwValue ? SPECIAL_COND_TRUE : SPECIAL_COND_FALSE; break;
			//case OPERATOR_ULT:
			//	eSpecial = (unsigned int)oExpression1->toConstant()->dwValue < (unsigned int)oExpression2->toConstant()->dwValue ? SPECIAL_COND_TRUE : SPECIAL_COND_FALSE; break;
			//case OPERATOR_UGT:
			//	eSpecial = (unsigned int)oExpression1->toConstant()->dwValue > (unsigned int)oExpression2->toConstant()->dwValue ? SPECIAL_COND_TRUE : SPECIAL_COND_FALSE; break;
			case OPERATOR_ULE:
				eSpecial = (unsigned int)oExpression1->toConstant()->dwValue <= (unsigned int)oExpression2->toConstant()->dwValue ? SPECIAL_COND_TRUE : SPECIAL_COND_FALSE; break;
			case OPERATOR_GE:
				eSpecial = (signed int)oExpression1->toConstant()->dwValue >= (signed int)oExpression2->toConstant()->dwValue ? SPECIAL_COND_TRUE : SPECIAL_COND_FALSE; break;
			//case OPERATOR_LT:
			//	eSpecial = (signed int)oExpression1->toConstant()->dwValue > (signed int)oExpression2->toConstant()->dwValue ? SPECIAL_COND_TRUE : SPECIAL_COND_FALSE; break;
			//case OPERATOR_GT:
			//	eSpecial = (signed int)oExpression1->toConstant()->dwValue < (signed int)oExpression2->toConstant()->dwValue ? SPECIAL_COND_TRUE : SPECIAL_COND_FALSE; break;
			case OPERATOR_LE:
				eSpecial = (signed int)oExpression1->toConstant()->dwValue <= (signed int)oExpression2->toConstant()->dwValue ? SPECIAL_COND_TRUE : SPECIAL_COND_FALSE; break;
			}
			oExpression1 = nullptr;
			oExpression2 = nullptr;
			eOperator = OPERATOR_EQ;
		}
	}
}
