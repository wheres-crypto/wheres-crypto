#include <idp.hpp>
#include <Windows.h>
#include <sstream>

#include "common.hpp"
#include "SignatureEvaluator.hpp"
#include "Broker.hpp"
#include "DFGraph.hpp"
#include "DFGNode.hpp"
#include "Predicate.hpp"

inline opaque_node_assign_t OpaqueAssignmentImpl::Assign(DFGNode oSignatureNode, DFGNode oCandidate) {
	if (NODE_IS_OPAQUE(oSignatureNode)) {
		int dwOpaqueRefId = oSignatureNode->toOpaque()->dwOpaqueRefId;
		if (dwOpaqueRefId != -1) {
			if ((*this)[dwOpaqueRefId].eNodeType == NODE_TYPE_UNKNOWN) {
				(*this)[dwOpaqueRefId] = node_type_spec_t(oCandidate);
				return OPAQUE_NODE_ASSIGN_OK;
			}
			else if ((*this)[dwOpaqueRefId].Matches(oCandidate)) {
				return OPAQUE_NODE_ASSIGN_ALREADY_SET;
			}
			return OPAQUE_NODE_ASSIGN_NOK;
		}
	}
	return OPAQUE_NODE_ASSIGN_ALREADY_SET;
}

inline opaque_node_assign_t OpaqueAssignmentImpl::AssignPossible(DFGNode oSignatureNode, DFGNode oCandidate) {
	if (NODE_IS_OPAQUE(oSignatureNode)) {
		int dwOpaqueRefId = oSignatureNode->toOpaque()->dwOpaqueRefId;
		if (dwOpaqueRefId != -1) {
			if ((*this)[dwOpaqueRefId].eNodeType == NODE_TYPE_UNKNOWN) {
				return OPAQUE_NODE_ASSIGN_OK;
			} else if ((*this)[dwOpaqueRefId].Matches(oCandidate)) {
				return OPAQUE_NODE_ASSIGN_ALREADY_SET;
			}
			return OPAQUE_NODE_ASSIGN_NOK;
		}
	}
	return OPAQUE_NODE_ASSIGN_OK;
}

inline void OpaqueAssignmentImpl::Unassign(DFGNode oSignatureNode) {
	if (NODE_IS_OPAQUE(oSignatureNode)) {
		int dwOpaqueRefId = oSignatureNode->toOpaque()->dwOpaqueRefId;
		if (dwOpaqueRefId != -1) {
			(*this)[dwOpaqueRefId].eNodeType = NODE_TYPE_UNKNOWN;
		}
	}
}
void SignatureEvaluatorImpl::PushFlagged() {
	aStack.push_back(oFlagged->copy());
}

void SignatureEvaluatorImpl::PopFlagged() {
	if (aStack.begin() != aStack.end()) {
		oFlagged = aStack.back();
		aStack.pop_back();
	}
}

void SignatureEvaluatorImpl::ClearFlagged() {
	if (aStack.begin() != aStack.end()) {
		aStack.pop_back();
	}
}

