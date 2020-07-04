#include "common.hpp"
#include "BlockPermutationEvaluator.hpp"
#include "Broker.hpp"
#include "DFGraph.hpp"
#include "Predicate.hpp"

#include <vector>
#include <algorithm>
#include <sstream>
#include <Windows.h>

typedef struct LoadSpec {
	inline LoadSpec(const DFGNode &oNode, unsigned int dwConstant) : oNode(oNode), dwConstant(dwConstant) { }
	DFGNode oNode;
	unsigned int dwConstant;
} LoadSpec;

class SpecMap : public std::unordered_map<std::string, std::list<LoadSpec>> {
public:
	inline SpecMap() { }
	void Insert(const std::string &szAddSpec, const LoadSpec &oLoadSpec) {
		SpecMap::iterator it = find(szAddSpec);
		if (it == end()) {
			insert(std::pair<std::string, std::list<LoadSpec>>(szAddSpec, std::list<LoadSpec>(1, oLoadSpec)));
		} else {
			std::list<LoadSpec>::iterator itNested;
			for (itNested = it->second.begin(); itNested != it->second.end(); itNested++) {
				if (itNested->dwConstant > oLoadSpec.dwConstant) {
					break;
				}
			}
			it->second.insert(itNested, oLoadSpec);
		}
	}
};

