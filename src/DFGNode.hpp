#pragma once

#include <string>
#include <list>
#include <unordered_map>

#include "types.hpp"

typedef enum {
	NODE_TYPE_UNKNOWN = 0,
	NODE_TYPE_CONSTANT,
	NODE_TYPE_REGISTER,
	NODE_TYPE_ADD,
	NODE_TYPE_MULT,
	NODE_TYPE_CALL,
	NODE_TYPE_LOAD,
	NODE_TYPE_STORE,
	NODE_TYPE_XOR,
	NODE_TYPE_AND,
	NODE_TYPE_OR,
	NODE_TYPE_SHIFT,
	NODE_TYPE_ROTATE,
	NODE_TYPE_CARRY,
	NODE_TYPE_OVERFLOW,
	NODE_TYPE_OPAQUE
} node_type_t;

#define NODE_IS_CONSTANT(x) ((x)->eNodeType == NODE_TYPE_CONSTANT)
#define NODE_IS_REGISTER(x) ((x)->eNodeType == NODE_TYPE_REGISTER)
#define NODE_IS_ADD(x) ((x)->eNodeType == NODE_TYPE_ADD)
#define NODE_IS_MULT(x) ((x)->eNodeType == NODE_TYPE_MULT)
#define NODE_IS_CALL(x) ((x)->eNodeType == NODE_TYPE_CALL)
#define NODE_IS_LOAD(x) ((x)->eNodeType == NODE_TYPE_LOAD)
#define NODE_IS_STORE(x) ((x)->eNodeType == NODE_TYPE_STORE)
#define NODE_IS_XOR(x) ((x)->eNodeType == NODE_TYPE_XOR)
#define NODE_IS_AND(x) ((x)->eNodeType == NODE_TYPE_AND)
#define NODE_IS_OR(x) ((x)->eNodeType == NODE_TYPE_OR)
#define NODE_IS_SHIFT(x) ((x)->eNodeType == NODE_TYPE_SHIFT)
#define NODE_IS_ROTATE(x) ((x)->eNodeType == NODE_TYPE_ROTATE)
#define NODE_IS_CARRY(x) ((x)->eNodeType == NODE_TYPE_CARRY)
#define NODE_IS_OPAQUE(x) ((x)->eNodeType == NODE_TYPE_OPAQUE)

#define CACHED(method) \
	virtual std::string method ## _impl() const = 0; \
	std::string sz_ ## method ## _cache; \
	bool b_ ## method ## _is_cached = false; \
	inline std::string method() const { \
		if (!b_ ## method ## _is_cached) { \
			((DFGNodeImpl *)this)->sz_ ## method ## _cache = method ## _impl(); \
			((DFGNodeImpl *)this)->b_ ## method ## _is_cached = true; \
		} \
		return sz_ ## method ## _cache; \
	}

class DFGNodeImpl: virtual public ReferenceCounted {
public:
	node_type_t eNodeType;
	unsigned int dwNodeId;

	inline DFGNodeImpl() : eNodeType(NODE_TYPE_UNKNOWN), dwNodeId(0) { }
	virtual ~DFGNodeImpl() { }
//	CACHED(mnemonic)
//	CACHED(idx)
//	CACHED(expression)
	virtual std::string mnemonic() const = 0;
	virtual std::string idx() const = 0;
	virtual std::string expression(int dwMaxDepth = -1) const = 0;

	std::list<DFGNode> aInputNodes;
	std::unordered_map<unsigned int, DFGNode> aInputNodesUnique;
	//std::list<DFGNode> aOutputNodes;
	std::unordered_map<unsigned int, DFGNode> aOutputNodes;

