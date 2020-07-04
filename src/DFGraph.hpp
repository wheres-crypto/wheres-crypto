#pragma once

#include <list>
#include <unordered_map>
#include <string>

#include "types.hpp"

class DFGraphImpl : virtual public ReferenceCounted, public std::unordered_map<std::string, DFGNode> {
public:
	DFGraphImpl();
	~DFGraphImpl();

	unsigned int dwNodeCounter;
	inline DFGNode FindNode(const std::string &szIndex) {
		std::unordered_map<std::string, DFGNode>::iterator it;
		it = find(szIndex);
		return it == end() ? nullptr : it->second;
	}
	inline DFGNode FindNode(unsigned int dwNodeId) {
		std::unordered_map<unsigned int, DFGNode>::iterator it;
		it = aIdMap.find(dwNodeId);
		return it == aIdMap.end() ? nullptr : it->second;
	}
	void InsertNode(DFGNode oNode);
	void RemoveNode(DFGNode oNode);

	std::unordered_map<unsigned int, DFGNode> aIdMap;
	DFGraph fork() const;

private:
	/* fork helper function */
	DFGNode CopyNode(const DFGNode &oNode, unsigned int dwStackSize=10000);
};