#include <idp.hpp>

#include "common.hpp"
#include "SignatureParser.hpp"
#include "DFGraph.hpp"
#include "DFGNode.hpp"
#include "Broker.hpp"

SignatureParserImpl::SignatureParserImpl() { }

std::string SignatureParserImpl::GetKeyword(const std::string& szKeyword) {
	PushOffset();
	SkipSpaces();
	if (
		dwOffset + szKeyword.length() >= dwLength ||
		strncmp(&szSignatureDefinition[dwOffset], szKeyword.c_str(), szKeyword.length()) != 0
	) {
		PopOffset();
		return "";
	}

	dwOffset += szKeyword.length();
	if (!SkipSpaces()) {
		PopOffset();
		return "";
	}

	char szValue[64];
	unsigned int dwValueOffset = 0;
	while (dwOffset < dwLength) {
		if (szSignatureDefinition[dwOffset] != '\n') {
			if (dwValueOffset < 63) {
				szValue[dwValueOffset] = szSignatureDefinition[dwOffset];
				dwValueOffset++;
			}
			dwOffset++;
			continue;
		}
		break;
	}

	while (dwValueOffset > 0 && isspace(szValue[dwValueOffset - 1])) {
		dwValueOffset--;
	}
	szValue[dwValueOffset] = '\0';

	if (dwValueOffset != 0) {
		ClearOffset();
		return szValue;
	}

	PopOffset();
	return "";
}

parser_status_t SignatureParserImpl::Parse(SignatureDefinition *lpResult, const std::string& szSignatureDefinition, const std::string &szFilename) {
	this->szSignatureDefinition = szSignatureDefinition;
	dwOffset = 0;
	dwLength = szSignatureDefinition.length();
	aStack.clear();
	aLabeledNodes.clear();
	SignatureBroker oBuilder(SignatureBroker::create());
	SignatureDefinition oResult(SignatureDefinition::create());

	std::string szIdentifier = GetKeyword("IDENTIFIER");
	std::string szVariant = GetKeyword("VARIANT");

	if (szIdentifier.length() == 0) {
		szIdentifier = szFilename;
	}
	oResult->SetIdentifier(szIdentifier);
	oBuilder->SetIdentifier(szIdentifier);

	if (szVariant.length() != 0) {
		oBuilder->SetVariant(szVariant);
	}

	for (;;) {
		bool bTransient;
		szVariant = GetKeyword("VARIANT");
		if (szVariant.length() != 0) {
			oBuilder->Cleanup();
			if (oBuilder->oGraph->size() != 0) {
				oBuilder->dwNumOpaqueRefs = aOpaqueLabels.size();
				oResult->push_back(oBuilder);
			}
			oBuilder = SignatureBroker::create();
			oBuilder->SetIdentifier(szIdentifier);
			oBuilder->SetVariant(szVariant);
			aLabeledNodes.clear();
			aOpaqueLabels.clear();
			aStack.clear();
		}
		SkipSpaces();
		PushOffset();
		if (dwOffset + 9 < dwLength &&
			strncmp(&szSignatureDefinition[dwOffset], "TRANSIENT", 9) == 0 && 
			(dwOffset += 9, SkipSpaces())
		) {
			bTransient = true;
		}
		bTransient = false;
		std::string szLabel = ParseName_Impl();
		SkipSpaces();
		if (szLabel.length() != 0 && dwOffset < dwLength && szSignatureDefinition[dwOffset] == ':') {
			ClearOffset();
			dwOffset++;
			SkipSpaces();
		} else {
			PopOffset();
			szLabel.clear();
		}
		DFGNode oSignatureDefinition = ParseExpression(oBuilder);
		if (oSignatureDefinition != nullptr) {
			if (!bTransient) {
				oBuilder->FlagNodeToKeep(oSignatureDefinition);
			}
			if (szLabel.length() != 0) {
				std::map<std::string, DFGNode>::iterator it = aLabeledNodes.find(szLabel);
				if (it != aLabeledNodes.end()) {
					wc_error("[-] Re-defining label '%s' at offset %u\n", szLabel.c_str(), dwOffset);
					return PARSER_STATUS_NOK;
				}
				aLabeledNodes.insert(std::pair<std::string, DFGNode>(szLabel, oSignatureDefinition));
			}
			SkipSpaces();
			if (dwOffset >= dwLength || szSignatureDefinition[dwOffset] != ';') {
				break;
			}
			dwOffset++;
			continue;
		}
		break;
	}

	if (dwOffset < dwLength) {
		wc_error("Unexpected token '%c' at offset %u\n", szSignatureDefinition[dwOffset], dwOffset);
		return PARSER_STATUS_NOK;
	}

	oBuilder->Cleanup();
	if (oBuilder->oGraph->size()) {
		oBuilder->dwNumOpaqueRefs = aOpaqueLabels.size();
		oResult->push_back(oBuilder);
	}

	*lpResult = oResult;
	return PARSER_STATUS_OK;
}

