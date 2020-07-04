#include <segment.hpp>
#include <funcs.hpp>
#include <idd.hpp>
#include <Windows.h>
#include <sysinfoapi.h>
#include <sstream>

#include "common.hpp"
#include "Broker.hpp"
#include "DFGNode.hpp"
#include "DFGraph.hpp"
#include "Processor.hpp"
#include "Predicate.hpp"
#include "Backlog.hpp"
#include "PathOracle.hpp"
#include "ThreadPool.hpp"
#include "SignatureParser.hpp"
#include "ThreadPool.hpp"

#define BAD_ZERO_VALUE 0xfeedface

BrokerImpl::~BrokerImpl() { }

DFGNode BrokerImpl::NewConstant(unsigned int dwValue) {
	DFGConstantImpl oConstant(dwValue);
	return FindNode(oConstant);
}

DFGNode BrokerImpl::NewRegister(unsigned char bRegister) {
	DFGRegisterImpl oRegister(bRegister);
	return FindNode(oRegister);
}

DFGNode BrokerImpl::NewAdd(DFGNode & oNode1, DFGNode & oNode2, DFGNode *lpCarry, DFGNode *lpOverflow) {
	std::list<DFGNode>::iterator it1, it2;

	std::list<DFGNode> aList1 = NODE_IS_ADD(oNode1) ? oNode1->aInputNodes : std::list<DFGNode>(1, oNode1);
	std::list<DFGNode> aList2 = NODE_IS_ADD(oNode2) ? oNode2->aInputNodes : std::list<DFGNode>(1, oNode2);

	for (it1 = aList1.begin(); it1 != aList1.end(); it1++) {
		if (!NODE_IS_MULT(*it1)) {
			for (it2 = aList2.begin(); it2 != aList2.end(); it2++) {
				if (*it1 == *it2) {
					*it1 = NewMult(*it1, NewConstant(2), lpCarry, lpOverflow);
					it2 = aList2.erase(it2);
					break;
				}
				if (NODE_IS_MULT(*it2) && (*it2)->aInputNodes.size() == 2) {
					/* only support 2 inputs to mult for now */
					DFGNode oMult1 = *(*it2)->aInputNodes.begin();
					DFGNode oMult2 = *++(*it2)->aInputNodes.begin();
					if (*it1 == oMult1 && NODE_IS_CONSTANT(oMult2)) {
						*it1 = NewMult(oMult1, NewConstant(oMult2->toConstant()->dwValue + 1), lpCarry, lpOverflow);
						it2 = aList2.erase(it2);
						break;
					} else if (*it1 == oMult2 && NODE_IS_CONSTANT(oMult1)) {
						*it1 = NewMult(oMult2, NewConstant(oMult1->toConstant()->dwValue + 1), lpCarry, lpOverflow);
						it2 = aList2.erase(it2);
						break;
					}
				}
			}
		} else if ((*it1)->aInputNodes.size() == 2) {
			DFGNode oMulta1 = *(*it1)->aInputNodes.begin();
			DFGNode oMulta2 = *++(*it1)->aInputNodes.begin();
			for (it2 = aList2.begin(); it2 != aList2.end(); it2++) {
				if (oMulta1 == *it2 && NODE_IS_CONSTANT(oMulta2)) {
					*it1 = NewMult(oMulta1, NewConstant(oMulta2->toConstant()->dwValue + 1), lpCarry, lpOverflow);
					it2 = aList2.erase(it2);
					break;
				} else if (oMulta2 == *it2 && NODE_IS_CONSTANT(oMulta1)) {
					*it1 = NewMult(oMulta2, NewConstant(oMulta1->toConstant()->dwValue + 1), lpOverflow);
					it2 = aList2.erase(it2);
					break;
				}
				if (NODE_IS_MULT(*it2) && (*it2)->aInputNodes.size() == 2) {
					DFGNode oMultb1 = *(*it2)->aInputNodes.begin();
					DFGNode oMultb2 = *++(*it2)->aInputNodes.begin();
					if (oMulta1 == oMultb1 && NODE_IS_CONSTANT(oMulta2) && NODE_IS_CONSTANT(oMultb2)) {
						*it1 = NewMult(oMulta1, NewConstant(oMulta2->toConstant()->dwValue + oMultb2->toConstant()->dwValue), lpCarry, lpOverflow);
					} else if (oMulta2 == oMultb2 && NODE_IS_CONSTANT(oMulta1) && NODE_IS_CONSTANT(oMultb1)) {
						*it1 = NewMult(oMulta2, NewConstant(oMulta1->toConstant()->dwValue + oMultb1->toConstant()->dwValue), lpCarry, lpOverflow);
					} else if (oMulta1 == oMultb2 && NODE_IS_CONSTANT(oMulta2) && NODE_IS_CONSTANT(oMultb1)) {
						*it1 = NewMult(oMulta1, NewConstant(oMulta2->toConstant()->dwValue + oMultb1->toConstant()->dwValue), lpCarry, lpOverflow);
					} else if (oMulta2 == oMultb1 && NODE_IS_CONSTANT(oMulta1) && NODE_IS_CONSTANT(oMultb2)) {
						*it1 = NewMult(oMulta2, NewConstant(oMulta1->toConstant()->dwValue + oMultb2->toConstant()->dwValue), lpCarry, lpOverflow);
					} else {
						continue;
					}
					it2 = aList2.erase(it2);
					break;
				}
			}
		}
	}

	DFGNode oTempNode1, oTempNode2;
	aList1.insert(aList1.end(), aList2.begin(), aList2.end());
	switch (aList1.size()) {
	case 0:
		return NewConstant(0);
	case 1:
		return *aList1.begin();
	case 2:
		oTempNode1 = *aList1.begin();
		oTempNode2 = *++aList1.begin();
		break;
	default:
		oTempNode2 = aList1.back();
		aList1.pop_back();
		oTempNode1 = DFGAdd::create(*aList1.begin(), *++aList1.begin())->toGeneric();
		oTempNode1->aInputNodes = aList1;
		oTempNode1 = FindNode(oTempNode1);
		break;
	}

	return FindNode(MergeOperationsCommutative(
		oTempNode1, oTempNode2,
		lpCarry, lpOverflow,
		NODE_TYPE_ADD,
		[](DFGNode &a, DFGNode &b) { return DFGAdd::create(a, b)->toGeneric(); },
		[](dot_result_t *lpResult, unsigned int a, unsigned int b) {
			unsigned long long qwResult = (unsigned long long)a + b;
			lpResult->dwValue = (unsigned int)qwResult;
			lpResult->dwFlags = (qwResult & 0x100000000) ? DOT_FLAG_CARRY : 0;
			lpResult->dwFlags |= (((a & 0x80000000) == (b & 0x80000000)) && (!!(qwResult & 0x80000000) != (a & 0x80000000))) ? DOT_FLAG_OVERFLOW : 0;
		},
		0,
		BAD_ZERO_VALUE
	));
}

