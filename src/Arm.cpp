#include <ida.hpp>
#include <ua.hpp>
#include <idp.hpp>
#include <allins.hpp>
#include <segregs.hpp>

#include "common.hpp"
#include "Arm.hpp"
#include "DFGraph.hpp"
#include "Broker.hpp"
#include "Condition.hpp"

typedef enum {
	LSL,          // logical left         LSL #0 - don't shift
	LSR,          // logical right        LSR #0 means LSR #32
	ASR,          // arithmetic right     ASR #0 means ASR #32
	ROR,          // rotate right         ROR #0 means RRX
	RRX,          // extended rotate right
} shift_t;

typedef enum {
	cEQ,          // 0000 Z                        Equal
	cNE,          // 0001 !Z                       Not equal
	cCS,          // 0010 C                        Unsigned higher or same
	cCC,          // 0011 !C                       Unsigned lower
	cMI,          // 0100 N                        Negative
	cPL,          // 0101 !N                       Positive or Zero
	cVS,          // 0110 V                        Overflow
	cVC,          // 0111 !V                       No overflow
	cHI,          // 1000 C & !Z                   Unsigned higher
	cLS,          // 1001 !C | Z                   Unsigned lower or same
	cGE,          // 1010 (N & V) | (!N & !V)      Greater or equal
	cLT,          // 1011 (N & !V) | (!N & V)      Less than
	cGT,          // 1100 !Z & ((N & V)|(!N & !V)) Greater than
	cLE,          // 1101 Z | (N & !V) | (!N & V)  Less than or equal
	cAL,          // 1110 Always
	cNV          // 1111 Never
} cond_t;

// ARM insn.auxpref bits
#define aux_cond        0x0001  // set condition codes (S postfix is required)
#define aux_byte        0x0002  // byte transfer (B postfix is required)
#define aux_npriv       0x0004  // non-privileged transfer (T postfix is required)
#define aux_regsh       0x0008  // shift count is held in a register (see o_shreg)
#define aux_negoff      0x0010  // memory offset is negated in LDR,STR
#define aux_immcarry    0x0010  // carry flag is set to bit 31 of the immediate operand (see may_set_carry)
#define aux_wback       0x0020  // write back (! postfix is required)
#define aux_wbackldm    0x0040  // write back for LDM/STM (! postfix is required)
#define aux_postidx     0x0080  // post-indexed mode in LDR,STR
#define aux_ltrans      0x0100  // long transfer in LDC/STC (L postfix is required)
#define aux_wimm        0x0200  // thumb32 wide encoding of immediate constant (MOVW)
#define aux_sb          0x0400  // signed byte (SB postfix)
#define aux_sh          0x0800  // signed halfword (SH postfix)
#define aux_sw          (aux_sb|aux_sh) // signed word (SW postfix)
#define aux_h           0x1000  // halfword (H postfix)
#define aux_x           (aux_h|aux_byte) //   doubleword (X postfix)
#define aux_p           0x2000  // priviledged (P postfix)
#define aux_coproc      0x4000  // coprocessor instruction
#define aux_wide        0x8000  // thumb32 instruction (.W suffix)
#define aux_pac        0x10000  // Pointer Authentication Code instruction (see PAC_ flags)

void ArmImpl::initialize(CodeBroker &oBuilder) {
	unsigned int i;
	aRegisters.reserve(16);

	for (i = 0; i < 16; i++) {
		aRegisters.push_back(oBuilder->NewRegister(i));
	}

	dwMaxCallDepth = oBuilder->MaxCallDepth();
}

processor_status_t ArmImpl::JumpToNode(CodeBroker &oBuilder, unsigned long *lpNextAddress, unsigned long lpInstructionAddress, DFGNode oAddress) {
	unsigned long lpTarget(0);

	if (NODE_IS_CONSTANT(oAddress)) {
		lpTarget = oAddress->toConstant()->dwValue & ~1;
_jump:
		API_LOCK();
		segment_t *lpSegment = getseg(lpTarget);
		qstring szSegmentName;
		get_segm_name(&szSegmentName, lpSegment);
		API_UNLOCK();

		/* jumping to some address -> pop it off the call stack */
		PopCallStack(lpTarget);
		/* always create CALL node in case target is external, or max call depth is exceeded */
		if (lpSegment != NULL && szSegmentName != "extern" && aCallStack.size() < dwMaxCallDepth) {
			/*
			 * MaxCallDepth not reached -> continue the analysis by inlining
			 * we rely on the calling function to push entries onto the stack
			 * when a call is found. Thus, jumps should pass just fine
			 */
			*lpNextAddress = lpTarget;
		} else {
			DFGNode oCallNode = oBuilder->NewCall(lpTarget, GetRegister(oBuilder, lpInstructionAddress, 0));
			/*
			 * We're adding a CALL node, check if that function is set to return
			 * analysis stops here if it doesn't
			 */
			API_LOCK();
			func_t *lpFunction = get_func(lpTarget);
			bool bFunctionReturns = true;
			if (
				lpFunction != NULL &&
				(lpFunction->start_ea & ~1) == (lpTarget & ~1) && 
				!func_does_return(lpTarget & ~1)
			) {
				bFunctionReturns = false;
			}
			API_UNLOCK();
			if (!bFunctionReturns) {
				wc_debug("[*] CALL at address 0x%x does not return, stopping analysis\n", lpInstructionAddress);
				return PROCESSOR_STATUS_DONE;
			}

			SetRegister(oBuilder, lpInstructionAddress, lpNextAddress, 0, oCallNode);
			DFGNode oLr = GetRegister(oBuilder, lpInstructionAddress, 14);
			if (!NODE_IS_CONSTANT(oLr) && !(NODE_IS_REGISTER(oLr) && oLr->toRegister()->bRegister == 14)) {
				wc_debug("[-] loading of non-constant expression %s into PC is not supported @ 0x%x\n", oLr->expression(2).c_str(), lpInstructionAddress);
				return PROCESSOR_STATUS_INTERNAL_ERROR;
			}

			if (NODE_IS_REGISTER(oLr)) {
				goto _load_lr;
			} else {
				*lpNextAddress = oLr->toConstant()->dwValue & ~1;
				PopCallStack(*lpNextAddress);
			}
		}
		return PROCESSOR_STATUS_OK;
	} else if (NODE_IS_LOAD(oAddress)) {
		DFGNode oLoadAddress = *oAddress->toLoad()->aInputNodes.begin();
		if (NODE_IS_CONSTANT(oLoadAddress)) {
			API_LOCK();
			lpTarget = get_dword(oLoadAddress->toConstant()->dwValue) & ~1;
			API_UNLOCK();
			goto _jump;
		}
	}
	if (NODE_IS_REGISTER(oAddress) && oAddress->toRegister()->bRegister == 14) {
_load_lr:
		wc_debug("[+] loading %s into PC @ 0x%x\n", oAddress->expression(2).c_str(), lpInstructionAddress);
		if (aCallStack.size() > 0) {
			wc_debug("[-] call stack not empty :(\n");

			//std::list<unsigned long>::reverse_iterator itS;
			//int i;
			//for (i = aCallStack.size() - 1, itS = aCallStack.rbegin(); itS != aCallStack.rend(); itS++, i--) {
			//	wc_debug("   [*] %2d 0x%x\n", i, *itS);
			//}
			//return PROCESSOR_STATUS_INTERNAL_ERROR;
		}
		return PROCESSOR_STATUS_DONE;
	} else {
		wc_debug("[-] loading of non-constant expression %s into PC is not supported @ 0x%x\n", oAddress->expression(2).c_str(), lpInstructionAddress);
		return PROCESSOR_STATUS_INTERNAL_ERROR;
	}
}