void SignatureParserImpl::PushOffset() {
	aStack.push_back(dwOffset);
}
void SignatureParserImpl::ClearOffset() {
	if (aStack.begin() != aStack.end()) {
		aStack.pop_back();
	}
}
void SignatureParserImpl::PopOffset() {
	if (aStack.begin() != aStack.end()) {
		dwOffset = aStack.back();
		aStack.pop_back();
	}
}
bool SignatureParserImpl::SkipSpaces() {
	bool bResult = false;
	while (dwOffset < dwLength && (
		szSignatureDefinition[dwOffset] == ' ' ||
		szSignatureDefinition[dwOffset] == '\n' ||
		szSignatureDefinition[dwOffset] == '\r'
	)) {
		dwOffset++;
		bResult = true;
	}
	return bResult;
}

DFGNode SignatureParserImpl::ParseNumber(SignatureBroker &oBuilder) {
	long long dwFactor = 1;
	PushOffset();

	if (dwOffset < dwLength && szSignatureDefinition[dwOffset] == '-') {
		dwFactor = -1;
		dwOffset += 1;
	}

	if (dwOffset + 2 <= dwLength && szSignatureDefinition[dwOffset] == '0' && szSignatureDefinition[dwOffset + 1] == 'x') {
		dwOffset += 2;
		unsigned int dwHexOffset = 0;
		char szHex[16];

		while (dwOffset < dwLength) {
			if (dwHexOffset < 15 && (
				(szSignatureDefinition[dwOffset] >= '0' && szSignatureDefinition[dwOffset] <= '9') ||
				(szSignatureDefinition[dwOffset] >= 'a' && szSignatureDefinition[dwOffset] <= 'f') ||
				(szSignatureDefinition[dwOffset] >= 'A' && szSignatureDefinition[dwOffset] <= 'F')
			)) {
				szHex[dwHexOffset] = szSignatureDefinition[dwOffset];
				dwOffset++;
				dwHexOffset++;
				continue;
			}
			break;
		}

		szHex[dwHexOffset] = '\0';
		if (dwHexOffset == 0) {
			PopOffset();
			return nullptr;
		}
		ClearOffset();
		return oBuilder->NewConstant(dwFactor * strtol(szHex, NULL, 16));
	} else {
		unsigned int dwDecimalOffset = 0;
		char szDecimal[16];

		while (dwOffset < dwLength) {
			if (dwDecimalOffset < 15 && (
				(szSignatureDefinition[dwOffset] >= '0' && szSignatureDefinition[dwOffset] <= '9')
			)) {
				szDecimal[dwDecimalOffset] = szSignatureDefinition[dwOffset];
				dwOffset++;
				dwDecimalOffset++;
				continue;
			}
			break;
		}

		szDecimal[dwDecimalOffset] = '\0';
		if (dwDecimalOffset == 0) {
			PopOffset();
			return nullptr;
		}
		ClearOffset();
		return oBuilder->NewConstant(dwFactor * strtoull(szDecimal, NULL, 10));
	}
}

