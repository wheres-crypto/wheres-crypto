#pragma once

#include <unordered_map>
#include <list>

#include "types.hpp"

class BacklogImpl : virtual public ReferenceCounted, public std::list<bool> {
public:
	BacklogImpl() { }
	BacklogImpl(bool bValue) : std::list<bool>(1, bValue) { }
	BacklogImpl(const BacklogImpl &other) = default;
};

class BacklogDbImpl : virtual public ReferenceCounted, public std::unordered_map<unsigned long, Backlog> {
public:
	BacklogDbImpl() { }
	~BacklogDbImpl() { }

	void NewEntry(unsigned long lpAddress, bool bDecision);
	Backlog GetLog(unsigned long lpAddress);
	bool GetFirst(unsigned long lpAddress, bool bDefault = false);
	bool GetLast(unsigned long lpAddress, bool bDefault = false);
	BacklogDb fork();
	bool Exists(unsigned long lpAddress);
};