void ArmImpl::PushCallStack(unsigned long lpAddress) {
	aCallStack.push_back(lpAddress);
}

void ArmImpl::PopCallStack(unsigned long lpAddress) {
	std::list<unsigned long>::reverse_iterator it;
	/*
	 * We traverse down the stack to find the entry
	 * since we found examples in the wild where the go-to-begin
	 * is done through a BL, discarding LR. This poisons our aCallStack.
	 */
	for (it = aCallStack.rbegin(); it != aCallStack.rend(); it++) {
		if (*it == lpAddress) {
			aCallStack.erase((++it).base(), aCallStack.end());
			break;
		}
	}
}

DFGNode ArmImpl::GetRegister(CodeBroker &oBuilder, unsigned long lpInstructionAddress, unsigned char bReg) {
	if (bReg == 15) {
		API_LOCK();
		sel_t t = get_sreg(lpInstructionAddress, 20);
		API_UNLOCK();
		if((t != BADSEL && t != 0)) { /* instruction is thumb */
			return oBuilder->NewConstant(lpInstructionAddress + 4);
		}
		/* plain ARM */
		return oBuilder->NewConstant(lpInstructionAddress + 8);
	}
	return aRegisters[bReg];
}

processor_status_t ArmImpl::SetRegister(CodeBroker & oBuilder, unsigned long lpInstructionAddress, unsigned long *lpNextAddress, unsigned char bReg, DFGNode & oNode) {
	if (bReg == 15) {
		return JumpToNode(oBuilder, lpNextAddress, lpInstructionAddress, oNode);
	}
	aRegisters[bReg] = oNode;
	return PROCESSOR_STATUS_OK;
}


DFGNode ArmImpl::GetOperandShift(CodeBroker & oBuilder, DFGNode & oBaseNode, DFGNode & oShift, char bShiftType, bool bSetFlags) {
	switch (bShiftType) {
	case LSL:
		break;
	case LSR:
	case ASR:
		oShift = oBuilder->NewMult(oShift, oBuilder->NewConstant(-1));
		break;
	case ROR:
		break;
	case RRX: {
		DFGNode oCarryNode = oCarryFlag->Carry(oBuilder);
		oShift = oBuilder->NewConstant(-1);
		DFGNode oResult = oBuilder->NewShift(oBaseNode, oShift);
		if (oCarryNode != nullptr && (!NODE_IS_CONSTANT(oCarryNode) || oCarryNode->toConstant()->dwValue != 0)) {
			oResult = oBuilder->NewAdd(oResult, oBuilder->NewShift(oCarryNode, oBuilder->NewConstant(31)));
		}
		if (bSetFlags) {
			SetFlag(FLAG_OP_SHIFT, oBaseNode, oShift);
		}
		return oResult;
	}
	}
	if (bSetFlags) {
		SetFlag(FLAG_OP_SHIFT, oBaseNode, oShift);
	}
	if (bShiftType == ROR) {
		return oBuilder->NewRotate(oBaseNode, oShift);
	} else {
		return oBuilder->NewShift(oBaseNode, oShift);
	}
}

DFGNode ArmImpl::GetOperand(CodeBroker &oBuilder, const op_t &stOperand, unsigned long lpInstructionAddress, bool bSetFlags) {
	switch (stOperand.type) {
	case o_reg: {
		DFGNode oReg = GetRegister(oBuilder, lpInstructionAddress, stOperand.reg);
		if (bSetFlags) {
			SetFlag(FLAG_OP_ADD, oReg, oBuilder->NewConstant(0));
		}
		return oReg;
	}
	case o_mem:
	case o_near:
	case o_far:
		return oBuilder->NewConstant(stOperand.addr);
	case o_imm:
		return oBuilder->NewConstant(stOperand.value);
	case o_phrase: {
		unsigned int dwShiftAmount = stOperand.value;
		DFGNode oReg1 = GetRegister(oBuilder, lpInstructionAddress, stOperand.reg);
		DFGNode oReg2 = GetRegister(oBuilder, lpInstructionAddress, stOperand.specflag1);

		if (dwShiftAmount == 0) {
			if (bSetFlags) {
				SetFlag(FLAG_OP_ADD, oReg1, oReg2);
			}
			return oBuilder->NewAdd(oReg1, oReg2);
		} else {
			DFGNode oShiftAmount = oBuilder->NewConstant(dwShiftAmount);
			DFGNode oResult = GetOperandShift(oBuilder, oReg2, oShiftAmount, stOperand.specflag2, bSetFlags);
			return oBuilder->NewAdd(oReg1, oResult);
		}
	}
	case o_displ: {
		DFGNode oReg = GetRegister(oBuilder, lpInstructionAddress, stOperand.reg);
		if (bSetFlags) {
			SetFlag(FLAG_OP_ADD, oReg, oBuilder->NewConstant(stOperand.addr));
		}
		return oBuilder->NewAdd(oReg, oBuilder->NewConstant(stOperand.addr));
	}
	case o_idpspec0: {
		DFGNode oShift;
		DFGNode oBaseNode = GetRegister(oBuilder, lpInstructionAddress, stOperand.reg);
		if (stOperand.value == 0 && stOperand.specflag2 != 0) {
			oShift = GetRegister(oBuilder, lpInstructionAddress, stOperand.specflag1);
		} else {
			oShift = oBuilder->NewConstant(stOperand.value);
		}
		return GetOperandShift(oBuilder, oBaseNode, oShift, stOperand.specflag2, bSetFlags);
	}
	case o_void:
		return nullptr;
	default:
		wc_debug("[-] unsupported operand type: %d\n", stOperand.type);
		return nullptr;
	}
}