DFGNode BrokerImpl::NewMult(DFGNode & oNode1, DFGNode & oNode2, DFGNode *lpCarry, DFGNode *lpOverflow) {
	std::list<DFGNode>::iterator it;

	if (NODE_IS_ADD(oNode1)) {
		DFGNode oResult = NewMult(*oNode1->aInputNodes.begin(), oNode2);
		for (it = ++oNode1->aInputNodes.begin(); it != oNode1->aInputNodes.end(); it++) {
			oResult = NewAdd(oResult, NewMult(*it, oNode2));
		}
		return oResult;
	} else if (NODE_IS_ADD(oNode2)) {
		DFGNode oResult = NewMult(*oNode2->aInputNodes.begin(), oNode1);
		for (it = ++oNode2->aInputNodes.begin(); it != oNode2->aInputNodes.end(); it++) {
			oResult = NewAdd(oResult, NewMult(*it, oNode1));
		}
		return oResult;
	}

	return FindNode(MergeOperationsCommutative(
		oNode1, oNode2,
		lpCarry, lpOverflow,
		NODE_TYPE_MULT,
		[](DFGNode &a, DFGNode &b) { return DFGMult::create(a, b)->toGeneric(); },
		[](dot_result_t *lpResult, unsigned int a, unsigned int b) { lpResult->dwValue = a * b; },
		1,
		0
	));
}

DFGNode BrokerImpl::NewCall(unsigned long lpAddress, DFGNode & oArgument1) {
	DFGCallImpl oCall(lpAddress, oArgument1);
	return FindNode(oCall);
}

DFGNode BrokerImpl::NewLoad(DFGNode & oMemoryLocation) {
	std::unordered_map<unsigned int, DFGNode>::iterator it;
	if (NODE_IS_CONSTANT(oMemoryLocation)) {
		unsigned long lpMemoryAddress = oMemoryLocation->toConstant()->dwValue;
		API_LOCK();
		segment_t *lpSegment = getseg(lpMemoryAddress);
		if (lpSegment != NULL && !(lpSegment->perm & SEGPERM_WRITE)) {
			unsigned int dwResult = get_dword(lpMemoryAddress);
			API_UNLOCK();
			return NewConstant(dwResult);
		}
		API_UNLOCK();
	}

	if ((it = aMemoryMap.find(oMemoryLocation->dwNodeId)) != aMemoryMap.end()) {
		return (*it).second;
	}

	DFGLoadImpl oLoad(oMemoryLocation);
	return FindNode(oLoad);
}

DFGNode BrokerImpl::NewStore(DFGNode & oData, DFGNode & oMemoryLocation) {
	std::unordered_map<unsigned int, DFGNode>::iterator it;

	if ((it = aMemoryMap.find(oMemoryLocation->dwNodeId)) != aMemoryMap.end()) {
		it->second = oData;
	} else {
		aMemoryMap.insert(std::pair<unsigned int, DFGNode>(oMemoryLocation->dwNodeId, oData));
	}
	DFGStoreImpl oStore(oData, oMemoryLocation);
	return FindNode(oStore);
}

DFGNode BrokerImpl::NewXor(DFGNode & oNode1, DFGNode & oNode2, DFGNode *lpCarry, DFGNode *lpOverflow) {
	if (oNode1 == oNode2) {
		return NewConstant(0);
	}
	return FindNode(MergeOperationsCommutative(
		oNode1, oNode2,
		lpCarry, lpOverflow,
		NODE_TYPE_XOR,
		[](DFGNode &a, DFGNode &b) { return DFGXor::create(a, b)->toGeneric(); },
		[](dot_result_t *lpResult, unsigned int a, unsigned int b) { lpResult->dwValue = a ^ b; },
		0,
		BAD_ZERO_VALUE
	));
}

DFGNode BrokerImpl::NewAnd(DFGNode & oNode1, DFGNode & oNode2, DFGNode *lpCarry, DFGNode *lpOverflow) {
	if (NODE_IS_ROTATE(oNode1) && NODE_IS_CONSTANT(*++oNode1->aInputNodes.begin()) && NODE_IS_CONSTANT(oNode2)) {
		unsigned int dwMask = oNode2->toConstant()->dwValue;
		int dwShiftAmount = (int)(*++oNode1->aInputNodes.begin())->toConstant()->dwValue;
		if (dwShiftAmount >= 0) {
			dwShiftAmount = dwShiftAmount; /* rotate-right amount */
		} else {
			dwShiftAmount = 32 + dwShiftAmount;
		}
		dwShiftAmount &= 31;
		dwMask |= dwMask >> 1;
		dwMask |= dwMask >> 2;
		dwMask |= dwMask >> 4;
		dwMask |= dwMask >> 8;
		dwMask |= dwMask >> 16;

		if (!(dwMask & ~(0xffffffff >> dwShiftAmount))) {
			/* 
			 * rotate and shift are equivalent
			 */
			return NewAnd(NewShift(*oNode1->aInputNodes.begin(), NewConstant(-dwShiftAmount)), oNode2, lpCarry, lpOverflow);
		}
	} else if (NODE_IS_SHIFT(oNode1) && NODE_IS_CONSTANT(*++oNode1->aInputNodes.begin()) && NODE_IS_CONSTANT(oNode2)) {
		unsigned int dwMask = oNode2->toConstant()->dwValue;
		int dwShiftAmount = (int)(*++oNode1->aInputNodes.begin())->toConstant()->dwValue;
		if (!(dwMask ^ ~(0xffffffff << dwShiftAmount))) {
			return oNode1;
		}
	}
	return FindNode(MergeOperationsCommutative(
		oNode1, oNode2,
		lpCarry, lpOverflow,
		NODE_TYPE_AND,
		[](DFGNode &a, DFGNode &b) { return DFGAnd::create(a, b)->toGeneric(); },
		[](dot_result_t *lpResult, unsigned int a, unsigned int b) { lpResult->dwValue = a & b; },
		0xffffffff,
		0
	));
}