	inline DFGNode toGeneric() { return DFGNode::typecast(this); };
	inline DFGConstant toConstant() { return DFGConstant::typecast(this); };
	inline DFGRegister toRegister() { return DFGRegister::typecast(this); };
	inline DFGAdd toAdd() { return DFGAdd::typecast(this); };
	inline DFGMult toMult() { return DFGMult::typecast(this); };
	inline DFGCall toCall() { return DFGCall::typecast(this); };
	inline DFGLoad toLoad() { return DFGLoad::typecast(this); };
	inline DFGStore toStore() { return DFGStore::typecast(this); };
	inline DFGXor toXor() { return DFGXor::typecast(this); };
	inline DFGAnd toAnd() { return DFGAnd::typecast(this); };
	inline DFGOr toOr() { return DFGOr::typecast(this); };
	inline DFGShift toShift() { return DFGShift::typecast(this); };
	inline DFGRotate toRotate() { return DFGRotate::typecast(this); };
	inline DFGCarry toCarry() { return DFGCarry::typecast(this); };
	inline DFGOverflow toOverflow() { return DFGOverflow::typecast(this); }
	inline DFGOpaque toOpaque() { return DFGOpaque::typecast(this); };

protected:
	inline DFGNodeImpl(node_type_t eNodeType): eNodeType(eNodeType), dwNodeId(0) { }
	std::string GenericIdx(const char *szPrefix) const;
	std::string GenericExpression(const char *szPrefix, const char *szSeparator, int dwMaxDepth) const;
	virtual inline DFGNode copy() const = 0;

friend class DFGraphImpl;
friend class BrokerImpl;
friend class SignatureEvaluatorImpl;
};

class DFGConstantImpl : public DFGNodeImpl {
public:
	DFGConstantImpl(unsigned int dwValue);
	DFGConstantImpl(const DFGConstantImpl &) = default;
	~DFGConstantImpl();
	std::string mnemonic() const;
	std::string idx() const;
	std::string expression(int dwMaxDepth = -1) const;

	unsigned int dwValue;

protected:
	inline DFGNode copy() const {
		DFGConstant oCopy(DFGConstant::create(dwValue));
		oCopy->dwNodeId = dwNodeId;
		return oCopy->toGeneric();
	}

friend DFGConstant;
};

class DFGRegisterImpl : public DFGNodeImpl {
public:
	DFGRegisterImpl(unsigned char bRegister);
	DFGRegisterImpl(const DFGRegisterImpl &) = default;
	~DFGRegisterImpl();
	std::string mnemonic() const;
	std::string idx() const;
	std::string expression(int dwMaxDepth = -1) const;

	unsigned char bRegister;

protected:
	inline DFGNode copy() const {
		DFGRegister oCopy(DFGRegister::create(bRegister));
		oCopy->dwNodeId = dwNodeId;
		return oCopy->toGeneric();
	}

friend DFGRegister;
};

class DFGAddImpl : public DFGNodeImpl {
public:
	DFGAddImpl(DFGNode &oNode1, DFGNode &oNode2);
	DFGAddImpl(const DFGAddImpl &) = default;
	~DFGAddImpl();
	std::string mnemonic() const;
	std::string idx() const;
	std::string expression(int dwMaxDepth = -1) const;

protected:
	inline DFGNode copy() const {
		DFGAdd oCopy(DFGAdd::create());
		oCopy->dwNodeId = dwNodeId;
		return oCopy->toGeneric();
	}
private:
	inline DFGAddImpl() : DFGNodeImpl(NODE_TYPE_ADD) { }

friend DFGAdd;
};

class DFGMultImpl : public DFGNodeImpl {
public:
	DFGMultImpl(DFGNode &oNode1, DFGNode &oNode2);
	DFGMultImpl(const DFGMultImpl &) = default;
	~DFGMultImpl();
	std::string mnemonic() const;
	std::string idx() const;
	std::string expression(int dwMaxDepth = -1) const;

protected:
	inline DFGNode copy() const {
		DFGMult oCopy(DFGMult::create());
		oCopy->dwNodeId = dwNodeId;
		return oCopy->toGeneric();
	}
private:
	inline DFGMultImpl() : DFGNodeImpl(NODE_TYPE_MULT) { }

friend DFGMult;
};

