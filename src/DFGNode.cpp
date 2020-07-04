#include <sstream>
#include <ida.hpp>
#include <funcs.hpp>
#include <idp.hpp>

#include "DFGNode.hpp"
#include "ThreadPool.hpp"

std::string DFGNodeImpl::GenericIdx(const char *szPrefix) const {
	std::list<DFGNode>::const_iterator it;
	std::stringstream oStream;
	oStream << szPrefix;
	oStream << "(";
	for (it = aInputNodes.begin(); it != aInputNodes.end(); it++) {
		if (it != aInputNodes.begin()) {
			oStream << ",";
		}
		oStream << std::to_string((*it)->dwNodeId);
	}
	oStream << ")";
	return oStream.str();
}

std::string DFGNodeImpl::GenericExpression(const char *szPrefix, const char *szSeparator, int dwMaxDepth) const {
	std::list<DFGNode>::const_iterator it;
	std::stringstream oStream;
	oStream << szPrefix;
	oStream << "(";
	if (dwMaxDepth == 0) {
		oStream << "...";
	} else {
		for (it = aInputNodes.begin(); it != aInputNodes.end(); it++) {
			if (it != aInputNodes.begin()) {
				oStream << szSeparator;
			}
			oStream << (*it)->expression(dwMaxDepth < 0 ? dwMaxDepth : dwMaxDepth - 1);
		}
	}
	oStream << ")";
	return oStream.str();
}

DFGConstantImpl::DFGConstantImpl(unsigned int dwValue): DFGNodeImpl(NODE_TYPE_CONSTANT), dwValue(dwValue) { }
DFGConstantImpl::~DFGConstantImpl() { }

std::string DFGConstantImpl::mnemonic() const {
	char szMnemonic[24];
	if ((int)dwValue < 0) {
		qsnprintf(szMnemonic, 24, "CONST:-%x", -(int)dwValue);
	} else {
		qsnprintf(szMnemonic, 24, "CONST:%x", (int)dwValue);
	}
	return std::string(szMnemonic);
}

std::string DFGConstantImpl::idx() const {
	std::stringstream oStream;
	oStream << "C";
	oStream << std::hex << dwValue;

	return oStream.str();
}

std::string DFGConstantImpl::expression(int dwMaxDepth) const {
	return mnemonic();
}

DFGRegisterImpl::DFGRegisterImpl(unsigned char bRegister): DFGNodeImpl(NODE_TYPE_REGISTER), bRegister(bRegister) { }
DFGRegisterImpl::~DFGRegisterImpl() { }

std::string DFGRegisterImpl::mnemonic() const {
	switch (this->bRegister) {
	case 13:
		return std::string("SP");
	case 14:
		return std::string("LR");
	case 15:
		return std::string("PC");
	default:
		return std::string("R") + std::to_string(bRegister);
	}
}

std::string DFGRegisterImpl::idx() const {
	return std::string("R") + std::to_string(bRegister);
}

std::string DFGRegisterImpl::expression(int dwMaxDepth) const {
	return mnemonic();
}

DFGAddImpl::DFGAddImpl(DFGNode &oNode1, DFGNode &oNode2) : DFGNodeImpl(NODE_TYPE_ADD) {
	aInputNodes.push_back(oNode1);
	aInputNodes.push_back(oNode2);
}
DFGAddImpl::~DFGAddImpl() { }

std::string DFGAddImpl::mnemonic() const {
	return std::string("ADD");
}

std::string DFGAddImpl::idx() const {
	return GenericIdx("A");
}

std::string DFGAddImpl::expression(int dwMaxDepth) const {
	return GenericExpression("", "+", dwMaxDepth);
}

DFGMultImpl::DFGMultImpl(DFGNode &oNode1, DFGNode &oNode2) : DFGNodeImpl(NODE_TYPE_MULT) {
	aInputNodes.push_back(oNode1);
	aInputNodes.push_back(oNode2);
}
DFGMultImpl::~DFGMultImpl() { }

std::string DFGMultImpl::mnemonic() const {
	return std::string("MULT");
}