DFGNode BrokerImpl::NewOr(DFGNode & oNode1, DFGNode & oNode2, DFGNode *lpCarry, DFGNode *lpOverflow) {
	return FindNode(MergeOperationsCommutative(
		oNode1, oNode2,
		lpCarry, lpOverflow,
		NODE_TYPE_OR,
		[](DFGNode &a, DFGNode &b) { return DFGOr::create(a, b)->toGeneric(); },
		[](dot_result_t *lpResult, unsigned int a, unsigned int b) { lpResult->dwValue = a | b; },
		0,
		BAD_ZERO_VALUE
	));
}

DFGNode BrokerImpl::NewShift(DFGNode & oNode, DFGNode & oAmount, DFGNode *lpCarry, DFGNode *lpOverflow) {
	if (NODE_IS_SHIFT(oNode) && NODE_IS_CONSTANT(oAmount)) {
		DFGNode oAmountLeft = *++oNode->toShift()->aInputNodes.begin();
		if (NODE_IS_CONSTANT(oAmountLeft)) {
			int dwValue1 = oAmountLeft->toConstant()->dwValue;
			int dwValue2 = oAmount->toConstant()->dwValue;

			if ((dwValue1 < 0) == (dwValue2 < 0)) {
				return NewShift(*oNode->toShift()->aInputNodes.begin(), NewConstant(dwValue1 + dwValue2), lpCarry, lpOverflow);
			}
		}
	}
	if (NODE_IS_CONSTANT(oAmount) && (NODE_IS_AND(oNode) || NODE_IS_OR(oNode))) {
		/*
		 * (A & CONST:x) << CONST:y ==> (A << CONST:y) & (CONST:x << CONST:y)
		 */
		std::list<DFGNode>::iterator it;
		for (it = oNode->aInputNodes.begin(); it != oNode->aInputNodes.end(); it++) {
			if (NODE_IS_CONSTANT(*it)) {
				break;
			}
		}
		if (it != oNode->aInputNodes.end()) {
			/*
			 * oNode is of type AND or OR, and a constant is used as one of the inputs.
			 * we take out the constant and merge it with the AND/OR parameter
			 */
			DFGNode oTempNode(oNode->copy());
			DFGNode oNewNode;
			oTempNode->aInputNodes = oNode->aInputNodes;
			unsigned int dwValue = (*it)->toConstant()->dwValue;

			for (it = oTempNode->aInputNodes.begin(); it != oTempNode->aInputNodes.end(); it++) {
				/* re-create oNode, but with the constant removed */
				if (NODE_IS_CONSTANT(*it)) {
					oTempNode->aInputNodes.erase(it);
					break;
				}
			}
			/*
			 * in case only a single input remains, simply take that input
			 * as the AND/OR does not do anything meaningful
			 */
			if (oTempNode->aInputNodes.size() == 1) {
				DFGNode oNestedNode = *oTempNode->aInputNodes.begin();
				if (NODE_IS_AND(oNode) && NODE_IS_SHIFT(oNestedNode) && NODE_IS_CONSTANT(*++oNestedNode->aInputNodes.begin())) {
					/*
					 * ((A>>x)&m)<<y ==> (A>>(x-y))&(m<<y)
					 * normally, (A>>x)<<y need not be equivalent to A>>(x-y),
					 * but due to the AND operation, it is in this case.
					 * Some compilers do this, so we should do it as well
					 * in order to map both representations to a single form
					 */
					oNewNode = NewShift(
						*oNestedNode->aInputNodes.begin(),
						NewConstant(
							(*++oNestedNode->aInputNodes.begin())->toConstant()->dwValue +
							oAmount->toConstant()->dwValue
						), lpCarry, lpOverflow
					);
				} else {
					oNewNode = NewShift(oNestedNode, oAmount, lpCarry, lpOverflow);
				}
			} else {
				oNewNode = NewShift(FindNode(oTempNode), oAmount, lpCarry, lpOverflow);
			}
			if (NODE_IS_AND(oNode)) {
				return NewAnd(oNewNode, NewConstant(dwValue << oAmount->toConstant()->dwValue));
			} else {
				return NewOr(oNewNode, NewConstant(dwValue << oAmount->toConstant()->dwValue));
			}
		}
	}
	if (NODE_IS_CONSTANT(oAmount) && abs((int)oAmount->toConstant()->dwValue) >= 32) {
		if (lpCarry != NULL) {
			int dwShiftAmount = (int)oAmount->toConstant()->dwValue;
			if (abs(dwShiftAmount) > 32) {
				*lpCarry = NewConstant(0);
			} else if (dwShiftAmount < 0) {
				*lpCarry = NewAnd(NewShift(oNode, NewConstant(dwShiftAmount + 1)), NewConstant(1));
			} else {
				*lpCarry = NewAnd(NewShift(oNode, NewConstant(dwShiftAmount - 31)), NewConstant(1));
			}
		}
		return NewConstant(0);
	}
	return FindNode(MergeOperations(
		oNode, oAmount,
		lpCarry, lpOverflow,
		[](DFGNode &a, DFGNode &b) { return DFGShift::create(a, b)->toGeneric(); },
		[](dot_result_t *lpResult, unsigned int a, unsigned int b) {
			lpResult->dwValue = a << b;
			lpResult->dwFlags = ((b < 0) ? ((a << (b + 1)) & 1) : ((a << (b - 31)) & 1)) ? DOT_FLAG_CARRY : 0;
		},
		0,
		32
	));
}

