#pragma once

#include <unordered_map>
#include <map>

#include "types.hpp"
#include "ThreadPool.hpp"
#include "DFGNode.hpp"

#define DOT_FLAG_CARRY 1
#define DOT_FLAG_OVERFLOW 2

#define THREAD_RESULT_TYPE_CODE_GRAPH 0x463cd291
#define THREAD_RESULT_TYPE_ANALYSIS_ERROR 0x37a696cd

typedef struct {
	unsigned int dwFlags;
	unsigned int dwValue;
} dot_result_t;

typedef enum {
	GRAPH_PROCESS_INTERNAL_ERROR = 0,
	GRAPH_PROCESS_CONTINUE,
	GRAPH_PROCESS_SKIP
} graph_process_t;

class BrokerImpl;
class CodeBrokerImpl;
class SignatureBrokerImpl;
typedef rfc_ptr<BrokerImpl> Broker;
typedef rfc_ptr<CodeBrokerImpl> CodeBroker;
typedef rfc_ptr<SignatureBrokerImpl> SignatureBroker;

typedef bool (NodeIsCorrectType)(DFGNode &oNode);
typedef DFGNode (NodeCreator)(DFGNode &oInputNode1, DFGNode &oInputNode2);
typedef void (NodeDot)(dot_result_t *lpResult, unsigned int dwConstant1, unsigned int dwConstant2);

class BrokerImpl: virtual public ReferenceCounted {
public:
	BrokerImpl() : oGraph(DFGraph::create()) { }
	~BrokerImpl();
	DFGNode NewConstant(unsigned int dwValue);
	DFGNode NewRegister(unsigned char bRegister);
	DFGNode NewAdd(DFGNode &oNode1, DFGNode &oNode2, DFGNode *lpCarry = 0, DFGNode *lpOverflow = 0);
	DFGNode NewMult(DFGNode &oNode1, DFGNode &oNode2, DFGNode *lpCarry = 0, DFGNode *lpOverflow = 0);
	DFGNode NewCall(unsigned long lpAddress, DFGNode &oArgument1);
	DFGNode NewLoad(DFGNode &oMemoryLocation);
	DFGNode NewStore(DFGNode &oData, DFGNode &oMemoryLocation);
	DFGNode NewXor(DFGNode &oNode1, DFGNode &oNode2, DFGNode *lpCarry = 0, DFGNode *lpOverflow = 0);
	DFGNode NewAnd(DFGNode &oNode1, DFGNode &oNode2, DFGNode *lpCarry = 0, DFGNode *lpOverflow = 0);
	DFGNode NewOr(DFGNode &oNode1, DFGNode &oNode2, DFGNode *lpCarry = 0, DFGNode *lpOverflow = 0);
	DFGNode NewShift(DFGNode &oNode, DFGNode &oAmount, DFGNode *lpCarry = 0, DFGNode *lpOverflow = 0);
	DFGNode NewRotate(DFGNode &oNode, DFGNode &oAmount, DFGNode *lpCarry = 0, DFGNode *lpOverflow = 0);
	DFGNode NewOpaque(const std::string &szOpaqueRef = "", int dwOpaqueRefId = -1);

	virtual Broker fork() = 0;
	void Cleanup();
	void Cleanup_Impl(
		std::unordered_map<DFGNode, char> &aRemoved,
		std::unordered_map<DFGNode, std::unordered_map<DFGNode,char>> &aNumChildrenRemoved,
		DFGNode &oNode
	);
	std::string Export();
	std::string BrokerImpl::Export_Impl(
		std::map<DFGNode, std::pair<int, std::string>> &aExportMap,
		DFGNode oNode
	);
	virtual bool ShouldCleanNode(DFGNode &oNode) = 0;

	inline Broker toGeneric() { return Broker::typecast(this); };
	inline CodeBroker toCodeGraph() { return CodeBroker::typecast(this); };
	inline SignatureBroker toSignatureGraph() { return SignatureBroker::typecast(this); };