processor_status_t ArmImpl::instruction(CodeBroker &oBuilder, unsigned long *lpNextAddress, unsigned long lpAddress) {
	insn_t stInstruction;
	unsigned int i;
	unsigned int dwRegisterNo;
	int dwInstructionSize;

	API_LOCK();
	dwInstructionSize = decode_insn(&stInstruction, lpAddress);
	API_UNLOCK();

	if (dwInstructionSize <= 0) {
		return PROCESSOR_STATUS_INTERNAL_ERROR;
	}

	/* default lpNextAddress to next instruction, may be overwritten further down */
	*lpNextAddress = lpAddress + dwInstructionSize;

	DFGNode oConditionNode;
	graph_process_t eVerdict = GRAPH_PROCESS_CONTINUE;
	Condition oCondition;
	switch (stInstruction.segpref) {
	case cEQ:          // 0000 Z                        Equal
	case cNE:          // 0001 !Z                       Not equal
		if (oZeroFlag != nullptr) {
			oCondition = oZeroFlag->ConditionalInstruction(oBuilder, stInstruction.segpref);
		} else {
_missing_flags:
			wc_debug("[-] conditional instruction but flags are not set @ 0x%x\n", lpAddress);
			return PROCESSOR_STATUS_INTERNAL_ERROR;
		}
		break;
	case cCS:          // 0010 C                        Unsigned higher or same
	case cCC:          // 0011 !C                       Unsigned lower
		if (oCarryFlag != nullptr) {
			oCondition = oCarryFlag->ConditionalInstruction(oBuilder, stInstruction.segpref);
		} else {
			goto _missing_flags;
		}
		break;
	case cMI:          // 0100 N                        Negative
	case cPL:          // 0101 !N                       Positive or Zero
		if (oNegativeFlag != nullptr) {
			oCondition = oNegativeFlag->ConditionalInstruction(oBuilder, stInstruction.segpref);
		} else {
			goto _missing_flags;
		}
		break;
	case cVS:          // 0110 V                        Overflow
	case cVC:          // 0111 !V                       No overflow
		if (oOverflowFlag != nullptr) {
			oCondition = oOverflowFlag->ConditionalInstruction(oBuilder, stInstruction.segpref);
		} else {
			goto _missing_flags;
		}
		break;
	case cHI:          // 1000 C & !Z                   Unsigned higher
	case cLS:          // 1001 !C | Z                   Unsigned lower or same
		if (oCarryFlag != nullptr && oZeroFlag != nullptr) {
			if (oCarryFlag != oZeroFlag) {
_flags_from_different_operations:
				wc_debug("[-] flags used in conditional instruction originate from two different operations which is not supported @ 0x%x\n", lpAddress);
				return PROCESSOR_STATUS_INTERNAL_ERROR;
			}
			oCondition = oCarryFlag->ConditionalInstruction(oBuilder, stInstruction.segpref);
		} else {
			goto _missing_flags;
		}
		break;
		
	case cGE:          // 1010 (N & V) | (!N & !V)      Greater or equal
	case cLT:          // 1011 (N & !V) | (!N & V)      Less than
		if (oNegativeFlag != nullptr && oOverflowFlag != nullptr) {
			if (oNegativeFlag != oOverflowFlag) {
				goto _flags_from_different_operations;
			}
			oCondition = oNegativeFlag->ConditionalInstruction(oBuilder, stInstruction.segpref);
		} else {
			goto _missing_flags;
		}
		break;

	case cGT:          // 1100 !Z & ((N & V)|(!N & !V)) Greater than
	case cLE:          // 1101 Z | (N & !V) | (!N & V)  Less than or equal
		if (oZeroFlag != nullptr && oNegativeFlag != nullptr && oOverflowFlag != nullptr) {
			if (oZeroFlag != oNegativeFlag || oZeroFlag != oOverflowFlag) {
				goto _flags_from_different_operations;
			}
			oCondition = oZeroFlag->ConditionalInstruction(oBuilder, stInstruction.segpref);
		} else {
			goto _missing_flags;
		}
		break;
		
	case cAL:          // 1110 Always
		goto _always;
	case cNV:
		goto _skip;
	}

	/*
	 * arch-specific conditional instruction hacks below
	 */
	if (oCondition->eSpecial == SPECIAL_COND_NORMAL && ((
		NODE_IS_REGISTER(oCondition->oExpression1) &&
		oCondition->oExpression1->toRegister()->bRegister == 13 // SP
	) || (
		NODE_IS_ADD(oCondition->oExpression1) && oCondition->oExpression1->aInputNodes.size() == 2 &&
		NODE_IS_REGISTER(*oCondition->oExpression1->aInputNodes.begin()) &&
		(*oCondition->oExpression1->aInputNodes.begin())->toRegister()->bRegister == 13 && // SP+CONST:x
		NODE_IS_CONSTANT(*++oCondition->oExpression1->aInputNodes.begin())
	)) && (
		NODE_IS_CONSTANT(oCondition->oExpression2)
	)) {
		switch (oCondition->eOperator) {
		case OPERATOR_UGE:
			if (
				oCondition->oExpression2->toConstant()->dwValue == 0 ||
				oCondition->oExpression2->toConstant()->dwValue == 0xffffffff
				) {
				/*
				 * apparently, sometimes CMP SP, #0 is used as a carry set.
				 * likewise, CMN SP, #0 is used as a carry clear.
				 * this hack deals with that situation, rather than causing the builder to fork
				 */
				wc_debug("[*] carry (un)set : %s, following %s @ 0x%x\n", oCondition->expression(2).c_str(), oCondition->oExpression2->toConstant()->dwValue == 0 ? "true" : "false", lpAddress);
				if (oCondition->oExpression2->toConstant()->dwValue == 0) {
					goto _always;
				} else {
					goto _skip;
				}
			}
			break;
		case OPERATOR_EQ:
		case OPERATOR_NEQ:
			if (oCondition->oExpression2->toConstant()->dwValue == 0) {
				/*
				 * this following situation occurs when a function is inlined.
				 * the caller passes a pointer to the stack as a parameter, and the callee checks whether that parameter is NULL.
				 * we don't want to fork based on SP+x == 0 vs SP+x != 0, but simply take the latter path instead.
				 */
				//wc_debug("[*] parameter NULL check : %s, following %s @ 0x%x\n", oCondition->expression(4).c_str(), oCondition->eOperator == OPERATOR_NEQ ? "true" : "false", lpAddress);
				if (oCondition->eOperator == OPERATOR_EQ) {
					goto _skip;
				} else {
					goto _always;
				}
			}
			break;
		}
	}

	eVerdict = oBuilder->IntroduceCondition(oCondition, lpAddress + dwInstructionSize);
	if (eVerdict == GRAPH_PROCESS_SKIP) {
		goto _skip;
	} else if (eVerdict == GRAPH_PROCESS_INTERNAL_ERROR) {
		return PROCESSOR_STATUS_INTERNAL_ERROR;
	}

_always:
	switch (stInstruction.itype) {
	case ARM_ldr:
	case ARM_ldrpc: {
		processor_status_t eStatus;
		DFGNode oLoad;
		if (stInstruction.ops[1].type == o_displ) {
			DFGNode oReg = GetRegister(oBuilder, lpAddress, stInstruction.ops[1].reg);
			//LDR{ type }{cond} Rt, [Rn, #offset]!; pre - indexed
			//LDR{ type }{cond} Rt, [Rn], #offset; post - indexed
			if (stInstruction.auxpref & aux_wback) { // pre-indexed
				oReg = oBuilder->NewAdd(oReg, oBuilder->NewConstant(stInstruction.ops[1].addr));
				oLoad = oBuilder->NewLoad(oReg);
				if ((eStatus = SetRegister(oBuilder, lpAddress, lpNextAddress, stInstruction.ops[1].reg, oReg)) != PROCESSOR_STATUS_OK) {
					return eStatus;
				}
			} else if (stInstruction.auxpref & aux_postidx) { // post-indexed
				oLoad = oBuilder->NewLoad(oReg);
				oReg = oBuilder->NewAdd(oReg, oBuilder->NewConstant(stInstruction.ops[1].addr));
				if ((eStatus = SetRegister(oBuilder, lpAddress, lpNextAddress, stInstruction.ops[1].reg, oReg)) != PROCESSOR_STATUS_OK) {
					return eStatus;
				}
			} else {
				oReg = oBuilder->NewAdd(oReg, oBuilder->NewConstant(stInstruction.ops[1].addr));
				oLoad = oBuilder->NewLoad(oReg);
			}
		} else {
			oLoad = oBuilder->NewLoad(GetOperand(oBuilder, stInstruction.ops[1], lpAddress, false));
		}

		eStatus = SetRegister(oBuilder, lpAddress, lpNextAddress, stInstruction.ops[0].reg, oLoad);
		if (eStatus != PROCESSOR_STATUS_OK) {
			return eStatus;
		}
		break;
	}

	case ARM_str: {
		DFGNode oData = GetRegister(oBuilder, lpAddress, stInstruction.ops[0].reg);
		processor_status_t eStatus;

		if (stInstruction.ops[1].type == o_displ) {
			DFGNode oReg = GetRegister(oBuilder, lpAddress, stInstruction.ops[1].reg);
			//STR{ type }{cond} Rt, [Rn, #offset]!; pre - indexed
			//STR{ type }{cond} Rt, [Rn], #offset; post - indexed
			if (stInstruction.auxpref & aux_wback) { // pre-indexed
				oReg = oBuilder->NewAdd(oReg, oBuilder->NewConstant(stInstruction.ops[1].addr));
				oBuilder->NewStore(oData, oReg);
				if ((eStatus = SetRegister(oBuilder, lpAddress, lpNextAddress, stInstruction.ops[1].reg, oReg)) != PROCESSOR_STATUS_OK) {
					return eStatus;
				}
			} else if (stInstruction.auxpref & aux_postidx) { // post-indexed
				oBuilder->NewStore(oData, oReg);
				oReg = oBuilder->NewAdd(oReg, oBuilder->NewConstant(stInstruction.ops[1].addr));
				if ((eStatus = SetRegister(oBuilder, lpAddress, lpNextAddress, stInstruction.ops[1].reg, oReg)) != PROCESSOR_STATUS_OK) {
					return eStatus;
				}
			} else {
				oReg = oBuilder->NewAdd(oReg, oBuilder->NewConstant(stInstruction.ops[1].addr));
				oBuilder->NewStore(oData, oReg);
			}
		} else {
			oBuilder->NewStore(oData, GetOperand(oBuilder, stInstruction.ops[1], lpAddress, false));
		}
		break;
	}
	case ARM_mul: {
		DFGNode oNode1, oNode2;
		dwRegisterNo = stInstruction.ops[0].reg;
		if (stInstruction.ops[2].type == o_void) {
			oNode1 = GetOperand(oBuilder, stInstruction.ops[0], lpAddress, false);
			oNode2 = GetOperand(oBuilder, stInstruction.ops[1], lpAddress, false);
		}
		else {
			oNode1 = GetOperand(oBuilder, stInstruction.ops[1], lpAddress, false);
			oNode2 = GetOperand(oBuilder, stInstruction.ops[2], lpAddress, false);
		}
		DFGNode oMult = oBuilder->NewMult(oNode1, oNode2);
		processor_status_t eStatus = SetRegister(oBuilder, lpAddress, lpNextAddress, dwRegisterNo, oMult);
		if (eStatus != PROCESSOR_STATUS_OK) {
			return eStatus;
		}
		if (stInstruction.auxpref & aux_cond) {
			SetFlag(FLAG_OP_MULT, oNode1, oNode2);
		}
		break;
	}
	case ARM_add:
	case ARM_sub:
	case ARM_cmp:
	case ARM_cmn:
	case ARM_adc:
	case ARM_sbc:
	case ARM_rsb: {
		DFGNode oNode1, oNode2;
		dwRegisterNo = stInstruction.ops[0].reg;
		if (stInstruction.ops[2].type == o_void) {
			oNode1 = GetOperand(oBuilder, stInstruction.ops[0], lpAddress, false);
			oNode2 = GetOperand(oBuilder, stInstruction.ops[1], lpAddress, false);
		}
		else {
			oNode1 = GetOperand(oBuilder, stInstruction.ops[1], lpAddress, false);
			oNode2 = GetOperand(oBuilder, stInstruction.ops[2], lpAddress, false);
		}

		switch (stInstruction.itype) {
		case ARM_add:
			break;
		case ARM_adc:
		case ARM_sbc: {
			if (oCarryFlag == nullptr) {
				wc_debug("[-] instruction uses carry but carry not set @ 0x%x\n", lpAddress);
				return PROCESSOR_STATUS_INTERNAL_ERROR;
			}
			DFGNode oCarryNode = oCarryFlag->Carry(oBuilder);
			if (oCarryNode == nullptr) {
				wc_debug("[-] instruction uses carry but unable to construct a value for it @ 0x%x\n", lpAddress);
				return PROCESSOR_STATUS_INTERNAL_ERROR;
			}
			oNode2 = oBuilder->NewAdd(oNode2, oCarryNode);
			if (stInstruction.itype == ARM_adc) {
				break;
			}
			/* fall through */
		}
		case ARM_sub:
		case ARM_cmp:
			oNode2 = oBuilder->NewMult(oNode2, oBuilder->NewConstant(-1));
			break;
		case ARM_cmn:
			oNode2 = oBuilder->NewMult(
				oBuilder->NewXor(
					oNode2,
					oBuilder->NewConstant(0xffffffff)
				),
				oBuilder->NewConstant(-1)
			);
			break;
		case ARM_rsb:
			oNode1 = oBuilder->NewMult(oNode1, oBuilder->NewConstant(-1));
			break;
		}
		if (stInstruction.itype != ARM_cmp && stInstruction.itype != ARM_cmn) {
			DFGNode oAdd = oBuilder->NewAdd(oNode1, oNode2);
			processor_status_t eStatus = SetRegister(oBuilder, lpAddress, lpNextAddress, dwRegisterNo, oAdd);
			if (eStatus != PROCESSOR_STATUS_OK) {
				return eStatus;
			}
		}
		if (stInstruction.itype == ARM_cmp || stInstruction.itype == ARM_cmn || stInstruction.auxpref & aux_cond) {
			SetFlag(FLAG_OP_ADD, oNode1, oNode2);
		}
		break;
	}
	case ARM_adr:
	case ARM_adrl:
	case ARM_mov:
	case ARM_movl: {
		dwRegisterNo = stInstruction.ops[0].reg;
		DFGNode oNode = GetOperand(oBuilder, stInstruction.ops[1], lpAddress, !!(stInstruction.auxpref & aux_cond));
		processor_status_t eStatus = SetRegister(oBuilder, lpAddress, lpNextAddress, dwRegisterNo, oNode);
		if (eStatus != PROCESSOR_STATUS_OK) {
			return eStatus;
		}
		break;
	}

	case ARM_mvn: {
		dwRegisterNo = stInstruction.ops[0].reg;
		DFGNode oNode = oBuilder->NewXor(GetOperand(oBuilder, stInstruction.ops[1], lpAddress, !!(stInstruction.auxpref & aux_cond)), oBuilder->NewConstant(0xffffffff));
		processor_status_t eStatus = SetRegister(oBuilder, lpAddress, lpNextAddress, dwRegisterNo, oNode);
		if (eStatus != PROCESSOR_STATUS_OK) {
			return eStatus;
		}
		break;
	}
	case ARM_neg: {
		dwRegisterNo = stInstruction.ops[0].reg;
		DFGNode oNode = oBuilder->NewMult(GetOperand(oBuilder, stInstruction.ops[1], lpAddress, !!(stInstruction.auxpref & aux_cond)), oBuilder->NewConstant(-1));
		processor_status_t eStatus = SetRegister(oBuilder, lpAddress, lpNextAddress, dwRegisterNo, oNode);
		if (eStatus != PROCESSOR_STATUS_OK) {
			return eStatus;
		}
		break;
	}

	case ARM_ldrd: {
		unsigned int dwRegisterNoA = stInstruction.ops[0].reg;
		unsigned int dwRegisterNoB = stInstruction.ops[1].reg;
		DFGNode oOperand = GetOperand(oBuilder, stInstruction.ops[2], lpAddress, false);
		DFGNode oLoadA = oBuilder->NewLoad(oOperand);
		DFGNode oLoadB = oBuilder->NewLoad(oBuilder->NewAdd(oOperand, oBuilder->NewConstant(4)));
		processor_status_t eStatus;
		if ((eStatus = SetRegister(oBuilder, lpAddress, lpNextAddress, dwRegisterNoA, oLoadA)) != PROCESSOR_STATUS_OK) {
			return eStatus;
		} else if ((eStatus = SetRegister(oBuilder, lpAddress, lpNextAddress, dwRegisterNoB, oLoadB)) != PROCESSOR_STATUS_OK) {
			return eStatus;
		}
		break;
	}
	case ARM_strd: {
		unsigned int dwRegisterNoA = stInstruction.ops[0].reg;
		unsigned int dwRegisterNoB = stInstruction.ops[1].reg;
		DFGNode oOperand = GetOperand(oBuilder, stInstruction.ops[2], lpAddress, false);
		DFGNode oDataA = GetRegister(oBuilder, lpAddress, dwRegisterNoA);
		DFGNode oDataB = GetRegister(oBuilder, lpAddress, dwRegisterNoB);
		oBuilder->NewStore(oDataA, oOperand);
		oBuilder->NewStore(oDataB, oBuilder->NewAdd(oOperand, oBuilder->NewConstant(4)));
		break;
	}
	case ARM_push:
	case ARM_stm: {
		ea_t dwSpec;
		bool bPost;
		bool bIncrement;
		bool bWriteback;
		int dwIncrement = 0;

		if (stInstruction.itype == ARM_push) {
			dwRegisterNo = 13;
			dwSpec = stInstruction.ops[0].specval;
			bPost = false;
			bIncrement = false;
			bWriteback = true;
		} else {
			dwRegisterNo = stInstruction.ops[0].reg;
			dwSpec = stInstruction.ops[1].specval;
			bPost = !!(stInstruction.auxpref & aux_postidx);
			bIncrement = !(stInstruction.auxpref & aux_negoff);
			bWriteback = !!(stInstruction.auxpref & aux_wbackldm);
		}

		DFGNode oOperand = GetRegister(oBuilder, lpAddress, dwRegisterNo);

		for (
			i = bIncrement ? 0 : 15;
			(i < 16) && (i >= 0);
			bIncrement ? i++ : i--
		) {
			if (dwSpec & (1 << i)) {
				if (!bPost) { dwIncrement += 4; }
				DFGNode oTarget = GetRegister(oBuilder, lpAddress, i);
				oBuilder->NewStore(oTarget, oBuilder->NewAdd(oOperand, oBuilder->NewConstant(bIncrement ? dwIncrement : 0 - dwIncrement)));
				if (bPost) { dwIncrement += 4; }
			}
		}

		if (bWriteback) {
			DFGNode oWriteback = oBuilder->NewAdd(oOperand, oBuilder->NewConstant(bIncrement ? dwIncrement : 0 - dwIncrement));
			processor_status_t eStatus = SetRegister(oBuilder, lpAddress, lpNextAddress, dwRegisterNo, oWriteback);
			if (eStatus != PROCESSOR_STATUS_OK) {
				return eStatus;
			}
		}

		break;
	}
	case ARM_pop:
	case ARM_ldm: {
		ea_t dwSpec;
		bool bPost;
		bool bIncrement;
		bool bWriteback;
		int dwIncrement = 0;

		if (stInstruction.itype == ARM_pop) {
			dwRegisterNo = 13;
			dwSpec = stInstruction.ops[0].specval;
			bPost = true;
			bIncrement = true;
			bWriteback = true;
		} else {
			dwRegisterNo = stInstruction.ops[0].reg;
			dwSpec = stInstruction.ops[1].specval;
			bPost = !!(stInstruction.auxpref & aux_postidx);
			bIncrement = !(stInstruction.auxpref & aux_negoff);
			bWriteback = !!(stInstruction.auxpref & aux_wbackldm);
		}

		DFGNode oOperand = GetRegister(oBuilder, lpAddress, dwRegisterNo);

		for (
			i = bIncrement ? 0 : 15;
			(i < 16) && (i >= 0);
			bIncrement ? i++ : i--
		) {
			if (dwSpec & (1 << i)) {
				if (!bPost) { dwIncrement += 4; }
				DFGNode oLoad = oBuilder->NewLoad(oBuilder->NewAdd(oOperand, oBuilder->NewConstant(bIncrement ? dwIncrement : 0 - dwIncrement)));
				processor_status_t eStatus = SetRegister(oBuilder, lpAddress, lpNextAddress, i, oLoad);
				if (eStatus != PROCESSOR_STATUS_OK) {
					return eStatus;
				}
				if (bPost) { dwIncrement += 4; }
			}
		}

		if (bWriteback) {
			DFGNode oWriteback = oBuilder->NewAdd(oOperand, oBuilder->NewConstant(bIncrement ? dwIncrement : 0 - dwIncrement));
			processor_status_t eStatus = SetRegister(oBuilder, lpAddress, lpNextAddress, dwRegisterNo, oWriteback);
			if (eStatus != PROCESSOR_STATUS_OK) {
				return eStatus;
			}
		}
		break;
	}

	case ARM_tst:
	case ARM_teq:
	case ARM_and:
	case ARM_orr:
	case ARM_eor:
	case ARM_bic:
	case ARM_orn: {
		DFGNode oNode1, oNode2;
		unsigned int dwNode1RegisterNo;
		dwRegisterNo = stInstruction.ops[0].reg;
		bool bUpdateFlags = (stInstruction.auxpref & aux_cond) || stInstruction.itype == ARM_tst || stInstruction.itype == ARM_teq;

		if (stInstruction.ops[2].type == o_void) {
			dwNode1RegisterNo = dwRegisterNo;
			oNode2 = GetOperand(oBuilder, stInstruction.ops[1], lpAddress, bUpdateFlags);
		} else {
			dwNode1RegisterNo = stInstruction.ops[1].reg;
			oNode2 = GetOperand(oBuilder, stInstruction.ops[2], lpAddress, bUpdateFlags);
		}
		oNode1 = GetRegister(oBuilder, lpAddress, dwNode1RegisterNo);
		DFGNode oSource;
		flag_op_type_t eFlagOp;
		switch (stInstruction.itype) {
		case ARM_tst: eFlagOp = FLAG_OP_BITWISE_AND; break;
		case ARM_teq: eFlagOp = FLAG_OP_BITWISE_XOR; break;
		case ARM_and: oSource = oBuilder->NewAnd(oNode1, oNode2); eFlagOp = FLAG_OP_BITWISE_AND; break;
		case ARM_orr: oSource = oBuilder->NewOr(oNode1, oNode2); eFlagOp = FLAG_OP_BITWISE_OR; break;
		case ARM_eor: oSource = oBuilder->NewXor(oNode1, oNode2); eFlagOp = FLAG_OP_BITWISE_XOR; break;
		case ARM_bic: 
			oNode2 = oBuilder->NewXor(oNode2, oBuilder->NewConstant(0xffffffff));
			oSource = oBuilder->NewAnd(oNode1, oNode2);
			eFlagOp = FLAG_OP_BITWISE_AND;
			break;
		case ARM_orn:
			oNode2 = oBuilder->NewXor(oNode2, oBuilder->NewConstant(0xffffffff));
			oSource = oBuilder->NewOr(oNode1, oNode2);
			eFlagOp = FLAG_OP_BITWISE_OR;
			break;
		}
		if (stInstruction.itype != ARM_tst && stInstruction.itype != ARM_teq) {
			processor_status_t eStatus = SetRegister(oBuilder, lpAddress, lpNextAddress, dwRegisterNo, oSource);
			if (eStatus != PROCESSOR_STATUS_OK) {
				return eStatus;
			}
		}
		if (bUpdateFlags) {
			SetFlag(eFlagOp, oNode1, oNode2);
		}
		break;
	}
	case ARM_lsl:
	case ARM_asr:
	case ARM_lsr:
	case ARM_ror:
	case ARM_uxth:
	case ARM_uxtb: {
		DFGNode oNode1, oNode2;
		unsigned int dwNode1RegisterNo;
		dwRegisterNo = stInstruction.ops[0].reg;
		if (stInstruction.ops[2].type == o_void) {
			dwNode1RegisterNo = dwRegisterNo;
			oNode2 = GetOperand(oBuilder, stInstruction.ops[1], lpAddress, !!(stInstruction.auxpref & aux_cond));
		} else {
			dwNode1RegisterNo = stInstruction.ops[1].reg;
			oNode2 = GetOperand(oBuilder, stInstruction.ops[2], lpAddress, !!(stInstruction.auxpref & aux_cond));
		}
		oNode1 = GetRegister(oBuilder, lpAddress, dwNode1RegisterNo);
		processor_status_t eStatus;
		DFGNode oSource;
		switch (stInstruction.itype) {
		case ARM_lsr:
		case ARM_asr:
			/* XXX signedness expansion is not implemented */
			oNode2 = oBuilder->NewMult(oNode2, oBuilder->NewConstant(-1));
			/* fall through */
		case ARM_lsl:
		case ARM_ror:
			oSource = stInstruction.itype == ARM_ror ? oBuilder->NewRotate(oNode1, oNode2) : oBuilder->NewShift(oNode1, oNode2);
			if (stInstruction.auxpref & aux_cond) {
				SetFlag(FLAG_OP_SHIFT, oNode1, oNode2);
			}
			break;
		case ARM_uxth:
			oSource = oBuilder->NewAnd(oNode2, oBuilder->NewConstant(0xffff));
			break;
		case ARM_uxtb:
			oSource = oBuilder->NewAnd(oNode2, oBuilder->NewConstant(0xff));
			break;
		}
		eStatus = SetRegister(oBuilder, lpAddress, lpNextAddress, dwRegisterNo, oSource);
		if (eStatus != PROCESSOR_STATUS_OK) {
			return eStatus;
		}
		break;
	}
	case ARM_b:
	case ARM_bx: {
		DFGNode oAddress = GetOperand(oBuilder, stInstruction.ops[0], lpAddress, false);
		return JumpToNode(oBuilder, lpNextAddress, lpAddress, oAddress);
		//wc_debug("[*] skipping to address %lx (coming from %lx)\n", *lpNextAddress, lpAddress);
	}
	case ARM_bl:
	case ARM_blx1:
	case ARM_blx2: {
		DFGNode oAddress = GetOperand(oBuilder, stInstruction.ops[0], lpAddress, false);
		SetRegister(oBuilder, lpAddress, lpNextAddress, 14, oBuilder->NewConstant(lpAddress + dwInstructionSize));
		PushCallStack(lpAddress + dwInstructionSize);
		return JumpToNode(oBuilder, lpNextAddress, lpAddress, oAddress);
	}
	case ARM_rev: {
		dwRegisterNo = stInstruction.ops[0].reg;
		DFGNode oNode = GetOperand(oBuilder, stInstruction.ops[1], lpAddress, false);
		//  ((num >> 24) &       0xff) |
		//	((num >> 8)  &     0xff00) |
		//	((num << 8)  &   0xff0000) |
		//	((num << 24) & 0xff000000)
		DFGNode oNodeRevByte0  = oBuilder->NewShift(oNode, oBuilder->NewConstant(-24));
		DFGNode oNodeRevByte1  = oBuilder->NewAnd(oBuilder->NewShift(oNode, oBuilder->NewConstant(-8)), oBuilder->NewConstant(0xff00));
		DFGNode oNodeRevByte2 = oBuilder->NewAnd(oBuilder->NewShift(oNode, oBuilder->NewConstant(8)), oBuilder->NewConstant(0xff0000));
		DFGNode oNodeRevByte3 = oBuilder->NewAnd(oBuilder->NewShift(oNode, oBuilder->NewConstant(24)), oBuilder->NewConstant(0xff000000));
		DFGNode oNodeRev = oBuilder->NewOr(oBuilder->NewOr(oBuilder->NewOr(oNodeRevByte0, oNodeRevByte1), oNodeRevByte2), oNodeRevByte3);
		processor_status_t eStatus = SetRegister(oBuilder, lpAddress, lpNextAddress, dwRegisterNo, oNodeRev);
		if (eStatus != PROCESSOR_STATUS_OK) {
			return eStatus;
		}
		break;
	}
	case ARM_ubfx: {
		dwRegisterNo = stInstruction.ops[0].reg;
		DFGNode oNode = GetOperand(oBuilder, stInstruction.ops[1], lpAddress, false);
		oNode = oBuilder->NewShift(oNode, oBuilder->NewConstant(stInstruction.ops[2].value * -1));
		oNode = oBuilder->NewAnd(oNode, oBuilder->NewConstant((1 << stInstruction.ops[3].value) - 1));
		processor_status_t eStatus = SetRegister(oBuilder, lpAddress, lpNextAddress, dwRegisterNo, oNode);
		if (eStatus != PROCESSOR_STATUS_OK) {
			return eStatus;
		}
		break;
	}
	case ARM_sbfx: {
		dwRegisterNo = stInstruction.ops[0].reg;
		DFGNode oNode = GetOperand(oBuilder, stInstruction.ops[1], lpAddress, false);
		oNode = oBuilder->NewShift(oNode, oBuilder->NewConstant(stInstruction.ops[2].value * -1));
		oNode = oBuilder->NewAnd(oNode, oBuilder->NewConstant((1 << stInstruction.ops[3].value) - 1));
		/* 
		 * We only sign extend here in case of a constant,
		 * since we don't want to introduce a condition and potentially cause a fork
		 */
		if (NODE_IS_CONSTANT(oNode) && oNode->toConstant()->dwValue & (1 << (stInstruction.ops[3].value - 1))) {
			oNode = oBuilder->NewConstant(oNode->toConstant()->dwValue | 0xffffffff << (stInstruction.ops[3].value));
		}
		processor_status_t eStatus = SetRegister(oBuilder, lpAddress, lpNextAddress, dwRegisterNo, oNode);
		if (eStatus != PROCESSOR_STATUS_OK) {
			return eStatus;
		}
		break;
	}
	case ARM_movt: {
		dwRegisterNo = stInstruction.ops[0].reg;
		/* MOVT always receives an immediate operand */
		DFGNode oNode = GetRegister(oBuilder, lpAddress, dwRegisterNo);
		oNode = oBuilder->NewAnd(oNode, oBuilder->NewConstant(0xffff));
		oNode = oBuilder->NewOr(oNode, oBuilder->NewConstant((unsigned int)stInstruction.ops[1].value << 16));
		processor_status_t eStatus = SetRegister(oBuilder, lpAddress, lpNextAddress, dwRegisterNo, oNode);
		if (eStatus != PROCESSOR_STATUS_OK) {
			return eStatus;
		}
		break;
	}
	case ARM_cbz:
	case ARM_cbnz: {
		dwRegisterNo = stInstruction.ops[0].reg;
		DFGNode oNode = GetRegister(oBuilder, lpAddress, dwRegisterNo);
		Condition oCondition(Condition::create(oNode, stInstruction.itype == ARM_cbz ? OPERATOR_EQ : OPERATOR_NEQ, oBuilder->NewConstant(0)));
		eVerdict = oBuilder->IntroduceCondition(oCondition, lpAddress + dwInstructionSize);
		if (eVerdict == GRAPH_PROCESS_SKIP) {
			goto _skip;
		} else if (eVerdict == GRAPH_PROCESS_INTERNAL_ERROR) {
			return PROCESSOR_STATUS_INTERNAL_ERROR;
		}
		break;
	}
	case ARM_ret: {
		DFGNode oAddress = GetRegister(oBuilder, lpAddress, 14);
		return JumpToNode(oBuilder, lpNextAddress, lpAddress, oAddress);
		break;
	}
	case ARM_nop:
		break;


	default:
		wc_debug("[-] Unhandled instruction type=%d @ 0x%x\n", stInstruction.itype, lpAddress);
		return PROCESSOR_STATUS_INTERNAL_ERROR;
	}

_skip:
	return PROCESSOR_STATUS_OK;
}

