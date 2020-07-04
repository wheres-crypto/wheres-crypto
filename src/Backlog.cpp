#include "Backlog.hpp"

void BacklogDbImpl::NewEntry(unsigned long lpAddress, bool bDecision) {
	BacklogDbImpl::iterator it = find(lpAddress);
	if (it == end()) {
		std::pair<unsigned long, Backlog> stInsert(lpAddress, Backlog::create(bDecision));
		insert(stInsert);
	} else {
		it->second->push_back(bDecision);
	}
}

Backlog BacklogDbImpl::GetLog(unsigned long lpAddress) {
	BacklogDbImpl::iterator it = find(lpAddress);
	if (it == end()) {
		return Backlog();
	}
	else {
		return it->second;
	}
}

bool BacklogDbImpl::GetFirst(unsigned long lpAddress, bool bDefault) {
	BacklogDbImpl::iterator it = find(lpAddress);
	if (it == end()) {
		return bDefault;
	}
	else {
		return *it->second->begin();
	}
}

bool BacklogDbImpl::GetLast(unsigned long lpAddress, bool bDefault) {
	BacklogDbImpl::iterator it = find(lpAddress);
	if (it == end()) {
		return bDefault;
	}
	else {
		return *--it->second->end();
	}
}

BacklogDb BacklogDbImpl::fork() {
	BacklogDb oFork = BacklogDb::create();
	BacklogDbImpl::iterator it;

	for (it = begin(); it != end(); it++) {
		oFork->insert(std::pair<unsigned long, Backlog>(it->first, Backlog::create(*it->second)));
	}
	return oFork;
}

bool BacklogDbImpl::Exists(unsigned long lpAddress) {
	return find(lpAddress) != end();
}