std::string SignatureParserImpl::ParseName_Impl() {
	PushOffset();
	char szName[32];
	unsigned int dwNameOffset = 0;

	if (dwOffset < dwLength && (
		(szSignatureDefinition[dwOffset] >= 'a' && szSignatureDefinition[dwOffset] <= 'z') ||
		(szSignatureDefinition[dwOffset] >= 'A' && szSignatureDefinition[dwOffset] <= 'Z') ||
		(szSignatureDefinition[dwOffset] == '_')
	)) {
		szName[dwNameOffset] = szSignatureDefinition[dwOffset];
		dwOffset++;
		dwNameOffset++;
	} else {
		PopOffset();
		return "";
	}

	while (dwOffset < dwLength) {
		if (dwOffset < dwLength && dwNameOffset < 31 && (
			(szSignatureDefinition[dwOffset] >= 'a' && szSignatureDefinition[dwOffset] <= 'z') ||
			(szSignatureDefinition[dwOffset] >= 'A' && szSignatureDefinition[dwOffset] <= 'Z') ||
			(szSignatureDefinition[dwOffset] >= '0' && szSignatureDefinition[dwOffset] <= '9') ||
			(szSignatureDefinition[dwOffset] == '_')
		)) {
			szName[dwNameOffset] = szSignatureDefinition[dwOffset];
			dwOffset++;
			dwNameOffset++;
			continue;
		}
		break;
	}
	szName[dwNameOffset] = '\0';

	ClearOffset();
	return std::string(szName);
}

DFGNode SignatureParserImpl::ParseName(SignatureBroker &oBuilder) {
	PushOffset();
	std::string szName = ParseName_Impl();
	if (szName.length() != 0) {
		ClearOffset();
		std::map<std::string, DFGNode>::iterator it = aLabeledNodes.find(szName);
		if (it == aLabeledNodes.end()) {
			wc_error("Referencing label '%s' which is undefined at offset %u\n", szName.c_str(), dwOffset);
			return nullptr;
		}
		return it->second;
	} else {
		PopOffset();
		return nullptr;
	}
}