assignment_t SignatureEvaluatorImpl::Pass1Recurse(const DFGNode& oSignatureNode, const DFGNode& oCodeNode) {
	assignment_t eLookup = GetAssignment(oSignatureNode->dwNodeId, oCodeNode->dwNodeId);
	if (eLookup == ASSIGNMENT_UNEXPLORED) {
		if (IsCandidate(oSignatureNode, oCodeNode)) {
			std::unordered_map<unsigned int, DFGNode>::const_iterator itE, itC;
			std::list<DFGNode>::const_iterator itOrderE, itOrderC;
			assignment_t eResult(ASSIGNMENT_UNDEFINED);

			if ((GetTickCount() - dwStartTime) > dwMaxEvaluationTime) {
				return ASSIGNMENT_INVALID;
			}

			if (
				!NODE_IS_STORE(oSignatureNode) &&
				!NODE_IS_LOAD(oSignatureNode) &&
				!NODE_IS_SHIFT(oSignatureNode) &&
				!NODE_IS_ROTATE(oSignatureNode)
			) {
				/*
				 * order of input nodes is not important
				 * (forall S : exists V)
				 */
				for (itE = oSignatureNode->aInputNodesUnique.begin(); itE != oSignatureNode->aInputNodesUnique.end(); itE++) {
					bool bExists = false;
					for (itC = oCodeNode->aInputNodesUnique.begin(); itC != oCodeNode->aInputNodesUnique.end(); itC++) {
						if (Pass1Recurse(itE->second, itC->second) == ASSIGNMENT_UNDEFINED) {
							bExists = true;
							/*
							 * don't break here, we want to map out all possible assignments
							 * until oSignatureNode <-> oCodeNode is proven invalid
							 */
						}
					}
					if (!bExists) {
						eResult = ASSIGNMENT_INVALID;
						break;
					}
				}
			} else {
				/*
				 * order is important
				 */
				if (oSignatureNode->aInputNodes.size() == oCodeNode->aInputNodes.size()) {
					for (
						itOrderE = oSignatureNode->aInputNodes.begin(), itOrderC = oCodeNode->aInputNodes.begin();
						itOrderE != oSignatureNode->aInputNodes.end();
						itOrderE++, itOrderC++
					) {
						if (Pass1Recurse(*itOrderE, *itOrderC) == ASSIGNMENT_INVALID) {
							eResult = ASSIGNMENT_INVALID;
							break;
						}
					}
				}
			}
			Assign(oSignatureNode->dwNodeId, oCodeNode->dwNodeId, eResult, true);
			return eResult;
		} else {
			Assign(oSignatureNode->dwNodeId, oCodeNode->dwNodeId, ASSIGNMENT_INVALID, true);
			return ASSIGNMENT_INVALID;
		}
	} else {
		return eLookup;
	}
}

