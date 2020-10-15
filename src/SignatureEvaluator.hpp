#pragma once

#include <map>
#include <vector>
#include <unordered_map>
#include <idp.hpp>

#include "types.hpp"
#include "DFGNode.hpp" // for node_type_t
#include "DFGraph.hpp"
#include "Broker.hpp" // for New(Xor|And|Add)
#include "SignatureParser.hpp"
#include "AnalysisResult.hpp"

typedef enum {
	ASSIGNMENT_UNEXPLORED = 0,
	ASSIGNMENT_INVALID = 1,
	ASSIGNMENT_UNDEFINED = 2,
	ASSIGNMENT_VALID = 3,
} assignment_t;

class AssignmentMapImpl : virtual public ReferenceCounted, public std::unordered_map<DFGNode, DFGNode> {
public:
	inline AssignmentMapImpl() { }
	inline void Assign(DFGNode &oSignatureNode, DFGNode &oCodeNode) {
		iterator it = find(oCodeNode);
		if (it == end()) {
			insert(std::pair<DFGNode, DFGNode>(oCodeNode, oSignatureNode));
		} else {
			it->second = oSignatureNode;
		}
	}
	inline DFGNode Lookup(DFGNode &oCodeNode) {
		iterator it = find(oCodeNode);
		if (it == end()) {
			return nullptr;
		} else {
			return it->second;
		}
	}
};

/*
 * sparse_matrix_iterator_t previously was a
 * std::pair<unsigned int, std::map<unsigned long long, unsigned long long>::iterator>
 * this was changed since the SparseMatrix is updated while iterating,
 * potentially causing the iterator to become invalid
 */
typedef struct sparse_matrix_iterator_t {
	sparse_matrix_iterator_t(unsigned int dwSignatureNodeId, unsigned int dwCodeNodeId) :
		dwSignatureNodeId(dwSignatureNodeId), dwCodeNodeId(dwCodeNodeId) { }
	unsigned int dwSignatureNodeId;
	unsigned int dwCodeNodeId;
	inline bool operator==(const sparse_matrix_iterator_t &o) const {
		return dwSignatureNodeId == o.dwSignatureNodeId && dwCodeNodeId == o.dwCodeNodeId;
	}
} sparse_matrix_iterator_t;

class SparseMatrixImpl : virtual public ReferenceCounted, public std::map<unsigned long long, unsigned long long> {
public:
	inline assignment_t GetAssignment(unsigned int dwSignatureNodeId, unsigned int dwCodeNodeId) const {
		unsigned long long qwIndex = (((unsigned long long)dwSignatureNodeId) << 27) | (dwCodeNodeId >> 5);
		unsigned int dwShift = ((dwCodeNodeId & 0x1f) << 1);
		const_iterator it = find(qwIndex);
		if (it == end()) {
			return ASSIGNMENT_UNEXPLORED;
		}
		return (assignment_t)((it->second >> dwShift) & 0x3);
	}
	inline SparseMatrixImpl() { }
	inline SparseMatrixImpl(const SparseMatrixImpl &other) = default;
	inline SparseMatrix copy() const { return SparseMatrix::create(*this); }
	inline void Assign(unsigned int dwSignatureNodeId, unsigned int dwCodeNodeId, assignment_t eType, bool bKeepInvalid = false) {
		unsigned long long qwIndex = (((unsigned long long)dwSignatureNodeId) << 27) | (dwCodeNodeId >> 5);
		unsigned int dwShift = ((dwCodeNodeId & 0x1f) << 1);
		iterator it = find(qwIndex);
		if (it != end()) {
			it->second &= ~(3ULL << dwShift);
			it->second |= (unsigned long long)eType << dwShift;

			if (!bKeepInvalid && !(it->second & ~0x5555555555555555ULL)) {
				erase(it);
			}
		} else if ((bKeepInvalid && eType >= ASSIGNMENT_INVALID) || eType >= ASSIGNMENT_UNDEFINED) {
			insert(std::pair<unsigned long long, unsigned long long>(qwIndex, (unsigned long long)eType << dwShift));
		}
	}
	inline void CleanInvalid() {
		iterator it;
		for (it = begin(); it != end(); ) {
			if (!(it->second & ~0x5555555555555555ULL)) {
				it = erase(it);
			} else {
				it++;
			}
		}
	}
	sparse_matrix_iterator_t FirstCandidate(unsigned int dwSignatureNodeId) {
		iterator itLowerBound = lower_bound(((unsigned long long)dwSignatureNodeId) << 27);
		while (itLowerBound != end() && (itLowerBound->first >> 27) == dwSignatureNodeId) {
			unsigned int i;
			for (i = 0; i < 32; i++) {
				assignment_t eAssignment = (assignment_t)((itLowerBound->second >> (i << 1)) & 3);
				if (eAssignment == ASSIGNMENT_VALID || eAssignment == ASSIGNMENT_UNDEFINED) {
					return sparse_matrix_iterator_t(dwSignatureNodeId, (unsigned int)((itLowerBound->first << 5) | i));
				}
			}
			itLowerBound++;
		};
		return sparse_matrix_iterator_t(0xffffffff, 0xffffffff);
	}