DFGNode SignatureParserImpl::ParseFunction(SignatureBroker &oBuilder) {
	PushOffset();
	std::string szName = ParseName_Impl();
	std::string szOpaqueRef;
	int dwOpaqueRefId = -1;
	std::list<DFGNode> aArguments;
	std::list<DFGNode>::iterator it;

	if (szName.length() == 0) {
		PopOffset();
		return nullptr;
	}

	SkipSpaces();

	if (
		szName.compare("OPAQUE") == 0 &&
		dwOffset < dwLength &&
		szSignatureDefinition[dwOffset] == '<' && szSignatureDefinition[dwOffset+1] != '<'
	) {
		std::map<std::string, int>::iterator it;
		dwOffset++;
		SkipSpaces();
		szOpaqueRef = ParseName_Impl();
		if (szOpaqueRef.length() == 0) {
			wc_error("Opaque ref cannot be empty at offset %u\n", dwOffset);
			PopOffset();
			return nullptr;
		}
		SkipSpaces();
		if (dwOffset >= dwLength || szSignatureDefinition[dwOffset] != '>') {
			wc_error("Expected '>' at offset %u\n", dwOffset);
			PopOffset();
			return nullptr;
		}
		dwOffset++;
		SkipSpaces();

		it = aOpaqueLabels.find(szOpaqueRef);
		if (it == aOpaqueLabels.end()) {
			dwOpaqueRefId = aOpaqueLabels.size();
			aOpaqueLabels.insert(std::pair<std::string, int>(szOpaqueRef, dwOpaqueRefId));
		} else {
			dwOpaqueRefId = it->second;
		}
	}

	if (dwOffset >= dwLength || szSignatureDefinition[dwOffset] != '(') {
		if (szName.compare("OPAQUE") == 0) {
			/* opaque is allowed with no arguments */
			ClearOffset();
			return oBuilder->NewOpaque(szOpaqueRef, dwOpaqueRefId);
		}
		PopOffset();
		return nullptr;
	}
	dwOffset++;

	for (;;) {
		SkipSpaces();
		DFGNode oArgument = ParseExpression(oBuilder);
		if (oArgument != nullptr) {
			aArguments.push_back(oArgument);
			SkipSpaces();
			if (dwOffset >= dwLength || szSignatureDefinition[dwOffset] != ',') {
				break;
			}
			dwOffset++;
			continue;
		}
		break;
	}

	if (dwOffset >= dwLength || szSignatureDefinition[dwOffset] != ')') {
		wc_error("Expected ')' at offset %u\n", dwOffset);
		PopOffset();
		return nullptr;
	}

	dwOffset++;

	if (szName.compare("STORE") == 0) {
		if (aArguments.size() != 2) {
			wc_error("STORE must have exactly 2 arguments (found %u) at offset %u\n", aArguments.size(), dwOffset);
			PopOffset();
			return nullptr;
		}
		ClearOffset();
		return oBuilder->NewStore(*aArguments.begin(), *++aArguments.begin());
	} else if (szName.compare("LOAD") == 0) {
		if (aArguments.size() != 1) {
			wc_error("LOAD must have exactly 1 argument (found %u) at offset %u\n", aArguments.size(), dwOffset);
			PopOffset();
			return nullptr;
		}
		ClearOffset();
		return oBuilder->NewLoad(*aArguments.begin());
	} else if (szName.compare("XOR") == 0) {
		if (aArguments.size() < 2) {
			wc_error("XOR can not have less than 2 arguments (found %u) at offset %u\n", aArguments.size(), dwOffset);
			PopOffset();
			return nullptr;
		}
		ClearOffset();
		DFGNode oTemp(oBuilder->NewXor(*aArguments.begin(), *++aArguments.begin()));
		it = ++(++aArguments.begin());
		for (; it != aArguments.end(); it++) {
			oTemp = oBuilder->NewXor(oTemp, *it);
		}
		return oTemp;
	} else if (szName.compare("OR") == 0) {
		if (aArguments.size() < 2) {
			wc_error("OR can not have less than 2 arguments (found %u) at offset %u\n", aArguments.size(), dwOffset);
			PopOffset();
			return nullptr;
		}
		ClearOffset();
		DFGNode oTemp(oBuilder->NewOr(*aArguments.begin(), *++aArguments.begin()));
		it = ++(++aArguments.begin());
		for (; it != aArguments.end(); it++) {
			oTemp = oBuilder->NewOr(oTemp, *it);
		}
		return oTemp;
	} else if (szName.compare("AND") == 0) {
		if (aArguments.size() < 2) {
			wc_error("AND can not have less than 2 arguments (found %u) at offset %u\n", aArguments.size(), dwOffset);
			PopOffset();
			return nullptr;
		}
		ClearOffset();
		DFGNode oTemp(oBuilder->NewAnd(*aArguments.begin(), *++aArguments.begin()));
		it = ++(++aArguments.begin());
		for (; it != aArguments.end(); it++) {
			oTemp = oBuilder->NewAnd(oTemp, *it);
		}
		return oTemp;
	} else if (szName.compare("MULT") == 0) {
		if (aArguments.size() < 2) {
			wc_error("MULT can not have less than 2 arguments (found %u) at offset %u\n", aArguments.size(), dwOffset);
			PopOffset();
			return nullptr;
		}
		ClearOffset();
		DFGNode oTemp(oBuilder->NewMult(*aArguments.begin(), *++aArguments.begin()));
		it = ++(++aArguments.begin());
		for (; it != aArguments.end(); it++) {
			oTemp = oBuilder->NewMult(oTemp, *it);
		}
		return oTemp;
	} else if (szName.compare("ROTATE") == 0) {
		if (aArguments.size() != 2) {
			wc_error("ROTATE must have 2 arguments (found %u) at offset %u\n", aArguments.size(), dwOffset);
			PopOffset();
			return nullptr;
		}
		ClearOffset();
		return oBuilder->NewRotate(*aArguments.begin(), *++aArguments.begin());
	} else if (szName.compare("OPAQUE") == 0) {
		ClearOffset();
		/*
		 * somewhat dirty code below: NewOpaque does not allow us to specify input nodes,
		 * and even if it did, we can't build our node similar to ADD/AND/OR/XOR/MULT, etc.
		 * since OPAQUE(OPAQUE(x,y),z) need not necessarily equal OPAQUE(x,y,z).
		 * So we build the new node here, declaring the list of input nodes directly
		 */
		DFGOpaqueImpl oTemp(oBuilder->oGraph->dwNodeCounter, szOpaqueRef, dwOpaqueRefId);
		oTemp.aInputNodes = aArguments;
		return oBuilder->FindNode(oTemp);
	} else {
		wc_error("Unknown function \"%s\" at offset %u\n", szName.c_str(), dwOffset);
		PopOffset();
		return nullptr;
	}
}