bool BlockPermutationEvaluatorImpl::Evaluate(AbstractEvaluationResult * lpOutput) {
	DFGraphImpl::iterator it;
	SpecMap aSpecMap;
	SpecMap::iterator itSpec;
	std::unordered_map<DFGNode, std::list<NodeTriplet>> aCandidates;
	std::unordered_map<DFGNode, std::list<NodeTriplet>>::iterator itCand;
	this->oEvalCache = SparseMatrix::create();

	dwStartTime = GetTickCount();

	for (it = oCodeGraph->oGraph->begin(); it != oCodeGraph->oGraph->end(); it++) {
		if (NODE_IS_LOAD(it->second)) {
			std::string szNodeIds;
			unsigned int dwConstant;
			SpecString(&szNodeIds, &dwConstant, *it->second->aInputNodes.begin());
			aSpecMap.Insert(szNodeIds, LoadSpec(it->second, dwConstant));
		}
	}

	for (itSpec = aSpecMap.begin(); itSpec != aSpecMap.end(); itSpec++) {
		std::list<LoadSpec>::iterator itOne, itTwo, itThree;
		std::unordered_map<int, char> aDistances;
		if (itSpec->second.size() < 3) {
			continue;
		}

		itOne = itSpec->second.begin();

		while (itOne != itSpec->second.end()) {
			itTwo = itOne;
			itTwo++;
			while(itTwo != itSpec->second.end()) {
				int dwDiff = itTwo->dwConstant - itOne->dwConstant;
				if (dwDiff < 16) {
					goto _continue_two;
				} else if (aDistances.find(dwDiff) != aDistances.end()) {
					goto _continue_two;
				}
				for (itThree = itTwo, itThree++; itThree != itSpec->second.end(); itThree++) {
					if (itTwo->dwConstant + dwDiff <= itThree->dwConstant) {
						break;
					}
				}
				if (itThree == itSpec->second.end() ||
					itTwo->dwConstant + dwDiff != itThree->dwConstant) {
					/* two + diff(one, two) != three */
					goto _continue_one;
				}

				itCand = aCandidates.find(itOne->oNode);
				if (itCand == aCandidates.end()) {
					aCandidates.insert(std::pair<DFGNode, std::list<NodeTriplet>>(
						itOne->oNode,
						std::list<NodeTriplet>(1, NodeTriplet(itOne->oNode, itTwo->oNode, itThree->oNode, dwDiff))
					));
				} else {
					itCand->second.push_back(NodeTriplet(itOne->oNode, itTwo->oNode, itThree->oNode, dwDiff));
				}

				//wc_debug("[+] found load distance triplet : %d - %d - %d\n", itOne->dwConstant, itTwo->dwConstant, itThree->dwConstant);
				aDistances.insert(std::pair<int, char>(dwDiff, 0));
_continue_two:
				itTwo++;
			}

_continue_one:
			itOne++;
		}
	}

	for (itCand = aCandidates.begin(); itCand != aCandidates.end(); itCand++) {
		DepthFirstSearch(&itCand->second, itCand->first);
		if ((GetTickCount() - dwStartTime) > dwMaxEvaluationTime) {
_time_exceeded:
			wc_debug("[-] max evaluation time exceeded for function %s (%s), signature : Block Permutation\n",
				this->oCodeGraph->toCodeGraph()->szFunctionName.c_str(),
				this->oCodeGraph->toCodeGraph()->oStatePredicate->expression(2).c_str()
			);
			goto _evaluation_error;
		}
	}

	for (itCand = aCandidates.begin(); itCand != aCandidates.end(); itCand++) {
		std::list<NodeTriplet>::iterator itTrip;
		for (itTrip = itCand->second.begin(); itTrip != itCand->second.end(); itTrip++) {
			if (itTrip->oPath != nullptr && itTrip->oPath->dwDepth >= 4) {
				if (BFSMapPath(itTrip->oPath, itTrip->oNode3, itTrip->oNode2)) {
					*lpOutput = BlockPermutationEvaluationResult::create(
						BlockPermutationEvaluator::typecast(this),
						oCodeGraph,
						EVALUATION_RESULT_MATCH_FOUND,
						itTrip->oPath
					)->toAbstract();
					wc_debug("[+] found a merkle damgard pattern for %s - %s - %s depth=%d\n",
						itTrip->oNode1->expression(2).c_str(),
						itTrip->oNode2->expression(2).c_str(),
						itTrip->oNode3->expression(2).c_str(),
						itTrip->oPath->dwDepth
					);
					DWORD dwEndTime = GetTickCount();
					wc_debug("[*] time taken to evalutate %s (%.1500s) for Merkle Damgard found=%d: %fs\n",
						oCodeGraph->toCodeGraph()->szFunctionName.c_str(),
						oCodeGraph->toCodeGraph()->oStatePredicate->expression(2).c_str(),
						true,
						((double)(dwEndTime - dwStartTime) / 1000)
					);
					oEvalCache = nullptr;
					return true;
				}

				if ((GetTickCount() - dwStartTime) > dwMaxEvaluationTime) {
					goto _time_exceeded;
				}
			}
		}
	}

_evaluation_error:
	DWORD dwEndTime = GetTickCount();
	wc_debug("[*] time taken to evalutate %s (%.1500s) for Merkle Damgard found=%d: %fs\n",
		oCodeGraph->toCodeGraph()->szFunctionName.c_str(),
		oCodeGraph->toCodeGraph()->oStatePredicate->expression(2).c_str(),
		false,
		((double)(dwEndTime - dwStartTime) / 1000)
	);
	*lpOutput = BlockPermutationEvaluationResult::create(
		BlockPermutationEvaluator::typecast(this),
		oCodeGraph,
		EVALUATION_RESULT_NO_MATCH_FOUND,
		nullptr
	)->toAbstract();
	oEvalCache = nullptr;
	return false;
}