DFGNode BrokerImpl::NewRotate(DFGNode & oNode, DFGNode & oAmount, DFGNode *lpCarry, DFGNode *lpOverflow) {
	if (NODE_IS_ROTATE(oNode) && NODE_IS_CONSTANT(oAmount)) {
		DFGNode oAmountLeft = *++oNode->aInputNodes.begin();
		if (NODE_IS_CONSTANT(oAmountLeft)) {
			unsigned int dwValue1 = oAmountLeft->toConstant()->dwValue;
			unsigned int dwValue2 = oAmount->toConstant()->dwValue;

			return NewRotate(*oNode->toRotate()->aInputNodes.begin(), NewConstant(dwValue1 + dwValue2), lpCarry, lpOverflow);
		}
	}
	DFGNode oAmountLocal = oAmount;
	if (NODE_IS_CONSTANT(oAmount) && abs((int)oAmount->toConstant()->dwValue) >= 32) {
		int dwModulo = abs((int)oAmount->toConstant()->dwValue) % 32;
		oAmountLocal = NewConstant(dwModulo && oAmount->toConstant()->dwValue < 0 ? 32 - dwModulo : dwModulo);
	}
	return FindNode(MergeOperations(
		oNode, oAmountLocal,
		lpCarry, lpOverflow,
		[](DFGNode &a, DFGNode &b) { return DFGRotate::create(a, b)->toGeneric(); },
		[](dot_result_t *lpResult, unsigned int a, unsigned int b) {
			lpResult->dwValue = (a >> b) | (a << (32-b));
			lpResult->dwFlags = ((b < 0) ? ((a << (b + 1)) & 1) : ((a << (b - 32)) & 1)) ? DOT_FLAG_CARRY : 0; },
		0,
		BAD_ZERO_VALUE
	));
}

DFGNode BrokerImpl::NewOpaque(const std::string &szOpaqueRef, int dwOpaqueRefId) {
	DFGOpaqueImpl oOpaque(oGraph->dwNodeCounter, szOpaqueRef, dwOpaqueRefId); // enforces uniqueness of this node
	return FindNode(oOpaque);
}

DFGNode BrokerImpl::FindNode(const DFGNodeImpl &oNode) {
	DFGNode oGraphNode = oGraph->FindNode(oNode.idx());

	if (oGraphNode != nullptr) {
		return oGraphNode;
	} else {
		DFGNode oResult = oNode.copy();
		oResult->aInputNodes = oNode.aInputNodes;
		//oResult->aOutputNodes = oNode.aOutputNodes;
		//oResult->aInputNodesUnique = oNode.aInputNodesUnique;
		oGraph->InsertNode(oResult);
		return oResult;
	}
}

DFGNode BrokerImpl::FindNode(DFGNode &oNode) {
	DFGNode oGraphNode = oGraph->FindNode(oNode->idx());

	if (oGraphNode != nullptr) {
		return oGraphNode;
	} else {
		oGraph->InsertNode(oNode);
		return oNode;
	}
}

DFGNode BrokerImpl::MergeOperations(
	DFGNode & oNode1, DFGNode & oNode2,
	DFGNode *lpCarry, DFGNode *lpOverflow,
	NodeCreator *lpCreator,
	NodeDot *lpDot,
	unsigned int dwIdentityValue,
	unsigned int dwZeroValue
) {
	bool bConstant1, bConstant2;
	unsigned int dwValue1, dwValue2;

	bConstant1 = NODE_IS_CONSTANT(oNode1);
	bConstant2 = NODE_IS_CONSTANT(oNode2);

	if (bConstant1) { dwValue1 = oNode1->toConstant()->dwValue; }
	if (bConstant2) { dwValue2 = oNode2->toConstant()->dwValue; }

	if (bConstant1 && bConstant2) {
		dot_result_t stDotResult;
		lpDot(&stDotResult, dwValue1, dwValue2);
		if (lpCarry != NULL) { *lpCarry = NewConstant(!!(stDotResult.dwFlags & DOT_FLAG_CARRY)); }
		if (lpOverflow != NULL) { *lpOverflow = NewConstant(!!(stDotResult.dwFlags & DOT_FLAG_OVERFLOW)); }
		return NewConstant(stDotResult.dwValue);
	} else if (dwZeroValue != BAD_ZERO_VALUE && bConstant1 && dwValue1 == dwZeroValue) {
		if (lpCarry != NULL) { *lpCarry = NewConstant(0); }
		if (lpOverflow != NULL) { *lpOverflow = NewConstant(0); }
		return NewConstant(dwZeroValue);
	} else if (dwZeroValue != BAD_ZERO_VALUE && bConstant2 && dwValue2 == dwZeroValue) {
		if (lpCarry != NULL) { *lpCarry = NewConstant(0); }
		if (lpOverflow != NULL) { *lpOverflow = NewConstant(0); }
		return NewConstant(dwZeroValue);
	} else if (bConstant1 && dwValue1 == dwIdentityValue) {
		if (lpCarry != NULL) { *lpCarry = NewConstant(0); }
		if (lpOverflow != NULL) { *lpOverflow = NewConstant(0); }
		//return oNode2;
		return oNode2;
	} else if (bConstant2 && dwValue2 == dwIdentityValue) {
		if (lpCarry != NULL) { *lpCarry = NewConstant(0); }
		if (lpOverflow != NULL) { *lpOverflow = NewConstant(0); }
		//return oNode1;
		return oNode1;
	} else {
		DFGNode oResult = FindNode(lpCreator(oNode1, oNode2));
		if (lpCarry != NULL) { *lpCarry = NewCarry(oResult); }
		if (lpOverflow != NULL) { *lpOverflow = NewOverflow(oResult); }
		return oResult;
	}
}

