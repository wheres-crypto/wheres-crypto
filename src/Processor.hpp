#pragma once

#include "types.hpp"

typedef enum {
	PROCESSOR_STATUS_INTERNAL_ERROR = 0,
	PROCESSOR_STATUS_OK,
	PROCESSOR_STATUS_DONE
	//PROCESSOR_STATUS_BRANCH
} processor_status_t;

class ProcessorImpl: virtual public ReferenceCounted {
public:
	inline ProcessorImpl() { }
	inline ~ProcessorImpl() { }

	virtual void initialize(CodeBroker &oBuilder) = 0;
	virtual processor_status_t instruction(CodeBroker &oBuilder, unsigned long *lpNextAddress, unsigned long lpAddress) = 0;
	virtual bool ShouldClean(DFGNode &oNode) = 0;
protected:
	virtual Processor Migrate(DFGraph oGraph) = 0;

friend class CodeBrokerImpl;
};