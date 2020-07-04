#pragma once

#include <map>
#include <vector>
#include <unordered_map>
#include <idp.hpp>

#include "types.hpp"
#include "Broker.hpp"
#include "SignatureEvaluator.hpp"

typedef enum {
	PATH_DIRECTION_UP,
	PATH_DIRECTION_DOWN
} BFSPathDirection;

class BFSPathImpl : public virtual ReferenceCounted {
public:
	inline BFSPathImpl(
		const DFGNode &oNode,
		const rfc_ptr<BFSPathImpl> &oParent,
		BFSPathDirection eDirection,
		int dwDepth
	) : oNode(oNode), oParent(oParent), eDirection(eDirection), dwDepth(dwDepth) { }

	DFGNode oNode;
	rfc_ptr<BFSPathImpl> oParent;
	BFSPathDirection eDirection;
	int dwDepth;
};
typedef rfc_ptr<BFSPathImpl> BFSPath;

typedef struct NodeTriplet {
	inline NodeTriplet(const DFGNode &oNode1, const DFGNode &oNode2, const DFGNode &oNode3, int dwDistance)
		: oNode1(oNode1), oNode2(oNode2), oNode3(oNode3), dwDistance(dwDistance) { }

	DFGNode oNode1;
	DFGNode oNode2;
	DFGNode oNode3;
	int dwDistance;
	BFSPath oPath;
} NodeTriplet;

class BlockPermutationEvaluatorImpl : virtual public ReferenceCounted, public AbstractEvaluatorImpl {
public:
	inline BlockPermutationEvaluatorImpl() { }

	bool Evaluate(AbstractEvaluationResult *lpOutput);

private:
	void DepthFirstSearch(std::list<NodeTriplet> *lpOutput, DFGNode oNode1);
	bool BFSMapPath(BFSPath oPath, DFGNode oSource, DFGNode oTarget);
	void SpecString(std::string *lpOutput, unsigned int *lpConstant, DFGNode oLoad);
	SparseMatrix oEvalCache; // borrowed from SignatureEvaluator, used to signifying node compatibility
	unsigned int dwStartTime;
};

class BlockPermutationEvaluationResultImpl : virtual public ReferenceCounted, public AbstractEvaluationResultImpl {
public:
	inline BlockPermutationEvaluationResultImpl(
		BlockPermutationEvaluator oEvaluator,
		Broker oCodeGraph,
		evaluation_status_t eEvaluationResult,
		BFSPath oBFSPath
	) : AbstractEvaluationResultImpl(oEvaluator->toAbstract(), oCodeGraph, eEvaluationResult), oBFSPath(oBFSPath) { }

	bool Mark(DFGNode &oCodeNode);
	inline std::string Label() { return "Sequential Block Permutation"; }

	BFSPath oBFSPath;
};