void BlockPermutationEvaluatorImpl::DepthFirstSearch(std::list<NodeTriplet> *lpOutput, DFGNode oNode1) {
	std::list<BFSPath> aQueue;
	std::list<DFGNode>::iterator it;
	std::unordered_map<unsigned int, DFGNode>::iterator itOut;
	aQueue.push_back(BFSPath::create(oNode1, nullptr, PATH_DIRECTION_DOWN, 0));
	std::string szLoadSpec;
	unsigned int dwConstant;
	SpecString(&szLoadSpec, &dwConstant, *oNode1->aInputNodes.begin());
	std::unordered_map<DFGNode, char> aFlaggedNodes;
	aFlaggedNodes.insert(std::pair<DFGNode, char>(oNode1, 0));
	std::unordered_map<DFGNode, NodeTriplet *> aOutputMap;
	std::unordered_map<DFGNode, NodeTriplet *>::iterator itOutputMap;
	std::list<NodeTriplet>::iterator itResult;

	for (itResult = lpOutput->begin(); itResult != lpOutput->end(); itResult++) {
		aOutputMap.insert(std::pair<DFGNode, NodeTriplet *>(itResult->oNode2, &*itResult));
	}

	while (aQueue.size()) {
		BFSPath oCurrent = aQueue.front();
		aQueue.pop_front();
		//aFlaggedNodes.insert(std::pair<DFGNode, char>(oCurrent->oNode, 0));

		itOutputMap = aOutputMap.find(oCurrent->oNode);
		if (itOutputMap != aOutputMap.end()) { //if (oCurrent->oNode == oNode2) {
			itOutputMap->second->oPath = oCurrent;
		} else if (NODE_IS_CONSTANT(oCurrent->oNode)) {
			continue;
		} else if (NODE_IS_STORE(oCurrent->oNode)) {
			continue;
		} else if (NODE_IS_REGISTER(oCurrent->oNode)) {
			continue;
		} else if (oCurrent->oNode != oNode1 && NODE_IS_LOAD(oCurrent->oNode)) {
			std::string szNodeIds;
			unsigned int dwCounter;
			SpecString(&szNodeIds, &dwCounter, *oCurrent->oNode->aInputNodes.begin());
			if (szNodeIds.compare(szLoadSpec) == 0) {
				//wc_debug("[-] skipping %s (coming from %s)\n", oCurrent->oNode->expression(4).c_str(), oNode1->expression(4).c_str());
				continue;
			}
		}

		if (oCurrent->oNode != oNode1) {
			for (it = oCurrent->oNode->aInputNodes.begin();
				it != oCurrent->oNode->aInputNodes.end();
				it++
			) {
				if (aFlaggedNodes.find(*it) == aFlaggedNodes.end()) {
				//if (oCurrent->oParent == nullptr || *it != oCurrent->oParent->oNode) {
					aQueue.push_back(BFSPath::create(*it, oCurrent, PATH_DIRECTION_UP, oCurrent->dwDepth + 1));
					aFlaggedNodes.insert(std::pair<DFGNode, char>(*it, 0));
				}
			}
		}
		for (itOut = oCurrent->oNode->aOutputNodes.begin();
			itOut != oCurrent->oNode->aOutputNodes.end();
			itOut++
		) {
			if (aFlaggedNodes.find(itOut->second) == aFlaggedNodes.end()) {
			//if (oCurrent->oParent == nullptr || itOut->second != oCurrent->oParent->oNode) {
				aQueue.push_back(BFSPath::create(itOut->second, oCurrent, PATH_DIRECTION_DOWN, oCurrent->dwDepth + 1));
				aFlaggedNodes.insert(std::pair<DFGNode, char>(itOut->second, 0));
			}
		}
	}
}

