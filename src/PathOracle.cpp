#include <mutex>

#include "PathOracle.hpp"
#include "Backlog.hpp"

std::unordered_map<unsigned long, int> g_aNumForksLeft;
std::mutex g_stNumForksMutex;

void PathOracleImpl::Initialize() {
	g_aNumForksLeft.clear();
}

fork_policy_t PathOracleImpl::ShouldFork(BacklogDb &oBacklog, unsigned long lpAddress) {
	if (!oBacklog->Exists(lpAddress)) {
		/* first time -> fork */
		std::unordered_map<unsigned long, int>::iterator it;
		std::unique_lock<std::mutex> mLock(g_stNumForksMutex);

		/* look up per-function metadata (currently only holds num forks left per function) */
		it = g_aNumForksLeft.find(lpFunctionAddress);
		if (it == g_aNumForksLeft.end()) {
			g_aNumForksLeft.insert(std::pair<unsigned long, int>(lpFunctionAddress, 99));
			return FORK_POLICY_TAKE_BOTH;
		} else if (it->second > 0) {
			it->second--;
			return FORK_POLICY_TAKE_BOTH;
		}
		return FORK_POLICY_TAKE_FALSE;
	}

	if (oBacklog->GetLog(lpAddress)->size() >= 4) {
		/*
		 * take path opposite of the one that got us here
		 */
		return oBacklog->GetFirst(lpAddress) ? FORK_POLICY_TAKE_FALSE : FORK_POLICY_TAKE_TRUE;
	} else {
		/*
		 * re-take the same path 10 times
		 */
		return oBacklog->GetFirst(lpAddress) ? FORK_POLICY_TAKE_TRUE : FORK_POLICY_TAKE_FALSE;
	}
}

int PathOracleImpl::MaxCallDepth() {
	return 2; // inline functions 2 levels deep
}

int PathOracleImpl::MaxGraphSize() {
	return 200000; // graphs consisting of over 100000 nodes probably represent some infinite loop
}

int PathOracleImpl::MaxConsecutiveNoopInstructions() {
	return 1000; // allow for 1000 instructions to be processed without any contribution to the graph before giving up
}

int PathOracleImpl::MaxConstructionTime() {
	return 10000; // 10s
}

int PathOracleImpl::MaxConditions() {
	return 1000;
}

int PathOracleImpl::MaxEvaluationTime() {
	return 10000; // 10s
}