bool SignatureEvaluatorImpl::PruneFlagged() {
	DFGraphImpl::const_iterator itP;
	bool bAgain;

	do {
		bAgain = false;
		for (itP = oSignatureGraph->oGraph->begin(); itP != oSignatureGraph->oGraph->end(); itP++) {
			DFGNode oSignatureNode = itP->second;
			sparse_matrix_iterator_t itCodeNode = oFlagged->FirstCandidate(oSignatureNode->dwNodeId);

			/*
			 * no candiate for this node exists -> bail
			 */
			if (itCodeNode.dwCodeNodeId == 0xffffffff) {
				return false;
			}

			while (itCodeNode.dwCodeNodeId != 0xffffffff) {
				/* lookup object for candidate node id since as we need it to lookup its inputs */
				DFGNode oCodeNode = oCodeGraph->oGraph->FindNode(itCodeNode.dwCodeNodeId);
				std::list<DFGNode>::const_iterator itOrderEN;
				std::list<DFGNode>::const_iterator itOrderCN;
				std::unordered_map<unsigned int, DFGNode>::const_iterator itEN, itCN;
				std::unordered_map<unsigned int, DFGNode>::const_iterator itOutEN, itOutCN;

				if (
					/* the following node types are sensitive to input order */
					NODE_IS_STORE(oSignatureNode) ||
					NODE_IS_LOAD(oSignatureNode) ||
					NODE_IS_SHIFT(oSignatureNode) ||
					NODE_IS_ROTATE(oSignatureNode)
				) {
					if (oSignatureNode->aInputNodes.size() != oCodeNode->aInputNodes.size()) {
						goto _invalid;
					}
					for (
						itOrderEN = oSignatureNode->aInputNodes.begin(), itOrderCN = oCodeNode->aInputNodes.begin();
						itOrderEN != oSignatureNode->aInputNodes.end();
						itOrderEN++, itOrderCN++
					) {
						assignment_t eAssignment = GetAssignment((*itOrderEN)->dwNodeId, (*itOrderCN)->dwNodeId);
						if (eAssignment == ASSIGNMENT_VALID || eAssignment == ASSIGNMENT_UNDEFINED) {
							continue;
						}
						goto _invalid;
					}
				} else {
					/* input order agnostic node */
					if (oSignatureNode->aInputNodes.size() > oCodeNode->aInputNodes.size()) {
						goto _invalid;
					}
					for (itEN = oSignatureNode->aInputNodesUnique.begin();
						itEN != oSignatureNode->aInputNodesUnique.end();
						itEN++
					) {
						for (itCN = oCodeNode->aInputNodesUnique.begin(); itCN != oCodeNode->aInputNodesUnique.end(); itCN++) {
							assignment_t eAssignment = GetAssignment(itEN->first, itCN->first);
							if (eAssignment == ASSIGNMENT_VALID || eAssignment == ASSIGNMENT_UNDEFINED) {
								break;
							}
						}
						if (itCN == oCodeNode->aInputNodesUnique.end()) {
							goto _invalid;
						}
					}
				}

				if (false) { _invalid:
					Assign(oSignatureNode->dwNodeId, oCodeNode->dwNodeId, ASSIGNMENT_INVALID);
					bAgain = true;
					goto _continue;
				}

				for (itOutEN = oSignatureNode->aOutputNodes.begin(); itOutEN != oSignatureNode->aOutputNodes.end(); itOutEN++) {
					for (itOutCN = oCodeNode->aOutputNodes.begin(); itOutCN != oCodeNode->aOutputNodes.end(); itOutCN++) {
						assignment_t eAssignment = GetAssignment(itOutEN->first, itOutCN->first);
						if (eAssignment == ASSIGNMENT_VALID || eAssignment == ASSIGNMENT_UNDEFINED) {
							break;
						}
					}
					if (itOutCN == oCodeNode->aOutputNodes.end()) {
						goto _invalid;
					}
				}

_continue:
				itCodeNode = oFlagged->NextCandidate(itCodeNode);
				if ((GetTickCount() - dwStartTime) > dwMaxEvaluationTime) {
					return false;
				}
			}
		}
	} while (bAgain);

	return true;
}

