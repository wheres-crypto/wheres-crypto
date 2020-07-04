#include <idp.hpp>

#include "common.hpp"
#include "DFGraph.hpp"
#include "DFGNode.hpp"

DFGraphImpl::DFGraphImpl(): dwNodeCounter(0) { }
DFGraphImpl::~DFGraphImpl() {
	iterator it;
	for (it = begin(); it != end(); it++) {
		it->second->aInputNodes.clear();
		it->second->aOutputNodes.clear();
		it->second->aInputNodesUnique.clear();
	}
}

void DFGraphImpl::InsertNode(DFGNode oNode) {
	std::list<DFGNode>::iterator itUp;
	std::unordered_map<unsigned int, DFGNode>::iterator itDown;
	oNode->dwNodeId = dwNodeCounter++;
	aIdMap.insert(std::pair<unsigned int, DFGNode>(oNode->dwNodeId, oNode));
	insert(std::pair<std::string, DFGNode>(oNode->idx(), oNode));

	/* create output arcs from incoming nodes */
	for (itUp = oNode->aInputNodes.begin(); itUp != oNode->aInputNodes.end(); itUp++) {
		/* same input can be specified multiple times, make only a single output arc */
		itDown = (*itUp)->aOutputNodes.find(oNode->dwNodeId);
		if (itDown == (*itUp)->aOutputNodes.end()) {
			(*itUp)->aOutputNodes.insert(std::pair<unsigned int, DFGNode>(oNode->dwNodeId, oNode));
		}
		if (oNode->aInputNodesUnique.find((*itUp)->dwNodeId) == oNode->aInputNodesUnique.end()) {
			oNode->aInputNodesUnique.insert(std::pair<unsigned int, DFGNode>((*itUp)->dwNodeId, *itUp));
		}
	}
}

void DFGraphImpl::RemoveNode(DFGNode oNode) {
	/* untie node from incoming nodes */
	std::unordered_map<unsigned int, DFGNode>::iterator itArcUp;
	std::unordered_map<unsigned int, DFGNode>::iterator itArcDown;

	for (itArcUp = oNode->aInputNodesUnique.begin(); itArcUp != oNode->aInputNodesUnique.end(); itArcUp++) {
		/* iterate through upward facing arcs */
		itArcDown = itArcUp->second->aOutputNodes.find(oNode->dwNodeId);
		if (itArcDown != itArcUp->second->aOutputNodes.end()) {
			/* remove corresponding downward facing arc */
			itArcUp->second->aOutputNodes.erase(itArcDown);
		}
	}

	oNode->aOutputNodes.clear();
	oNode->aInputNodesUnique.clear();
	aIdMap.erase(oNode->dwNodeId);
	erase(oNode->idx());
}

DFGNode DFGraphImpl::CopyNode(const DFGNode &oNode, unsigned int dwStackSize) {
	std::unordered_map<unsigned int,DFGNode>::iterator it;
	if ((it = aIdMap.find(oNode->dwNodeId)) != aIdMap.end()) {
		/* node already exists in current graph */
		return it->second;
	}
	if (dwStackSize == 0) {
		return nullptr;
	}
	DFGNode oCopy = oNode->copy();
	std::list<DFGNode>::const_iterator itUp;
	std::unordered_map<unsigned int, DFGNode>::const_iterator itDown;

	for (itUp = oNode->aInputNodes.begin(); itUp != oNode->aInputNodes.end(); itUp++) {
		DFGNode oInput = CopyNode(*itUp, dwStackSize - 1);
		if (oInput == nullptr) {
			return nullptr;
		}
		oCopy->aInputNodes.push_back(oInput);
		/* same input can be specified multiple times, make only a single output arc */
		itDown = oInput->aOutputNodes.find(oCopy->dwNodeId);
		if (itDown == oInput->aOutputNodes.end()) {
			oInput->aOutputNodes.insert(std::pair<unsigned int, DFGNode>(oCopy->dwNodeId, oCopy));
		}
		if (oCopy->aInputNodesUnique.find(oInput->dwNodeId) == oCopy->aInputNodesUnique.end()) {
			oCopy->aInputNodesUnique.insert(std::pair<unsigned int, DFGNode>(oInput->dwNodeId, oInput));
		}
	}

	aIdMap.insert(std::pair<unsigned int, DFGNode>(oCopy->dwNodeId, oCopy));
	insert(std::pair<std::string, DFGNode>(oCopy->idx(), oCopy));
	//if (oCopy->idx() != oNode->idx()) {
	//	wc_debug("[-] assertion failed : %s != %s", oCopy->idx().c_str(), oNode->idx().c_str());
	//}
	return oCopy;
}

DFGraph DFGraphImpl::fork() const {
	std::unordered_map<unsigned int,DFGNode>::const_iterator it;
	DFGraph oFork(DFGraph::create());
	oFork->dwNodeCounter = dwNodeCounter;

	for (it = aIdMap.begin(); it != aIdMap.end(); it++) {
		if (oFork->CopyNode(it->second) == nullptr) {
			return nullptr;
		}
	}

	return oFork;
}