class DFGCallImpl : public DFGNodeImpl {
public:
	DFGCallImpl(unsigned long lpAddress, DFGNode &oArgument1);
	DFGCallImpl(const DFGCallImpl &) = default;
	~DFGCallImpl();
	std::string mnemonic() const;
	std::string idx() const;
	std::string expression(int dwMaxDepth = -1) const;

	unsigned long lpAddress;

protected:
	inline DFGNode copy() const {
		DFGCall oCopy(DFGCall::create());
		oCopy->dwNodeId = dwNodeId;
		oCopy->lpAddress = lpAddress;
		return oCopy->toGeneric();
	}
private:
	inline DFGCallImpl() : DFGNodeImpl(NODE_TYPE_CALL) { }
	std::string label() const;

friend DFGCall;
};

class DFGLoadImpl : public DFGNodeImpl {
public:
	DFGLoadImpl(DFGNode &oMemoryAddress);
	DFGLoadImpl(const DFGLoadImpl &) = default;
	~DFGLoadImpl();
	std::string mnemonic() const;
	std::string idx() const;
	std::string expression(int dwMaxDepth = -1) const;

protected:
	inline DFGNode copy() const {
		DFGLoad oCopy(DFGLoad::create());
		oCopy->dwNodeId = dwNodeId;
		return oCopy->toGeneric();
	}
private:
	inline DFGLoadImpl() : DFGNodeImpl(NODE_TYPE_LOAD) { }

friend DFGLoad;
};

class DFGStoreImpl : public DFGNodeImpl {
public:
	DFGStoreImpl(DFGNode &oData, DFGNode &oMemoryAddress);
	DFGStoreImpl(const DFGStoreImpl &) = default;
	~DFGStoreImpl();
	std::string mnemonic() const;
	std::string idx() const;
	std::string expression(int dwMaxDepth = -1) const;

protected:
	inline DFGNode copy() const {
		DFGStore oCopy(DFGStore::create());
		oCopy->dwNodeId = dwNodeId;
		return oCopy->toGeneric();
	}
private:
	inline DFGStoreImpl() : DFGNodeImpl(NODE_TYPE_STORE) { }

friend DFGStore;
};

class DFGXorImpl : public DFGNodeImpl {
public:
	DFGXorImpl(DFGNode &oNode1, DFGNode &oNode2);
	DFGXorImpl(const DFGXorImpl &) = default;
	~DFGXorImpl();
	std::string mnemonic() const;
	std::string idx() const;
	std::string expression(int dwMaxDepth = -1) const;

protected:
	inline DFGNode copy() const {
		DFGXor oCopy(DFGXor::create());
		oCopy->dwNodeId = dwNodeId;
		return oCopy->toGeneric();
	}
private:
	inline DFGXorImpl() : DFGNodeImpl(NODE_TYPE_XOR) { }

friend DFGXor;
};

class DFGAndImpl : public DFGNodeImpl {
public:
	DFGAndImpl(DFGNode &oNode1, DFGNode &oNode2);
	DFGAndImpl(const DFGAndImpl &) = default;
	~DFGAndImpl();
	std::string mnemonic() const;
	std::string idx() const;
	std::string expression(int dwMaxDepth = -1) const;

protected:
	inline DFGNode copy() const {
		DFGAnd oCopy(DFGAnd::create());
		oCopy->dwNodeId = dwNodeId;
		return oCopy->toGeneric();
	}
private:
	inline DFGAndImpl() : DFGNodeImpl(NODE_TYPE_AND) { }

friend DFGAnd;
};

class DFGOrImpl : public DFGNodeImpl {
public:
	DFGOrImpl(DFGNode &oNode1, DFGNode &oNode2);
	DFGOrImpl(const DFGOrImpl &) = default;
	~DFGOrImpl();
	std::string mnemonic() const;
	std::string idx() const;
	std::string expression(int dwMaxDepth = -1) const;

protected:
	inline DFGNode copy() const {
		DFGOr oCopy(DFGOr::create());
		oCopy->dwNodeId = dwNodeId;
		return oCopy->toGeneric();
	}
private:
	inline DFGOrImpl() : DFGNodeImpl(NODE_TYPE_OR) { }

friend DFGOr;
};