bool ArmImpl::ShouldClean(DFGNode & oNode) {
	if (oNode == aRegisters[0]) {
		/* node is return value -> keep */
		return false;
	}
	if (NODE_IS_STORE(oNode)) {
		/* 
		 * stores on the stack are temporary
		 * and should be removed from the graph if their outputs remain unused
		 */
		DFGNode oMemoryAddress = *++oNode->aInputNodes.begin();
		if (NODE_IS_REGISTER(oMemoryAddress) && oMemoryAddress->toRegister()->bRegister == 13) {
			/* store to SP */
			return true;
		} else if (NODE_IS_ADD(oMemoryAddress)) {
			std::list<DFGNode>::iterator it;
			/* store to SP+x */
			for (it = oMemoryAddress->aInputNodes.begin(); it != oMemoryAddress->aInputNodes.end(); it++) {
				if (NODE_IS_REGISTER(*it) && (*it)->toRegister()->bRegister == 13) {
					return true;
				}
			}
		}
		/* any other store should be considered to be an output value */
		return false;
	}
	return true;
}

Processor ArmImpl::Migrate(DFGraph oGraph) {
	Arm lpFork(Arm::create(*this));
	std::vector<DFGNode>::iterator it;

	lpFork->aRegisters.clear();
	lpFork->aRegisters.reserve(aRegisters.size());
	for (it = aRegisters.begin(); it != aRegisters.end(); it++) {
		lpFork->aRegisters.push_back(oGraph->FindNode((*it)->dwNodeId));
	}

	/*
	 * we can't just create copies of the flag structs
	 * since the code in this class depends on equivalence of pointers
	 * when the flag nodes are equivalent
	 */
	if (lpFork->oCarryFlag) lpFork->oCarryFlag = lpFork->oCarryFlag->Migrate(oGraph);
	if (lpFork->oOverflowFlag) {
		if (lpFork->oOverflowFlag == oCarryFlag) { lpFork->oOverflowFlag = lpFork->oCarryFlag; }
		else { lpFork->oOverflowFlag = lpFork->oOverflowFlag->Migrate(oGraph); }
	}
	if (lpFork->oZeroFlag) {
		if (lpFork->oZeroFlag == oCarryFlag) { lpFork->oZeroFlag = lpFork->oCarryFlag; }
		else if (lpFork->oZeroFlag == oOverflowFlag) { lpFork->oZeroFlag = lpFork->oOverflowFlag; }
		else { lpFork->oZeroFlag = lpFork->oZeroFlag->Migrate(oGraph); }
	}
	if (lpFork->oNegativeFlag) {
		if (lpFork->oNegativeFlag == oCarryFlag) { lpFork->oNegativeFlag = lpFork->oCarryFlag; }
		else if (lpFork->oNegativeFlag == oOverflowFlag) { lpFork->oNegativeFlag = lpFork->oOverflowFlag; }
		else if (lpFork->oNegativeFlag == oZeroFlag) { lpFork->oNegativeFlag = lpFork->oZeroFlag; }
		else { lpFork->oNegativeFlag = lpFork->oNegativeFlag->Migrate(oGraph); }
	}

	return Processor::typecast(lpFork);
}