	DFGraph oGraph;
	DFGNode FindNode(DFGNode &oNode);

protected:
	DFGNode NewCarry(DFGNode &oNode);
	DFGNode NewOverflow(DFGNode &oNode);
	DFGNode FindNode(const DFGNodeImpl &oNode);
	std::unordered_map<unsigned int, DFGNode> aMemoryMap;

	DFGNode MergeOperationsCommutative(
		DFGNode &oNode1, DFGNode &oNode2,
		DFGNode *lpCarry, DFGNode *lpOverflow,
		node_type_t eType,
		NodeCreator *lpCreator,
		NodeDot *lpDot,
		unsigned int dwIdentityValue,
		unsigned int dwZeroValue
	);
	DFGNode MergeOperations(
		DFGNode &oNode1, DFGNode &oNode2,
		DFGNode *lpCarry, DFGNode *lpOverflow,
		NodeCreator *lpCreator,
		NodeDot *lpDot,
		unsigned int dwIdentityValue,
		unsigned int dwZeroValue
	);

friend class SignatureParserImpl;
};

class EmptyAnalysisResultImpl : public ThreadTaskResultImpl {
public:
	inline EmptyAnalysisResultImpl() : ThreadTaskResultImpl(THREAD_RESULT_TYPE_ANALYSIS_ERROR) { }
};

class CodeBrokerImpl : public BrokerImpl, public ThreadTaskImpl, public ThreadTaskResultImpl {
public:
	CodeBrokerImpl(const CodeBrokerImpl &other) = default;
	static bool ScheduleBuild(
		Processor oProcessor,
		ThreadPool oThreadPool,
		unsigned long lpStartAddress,
		PathOracle oPathOracle = nullptr,
		bool bOnlyIfResourceAvailable = false
	);
	graph_process_t IntroduceCondition(Condition &oCondition, unsigned long lpNextAddress);
	Broker fork();
	int MaxCallDepth();
	bool ShouldCleanNode(DFGNode &oNode);

protected:
	CodeBrokerImpl(
		Processor oProcessor,
		ThreadPool oThreadpool,
		unsigned long lpStartAddress,
		PathOracle oPathOracle
	);
	/* ThreadTask */
	unsigned long Execute(void *lpPrivate) { Build_Impl((unsigned long)lpPrivate); return 0; }
	void Build_Impl(unsigned long lpAddress);
	Predicate oStatePredicate;
	BacklogDb oBacklog;
	Processor oProcessor;
	PathOracle oPathOracle;
	ThreadPool oThreadPool;
	unsigned long lpCurrentAddress;
	unsigned long lpStartAddress;
	std::string szFunctionName;
	int dwMaxGraphSize;
	int dwMaxConsecutiveNoopInstructions;
	int dwMaxConstructionTime;
	int dwMaxConditions;
	int dwNumConditions;

friend class DFGPlugin;
friend CodeBroker;
friend class WorkerThread;
friend class ControlDialog;
friend class SignatureEvaluatorImpl;
friend class BlockPermutationEvaluatorImpl;
};

class SignatureBrokerImpl : public BrokerImpl {
public:
	SignatureBrokerImpl();
	SignatureBrokerImpl(const SignatureBrokerImpl &other) = default;
	Broker fork();
	inline void SetIdentifier(const std::string& szIdentifier) { this->szIdentifier = szIdentifier; }
	inline void SetVariant(const std::string& szVariant) { this->szVariant = szVariant; }
	inline void FlagNodeToKeep(DFGNode &oNode) { aNodesToKeep.insert(std::pair<DFGNode, char>(oNode, (char)0)); }
	bool ShouldCleanNode(DFGNode &oNode);

	std::unordered_map<DFGNode, char> aNodesToKeep;
	std::string szIdentifier;
	std::string szVariant;
	int dwNumOpaqueRefs;
};