DFGNode BrokerImpl::NewCarry(DFGNode &oNode) {
	DFGCarryImpl oCarry(oNode);
	return FindNode(oCarry);
}

DFGNode BrokerImpl::NewOverflow(DFGNode &oNode) {
	DFGOverflowImpl oOverflow(oNode);
	return FindNode(oOverflow);
}

DFGNode BrokerImpl::MergeOperationsCommutative(
	DFGNode & oNode1, DFGNode & oNode2,
	DFGNode *lpCarry, DFGNode *lpOverflow,
	node_type_t eType,
	NodeCreator * lpCreator,
	NodeDot * lpDot,
	unsigned int dwIdentityValue,
	unsigned int dwZeroValue
) {
	bool bNode1IsCorrectType = oNode1->eNodeType == eType;
	bool bNode2IsCorrectType = oNode2->eNodeType == eType;

	if (bNode1IsCorrectType || bNode2IsCorrectType) {
		DFGNode oTempNode;
		std::list<DFGNode>::iterator it;

		if (bNode1IsCorrectType && bNode2IsCorrectType) {
			oTempNode = oNode1->copy();
			oTempNode->aInputNodes = oNode1->aInputNodes;
			for (it = oNode2->aInputNodes.begin(); it != oNode2->aInputNodes.end(); it++) {
				oTempNode->aInputNodes.push_back(*it);
			}
		} else if (bNode1IsCorrectType) {
			oTempNode = oNode1->copy();
			oTempNode->aInputNodes = oNode1->aInputNodes;
			oTempNode->aInputNodes.push_back(oNode2);
		} else {
			oTempNode = oNode2->copy();
			oTempNode->aInputNodes = oNode2->aInputNodes;
			oTempNode->aInputNodes.push_front(oNode1);
		}

		dot_result_t stDotResult = { 0, dwIdentityValue };
		for (it = oTempNode->aInputNodes.begin(); it != oTempNode->aInputNodes.end(); ) {
			if (NODE_IS_CONSTANT(*it)) {
				lpDot(&stDotResult, stDotResult.dwValue, (*it)->toConstant()->dwValue);
				it = oTempNode->aInputNodes.erase(it);
			} else {
				it++;
			}
		}
		if (dwZeroValue != BAD_ZERO_VALUE && stDotResult.dwValue == dwZeroValue) {
			if (lpCarry != 0) { *lpCarry = NewConstant(0); }
			if (lpOverflow != 0) { *lpOverflow = NewConstant(0); }
			return NewConstant(dwZeroValue);
		} else if (stDotResult.dwValue != dwIdentityValue) {
			oTempNode->aInputNodes.push_back(NewConstant(stDotResult.dwValue));
		}
		if (oTempNode->aInputNodes.begin() == oTempNode->aInputNodes.end()) {
			if (lpCarry != 0) { *lpCarry = NewConstant(0); }
			if (lpOverflow != 0) { *lpOverflow = NewConstant(0); }
			return NewConstant(dwIdentityValue);
		} else if (++oTempNode->aInputNodes.begin() == oTempNode->aInputNodes.end()) { // only one element
			if (lpCarry != 0) { *lpCarry = NewConstant(0); }
			if (lpOverflow != 0) { *lpOverflow = NewConstant(0); }
			return *oTempNode->aInputNodes.begin();
		}
		DFGNode oResult = FindNode(oTempNode);
		if (lpCarry != 0) { *lpCarry = NewCarry(oResult); }
		if (lpOverflow != 0) { *lpOverflow = NewOverflow(oResult); }
		return oResult;
	}

	return MergeOperations(oNode1, oNode2, lpCarry, lpOverflow, lpCreator, lpDot, dwIdentityValue, dwZeroValue);
}

void BrokerImpl::Cleanup() {
	DFGraphImpl::iterator it;
	std::unordered_map<DFGNode, char> aScheduledForRemoval;
	std::unordered_map<DFGNode, std::unordered_map<DFGNode,char>> aNumChildrenRemoved;
	std::unordered_map<DFGNode, char>::iterator itRemove;

	for (it = oGraph->begin(); it != oGraph->end(); it++) {
		Cleanup_Impl(aScheduledForRemoval, aNumChildrenRemoved, it->second);
	}

	/* perform actual removal of nodes scheduled for removal */
	for (itRemove = aScheduledForRemoval.begin(); itRemove != aScheduledForRemoval.end(); itRemove++) {
		oGraph->RemoveNode(itRemove->first);
	}
}

void BrokerImpl::Cleanup_Impl(
	std::unordered_map<DFGNode, char> &aScheduledForRemoval,
	std::unordered_map<DFGNode, std::unordered_map<DFGNode,char>> &aNumChildrenRemoved,
	DFGNode &oNode
) {

	std::unordered_map<unsigned int, DFGNode>::iterator itArcUp;
	std::unordered_map<DFGNode, std::unordered_map<DFGNode, char>>::iterator itNum;

	if (aScheduledForRemoval.find(oNode) != aScheduledForRemoval.end()) {
		/* already scheduled for removal */
		return;
	}

	if (oNode->aOutputNodes.begin() != oNode->aOutputNodes.end()) {
		/* node's output seems to be used -> double-check */
		if ((itNum = aNumChildrenRemoved.find(oNode)) != aNumChildrenRemoved.end()) {
			if (itNum->second.size() != oNode->aOutputNodes.size()) {
				return; /* output usage is legit */
			} /* else fall through */
		} else {
			return;
		}
	}

	if (!ShouldCleanNode(oNode)) {
		return;
	}

	/* === removal part === */
	/* schedule this node for removal */
	aScheduledForRemoval.insert(std::pair<DFGNode, char>(oNode, 0));
	for (itArcUp = oNode->aInputNodesUnique.begin(); itArcUp != oNode->aInputNodesUnique.end(); itArcUp++) {
		if ((itNum = aNumChildrenRemoved.find(itArcUp->second)) == aNumChildrenRemoved.end()) {
			std::unordered_map<DFGNode, char> oList;
			oList.insert(std::pair<DFGNode, char>(oNode, 0));
			aNumChildrenRemoved.insert(std::pair<DFGNode, std::unordered_map<DFGNode, char>>(itArcUp->second, oList));
		} else {
			if (itNum->second.find(oNode) == itNum->second.end()) {
				itNum->second.insert(std::pair<DFGNode, char>(oNode, 0));
			}
		}
	}

	/* all parent nodes have lost a child
	 * in case a parent lost all its children, we should (re-)evaluate its removal once more
	 */
	for (itArcUp = oNode->aInputNodesUnique.begin(); itArcUp != oNode->aInputNodesUnique.end(); itArcUp++) {
		Cleanup_Impl(aScheduledForRemoval, aNumChildrenRemoved, itArcUp->second);
	}
}