class DFGShiftImpl : public DFGNodeImpl {
public:
	DFGShiftImpl(DFGNode &oNode, DFGNode &oAmount);
	DFGShiftImpl(const DFGShiftImpl &) = default;
	~DFGShiftImpl();
	std::string mnemonic() const;
	std::string idx() const;
	std::string expression(int dwMaxDepth = -1) const;

protected:
	inline DFGNode copy() const {
		DFGShift oCopy(DFGShift::create());
		oCopy->dwNodeId = dwNodeId;
		return oCopy->toGeneric();
	}
private:
	inline DFGShiftImpl() : DFGNodeImpl(NODE_TYPE_SHIFT) { }

friend DFGShift;
};

class DFGRotateImpl : public DFGNodeImpl {
public:
	DFGRotateImpl(DFGNode &oNode, DFGNode &oAmount);
	DFGRotateImpl(const DFGRotateImpl &) = default;
	~DFGRotateImpl();
	std::string mnemonic() const;
	std::string idx() const;
	std::string expression(int dwMaxDepth = -1) const;

protected:
	inline DFGNode copy() const {
		DFGRotate oCopy(DFGRotate::create());
		oCopy->dwNodeId = dwNodeId;
		return oCopy->toGeneric();
	}
private:
	inline DFGRotateImpl() : DFGNodeImpl(NODE_TYPE_ROTATE) { }

friend DFGRotate;
};

class DFGOpaqueImpl : public DFGNodeImpl {
public:
	DFGOpaqueImpl(unsigned int dwOpaqueId, const std::string &szOpaqueRef, int dwOpaqueRefId);
	DFGOpaqueImpl(const DFGOpaqueImpl &) = default;
	~DFGOpaqueImpl();
	std::string mnemonic() const;
	std::string idx() const;
	std::string expression(int dwMaxDepth = -1) const;

	unsigned int dwOpaqueId;
	std::string szOpaqueRef;
	int dwOpaqueRefId;

protected:
	inline DFGNode copy() const {
		DFGOpaque oCopy(DFGOpaque::create());
		oCopy->dwNodeId = dwNodeId;
		oCopy->dwOpaqueId = dwOpaqueId;
		oCopy->szOpaqueRef = szOpaqueRef;
		oCopy->dwOpaqueRefId = dwOpaqueRefId;
		return oCopy->toGeneric();
	}
private:
	inline DFGOpaqueImpl() : DFGNodeImpl(NODE_TYPE_OPAQUE) { }

friend DFGOpaque;
};

class DFGCarryImpl : public DFGNodeImpl {
public:
	DFGCarryImpl(DFGNode &oNode);
	DFGCarryImpl(const DFGCarryImpl &) = default;
	~DFGCarryImpl();
	std::string mnemonic() const;
	std::string idx() const;
	std::string expression(int dwMaxDepth = -1) const;

protected:
	inline DFGNode copy() const {
		DFGCarry oCopy(DFGCarry::create());
		oCopy->dwNodeId = dwNodeId;
		return oCopy->toGeneric();
	}
private:
	inline DFGCarryImpl() : DFGNodeImpl(NODE_TYPE_CARRY) { }

friend DFGCarry;
};

class DFGOverflowImpl : public DFGNodeImpl {
public:
	DFGOverflowImpl(DFGNode &oNode);
	DFGOverflowImpl(const DFGOverflowImpl &) = default;
	~DFGOverflowImpl();
	std::string mnemonic() const;
	std::string idx() const;
	std::string expression(int dwMaxDepth = -1) const;
	
protected:
	inline DFGNode copy() const {
		DFGOverflow oCopy(DFGOverflow::create());
		oCopy->dwNodeId = dwNodeId;
		return oCopy->toGeneric();
	}
private:
	inline DFGOverflowImpl() : DFGNodeImpl(NODE_TYPE_OVERFLOW) { }

friend DFGOverflow;
};