	sparse_matrix_iterator_t NextCandidate(const sparse_matrix_iterator_t&it) {
		iterator itLowerBound = lower_bound(((unsigned long long)it.dwSignatureNodeId << 27) | (it.dwCodeNodeId >> 5));
		unsigned int i;
		if (itLowerBound != end() && (itLowerBound->first >> 27) == it.dwSignatureNodeId) {
			for (i = (it.dwCodeNodeId & 0x1f) + 1; i < 32; i++) {
				assignment_t eAssignment = (assignment_t)((itLowerBound->second >> (i << 1)) & 3);
				if (eAssignment == ASSIGNMENT_VALID || eAssignment == ASSIGNMENT_UNDEFINED) {
					return sparse_matrix_iterator_t(it.dwSignatureNodeId, (unsigned int)((itLowerBound->first << 5) | i));
				}
			}
			itLowerBound++;
			while (itLowerBound != end() && (itLowerBound->first >> 27) == it.dwSignatureNodeId) {
				for (i = 0; i < 32; i++) {
					assignment_t eAssignment = (assignment_t)((itLowerBound->second >> (i << 1)) & 3);
					if (eAssignment == ASSIGNMENT_VALID || eAssignment == ASSIGNMENT_UNDEFINED) {
						return sparse_matrix_iterator_t(it.dwSignatureNodeId, (unsigned int)((itLowerBound->first << 5) | i));
					}
				}
				itLowerBound++;
			}
		}
		return sparse_matrix_iterator_t(0xffffffff, 0xffffffff);
	}
};

class FlagMapImpl;
typedef rfc_ptr<FlagMapImpl> FlagMap;
class FlagMapImpl : public ReferenceCounted, public std::unordered_map<unsigned int, unsigned long long> {
public:
	inline FlagMapImpl() { }
	inline void Assign(unsigned int dwCodeNodeId) {
		unsigned int dwIndex = dwCodeNodeId >> 5;
		iterator it = find(dwIndex);
		if (it != end()) {
			it->second |= 1ULL << (dwCodeNodeId & 0x3f);
		} else {
			insert(std::pair<unsigned int, unsigned long long>(dwIndex, 1 << (dwCodeNodeId & 0x3f)));
		}
	}
	inline void Unassign(unsigned int dwCodeNodeId) {
		unsigned int dwIndex = dwCodeNodeId >> 5;
		iterator it = find(dwIndex);
		if (it != end()) {
			it->second &= ~(1 << (dwCodeNodeId & 0x3f));
		}
	}
	inline bool IsAssigned(unsigned int dwCodeNodeId) {
		unsigned int dwIndex = dwCodeNodeId >> 5;
		iterator it = find(dwIndex);
		if (it != end()) {
			return it->second & (1 << dwCodeNodeId & 0x3f);
		} else {
			return false;
		}
	}
};