bool BlockPermutationEvaluatorImpl::BFSMapPath(BFSPath oPath, DFGNode oSource, DFGNode oTarget) {
	if (oPath == nullptr) {
		return false;
	} else if (oPath->oParent == nullptr) {
		return oSource == oTarget;
	}

	if (oPath->oNode == oSource) {
		/* same node, no need to test compatibility */
		goto _skip;
	}
	assignment_t eCacheVal = oEvalCache->GetAssignment(oPath->oNode->dwNodeId, oSource->dwNodeId);
	/* consult cache */
	if (eCacheVal == ASSIGNMENT_INVALID) {
		goto _not_found;
	} else if (eCacheVal == ASSIGNMENT_VALID) {
		goto _skip;
	}

	if ((GetTickCount() - dwStartTime) > dwMaxEvaluationTime) {
		return false;
	}

	if (node_type_spec_t(oPath->oNode).Matches(oSource)) {
		std::unordered_map<unsigned int, DFGNode>::iterator itM;
		std::unordered_map<unsigned int, DFGNode>::iterator itS;
		std::unordered_map<unsigned int, DFGNode>::iterator itOutM, itOutS;
		std::multimap<node_type_spec_t, DFGNode> aSpecToNode;
		std::multimap<node_type_spec_t, DFGNode>::iterator itSpec;

		for (itS = oSource->aInputNodesUnique.begin(); itS != oSource->aInputNodesUnique.end(); itS++) {
			aSpecToNode.insert(std::pair<node_type_spec_t,DFGNode>(node_type_spec_t(itS->second), itS->second));
		}

		for (itM = oPath->oNode->aInputNodesUnique.begin();
			itM != oPath->oNode->aInputNodesUnique.end();
			itM++
		) {
			/* any of these types could indicate an IV */
			if (NODE_IS_LOAD(itM->second) || NODE_IS_REGISTER(itM->second) || NODE_IS_CONSTANT(itM->second)) {
				continue;
			}

			itSpec = aSpecToNode.find(node_type_spec_t(itM->second));
			if (itSpec == aSpecToNode.end()) {
				goto _not_found;
			} else {
				aSpecToNode.erase(itSpec);
			}
		}

		aSpecToNode.clear();
		for (itOutM = oPath->oNode->aOutputNodes.begin(); itOutM != oPath->oNode->aOutputNodes.end(); itOutM++) {
			aSpecToNode.insert(std::pair<node_type_spec_t, DFGNode>(node_type_spec_t(itOutM->second), itOutM->second));
		}

		for (itOutS = oSource->aOutputNodes.begin();
			itOutS != oSource->aOutputNodes.end();
			itOutS++
		) {
			itSpec = aSpecToNode.find(node_type_spec_t(itOutS->second));
			if (itSpec == aSpecToNode.end()) {
				goto _not_found;
			} else {
				aSpecToNode.erase(itSpec);
			}
		}
		 //fall through
	} else {
_not_found:
		oEvalCache->Assign(oPath->oNode->dwNodeId, oSource->dwNodeId, ASSIGNMENT_INVALID);
		return false;
	}

	oEvalCache->Assign(oPath->oNode->dwNodeId, oSource->dwNodeId, ASSIGNMENT_VALID);

_skip:
	if (oPath->eDirection == PATH_DIRECTION_DOWN) {
		std::unordered_map<unsigned int, DFGNode>::iterator it;
		for (it = oSource->aInputNodesUnique.begin(); it != oSource->aInputNodesUnique.end(); it++) {
			if (BFSMapPath(oPath->oParent, it->second, oTarget)) {
				return true;
			}
		}
	} else { /* PATH_DIRECTION_UP */
		std::unordered_map<unsigned int, DFGNode>::iterator it;
		for (it = oSource->aOutputNodes.begin(); it != oSource->aOutputNodes.end(); it++) {
			if (BFSMapPath(oPath->oParent, it->second, oTarget)) {
				return true;
			}
		}
	}
	return false;
}

void BlockPermutationEvaluatorImpl::SpecString(std::string * lpOutput, unsigned int * lpConstant, DFGNode oLoad) {
	if (NODE_IS_ADD(oLoad)) {
		DFGAdd oAdd = oLoad->toAdd();
		std::list<DFGNode>::iterator itAdd;
		std::vector<unsigned int> aNodeIds;
		std::vector<unsigned int>::iterator itVec;
		std::stringstream szNodeIds;
		aNodeIds.reserve(oAdd->aInputNodes.size());
		unsigned int dwConstant = 0;

		for (itAdd = oAdd->aInputNodes.begin(); itAdd != oAdd->aInputNodes.end(); itAdd++) {
			if (NODE_IS_CONSTANT(*itAdd)) {
				dwConstant += (*itAdd)->toConstant()->dwValue;
			} else {
				aNodeIds.push_back((*itAdd)->dwNodeId);
			}
		}
		std::sort(aNodeIds.begin(), aNodeIds.end());
		for (itVec = aNodeIds.begin(); itVec != aNodeIds.end(); itVec++) {
			if (itVec != aNodeIds.begin()) {
				szNodeIds << ",";
			}
			szNodeIds << *itVec;
		}

		*lpOutput = szNodeIds.str();
		*lpConstant = dwConstant;
	} else if (NODE_IS_CONSTANT(oLoad)) {
		*lpOutput = "";
		*lpConstant = oLoad->toConstant()->dwValue;
	} else {
		*lpOutput = std::to_string(oLoad->dwNodeId);
		*lpConstant = 0;
	}
}

bool BlockPermutationEvaluationResultImpl::Mark(DFGNode & oCodeNode) {
	BFSPath oCur = oBFSPath;
	while (oCur != nullptr) {
		if (oCur->oNode == oCodeNode) {
			return true;
		}
		oCur = oCur->oParent;
	}
	return false;
}