bool SignatureEvaluatorImpl::Pass2Recurse(
	FlagMap oFlagMap,
	DFGraphImpl::const_iterator itE
) {
	if (itE == oSignatureGraph->oGraph->end()) {
		DFGraphImpl::const_iterator itP;
		/*
		 * test for isomorphism
		 */
		oMapping->clear();

		for (itP = oSignatureGraph->oGraph->begin(); itP != oSignatureGraph->oGraph->end(); itP++) {
			/*
			 * go through all nodes in the expression
			 */
			DFGNode oSignatureNode = itP->second;
			sparse_matrix_iterator_t itCodeNode = oFlagged->FirstCandidate(oSignatureNode->dwNodeId);
			if (itCodeNode.dwCodeNodeId == 0xffffffff) {
				return false;
			}
			DFGNode oCodeNode = oCodeGraph->oGraph->FindNode(itCodeNode.dwCodeNodeId);
			/*
			 * oSignatureNode maps to oCodeNode
			 * below we check for every intput E to eSignatureNode,
			 *   that E maps to an input to oCodeNode
			 */
			std::list<DFGNode>::const_iterator itOrderEN;
			std::list<DFGNode>::const_iterator itOrderCN;
			std::unordered_map<unsigned int, DFGNode>::const_iterator itEN;
			std::unordered_map<unsigned int, DFGNode>::const_iterator itCN;

			if (
				/* the following node types are sensitive to input order */
				NODE_IS_STORE(oSignatureNode) ||
				NODE_IS_LOAD(oSignatureNode) ||
				NODE_IS_SHIFT(oSignatureNode) ||
				NODE_IS_ROTATE(oSignatureNode)
			) {
				for (
					itOrderEN = oSignatureNode->aInputNodes.begin(), itOrderCN = oCodeNode->aInputNodes.begin();
					itOrderEN != oSignatureNode->aInputNodes.end();
					itOrderEN++, itOrderCN++
				) {
					assignment_t eAssignment = GetAssignment((*itOrderEN)->dwNodeId, (*itOrderCN)->dwNodeId);
					if (!(eAssignment == ASSIGNMENT_VALID || eAssignment == ASSIGNMENT_UNDEFINED)) {
						goto _invalid;
					}
				}
			} else {
				for (itEN = oSignatureNode->aInputNodesUnique.begin(); itEN != oSignatureNode->aInputNodesUnique.end(); itEN++) {
					for (itCN = oCodeNode->aInputNodesUnique.begin(); itCN != oCodeNode->aInputNodesUnique.end(); itCN++) {
						assignment_t eAssignment = GetAssignment(itEN->first, itCN->first);
						if (eAssignment == ASSIGNMENT_VALID || eAssignment == ASSIGNMENT_UNDEFINED) {
							break;
						}
					}
					if (itCN == oCodeNode->aInputNodesUnique.end()) {
						goto _invalid;
					}
				}
			}

			if (false) { _invalid:
				return false;
			}

			oMapping->Assign(oSignatureNode, oCodeNode);
		}
		return true;
	} else {
		DFGNode oSignatureNode = itE->second;

		// only explore if the nodes are candidates for matching and the
		// column has not been assigned yet.
		sparse_matrix_iterator_t it = oFlagged->FirstCandidate(oSignatureNode->dwNodeId);
		if (it.dwCodeNodeId == 0xffffffff) {
			/* this node has no candidate */
			return false;
		}
		if (oFlagged->NextCandidate(it).dwCodeNodeId == 0xffffffff) {
			/*
			 * single candidate, may not need to push oSparseMatrix and prune
			 * thus, we differentiate between the two
			 */
			if (oFlagMap->IsAssigned(it.dwCodeNodeId)) {
				/* candidate is already assigned to a different signature node */
				return false;
			}
			opaque_node_assign_t eAssignStatus = OPAQUE_NODE_ASSIGN_ALREADY_SET;
			if (NODE_IS_OPAQUE(oSignatureNode) &&
				(eAssignStatus = oOpaqueAssignment->Assign(
					oSignatureNode,
					oCodeGraph->oGraph->FindNode(it.dwCodeNodeId)
				)) == OPAQUE_NODE_ASSIGN_NOK
			) {
				/*
				 * Signature node type is opaque,
				 * but currently assigned to a different node type
				 */
				return false;
			} else if (eAssignStatus == OPAQUE_NODE_ASSIGN_OK) {
				/*
				 * Opaque signature node type has just been assigned,
				 * below we invalidate all candidates for the same opaque type ref but a different type
				 */
				PushFlagged();
				int dwOpaqueRefId = oSignatureNode->toOpaque()->dwOpaqueRefId;
				node_type_spec_t oSpec = (*oOpaqueAssignment)[dwOpaqueRefId];
				std::multimap<int, unsigned int>::iterator itOpaque;

				for (
					itOpaque = aOpaqueIdToNode.find(dwOpaqueRefId);
					itOpaque != aOpaqueIdToNode.end() && itOpaque->first == dwOpaqueRefId;
					itOpaque++
				) {
					sparse_matrix_iterator_t itInner = oFlagged->FirstCandidate(itOpaque->second);
					while (itInner.dwCodeNodeId != 0xffffffff) {
						if (!oSpec.Matches(oCodeGraph->oGraph->FindNode(itInner.dwCodeNodeId))) {
							Assign(
								itOpaque->second,
								itInner.dwCodeNodeId,
								ASSIGNMENT_INVALID
							);
						}
						itInner = oFlagged->NextCandidate(itInner);
					}
				}

				/* propagate invalidation to neighbors */
				if (PruneFlagged()) {
					oFlagMap->Assign(it.dwCodeNodeId);
					DFGraphImpl::const_iterator itEP(itE);
					itEP++;
					if (Pass2Recurse(oFlagMap, itEP)) {
						return true;
					}
					oFlagMap->Unassign(it.dwCodeNodeId);
				}
				PopFlagged();
				oOpaqueAssignment->Unassign(oSignatureNode);
				return false;
			} else {
				Assign(oSignatureNode->dwNodeId, it.dwCodeNodeId, ASSIGNMENT_VALID);
				oFlagMap->Assign(it.dwCodeNodeId);
				DFGraphImpl::const_iterator itEP(itE);
				itEP++;
				if (Pass2Recurse(oFlagMap, itEP)) {
					return true;
				}
				oFlagMap->Unassign(it.dwCodeNodeId);
				Assign(oSignatureNode->dwNodeId, it.dwCodeNodeId, ASSIGNMENT_UNDEFINED);
				return false;
			}
		} else {
			SparseMatrix oCurrentLevel = oFlagged;
			while (it.dwCodeNodeId != 0xffffffff) { /* iterate candidates */
				/* unassigned nodes only */
				if (!oFlagMap->IsAssigned(it.dwCodeNodeId)) {
					/*
					 * attempt to claim opaque node type
					 */
					opaque_node_assign_t eAssignStatus = OPAQUE_NODE_ASSIGN_ALREADY_SET;
					if (NODE_IS_OPAQUE(oSignatureNode) && 
						(eAssignStatus = oOpaqueAssignment->Assign(
							oSignatureNode,
							oCodeGraph->oGraph->FindNode(it.dwCodeNodeId)
						)) == OPAQUE_NODE_ASSIGN_NOK
					) {
						/*
						 * Signature node type is opaque,
						 * but currently assigned to a different node type
						 */
						goto _continue;
					}

					/* unflag all possible assignments, except current candidate */
					PushFlagged();
					sparse_matrix_iterator_t itInner = oFlagged->FirstCandidate(oSignatureNode->dwNodeId);
					while (itInner.dwCodeNodeId != 0xffffffff) {
						Assign(
							oSignatureNode->dwNodeId,
							itInner.dwCodeNodeId,
							it.dwCodeNodeId == itInner.dwCodeNodeId ? ASSIGNMENT_VALID : ASSIGNMENT_INVALID
						);
						itInner = oFlagged->NextCandidate(itInner);
					}

					if (eAssignStatus == OPAQUE_NODE_ASSIGN_OK) {
						/*
						 * Opaque signature node type has just been assigned,
						 * invalid candidates
						 */
						int dwOpaqueRefId = oSignatureNode->toOpaque()->dwOpaqueRefId;
						node_type_spec_t oSpec = (*oOpaqueAssignment)[dwOpaqueRefId];
						std::multimap<int, unsigned int>::iterator itOpaque;

						for (
							itOpaque = aOpaqueIdToNode.find(dwOpaqueRefId);
							itOpaque != aOpaqueIdToNode.end() && itOpaque->first == dwOpaqueRefId;
							itOpaque++
						) {
							sparse_matrix_iterator_t itInner = oFlagged->FirstCandidate(itOpaque->second);
							while (itInner.dwCodeNodeId != 0xffffffff) {
								if(!oSpec.Matches(oCodeGraph->oGraph->FindNode(itInner.dwCodeNodeId))) {
									Assign(
										itOpaque->second,
										itInner.dwCodeNodeId,
										ASSIGNMENT_INVALID
									);
								}
								itInner = oFlagged->NextCandidate(itInner);
							}
						}
					}

					if (PruneFlagged()) {
						oFlagMap->Assign(it.dwCodeNodeId);
						DFGraphImpl::const_iterator itEP(itE);
						itEP++;
						if (Pass2Recurse(oFlagMap, itEP)) {
							return true;
						}
						oFlagMap->Unassign(it.dwCodeNodeId);
					}
					PopFlagged();
					if (eAssignStatus == OPAQUE_NODE_ASSIGN_OK) {
						oOpaqueAssignment->Unassign(oSignatureNode);
					}
				}
_continue:
				it = oCurrentLevel->NextCandidate(it);

				if ((GetTickCount() - dwStartTime) > dwMaxEvaluationTime) {
					return false;
				}
			}
			return false;
		}
	}
}