std::string BrokerImpl::Export_Impl(
	std::map<DFGNode, std::pair<int, std::string>> &aExportMap,
	DFGNode oNode
) {
	std::map<DFGNode, std::pair<int, std::string>>::iterator it;
	std::list<DFGNode>::iterator itInput;
	std::stringstream szOutput;
	std::string szReturnValue;

	if (NODE_IS_CONSTANT(oNode) || NODE_IS_REGISTER(oNode) ||
		oNode->aOutputNodes.size() == 1 || (it = aExportMap.find(oNode)) == aExportMap.end()
	) {
		switch (oNode->eNodeType) {
		case NODE_TYPE_ADD: {
			szOutput << "(";
			for (itInput = oNode->aInputNodes.begin(); itInput != oNode->aInputNodes.end(); itInput++) {
				if (itInput != oNode->aInputNodes.begin()) {
					szOutput << "+";
				}
				szOutput << Export_Impl(aExportMap, *itInput);
			}
			szOutput << ")";
			break;
		}
		case NODE_TYPE_AND:
		case NODE_TYPE_CALL:
		case NODE_TYPE_CARRY:
		case NODE_TYPE_LOAD:
		case NODE_TYPE_MULT:
		case NODE_TYPE_OVERFLOW:
		case NODE_TYPE_STORE:
		case NODE_TYPE_XOR: {
			szOutput << oNode->mnemonic();
		_enum_brackets:
			szOutput << "(";
			for (itInput = oNode->aInputNodes.begin(); itInput != oNode->aInputNodes.end(); itInput++) {
				if (itInput != oNode->aInputNodes.begin()) {
					szOutput << ",";
				}
				szOutput << Export_Impl(aExportMap, *itInput);
			}
			szOutput << ")";
			break;
		}
		case NODE_TYPE_CONSTANT:
			szOutput << oNode->toConstant()->dwValue;
			break;
		case NODE_TYPE_OPAQUE:
			szOutput << "OPAQUE";
			if (oNode->toOpaque()->szOpaqueRef.length() != 0) {
				szOutput << "<";
				szOutput << oNode->toOpaque()->szOpaqueRef;
				szOutput << ">";
			}
			if (oNode->aInputNodes.size() != 0) {
				goto _enum_brackets;
			}
			break;
		case NODE_TYPE_REGISTER:
			szOutput << oNode->mnemonic();
			break;
		case NODE_TYPE_ROTATE:
			szOutput << "ROTATE";
			goto _enum_brackets;
		case NODE_TYPE_OR:
			szOutput << "OR";
			goto _enum_brackets;
		case NODE_TYPE_SHIFT:
			szOutput << "(";
			szOutput << Export_Impl(aExportMap, *oNode->aInputNodes.begin());
			if (
				NODE_IS_CONSTANT(*++oNode->aInputNodes.begin()) &&
				(int)(*++oNode->aInputNodes.begin())->toConstant()->dwValue < 0
			) {
				szOutput << " >> ";
				szOutput << std::to_string(-(*++oNode->aInputNodes.begin())->toConstant()->dwValue);
			} else {
				szOutput << " << ";
				szOutput << Export_Impl(aExportMap, *++oNode->aInputNodes.begin());
			}
			szOutput << ")";
			break;
		}

		if (NODE_IS_CONSTANT(oNode) || NODE_IS_REGISTER(oNode) || oNode->aOutputNodes.size() == 1) {
			return szOutput.str();
		} else {
			szReturnValue = "sub_" + std::to_string(aExportMap.size());
			aExportMap.insert(
				std::pair<DFGNode, std::pair<int, std::string>>(
					oNode, std::pair<int, std::string>(
						(int)aExportMap.size(), szOutput.str()
					)
				)
			);
			return szReturnValue;
		}
	}
	return "sub_" + std::to_string(it->second.first);
}

std::string BrokerImpl::Export() {
	DFGraphImpl::iterator it;
	std::stringstream szOutput;
	std::map<DFGNode, std::pair<int, std::string>> aExportMap;
	std::map<DFGNode, std::pair<int, std::string>>::iterator itMap;
	std::map<int, std::string> aExportMapSorted;
	std::map<int, std::string>::iterator itSort;

	for (it = oGraph->begin(); it != oGraph->end(); it++) {
		if (aExportMap.find(it->second) == aExportMap.end()) {
			Export_Impl(aExportMap, it->second);
		}
	}

	for (itMap = aExportMap.begin(); itMap != aExportMap.end(); itMap++) {
		aExportMapSorted.insert(std::pair<int, std::string>(itMap->second.first, itMap->second.second));
	}
	for (itSort = aExportMapSorted.begin(); itSort != aExportMapSorted.end(); itSort++) {
		szOutput << "sub_" << std::to_string(itSort->first);
		szOutput << ":";
		szOutput << itSort->second;
		szOutput << ";\n";
	}
	return szOutput.str();
}