DFGNode flag_op_t::Carry(CodeBroker &oBuilder) {
	DFGNode oCarryNode(nullptr);
	switch (eOperation) {
	case FLAG_OP_ADD:
		oBuilder->NewAdd(oNode1, oNode2, &oCarryNode);
		break;
	case FLAG_OP_SHIFT:
		oBuilder->NewShift(oNode1, oNode2, &oCarryNode);
		break;
	}
	return oCarryNode;
}

Condition flag_op_t::ConditionalInstruction(CodeBroker & oBuilder, char segpref) {
	switch (eOperation) {
	case FLAG_OP_ADD:
		return ConditionalInstructionAdd(oBuilder, segpref);
	case FLAG_OP_SHIFT:
		return ConditionalInstructionShift(oBuilder, segpref);
	case FLAG_OP_MULT:
	case FLAG_OP_BITWISE_AND:
	case FLAG_OP_BITWISE_OR:
	case FLAG_OP_BITWISE_XOR:
		return ConditionalInstructionBitwise(oBuilder, segpref);
	}
}

Condition flag_op_t::ConditionalInstructionAdd(CodeBroker & oBuilder, char segpref) {
	operator_t eOperator;
	DFGNode oLeftNode, oRightNode;
	switch (segpref) {
	case cEQ:
	case cNE:
	case cCS:
	case cCC:
	case cHI:
	case cLS:
	case cGE:
	case cLT:
	case cGT:
	case cLE:
		oLeftNode = oNode1;
		oRightNode = oBuilder->NewMult(oNode2, oBuilder->NewConstant(-1));
		switch (segpref) {
		case cEQ: eOperator = OPERATOR_EQ; break;
		case cNE: eOperator = OPERATOR_NEQ; break;
		case cCS: eOperator = OPERATOR_UGE; break;
		case cCC: eOperator = OPERATOR_ULT; break;
		case cHI: eOperator = OPERATOR_UGT; break;
		case cLS: eOperator = OPERATOR_ULE; break;
		case cGE: eOperator = OPERATOR_GE; break;
		case cLT: eOperator = OPERATOR_LT; break;
		case cGT: eOperator = OPERATOR_GT; break;
		case cLE: eOperator = OPERATOR_LE; break;
		}
		break;
	case cMI:
	case cPL:
	case cVS:
	case cVC:
		oLeftNode = oBuilder->NewAdd(oNode1, oNode2);
		oRightNode = oBuilder->NewConstant(0);
		switch (segpref) {
		case cMI: eOperator = OPERATOR_LT; break;
		case cPL: eOperator = OPERATOR_GE; break;
		case cVS: eOperator = OPERATOR_NEQ; break;
		case cVC: eOperator = OPERATOR_EQ; break;
		}
		break;
	}
	return Condition::create(
		oLeftNode,
		eOperator,
		oRightNode
	);
}