bool SignatureEvaluatorImpl::Evaluate(AbstractEvaluationResult *lpOutput) {
	DFGraphImpl::iterator itEx;
	DFGraphImpl::iterator itCo;
	SignatureDefinitionImpl::iterator itSig;
	FlagMap oFlagMap;
	bool bEvaluationResult = false;

	dwStartTime = GetTickCount();
	for (itSig = oSignatureDefinition->begin(); itSig != oSignatureDefinition->end(); itSig++) {
		oSignatureGraph = (*itSig)->toGeneric(); // point oSignatureGraph to the current variant
		oFlagged = SparseMatrix::create();
		oMapping = AssignmentMap::create();
		oOpaqueAssignment = OpaqueAssignment::create(oSignatureGraph->toSignatureGraph()->dwNumOpaqueRefs);
		aStack.clear();
		aOpaqueIdToNode.clear();

		DWORD dwVariantStartTime = GetTickCount();
		for (itEx = oSignatureGraph->oGraph->begin(); itEx != oSignatureGraph->oGraph->end(); itEx++) {
			bool bExists = false;

			if (NODE_IS_OPAQUE(itEx->second)) {
				int dwOpaqueRef = DFGOpaque::typecast(itEx->second)->dwOpaqueRefId;
				if (dwOpaqueRef != -1) {
					aOpaqueIdToNode.insert(std::pair<int, unsigned int>(dwOpaqueRef, itEx->second->dwNodeId));
				}
			}

			if (itEx->second->aOutputNodes.begin() != itEx->second->aOutputNodes.end()) {
				// this node has output nodes -> will be covered by recursive traversal of child
				continue;
			}

			/*
			 * pass 1: enumerate all possible assignments
			 */
			for (itCo = oCodeGraph->oGraph->begin(); itCo != oCodeGraph->oGraph->end(); itCo++) {
				if (Pass1Recurse(itEx->second, itCo->second) == ASSIGNMENT_UNDEFINED) {
					bExists = true;
				}
			}

			if ((GetTickCount() - dwStartTime) > dwMaxEvaluationTime) {
				goto _time_exceeded;
			}

			if (!bExists) {
				goto _next;
			}
		}

		oFlagged->CleanInvalid();
		if (!PruneFlagged()) {
			goto _next;
		}
		oFlagMap = FlagMap::create();
		bEvaluationResult = Pass2Recurse(oFlagMap, oSignatureGraph->oGraph->begin());

_next:
		DWORD dwVariantEndTime = GetTickCount();
		wc_debug("[*] time taken to evalutate %s (%.1500s) against %s (variant %s) found=%d: %fs\n",
			oCodeGraph->toCodeGraph()->szFunctionName.c_str(),
			oCodeGraph->toCodeGraph()->oStatePredicate->expression(2).c_str(),
			oSignatureDefinition->szIdentifier.c_str(),
			oSignatureGraph->toSignatureGraph()->szVariant.c_str(),
			bEvaluationResult,
			((double)(dwVariantEndTime - dwVariantStartTime) / 1000)
		);
		if (bEvaluationResult) {
			break;
		}
		if ((GetTickCount() - dwStartTime) > dwMaxEvaluationTime) {
			goto _time_exceeded;
		}
	}

	if ((GetTickCount() - dwStartTime) > dwMaxEvaluationTime) {
_time_exceeded:
		wc_debug("[-] max evaluation time exceeded for function %s (%s), signature : %s\n",
			this->oCodeGraph->toCodeGraph()->szFunctionName.c_str(),
			this->oCodeGraph->toCodeGraph()->oStatePredicate->expression(2).c_str(),
			oSignatureDefinition->szIdentifier.c_str()
		);
	}

	DWORD dwEndTime = GetTickCount();
	wc_debug("[*] total time taken to evalutate %s (%.1500s) against %s found=%d: %fs\n",
		oCodeGraph->toCodeGraph()->szFunctionName.c_str(),
		oCodeGraph->toCodeGraph()->oStatePredicate->expression(2).c_str(),
		oSignatureDefinition->szIdentifier.c_str(),
		bEvaluationResult,
		((double)(dwEndTime - dwStartTime) / 1000)
	);

	*lpOutput = SignatureEvaluationResult::create(
		SignatureEvaluator::typecast(this),
		oCodeGraph,
		bEvaluationResult ? EVALUATION_RESULT_MATCH_FOUND : EVALUATION_RESULT_NO_MATCH_FOUND,
		bEvaluationResult ? oSignatureGraph : nullptr,
		bEvaluationResult ? oMapping : nullptr
	)->toAbstract();

	/* unref everything */
	oSignatureGraph = nullptr;
	oMapping = nullptr;
	oFlagged = nullptr;
	aStack.clear();
	aOpaqueIdToNode.clear();

	return bEvaluationResult;
}