DFGNode SignatureParserImpl::ParseShift(SignatureBroker &oBuilder) {
	PushOffset();
	DFGNode oFirst = ParseExpression(oBuilder, EXPR_TYPE_ALL & ~(EXPR_TYPE_ADDITION | EXPR_TYPE_SHIFT));

	if (oFirst == nullptr) {
		PopOffset();
		return nullptr;
	}
	SkipSpaces();
	if ((dwOffset + 1) < dwLength && (
		(szSignatureDefinition[dwOffset] == '>' && szSignatureDefinition[dwOffset + 1] == '>') ||
		(szSignatureDefinition[dwOffset] == '<' && szSignatureDefinition[dwOffset + 1] == '<')
	)) {
		bool bRightShift = szSignatureDefinition[dwOffset] == '>';
		dwOffset += 2;
		SkipSpaces();
		DFGNode oSecond = ParseExpression(oBuilder, EXPR_TYPE_ALL & ~(EXPR_TYPE_ADDITION | EXPR_TYPE_SHIFT));
		if (oSecond == nullptr) {
			wc_error("Got a shift, but no amount at offset %d\n", dwOffset);
			PopOffset();
			return nullptr;
		}

		ClearOffset();
		DFGNode oShift = bRightShift ? oBuilder->NewMult(oSecond, oBuilder->NewConstant(-1)) : oSecond;
		return oBuilder->NewShift(oFirst, oShift);
	}
	PopOffset();
	return nullptr;
}

DFGNode SignatureParserImpl::ParseAddition(SignatureBroker &oBuilder) {
	PushOffset();
	DFGNode oFirst = ParseExpression(oBuilder, EXPR_TYPE_ALL & ~(EXPR_TYPE_ADDITION | EXPR_TYPE_SHIFT));

	if (oFirst == nullptr) {
		PopOffset();
		return nullptr;
	}
	SkipSpaces();
	if (dwOffset >= dwLength || szSignatureDefinition[dwOffset] != '+') {
		PopOffset();
		return nullptr;
	}

	dwOffset++;
	std::list<DFGNode> aArguments;
	std::list<DFGNode>::iterator it;
	aArguments.push_back(oFirst);

	for (;;) {
		SkipSpaces();
		DFGNode oOther = ParseExpression(oBuilder, EXPR_TYPE_ALL & ~(EXPR_TYPE_ADDITION | EXPR_TYPE_SHIFT));

		if (oOther != nullptr) {
			aArguments.push_back(oOther);
			SkipSpaces();
			if (dwOffset >= dwLength || szSignatureDefinition[dwOffset] != '+') {
				break;
			}
			dwOffset++;
			continue;
		}
		break;
	}

	if (aArguments.size() < 2) {
		PopOffset();
		return nullptr;
	}

	ClearOffset();
	DFGNode oTemp = *aArguments.begin();
	for (it = ++aArguments.begin(); it != aArguments.end(); it++) {
		oTemp = oBuilder->NewAdd(oTemp, *it);
	}
	return oTemp;
}

DFGNode SignatureParserImpl::ParseExpression(SignatureBroker &oBuilder, unsigned int dwTypeMask) {
	if (dwTypeMask & EXPR_TYPE_ADDITION) {
		DFGNode oAddition = ParseAddition(oBuilder);
		if (oAddition != nullptr) {
			return oAddition;
		}
	}

	if (dwTypeMask & EXPR_TYPE_SHIFT) {
		DFGNode oShift = ParseShift(oBuilder);
		if (oShift != nullptr) {
			return oShift;
		}
	}

	if (dwTypeMask & EXPR_TYPE_FUNCTION) {
		DFGNode oFunction = ParseFunction(oBuilder);
		if (oFunction != nullptr) {
			return oFunction;
		}
	}

	if (dwTypeMask & EXPR_TYPE_NAME) {
		DFGNode oName = ParseName(oBuilder);
		if (oName != nullptr) {
			return oName;
		}
	}

	if (dwTypeMask & EXPR_TYPE_NUMBER) {
		DFGNode oNumber = ParseNumber(oBuilder);
		if (oNumber != nullptr) {
			return oNumber;
		}
	}

	PushOffset();
	SkipSpaces();
	if (dwOffset < dwLength && szSignatureDefinition[dwOffset] == '(') {
		dwOffset++;
		DFGNode oRecurse = ParseExpression(oBuilder, EXPR_TYPE_ALL);
		if (oRecurse != nullptr) {
			SkipSpaces();
			if (dwOffset >= dwLength || szSignatureDefinition[dwOffset] != ')') {
				goto _no_recurse;
			}
			dwOffset++;
			ClearOffset();
			return oRecurse;
		}
	}
_no_recurse:
	PopOffset();
	return nullptr;
}