CodeBrokerImpl::CodeBrokerImpl(
	Processor oProcessor,
	ThreadPool oThreadPool,
	unsigned long lpStartAddress,
	PathOracle oPathOracle
) :
	ThreadTaskResultImpl(THREAD_RESULT_TYPE_CODE_GRAPH),
	BrokerImpl(),
	oProcessor(oProcessor),
	oPathOracle(oPathOracle),
	oStatePredicate(Predicate::create()),
	oBacklog(BacklogDb::create()),
	oThreadPool(oThreadPool),
	lpStartAddress(lpStartAddress),
	dwMaxGraphSize(oPathOracle->MaxGraphSize()),
	dwMaxConsecutiveNoopInstructions(oPathOracle->MaxConsecutiveNoopInstructions()),
	dwMaxConstructionTime(oPathOracle->MaxConstructionTime()),
	dwMaxConditions(oPathOracle->MaxConditions()),
	dwNumConditions(0)
{
	qstring szFunctionName;
	API_LOCK();
	get_func_name(&szFunctionName, lpStartAddress);
	API_UNLOCK();
	if (szFunctionName.length() == 0) {
		std::stringstream oStream;
		oStream << "sub_";
		oStream << std::hex << lpStartAddress;
		szFunctionName = oStream.str().c_str();
	}
	this->szFunctionName = szFunctionName.c_str();
	oProcessor->initialize(CodeBroker::typecast(this));
}

bool CodeBrokerImpl::ScheduleBuild(
	Processor oProcessor,
	ThreadPool oThreadPool,
	unsigned long lpAddress,
	PathOracle oPathOracle,
	bool bOnlyIfResourceAvailable
) {
	CodeBroker oBuilder(CodeBroker::create(oProcessor, oThreadPool, lpAddress, oPathOracle == nullptr ? PathOracle::create(lpAddress) : oPathOracle));
	if (bOnlyIfResourceAvailable) {
		return oThreadPool->ScheduleIfResourceAvailable(oBuilder->toThreadTask(), (void*)lpAddress);
	}
	oThreadPool->Schedule(oBuilder->toThreadTask(), (void*)lpAddress);
	return true;
}

graph_process_t CodeBrokerImpl::IntroduceCondition(Condition &oCondition, unsigned long lpNextAddress) {
	//std::string szConditionStr(oCondition->expression(4));
	satisfied_t eSatisfied = oStatePredicate->IsSatisfied(oCondition, Broker::typecast(this));
	switch (eSatisfied) {
	case SATISFIED_ALWAYS:
		//oStatePredicate->MergeCondition(oCondition, Broker(this));
		//wc_debug("[*] always satisfied %s (normalized %s) @ 0x%lx\n", szConditionStr.c_str(), oCondition->expression(4).c_str(), lpCurrentAddress);
		return GRAPH_PROCESS_CONTINUE;
	case SATISFIED_NEVER:
		/* set conditions to false */
		//oStatePredicate->MergeCondition(oCondition, Broker(this));
		//wc_debug("[*] never satisfied %s (normalized %s) @ 0x%lx\n", szConditionStr.c_str(), oCondition->expression(4).c_str(), lpCurrentAddress);
		return GRAPH_PROCESS_SKIP;
	case SATISFIED_SOMETIMES:
		/*
		 * constraint solver is inconclusive
		 *  -> consult path oracle
		 */
		if (++dwNumConditions >= dwMaxConditions) {
			wc_debug("[*] max number of conditions exceeded @ 0x%lx\n", lpCurrentAddress);
			return GRAPH_PROCESS_INTERNAL_ERROR;
		}
		fork_policy_t eShouldFork = oPathOracle->ShouldFork(oBacklog, lpCurrentAddress);
		wc_debug("[*] path oracle says %s at conditional instruction @ 0x%lx\n",
			(eShouldFork == FORK_POLICY_TAKE_FALSE) ? "TAKE_FALSE" :
			((eShouldFork == FORK_POLICY_TAKE_TRUE) ? "TAKE_TRUE" : "TAKE_BOTH"),
			lpCurrentAddress
		);
		switch (eShouldFork) {
		case FORK_POLICY_TAKE_TRUE:
_true_case:
			if (oStatePredicate->MergeCondition(oCondition, Broker::typecast(this)) == MERGE_STATUS_INTERNAL_ERROR) {
				return GRAPH_PROCESS_INTERNAL_ERROR;
			}
			oBacklog->NewEntry(lpCurrentAddress, true);
			//wc_debug("[*] new condition introduced (0x%x): %s (%s | %s) new state: %s\n",
			//	lpCurrentAddress,
			//	szNewCondition.c_str(),
			//	szNormalized.c_str(),
			//	oCondition->expression(2).c_str(),
			//	oStatePredicate->expression(2).c_str()
			//);
			return GRAPH_PROCESS_CONTINUE;
		case FORK_POLICY_TAKE_FALSE:
			if (oStatePredicate->MergeCondition(oCondition->Negate(), Broker::typecast(this)) == MERGE_STATUS_INTERNAL_ERROR) {
				return GRAPH_PROCESS_INTERNAL_ERROR;
			}
			oBacklog->NewEntry(lpCurrentAddress, false);
			//wc_debug("[*] new condition introduced (0x%x): %s (%s | %s) new state: %s\n",
			//	lpCurrentAddress,
			//	szNewCondition.c_str(),
			//	szNormalized.c_str(),
			//	oCondition->expression(2).c_str(),
			//	oStatePredicate->expression(2).c_str()
			//);
			return GRAPH_PROCESS_SKIP;
		case FORK_POLICY_TAKE_BOTH:
			/* fork the current graph */
			Broker oTemp = fork();
			if (oTemp == nullptr) {
				goto _true_case;
			}
			CodeBroker oFork = oTemp->toCodeGraph();

			/* fork takes the false case, current builder takes true */
			if (oFork->oStatePredicate->MergeCondition(oCondition->Migrate(oFork->oGraph)->Negate(), oFork->toGeneric()) == MERGE_STATUS_INTERNAL_ERROR) {
				return GRAPH_PROCESS_INTERNAL_ERROR;
			}
			oFork->oBacklog->NewEntry(lpCurrentAddress, false);
			if (oStatePredicate->MergeCondition(oCondition, Broker::typecast(this)) == MERGE_STATUS_INTERNAL_ERROR) {
				return GRAPH_PROCESS_INTERNAL_ERROR;
			}
			oBacklog->NewEntry(lpCurrentAddress, true);
			//wc_debug("[*] new condition introduced (0x%x): %s (%s | %s) new state: %s forked state: %s\n",
			//	lpCurrentAddress,
			//	szNewCondition.c_str(),
			//	szNormalized.c_str(),
			//	oCondition->expression(2).c_str(),
			//	oStatePredicate->expression(2).c_str(),
			//	oFork->oStatePredicate->expression(2).c_str()
			//);
			oThreadPool->Schedule(oFork->toThreadTask(), (void*)lpNextAddress);
			return GRAPH_PROCESS_CONTINUE;
		}
	}
	wc_debug("[-] should never get here\n");
	return GRAPH_PROCESS_INTERNAL_ERROR;
}