bool SignatureEvaluatorImpl::IsCandidate(const DFGNode &oSignatureNode, const DFGNode &oCodeNode) {
	// flag the current node so we know it's being processed
	if (NODE_IS_CONSTANT(oSignatureNode)) {
		return oSignatureNode->eNodeType == oCodeNode->eNodeType &&
			((DFGNode)oSignatureNode)->toConstant()->dwValue == ((DFGNode)oCodeNode)->toConstant()->dwValue;
	} else if (NODE_IS_OPAQUE(oSignatureNode)) {
		return true;
	} else {
		return oSignatureNode->eNodeType == oCodeNode->eNodeType;
	}

	return true;
}

inline node_type_spec_t::node_type_spec_t(const DFGNode & oNode) {
	eNodeType = oNode->eNodeType;
	switch (eNodeType) {
	case NODE_TYPE_CONSTANT:
		dwValue = DFGConstant::typecast(oNode)->dwValue;
		break;
	case NODE_TYPE_REGISTER:
		bRegister = DFGRegister::typecast(oNode)->bRegister;
		break;
	case NODE_TYPE_CALL:
		lpAddress = DFGCall::typecast(oNode)->lpAddress;
		break;
	}
}

inline bool node_type_spec_t::Matches(const DFGNode & oNode) {
	if (eNodeType != oNode->eNodeType) {
		return false;
	}
	switch (eNodeType) {
	case NODE_TYPE_CONSTANT:
		return dwValue == DFGConstant::typecast(oNode)->dwValue;
	case NODE_TYPE_REGISTER:
		return bRegister == DFGRegister::typecast(oNode)->bRegister;
	case NODE_TYPE_CALL:
		return lpAddress == DFGCall::typecast(oNode)->lpAddress;
	default:
		return true;
	}
}

bool SignatureEvaluationResultImpl::Mark(DFGNode & oCodeNode) {
	return oMapping != nullptr && oMapping->Lookup(oCodeNode) != nullptr;
}

std::string SignatureEvaluationResultImpl::Label() {
	std::stringstream szLabel;
	szLabel << oSignatureGraph->toSignatureGraph()->szIdentifier;
	if (oSignatureGraph->toSignatureGraph()->szVariant.length()) {
		szLabel << " (Variant ";
		szLabel << oSignatureGraph->toSignatureGraph()->szVariant;
		szLabel << ")";
	}
	return szLabel.str();
}
