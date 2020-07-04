#pragma once

#include "types.hpp"

typedef enum {
	FORK_POLICY_TAKE_TRUE = 0,
	FORK_POLICY_TAKE_FALSE,
	FORK_POLICY_TAKE_BOTH
} fork_policy_t;

class PathOracleImpl: virtual public ReferenceCounted {
public:
	static void Initialize();
	inline PathOracleImpl(unsigned long lpFunctionAddress): lpFunctionAddress(lpFunctionAddress) { }
	inline PathOracleImpl(const PathOracleImpl &other) = default;
	inline ~PathOracleImpl() { }

	unsigned long lpFunctionAddress;
	fork_policy_t ShouldFork(BacklogDb &oBacklog, unsigned long lpAddress);
	int MaxCallDepth();
	int MaxGraphSize();
	int MaxConsecutiveNoopInstructions();
	int MaxConstructionTime();
	int MaxConditions();
	static int MaxEvaluationTime();
};