int CodeBrokerImpl::MaxCallDepth() {
	return oPathOracle->MaxCallDepth();
}

bool CodeBrokerImpl::ShouldCleanNode(DFGNode &oNode) {
	if (NODE_IS_CALL(oNode)) {
		/* don't remove CALLs */
		return false;
	}

	if (NODE_IS_STORE(oNode)) {
		/*
		 * STORE that was overwritten -> remove
		 */
		DFGNode oMemoryLocation = *++oNode->aInputNodes.begin();
		std::unordered_map<unsigned int, DFGNode>::iterator it;
		if ((it = aMemoryMap.find(oMemoryLocation->dwNodeId)) == aMemoryMap.end() ||
			it->second != *oNode->aInputNodes.begin()
		) {
			return true;
		}
	}

	if (!oProcessor->ShouldClean(oNode)) {
		/*
		 * processor-specific policy determines we should keep it
		 * e.g. return value or STORE outside of stack region
		 */
		return false;
	}

	return true;
}

void CodeBrokerImpl::Build_Impl(unsigned long lpAddress) {
	DWORD dwStartTime = GetTickCount();
	unsigned int dwLastNumNodes = 0;
	unsigned int dwNumIterationsWithoutProgress = 0;
	for (;;) {
		if (oGraph->size() > dwMaxGraphSize) {
			wc_debug("[-] max graph size exceeded for function %s (%s) construction time=%fs\n", szFunctionName.c_str(), oStatePredicate->expression(2).c_str(), (GetTickCount() - dwStartTime));
			goto _analysis_error;
		}
		if ((oGraph->size() - dwLastNumNodes) == 0) {
			if (++dwNumIterationsWithoutProgress == dwMaxConsecutiveNoopInstructions) {
				wc_debug("[-] %d instructions were processed without any contribution to the DFG @ %s (%s)\n", dwMaxConsecutiveNoopInstructions, szFunctionName.c_str(), oStatePredicate->expression(2).c_str());
				goto _analysis_error;
			}
		} else {
			dwNumIterationsWithoutProgress = 0;
		}
		dwLastNumNodes = oGraph->size();
		if ((GetTickCount() - dwStartTime) > dwMaxConstructionTime) {
			wc_debug("[-] max construction time exceeded for function %s (%s)\n", szFunctionName.c_str(), oStatePredicate->expression(2).c_str());
			goto _analysis_error;
		}
		unsigned long lpNextAddress;
		lpCurrentAddress = lpAddress;
		processor_status_t eStatus = oProcessor->instruction(CodeBroker::typecast(this), &lpNextAddress, lpAddress);
		if (eStatus == PROCESSOR_STATUS_OK) {
			lpAddress = lpNextAddress;
			continue;
		}
		break;
	}

	wc_debug("[*] size of graph : %llu\n", oGraph->size());
	Cleanup();
	wc_debug("[*] size of graph after cleanup : %llu\n", oGraph->size());
	DWORD dwEndTime = GetTickCount();
	wc_debug("[*] total construction time : %fs\n", ((double)(dwEndTime - dwStartTime) / 1000));

	//return oGraph;
	oThreadPool->YieldResult(ThreadTaskResult::typecast(this));
	return;

_analysis_error:
	oThreadPool->YieldResult(ThreadTaskResult::typecast(EmptyAnalysisResult::create()));
}

Broker CodeBrokerImpl::fork() {
	std::unordered_map<unsigned int, DFGNode>::iterator it;

	DFGraph oGraphFork = oGraph->fork();
	if (oGraphFork == nullptr) {
		return nullptr;
	}
	// use default constructor
	// so that we don't have to copy all members manually
	CodeBroker oFork(CodeBroker::create(*this));
	/* copy the graph */
	oFork->oGraph = oGraphFork;
	/* fork the processor module and migrate to the graph copy*/
	oFork->oProcessor = oProcessor->Migrate(oFork->oGraph);
	/* fork the current state predicate and migrate to the graph copy */
	oFork->oStatePredicate = oStatePredicate->Migrate(oFork->oGraph);
	/* fork the backlog */
	oFork->oBacklog = oBacklog->fork();
	//oFork->oPathOracle = PathOracle::create(*oFork->oPathOracle);

	for (it = oFork->aMemoryMap.begin(); it != oFork->aMemoryMap.end(); it++) {
		it->second = oFork->oGraph->FindNode(it->second->dwNodeId);
	}

	return oFork->toGeneric();
}

SignatureBrokerImpl::SignatureBrokerImpl()
 : BrokerImpl() {

}

Broker SignatureBrokerImpl::fork() {
	DFGraph oGraphFork = oGraph->fork();
	if (oGraphFork == nullptr) {
		return nullptr;
	}
	SignatureBroker oFork(SignatureBroker::create(*this));
	oFork->oGraph = oGraphFork;
	return oFork->toGeneric();
}

bool SignatureBrokerImpl::ShouldCleanNode(DFGNode &oNode) {
	return aNodesToKeep.find(oNode) == aNodesToKeep.end();
}