Condition flag_op_t::ConditionalInstructionShift(CodeBroker & oBuilder, char segpref) {
	operator_t eOperator;
	DFGNode oLeftNode, oRightNode, oCarryNode;
	switch (segpref) {
	case cEQ:
	case cNE:
	case cMI:
	case cPL:
		oLeftNode = oBuilder->NewShift(oNode1, oNode2);
		oRightNode = oBuilder->NewConstant(0);
		switch (segpref) {
		case cEQ: eOperator = OPERATOR_EQ; break;
		case cNE: eOperator = OPERATOR_NEQ; break;
		case cMI: eOperator = OPERATOR_LT; break;
		case cPL: eOperator = OPERATOR_GE; break;
		}
		break;
	case cCS:
	case cCC:
		oBuilder->NewShift(oNode1, oNode2, &oLeftNode);
		oRightNode = oBuilder->NewConstant(0);
		switch (segpref) {
		case cCS: eOperator = OPERATOR_NEQ; break;
		case cCC: eOperator = OPERATOR_EQ; break;
		}
		break;
	case cHI:
	case cLS:
		cHI,          // 1000 C & !Z                   Unsigned higher
		cLS,          // 1001 !C | Z                   Unsigned lower or same
		/* transform (C & !Z) to (C | ~Z) != 0 */
		oLeftNode = oBuilder->NewShift(oNode1, oNode2, &oCarryNode);
		oLeftNode = oBuilder->NewOr(
			oBuilder->NewXor(oLeftNode, oBuilder->NewConstant(0xffffffff)), // ~Z
			oCarryNode // C
		);
		oRightNode = oBuilder->NewConstant(0);
		switch (segpref) {
		case cHI: eOperator = OPERATOR_NEQ; break;
		case cLS: eOperator = OPERATOR_EQ; break;
		}
		break;
	}
	return Condition::create(
		oLeftNode,
		eOperator,
		oRightNode
	);
}

