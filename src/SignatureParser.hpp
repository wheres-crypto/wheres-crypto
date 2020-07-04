#pragma once

#include <list>
#include <map>

#include "types.hpp"

#define EXPR_TYPE_FUNCTION 1
#define EXPR_TYPE_ADDITION 2
#define EXPR_TYPE_NAME 4
#define EXPR_TYPE_NUMBER 8
#define EXPR_TYPE_SHIFT 16
#define EXPR_TYPE_ALL 0x1f

typedef enum {
	PARSER_STATUS_OK,
	PARSER_STATUS_NOK
} parser_status_t;

class SignatureDefinitionImpl: virtual public ReferenceCounted, public std::list<SignatureBroker> {
public:
	inline SignatureDefinitionImpl() { }
	inline ~SignatureDefinitionImpl() { }

	inline void SetIdentifier(const std::string& szIdentifier) { this->szIdentifier = szIdentifier; }
	std::string szIdentifier;
};

class SignatureParserImpl : virtual public ReferenceCounted {
public:
	SignatureParserImpl();
	parser_status_t Parse(SignatureDefinition *lpResult, const std::string &szSignatureDefinition, const std::string& szFilename);

private:
	std::string szSignatureDefinition;
	unsigned int dwOffset;
	unsigned int dwLength;
	std::list<unsigned int> aStack;
	std::map<std::string, DFGNode> aLabeledNodes;
	SignatureDefinition oSignatureDefinition;
	std::map<std::string, int> aOpaqueLabels;

	void PushOffset();
	void ClearOffset();
	void PopOffset();
	bool SkipSpaces();

	std::string GetKeyword(const std::string& szKeyword);

	DFGNode ParseNumber(SignatureBroker &oBuilder);
	std::string ParseName_Impl();
	DFGNode ParseName(SignatureBroker &oBuilder);
	DFGNode ParseFunction(SignatureBroker &oBuilder);
	DFGNode ParseAddition(SignatureBroker &oBuilder);
	DFGNode ParseShift(SignatureBroker &oBuilder);
	DFGNode ParseExpression(SignatureBroker &oBuilder, unsigned int dwTypeMask = EXPR_TYPE_ALL);
};