std::string DFGMultImpl::idx() const {
	return GenericIdx("M");
}

std::string DFGMultImpl::expression(int dwMaxDepth) const {
	return GenericExpression("", "*", dwMaxDepth);
}

DFGCallImpl::DFGCallImpl(unsigned long lpAddress, DFGNode &oArgument1) : DFGNodeImpl(NODE_TYPE_CALL), lpAddress(lpAddress) {
	aInputNodes.push_back(oArgument1);
}
DFGCallImpl::~DFGCallImpl() { }

std::string DFGCallImpl::label() const {
	qstring szFunctionName;
	std::stringstream oStream;
	API_LOCK();
	get_func_name(&szFunctionName, lpAddress & ~1);
	API_UNLOCK();
	if (szFunctionName.length() == 0) {
		oStream << "sub_";
		oStream << std::hex << lpAddress;
	} else {
		oStream << szFunctionName.c_str();
	}
	return oStream.str();
}

std::string DFGCallImpl::mnemonic() const {
	std::stringstream oStream;
	oStream << "CALL:" << label();
	return oStream.str();
}

std::string DFGCallImpl::idx() const {
	std::stringstream oStream;
	oStream << "C" << label();
	return GenericIdx(oStream.str().c_str());
}

std::string DFGCallImpl::expression(int dwMaxDepth) const {
	return GenericExpression(mnemonic().c_str(), ",", dwMaxDepth);
}

DFGOpaqueImpl::DFGOpaqueImpl(unsigned int dwOpaqueId, const std::string &szOpaqueRef, int dwOpaqueRefId)
  : DFGNodeImpl(NODE_TYPE_OPAQUE), dwOpaqueId(dwOpaqueId), szOpaqueRef(szOpaqueRef), dwOpaqueRefId(dwOpaqueRefId) { }
DFGOpaqueImpl::~DFGOpaqueImpl() { }

std::string DFGOpaqueImpl::mnemonic() const {
	std::stringstream oStream;
	oStream << "OPAQUE";
	if (szOpaqueRef.length()) {
		oStream << "<";
		oStream << szOpaqueRef;
		oStream << ">";
	}
	return oStream.str();
}

std::string DFGOpaqueImpl::idx() const {
	std::stringstream oStream;
	oStream << "O";
	oStream << dwOpaqueId;
	return oStream.str();
}

std::string DFGOpaqueImpl::expression(int dwMaxDepth) const {
	return mnemonic();
}

DFGCarryImpl::DFGCarryImpl(DFGNode &oNode) : DFGNodeImpl(NODE_TYPE_CARRY) {
	aInputNodes.push_back(oNode);
}
DFGCarryImpl::~DFGCarryImpl() { }

std::string DFGCarryImpl::mnemonic() const {
	return std::string("CARRY");
}

std::string DFGCarryImpl::idx() const {
	return GenericIdx("CA");
}

std::string DFGCarryImpl::expression(int dwMaxDepth) const {
	return GenericExpression("CARRY", ",", dwMaxDepth);
}

DFGOverflowImpl::DFGOverflowImpl(DFGNode& oNode) : DFGNodeImpl(NODE_TYPE_OVERFLOW) {
	aInputNodes.push_back(oNode);
}
DFGOverflowImpl::~DFGOverflowImpl() { }

std::string DFGOverflowImpl::mnemonic() const {
	return std::string("OVERFLOW");
}

std::string DFGOverflowImpl::idx() const {
	return GenericIdx("OV");
}

std::string DFGOverflowImpl::expression(int dwMaxDepth) const {
	return GenericExpression("OVERFLOW", ",", dwMaxDepth);
}

DFGRotateImpl::DFGRotateImpl(DFGNode &oNode, DFGNode &oAmount) : DFGNodeImpl(NODE_TYPE_ROTATE) {
	aInputNodes.push_back(oNode);
	aInputNodes.push_back(oAmount);
}
DFGRotateImpl::~DFGRotateImpl() { }