Condition flag_op_t::ConditionalInstructionBitwise(CodeBroker & oBuilder, char segpref) {
	operator_t eOperator;
	DFGNode oLeftNode, oRightNode;

	if (eOperation == FLAG_OP_BITWISE_XOR && (segpref == cEQ || segpref == cNE)) {
		return Condition::create(oNode1, segpref == cEQ ? OPERATOR_EQ : OPERATOR_NEQ, oNode2);
	}

	switch (eOperation) {
	case FLAG_OP_MULT: oLeftNode = oBuilder->NewMult(oNode1, oNode2); break;
	case FLAG_OP_BITWISE_AND: oLeftNode = oBuilder->NewAnd(oNode1, oNode2); break;
	case FLAG_OP_BITWISE_OR: oLeftNode = oBuilder->NewOr(oNode1, oNode2); break;
	case FLAG_OP_BITWISE_XOR: oLeftNode = oBuilder->NewXor(oNode1, oNode2); break;
	}
	oRightNode = oBuilder->NewConstant(0);

	switch (segpref) {
	case cEQ: eOperator = OPERATOR_EQ; break;
	case cNE: eOperator = OPERATOR_NEQ; break;
	case cMI: eOperator = OPERATOR_LT; break;
	case cPL: eOperator = OPERATOR_GE; break;
	}
	return Condition::create(
		oLeftNode,
		eOperator,
		oRightNode
	);
}

rfc_ptr<flag_op_t> flag_op_t::Migrate(DFGraph & oGraph) {
	rfc_ptr<flag_op_t> lpFork(rfc_ptr<flag_op_t>::create(*this));
	lpFork->oNode1 = oGraph->FindNode(oNode1->dwNodeId);
	lpFork->oNode2 = oGraph->FindNode(oNode2->dwNodeId);
	return lpFork;
}