typedef struct node_type_spec_t {
	inline node_type_spec_t() : eNodeType(NODE_TYPE_UNKNOWN) { }
	inline node_type_spec_t(const DFGNode &oNode);
	inline bool Matches(const DFGNode &oNode);
	inline bool operator < (const struct node_type_spec_t &other) const {
		if (eNodeType != other.eNodeType) {
			return eNodeType < other.eNodeType;
		}
		switch (eNodeType) {
		case NODE_TYPE_CONSTANT:
			return dwValue < other.dwValue;
		case NODE_TYPE_REGISTER:
			return bRegister < other.bRegister;
		case NODE_TYPE_CALL:
			return lpAddress < other.lpAddress;
		default:
			return false;
		}
	}
	node_type_t eNodeType;
	union {
		unsigned int dwValue;
		unsigned char bRegister;
		unsigned long lpAddress;
	};
} node_type_spec_t;

typedef enum {
	OPAQUE_NODE_ASSIGN_OK = 0,
	OPAQUE_NODE_ASSIGN_ALREADY_SET,
	OPAQUE_NODE_ASSIGN_NOK
} opaque_node_assign_t;

class OpaqueAssignmentImpl : public virtual ReferenceCounted, public std::vector<node_type_spec_t> {
public:
	inline OpaqueAssignmentImpl(const OpaqueAssignmentImpl &) = default;
	inline OpaqueAssignmentImpl(int dwNumNodes) : dwNumNodes(dwNumNodes) { resize(dwNumNodes); }
	inline OpaqueAssignment copy() { return OpaqueAssignment::create(*this); }
	inline opaque_node_assign_t Assign(DFGNode oSignatureNode, DFGNode oCandidate);
	inline opaque_node_assign_t AssignPossible(DFGNode oSignatureNode, DFGNode oCandidate);
	inline void Unassign(DFGNode oSignatureNode);

	int dwNumNodes;
};

class SignatureEvaluatorImpl : virtual public ReferenceCounted, public AbstractEvaluatorImpl {
public:
	inline SignatureEvaluatorImpl(SignatureDefinition oSignatureDefinition)
		: oSignatureDefinition(oSignatureDefinition) {}

	SignatureDefinition oSignatureDefinition;
	Broker oSignatureGraph;
	SparseMatrix oMatrix;
	std::multimap<int, unsigned int> aOpaqueIdToNode;
	OpaqueAssignment oOpaqueAssignment;
	AssignmentMap oMapping;
	std::list<SparseMatrix> aStack;
	unsigned int dwStartTime;

	void PushMatrix();
	void PopMatrix();
	void ClearFlagged();

	bool Evaluate(AbstractEvaluationResult *lpOutput);
	bool IsCandidate(const DFGNode& oSignatureNode, const DFGNode& oCodeNode);

private:
	assignment_t Pass1Recurse(const DFGNode& oSignatureNode, const DFGNode& oCodeNode);
	bool Pass2Recurse(
		FlagMap oFlagMap,
		DFGraphImpl::const_iterator itE
	);
	bool PruneFlagged();
};

class SignatureEvaluationResultImpl : virtual public ReferenceCounted, public AbstractEvaluationResultImpl {
public:
	inline SignatureEvaluationResultImpl(
		SignatureEvaluator oEvaluator,
		Broker oCodeGraph,
		evaluation_status_t eEvaluationResult,
		Broker oSignatureGraph,
		AssignmentMap oMapping
	) : AbstractEvaluationResultImpl(oEvaluator->toAbstract(), oCodeGraph, eEvaluationResult),
		oSignatureGraph(oSignatureGraph),
		oMapping(oMapping) { }

	bool Mark(DFGNode &oCodeNode);
	std::string Label();

	Broker oSignatureGraph;
	AssignmentMap oMapping;
};