std::string DFGRotateImpl::mnemonic() const {
	return std::string("ROTATE");
}

std::string DFGRotateImpl::idx() const {
	return GenericIdx("R");
}

std::string DFGRotateImpl::expression(int dwMaxDepth) const
{
	return GenericExpression("", " ROR ", dwMaxDepth);
}

DFGShiftImpl::DFGShiftImpl(DFGNode &oNode, DFGNode &oAmount) : DFGNodeImpl(NODE_TYPE_SHIFT) {
	aInputNodes.push_back(oNode);
	aInputNodes.push_back(oAmount);
}
DFGShiftImpl::~DFGShiftImpl() { }

std::string DFGShiftImpl::mnemonic() const {
	return std::string("SHIFT");
}

std::string DFGShiftImpl::idx() const {
	return GenericIdx("SL");
}

std::string DFGShiftImpl::expression(int dwMaxDepth) const {
	return GenericExpression("", "<<", dwMaxDepth);
}

DFGOrImpl::DFGOrImpl(DFGNode &oNode1, DFGNode &oNode2) : DFGNodeImpl(NODE_TYPE_OR) {
	aInputNodes.push_back(oNode1);
	aInputNodes.push_back(oNode2);
}
DFGOrImpl::~DFGOrImpl() { }

std::string DFGOrImpl::mnemonic() const {
	return std::string("ORR");
}

std::string DFGOrImpl::idx() const {
	return GenericIdx("OR");
}

std::string DFGOrImpl::expression(int dwMaxDepth) const {
	return GenericExpression("", "|", dwMaxDepth);
}

DFGAndImpl::DFGAndImpl(DFGNode &oNode1, DFGNode &oNode2) : DFGNodeImpl(NODE_TYPE_AND) {
	aInputNodes.push_back(oNode1);
	aInputNodes.push_back(oNode2);
}
DFGAndImpl::~DFGAndImpl() { }

std::string DFGAndImpl::mnemonic() const {
	return std::string("AND");
}

std::string DFGAndImpl::idx() const {
	return GenericIdx("AN");
}

std::string DFGAndImpl::expression(int dwMaxDepth) const {
	return GenericExpression("", "&", dwMaxDepth);
}

DFGXorImpl::DFGXorImpl(DFGNode &oNode1, DFGNode &oNode2) : DFGNodeImpl(NODE_TYPE_XOR) {
	aInputNodes.push_back(oNode1);
	aInputNodes.push_back(oNode2);
}
DFGXorImpl::~DFGXorImpl() { }

std::string DFGXorImpl::mnemonic() const {
	return std::string("XOR");
}

std::string DFGXorImpl::idx() const {
	return GenericIdx("X");
}

std::string DFGXorImpl::expression(int dwMaxDepth) const {
	return GenericExpression("", "^", dwMaxDepth);
}

DFGStoreImpl::DFGStoreImpl(DFGNode &oData, DFGNode &oMemoryAddress) : DFGNodeImpl(NODE_TYPE_STORE) {
	aInputNodes.push_back(oData);
	aInputNodes.push_back(oMemoryAddress);
}
DFGStoreImpl::~DFGStoreImpl() { }

std::string DFGStoreImpl::mnemonic() const {
	return std::string("STORE");
}

std::string DFGStoreImpl::idx() const {
	return GenericIdx("S");
}

std::string DFGStoreImpl::expression(int dwMaxDepth) const {
	return GenericExpression("STORE", ",", dwMaxDepth);
}

DFGLoadImpl::DFGLoadImpl(DFGNode &oMemoryAddress) : DFGNodeImpl(NODE_TYPE_LOAD) {
	aInputNodes.push_back(oMemoryAddress);
}
DFGLoadImpl::~DFGLoadImpl() { }

std::string DFGLoadImpl::mnemonic() const {
	return std::string("LOAD");
}

std::string DFGLoadImpl::idx() const {
	return GenericIdx("L");
}

std::string DFGLoadImpl::expression(int dwMaxDepth) const {
	return GenericExpression("LOAD", ",", dwMaxDepth);
}
