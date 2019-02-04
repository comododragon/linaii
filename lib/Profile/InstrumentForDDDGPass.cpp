#include "profile_h/InstrumentForDDDGPass.h"

#define DEBUG_TYPE "instrument-code-for-building-dddg"

staticInstID2OpcodeMapTy staticInstID2OpcodeMap;
instName2bbNameMapTy instName2bbNameMap;
headerBBFuncNamePair2lastInstMapTy headerBBFuncNamePair2lastInstMap;
headerBBFuncNamePair2lastInstMapTy exitingBBFuncNamePair2lastInstMap;
loopName2levelUnrollVecMapTy loopName2levelUnrollVecMap;

using namespace llvm;

void TraceLogger::initialiseDefaults(Module &M) {
	LLVMContext &C = M.getContext();

	log0 = cast<Function>(
		M.getOrInsertFunction(
			"trace_logger_log0",
			Type::getVoidTy(C),
			Type::getInt64Ty(C),
			Type::getInt8PtrTy(C),
			Type::getInt8PtrTy(C),
			Type::getInt8PtrTy(C),
			Type::getInt64Ty(C),
			nullptr
		)
	);

	logInt = cast<Function>(
		M.getOrInsertFunction(
			"trace_logger_log_int",
			Type::getVoidTy(C),
			Type::getInt64Ty(C),
			Type::getInt64Ty(C),
			Type::getInt64Ty(C),
			Type::getInt64Ty(C),
			Type::getInt8PtrTy(C),
			nullptr
		)
	);

	logDouble = cast<Function>(
		M.getOrInsertFunction(
			"trace_logger_log_double",
			Type::getVoidTy(C),
			Type::getInt64Ty(C),
			Type::getInt64Ty(C),
			Type::getDoubleTy(C),
			Type::getInt64Ty(C),
			Type::getInt8PtrTy(C),
			nullptr
		)
	);

	logIntNoReg = cast<Function>(
		M.getOrInsertFunction(
			"trace_logger_log_int_noreg",
			Type::getVoidTy(C),
			Type::getInt64Ty(C),
			Type::getInt64Ty(C),
			Type::getInt64Ty(C),
			Type::getInt64Ty(C),
			nullptr
		)
	);

	logDoubleNoReg = cast<Function>(
		M.getOrInsertFunction(
			"trace_logger_log_double_noreg",
			Type::getVoidTy(C),
			Type::getInt64Ty(C),
			Type::getInt64Ty(C),
			Type::getDoubleTy(C),
			Type::getInt64Ty(C),
			nullptr
		)
	);
}

int Injector::getMemSizeInBits(Type *T) {
	if(T->isPointerTy()) {
		return 8 * 8;
	}
	else if(T->isFunctionTy()) {
		return 0;
	}
	else if(T->isLabelTy()) {
		return 0;
	}
	else if(T->isStructTy()) {
		int size = 0;
		StructType *S = dyn_cast<StructType>(T);

		for(int i = 0; i < S->getNumElements(); i++)
			size += getMemSizeInBits(S->getElementType(i));

		return size;
	}
	else if(T->isFloatingPointTy()) {
		switch(T->getTypeID()) {
			case llvm::Type::HalfTyID:
				return 2 * 8;
			case llvm::Type::FloatTyID:
				return 4 * 8;
			case llvm::Type::DoubleTyID:
				return 8 * 8;
			case llvm::Type::X86_FP80TyID:
				return 10 * 8;
			case llvm::Type::FP128TyID:
				return 16 * 8;
			case llvm::Type::PPC_FP128TyID:
				return 16 * 8;
			default:
				assert(false && "Requested size of unknown floating point data type");
		}
	}
	else if(T->isIntegerTy()) {
		return cast<IntegerType>(T)->getBitWidth();
	}
	else if(T->isVectorTy()) {
		return cast<VectorType>(T)->getBitWidth();
	}
	else if(T->isArrayTy()) {
		ArrayType *A = dyn_cast<ArrayType>(T);
		return (int) A->getNumElements() * A->getElementType()->getPrimitiveSizeInBits();
	}
	else {
		assert(false && "Requested size of unknown data type");
	}

	return -1;
}

Constant *Injector::createGlobalVariableAndGetGetElementPtr(std::string value) {
	LLVMContext &C = M->getContext();

	// Creates an LLVM string with the provided value
	Constant *vValue = ConstantDataArray::getString(C, value, true);
	// Create an LLVM array type composed of value.length() + 1 elements of 8-bit integers
	ArrayType *arrayTy = ArrayType::get(IntegerType::get(C, 8), value.length() + 1);
	// Create an LLVM global variable array using the aforementioned array type
	GlobalVariable *gVarArray = new GlobalVariable(*M, arrayTy, true, GlobalValue::PrivateLinkage, 0, ".str");

	// Initialise the LLVM global variable array with value
	gVarArray->setInitializer(vValue);
	std::vector<Constant *> indexes;
	// Create an LLVM constant integer with value 0 in base 10
	ConstantInt *zero = ConstantInt::get(C, APInt(32, StringRef("0"), 10));
	indexes.push_back(zero);
	indexes.push_back(zero);

	// Create a getelementptr for the value string
	return ConstantExpr::getGetElementPtr(gVarArray, indexes);
}

void Injector::initialise(Module &M, TraceLogger &TL) {
	this->M = &M;
	this->TL = &TL;
}

void Injector::injectTraceHeader(BasicBlock::iterator it, int lineNo, std::string funcID, std::string bbID, std::string instID, int opcode) {
	LLVMContext &C = M->getContext();
	CallInst *tlCall;
	IRBuilder<> IRB(it);

	// Create LLVM values for the provided arguments
	Value *vLineNo = ConstantInt::get(IRB.getInt64Ty(), lineNo);
	Value *vOpcode = ConstantInt::get(IRB.getInt64Ty(), opcode);

	// Create global variables with strings and getelementptr calls
	Constant *vvFuncID = createGlobalVariableAndGetGetElementPtr(funcID);
	Constant *vvBB = createGlobalVariableAndGetGetElementPtr(bbID);
	Constant *vvInst = createGlobalVariableAndGetGetElementPtr(instID);

	// TODO: entender o que realmente signfica uma call para trace_logger_log0
	// Call trace_logger_log0 with the aforementioned values
	tlCall = IRB.CreateCall5(TL->log0, vLineNo, vvFuncID, vvBB, vvInst, vOpcode);

	// Update databases
	staticInstID2OpcodeMap.insert(std::make_pair(instID, opcode));
	instName2bbNameMap.insert(std::make_pair(instID, bbID));

	// This instruction is a branch
	if(LLVM_IR_Br == (unsigned) opcode) {
		bbFuncNamePairTy bbFnName = std::make_pair(bbID, funcID);
		bbFuncNamePair2lpNameLevelPairMapTy::iterator itHeader = headerBBFuncnamePair2lpNameLevelPairMap.find(bbFnName);
		headerBBFuncNamePair2lastInstMapTy::iterator itLastInst = headerBBFuncNamePair2lastInstMap.find(bbFnName);
		bool foundHeader = itHeader != headerBBFuncnamePair2lpNameLevelPairMap.end();
		bool foundLastInst = itLastInst != headerBBFuncNamePair2lastInstMap.end();

		// If not found, update database
		if(foundHeader && !foundLastInst)
			headerBBFuncNamePair2lastInstMap.insert(std::make_pair(bbFnName, instID));

		bbFuncNamePair2lpNameLevelPairMapTy::iterator itExiting = exitBBFuncnamePair2lpNameLevelPairMap.find(bbFnName);
		headerBBFuncNamePair2lastInstMapTy::iterator itLtItExiting = exitingBBFuncNamePair2lastInstMap.find(bbFnName);
		bool foundExiting = itExiting != exitBBFuncnamePair2lpNameLevelPairMap.end();
		bool foundLtItExiting = itLtItExiting != exitingBBFuncNamePair2lastInstMap.end();

		// If not found, update database
		if(foundExiting && !foundLtItExiting)
			exitingBBFuncNamePair2lastInstMap.insert(std::make_pair(bbFnName, instID));
	}
}

void Injector::injectTrace(BasicBlock::iterator it, int lineNo, std::string regOrFuncID, Type *T, Value *value, bool isReg) {
	LLVMContext &C = M->getContext();
	CallInst *tlCall;
	IRBuilder<> IRB(it);
	int type = T->getTypeID();

	// Create LLVM values for the provided arguments
	Value *vLineNo = ConstantInt::get(IRB.getInt64Ty(), lineNo);
	Value *vType = ConstantInt::get(IRB.getInt64Ty(), type);
	Value *vDataSize = ConstantInt::get(IRB.getInt64Ty(), getMemSizeInBits(T));
	Value *vIsReg = ConstantInt::get(IRB.getInt64Ty(), isReg);
	Value *vValue;

	if(isReg) {
		Constant *vvRegOrFuncID = createGlobalVariableAndGetGetElementPtr(regOrFuncID);

		if(value) {
			if(llvm::Type::IntegerTyID == type) {
				vValue = IRB.CreateZExt(value, IRB.getInt64Ty());
				tlCall = IRB.CreateCall5(TL->logInt, vLineNo, vDataSize, vValue, vIsReg, vvRegOrFuncID);
			}
			else if(type >= llvm::Type::HalfTyID && type <= llvm::Type::PPC_FP128TyID) {
				vValue = IRB.CreateFPExt(value, IRB.getDoubleTy());
				tlCall = IRB.CreateCall5(TL->logDouble, vLineNo, vDataSize, vValue, vIsReg, vvRegOrFuncID);
			}
			else if(llvm::Type::PointerTyID == type) {
				vValue = IRB.CreatePtrToInt(value, IRB.getInt64Ty());
				tlCall = IRB.CreateCall5(TL->logInt, vLineNo, vDataSize, vValue, vIsReg, vvRegOrFuncID);
			}
			else {
				// TODO: assert mesmo ou apenas um silent error?
				assert(false && "Value is of unsupported type");
			}
		}
		else {
			vValue = ConstantInt::get(IRB.getInt64Ty(), 0);
			tlCall = IRB.CreateCall5(TL->logInt, vLineNo, vDataSize, vValue, vIsReg, vvRegOrFuncID);
		}
	}
	else {
		if(value) {
			if(llvm::Type::IntegerTyID == type) {
				vValue = IRB.CreateZExt(value, IRB.getInt64Ty());
				tlCall = IRB.CreateCall4(TL->logIntNoReg, vLineNo, vDataSize, vValue, vIsReg);
			}
			else if(type >= llvm::Type::HalfTyID && type <= llvm::Type::PPC_FP128TyID) {
				vValue = IRB.CreateFPExt(value, IRB.getDoubleTy());
				tlCall = IRB.CreateCall4(TL->logDoubleNoReg, vLineNo, vDataSize, vValue, vIsReg);
			}
			else if(llvm::Type::PointerTyID == type) {
				vValue = IRB.CreatePtrToInt(value, IRB.getInt64Ty());
				tlCall = IRB.CreateCall4(TL->logIntNoReg, vLineNo, vDataSize, vValue, vIsReg);
			}
			else {
				// TODO: assert mesmo ou apenas um silent error?
				assert(false && "Value is of unsupported type");
			}
		}
		else {
			vValue = ConstantInt::get(IRB.getInt64Ty(), 0);
			tlCall = IRB.CreateCall4(TL->logIntNoReg, vLineNo, vDataSize, vValue, vIsReg);
		}
	}
}

InstrumentForDDDG::InstrumentForDDDG() : ModulePass(ID) {
	DEBUG(dbgs() << "\n\tInitialize InstrumentForDDDG pass\n");
	staticInstID2OpcodeMap.clear();
	instName2bbNameMap.clear();
	initializeInstrumentForDDDGPass(*PassRegistry::getPassRegistry());
}

void InstrumentForDDDG::getAnalysisUsage(AnalysisUsage &AU) const {
	AU.setPreservesCFG();
}

bool InstrumentForDDDG::doInitialization(Module &M) {
	assert(!args.kernelNames.empty() && "No kernel set\n");

	TL.initialiseDefaults(M);
	IJ.initialise(M, TL);
	ST = createSlotTracker(&M);
	ST->initialize();
	currModule = &M;
	currFunction = nullptr;

	return false;
}

// XXX: Yes, there is ABSOLUTELY a better way to do that, but I am afraid of breaking something
// up if I try to improve this code (maybe some valid logic depends on that? Who knows? God knows?
// The original developer knows? Or maybe no one knows?)
// Anywayssssss... Just leaving this as it is.
int InstrumentForDDDG::shouldTrace(std::string call) {
	std::string intrinsicsList[] = {
		"llvm.memcpy",
		"llvm.memmove",
		"llvm.memset",
		"llvm.sqrt",
		"llvm.powi",
		"llvm.sin",
		"llvm.cos",
		"llvm.pow",
		"llvm.exp",
		"llvm.exp2",
		"llvm.log",
		"llvm.log10",
		"llvm.log2",
		"llvm.fma",
		"llvm.fabs",
		"llvm.copysign",
		"llvm.floor",
		"llvm.ceil",
		"llvm.trunc",
		"llvm.rint",
		"llvm.nearbyint",
		"llvm.round",
		"llvm.bswap",
		"llvm.ctpop",
		"llvm.ctlz",
		"llvm.cttz",
		"llvm.sadd.with.overflow",
		"llvm.uadd.with.overflow",
		"llvm.ssub.with.overflow",
		"llvm.usub.with.overflow",
		"llvm.smul.with.overflow",
		"llvm.umul.with.overflow",
		"llvm.fmuladd"
	};
	const char *cCall = call.c_str();

	if(isFunctionOfInterest(call))
		return TRACE_FUNCTION_OF_INTEREST;

	if(cCall == strstr(cCall, "dmaLoad"))
		return TRACE_DMA_LOAD;
	if(cCall == strstr(cCall, "dmaStore"))
		return TRACE_DMA_STORE;

	for(std::string elem : intrinsicsList) {
		if(cCall == strstr(cCall, elem.c_str()))
			return TRACE_INTRINSIC;
	}

	return NOT_TRACE;
}

bool InstrumentForDDDG::getInstID(Instruction *I, std::string bbID, int &instCnt, std::string &instID) {
	int id = ST->getLocalSlot(I);

	if(I->hasName()) {
		instID = I->getName();
		return true;
	}
	else if(id >= 0) {
		instID = std::to_string(id);
		return true;
	}
	else if(-1 == id) {
		instID = bbID + "-" + std::to_string(instCnt++);
		return true;
	}

	// I'm not sure if the code would ever reach this position (i.e. id different from >= 0 and -1?). But anyway...
	return false;
}

std::string InstrumentForDDDG::getBBID(Value *BB) {
	int id = ST->getLocalSlot(BB);

	if(BB->hasName())
		return BB->getName();
	else if(id >= 0)
		return std::to_string(id);
	else
		return nullptr;
}

void InstrumentForDDDG::extractMemoryTraceForAccessPattern() {
	VERBOSE_PRINT(errs() << "[][memoryTrace] Memory trace started\n");

	std::string fileName = args.workDir + "mem_trace.txt";
	std::string traceFileName = args.workDir + "dynamic_trace.gz";
	std::ofstream memTraceFile;
	gzFile traceFile;
	bool traceEntry = false;
	int opcode = -1;

	memTraceFile.open(fileName);
	assert(memTraceFile.is_open() && "Could not open memory trace output file");

	traceFile = gzopen(traceFileName.c_str(), "r");
	assert(traceFile != Z_NULL && "Could not open trace input file");

	while(!gzeof(traceFile)) {
		char buffer[1024];
		if(Z_NULL == gzgets(traceFile, buffer, sizeof(buffer)))
			continue;

		std::string line(buffer);
		size_t tagPos = line.find(",");

		if(std::string::npos == tagPos)
			continue;

		std::string tag = line.substr(0, tagPos);
		std::string rest = line.substr(tagPos + 1);

		if(!traceEntry && !(tag.compare("0"))) {
			int lineNo;
			char buffer1[1024];
			char buffer2[1024];
			char buffer3[1024];
			int count;
			sscanf(rest.c_str(), "%d,%[^,],%[^,],%[^,],%d,%d\n", &lineNo, buffer1, buffer2, buffer3, &opcode, &count);
			std::string funcName(buffer1);
			std::string bbName(buffer2);
			std::string instName(buffer3);

			if(isStoreOp(opcode) || isLoadOp(opcode)) {
				traceEntry = true;

				bbFuncNamePair2lpNameLevelPairMapTy::iterator it = bbFuncNamePair2lpNameLevelPairMap.find(std::make_pair(bbName, funcName));
				if(it != bbFuncNamePair2lpNameLevelPairMap.end()) {
					lpNameLevelPairTy lpNameLevelPair = it->second;
					std::string loopName = lpNameLevelPair.first;
					std::string wholeLoopName = appendDepthToLoopName(loopName, lpNameLevelPair.second);
					unsigned int numLevels = LpName2numLevelMap.at(loopName);

					memTraceFile << wholeLoopName << "," << numLevels << "," << instName << ",";

					if(isLoadOp(opcode))
						memTraceFile << "load,";
					else if(isStoreOp(opcode))
						memTraceFile << "store,";

					memTraceFile << count << ",";
				}
			}
		}
		else if(traceEntry && ((!(tag.compare("1")) && isLoadOp(opcode)) || (!(tag.compare("2")) && isStoreOp(opcode)))) {
			unsigned long addr;

			sscanf(rest.c_str(), "%*d,%ld,%*d,%*s\n", &addr);
			memTraceFile << addr << ",";
		}
		else if(traceEntry && ((!(tag.compare("r")) && isLoadOp(opcode)) || (!(tag.compare("1")) && isStoreOp(opcode)))) {
			float value;

			sscanf(rest.c_str(), "%*d,%f,%*d,%*s\n", &value);
			memTraceFile << value << "\n";
			traceEntry = false;
		}
	}

	gzclose(traceFile);
	memTraceFile.close();

	VERBOSE_PRINT(errs() << "[][memoryTrace] Memory trace finished\n");
}

bool InstrumentForDDDG::runOnModule(Module &M) {
	errs() << "========================================================\n";
	errs() << "Starting code instrumentation for DDDG generation\n";

	// TODO: Ver se ela logica ta OK pro result
	bool result = false;
	for(Module::iterator FI = M.begin(); FI != M.end(); FI++) {
		if(isFunctionOfInterest(FI->getName())) {
			VERBOSE_PRINT(errs() << "[instrumentForDDDG] Injecting trace code in \"" + FI->getName() + "\"\n");
			for(Function::iterator BI = FI->begin(); BI != FI->end(); BI++)
				result += performOnBasicBlock(*BI);
		}
	}

	if(args.MODE_TRACE_AND_ESTIMATE == args.mode || args.MODE_TRACE_ONLY == args.mode) {
		VERBOSE_PRINT(errs() << "[instrumentForDDDG] Starting profiling engine\n");

		/// Integrate JIT profiling engine and run the embedded profiler
		ProfilingEngine P(M, TL);
		P.runOnProfiler();

		/// Finished Profiling
		VERBOSE_PRINT(errs() << "[instrumentForDDDG] Profiling finished\n");

		if(args.memTrace) {
			errs() << "========================================================\n";
			errs() << "Starting memory trace extraction\n";

			VERBOSE_PRINT(errs() << "[instrumentForDDDG] Starting memory trace extraction\n");
			extractMemoryTraceForAccessPattern();
			VERBOSE_PRINT(errs() << "[instrumentForDDDG] Memory trace extraction finished\n");
		}

		if(args.MODE_TRACE_ONLY == args.mode)
			return result;
	}
	else {
		VERBOSE_PRINT(errs() << "[instrumentForDDDG] Skipping dynamic trace\n");
	}

	// Verify the module
	assert(verifyModuleAndPrintErrors(M) && "Errors found in module\n");

	loopBasedTraceAnalysis();

	VERBOSE_PRINT(errs() << "[instrumentForDDDG] Finished\n");

#ifdef DBG_PRINT_ALL
	printDatabase();
#endif

	return result;
}

bool InstrumentForDDDG::performOnBasicBlock(BasicBlock &BB) {
	Function *F = BB.getParent();
	int instCnt = 0;
	std::string funcName = F->getName();

	if(currFunction != F) {
		ST->purgeFunction();
		ST->incorporateFunction(F);
		currFunction = F;
	}

	if(!isFunctionOfInterest(funcName))
		return false;

	// Deal with phi nodes
	BasicBlock::iterator insIt = BB.getFirstInsertionPt();
	BasicBlock::iterator it = BB.begin();
	if(dyn_cast<PHINode>(it)) {
		for(; PHINode *N = dyn_cast<PHINode>(it); it++) {
			Value *currOperand = nullptr;
			bool isReg = false;
			int size = 0, opcode;
			std::string bbID, instID, operR;
			int lineNumber = -1;

			bbID = getBBID(&BB);
			getInstID(it, bbID, instCnt, instID);
			opcode = it->getOpcode();

			if(MDNode *N = it->getMetadata("dbg")) {
				DILocation Loc(N);
				lineNumber = Loc.getLineNumber();
			}

			// Inject phi node trace
			IJ.injectTraceHeader(insIt, lineNumber, funcName, bbID, instID, opcode);
			//printLine(insIt, 0, lineNumber, funcName, bbID, instID, opcode);

			// Create calls to trace logger informing the inputs for phi
			int numOfOperands = it->getNumOperands();
			for(int i = numOfOperands - 1; i >= 0; i--) {
				currOperand = it->getOperand(i);
				isReg = currOperand->hasName();

				// Input to phi is an instruction
				if(Instruction *I = dyn_cast<Instruction>(currOperand)) {
					int flag = 0;
					isReg = getInstID(I, "", flag, operR);
					assert(0 == flag && "Unnamed instruction with no local slot found");

					// Input to phi is of vector type
					if(currOperand->getType()->isVectorTy())
						IJ.injectTrace(insIt, i + 1, operR, currOperand->getType(), nullptr, isReg);
					else
						IJ.injectTrace(insIt, i + 1, operR, I->getType(), nullptr, isReg);
				}
				// Input to phi is of vector type
				else if(currOperand->getType()->isVectorTy()) {
					IJ.injectTrace(insIt, i + 1, currOperand->getName(), currOperand->getType(), nullptr, isReg);
				}
				// Input to phi is none of the above
				else {
					IJ.injectTrace(insIt, i + 1, currOperand->getName(), currOperand->getType(), currOperand, isReg);
				}
			}

			// Inject phi node result trace
			if(!(it->getType()->isVoidTy())) {
				isReg = true;
				if(it->getType()->isVectorTy()) {
					IJ.injectTrace(insIt, RESULT_LINE, instID, it->getType(), nullptr, isReg);
				}
				else if(it->isTerminator()) {
					// TODO: put a silent warning or fail assert?
				}
				else {
					IJ.injectTrace(insIt, RESULT_LINE, instID, it->getType(), it, isReg);
				}
			}
		}
	}

	// Deal with the rest (non-phi)
	BasicBlock::iterator nextIt;
	for(it = insIt; it != BB.end(); it = nextIt) {
		Value *currOperand = nullptr;
		bool isReg = false;
		int size = 0, opcode;
		std::string bbID, instID, operR;
		int lineNumber = -1;

		nextIt = it;
		nextIt++;

		bbID = getBBID(&BB);
		getInstID(it, bbID, instCnt, instID);
		opcode = it->getOpcode();

		if(MDNode *N = it->getMetadata("dbg")) {
			DILocation Loc(N);
			lineNumber = Loc.getLineNumber();
		}

		int stRes = NOT_TRACE;
		if(CallInst *I = dyn_cast<CallInst>(it)) {
			stRes = shouldTrace(I->getCalledFunction()->getName());
			if(NOT_TRACE == stRes)
				continue;
		}

		int numOfOperands = it->getNumOperands();

		// This is a call instruction AND it is of our interest (i.e. function of interest or DMA load or store)
		if(Instruction::Call == it->getOpcode() && (TRACE_FUNCTION_OF_INTEREST == stRes || TRACE_DMA_LOAD == stRes || TRACE_DMA_STORE == stRes)) {
			CallInst *CI = dyn_cast<CallInst>(it);
			operR = CI->getCalledFunction()->getName();

			// Inject call trace
			if(TRACE_DMA_LOAD == stRes || TRACE_DMA_STORE == stRes)
				IJ.injectTraceHeader(it, lineNumber, funcName, bbID, instID, stRes);
			else
				IJ.injectTraceHeader(it, lineNumber, funcName, bbID, instID, opcode);

			currOperand = it->getOperand(numOfOperands - 1);
			isReg = currOperand->hasName();
			assert(isReg && "Last operand of call has no name");

			// Inject call last operand trace
			IJ.injectTrace(it, numOfOperands, operR, currOperand->getType(), currOperand, isReg);

			const Function::ArgumentListType &AL(CI->getCalledFunction()->getArgumentList());
			int numOfCallOperands = CI->getNumArgOperands();
			int callID = 0;

			for(Function::ArgumentListType::const_iterator argIt = AL.begin(); argIt != AL.end(); argIt++) {
				std::string currArgName = argIt->getName();

				currOperand = it->getOperand(callID);
				isReg = currOperand->hasName();

				// Argument to call is an instruction
				if(Instruction *I = dyn_cast<Instruction>(currOperand)) {
					int flag = 0;
					isReg = getInstID(I, "", flag, operR);
					assert(0 == flag && "Unnamed instruction with no local slot found");

					if(currOperand->getType()->isVectorTy()) {
						IJ.injectTrace(it, callID + 1, operR, currOperand->getType(), nullptr, isReg);
						IJ.injectTrace(it, FORWARD_LINE, currArgName, currOperand->getType(), nullptr, true);
					}
					else {
						IJ.injectTrace(it, callID + 1, operR, I->getType(), currOperand, isReg);
						IJ.injectTrace(it, FORWARD_LINE, currArgName, I->getType(), currOperand, true);
					}
				}
				// Argument to call is not an instruction
				else {
					if(currOperand->getType()->isVectorTy()) {
						IJ.injectTrace(it, callID + 1, currOperand->getName(), currOperand->getType(), nullptr, isReg);
						IJ.injectTrace(it, FORWARD_LINE, currArgName, currOperand->getType(), nullptr, true);
					}
					else if(currOperand->getType()->isLabelTy()) {
						IJ.injectTrace(it, callID + 1, getBBID(currOperand), currOperand->getType(), nullptr, isReg);
						IJ.injectTrace(it, FORWARD_LINE, currArgName, currOperand->getType(), nullptr, true);
					}
					else if(2 == currOperand->getValueID()) {
						IJ.injectTrace(it, callID + 1, currOperand->getName(), currOperand->getType(), nullptr, isReg);
						IJ.injectTrace(it, FORWARD_LINE, currArgName, currOperand->getType(), nullptr, true);
					}
					else {
						IJ.injectTrace(it, callID + 1, currOperand->getName(), currOperand->getType(), currOperand, isReg);
						IJ.injectTrace(it, FORWARD_LINE, currArgName, currOperand->getType(), currOperand, true);
					}
				}

				callID++;
			}
		}
		// This is not a call instruction
		else {
			// Insert trace (non-phi, non-call)
			IJ.injectTraceHeader(it, lineNumber, funcName, bbID, instID, opcode);

			// Inject operands of this instruction
			if(numOfOperands > 0) {
				for(int i = numOfOperands - 1; i >= 0; i--) {
					currOperand = it->getOperand(i);
					isReg = currOperand->hasName();

					// Input is an instruction
					if(Instruction *I = dyn_cast<Instruction>(currOperand)) {
						int flag = 0;
						isReg = getInstID(I, "", flag, operR);
						assert(0 == flag && "Unnamed instruction with no local slot found");

						if(currOperand->getType()->isVectorTy())
							IJ.injectTrace(it, i + 1, operR, currOperand->getType(), nullptr, isReg);
						else
							IJ.injectTrace(it, i + 1, operR, I->getType(), currOperand, isReg);
					}
					else {
						if(currOperand->getType()->isVectorTy())
							IJ.injectTrace(it, i + 1, currOperand->getName(), currOperand->getType(), nullptr, isReg);
						else if(currOperand->getType()->isLabelTy())
							IJ.injectTrace(it, i + 1, getBBID(currOperand), currOperand->getType(), nullptr, isReg);
						else if(2 == currOperand->getValueID())
							IJ.injectTrace(it, i + 1, currOperand->getName(), currOperand->getType(), nullptr, isReg);
						else
							IJ.injectTrace(it, i + 1, currOperand->getName(), currOperand->getType(), currOperand, isReg);
					}
				}
			}
		}

		// Inject instruction result trace
		if(!(it->getType()->isVoidTy())) {
			isReg = true;
			if(it->getType()->isVectorTy()) {
				IJ.injectTrace(nextIt, RESULT_LINE, instID, it->getType(), nullptr, isReg);
			}
			else if(it->isTerminator()) {
				// TODO: put a silent warning or fail assert?
			}
			else {
				IJ.injectTrace(nextIt, RESULT_LINE, instID, it->getType(), it, isReg);
			}
		}
	}

	return true;
}

void InstrumentForDDDG::removeConfig(std::string kernelName) {
	std::string inputKernel = args.outWorkDir + kernelName;

	std::string pipelining(inputKernel + "_pipelining.cfg");
	std::ifstream pipeliningFile(pipelining);
	if(pipeliningFile.is_open()) {
		pipeliningFile.close();
		assert(!remove(pipelining.c_str()) && "Error removing pipelining config file");
	}

	std::string unrolling(inputKernel + "_unrolling.cfg");
	std::ifstream unrollingFile(unrolling);
	if(unrollingFile.is_open()) {
		unrollingFile.close();
		assert(!remove(unrolling.c_str()) && "Error removing unrolling config file");
	}

	std::string arrayInfo(inputKernel + "_arrayInfo.cfg");
	std::ifstream arrayInfoFile(arrayInfo);
	if(arrayInfoFile.is_open()) {
		arrayInfoFile.close();
		assert(!remove(arrayInfo.c_str()) && "Error removing array info config file");
	}

	std::string partition(inputKernel + "_partition.cfg");
	std::ifstream partitionFile(partition);
	if(partitionFile.is_open()) {
		partitionFile.close();
		assert(!remove(partition.c_str()) && "Error removing partition config file");
	}

	std::string completePartition(inputKernel + "_completepartition.cfg");
	std::ifstream completePartitionFile(completePartition);
	if(completePartitionFile.is_open()) {
		completePartitionFile.close();
		assert(!remove(completePartition.c_str()) && "Error removing complete partition config file");
	}
}

void InstrumentForDDDG::parseConfig(std::string kernelName) {
#if 0
	ifstream config_file;
	//std::string input_kernel = input_path + kernel_name;
	std::string input_kernel = outputPath + kernel_name;
	//std::string config_file_name = input_kernel + "_configuration";
	//std::string config_file_name = input_path + config_filename;
	std::string config_file_name = config_filename;
	config_file.open(config_file_name);
	if (!config_file.is_open()) {
		assert(false && "Error: missing configuration file!\n");
	}
	std::string wholeline;

	std::vector<std::string> pipelining_config;
	std::vector<std::string> unrolling_config;
	std::vector<std::string> partition_config;
	std::vector<std::string> comp_partition_config;
	std::vector<std::string> array_info;

	pipeline_loop_levelVec.clear();

	while (!config_file.eof())
	{
		wholeline.clear();
		getline(config_file, wholeline);
		if (wholeline.size() == 0)
			break;
		string type, rest_line;
		int pos_end_tag = wholeline.find(",");
		if (pos_end_tag == -1)
			break;
		type = wholeline.substr(0, pos_end_tag);
		//rest_line = wholeline.substr(pos_end_tag + 1);

		if (!type.compare("pipeline")) {
			pipelining_config.push_back(wholeline);
		}
		else if (!type.compare("unrolling")) {
			unrolling_config.push_back(wholeline);
		}
		else if (!type.compare("array")) {
			array_info.push_back(wholeline);
		}
		else if (!type.compare("partition")) {
			if (wholeline.find("complete") == std::string::npos)
				partition_config.push_back(wholeline);
			else
				comp_partition_config.push_back(wholeline);
		}
		else
		{
			// Can not detect type of this line, ignore it and do
			// nothing here.
		}
	}
	config_file.close();
	//std::string test = "_test.txt";
	std::map<std::string, unsigned> whole_lpName2comp_unroll_factorMap;
	if (pipelining_config.size() != 0)
	{
		string pipelining(input_kernel);
		pipelining += "_pipelining_config";
		//pipelining += test;
		ofstream pipe_config;
		pipe_config.open(pipelining);
		for (unsigned i = 0; i < pipelining_config.size(); ++i) {
			std::string pipelining_conf = pipelining_config.at(i);
			pipe_config << pipelining_conf << endl;

			char type[256];
			char funcName_char[256];
			unsigned loop_num, loop_level;
			sscanf(pipelining_conf.c_str(), "%[^,],%[^,],%d,%d\n", type, funcName_char, &loop_num, &loop_level);
			std::string func_name(funcName_char);
			std::string lp_name = func_name + "_loop-" + std::to_string(loop_num);
			LpName2numLevelMapTy::iterator found_lpLev = LpName2numLevelMap.find(lp_name);
			assert(found_lpLev != LpName2numLevelMap.end() && "Error: Cannot find loop name in LpName2numLevelMap!\n");
			unsigned num_level = LpName2numLevelMap[lp_name];
			std::string whole_loop_name = lp_name + "_" + std::to_string(loop_level);
			pipeline_loop_levelVec.push_back(whole_loop_name);

			assert( (loop_level<=num_level) && "Error: loop_level is larger than num_level!\n" );
			if (loop_level == num_level) {
				// Apply loop pipelining to the innermost level loop
				continue;
			}
			for (unsigned i = loop_level+1; i < num_level+1; i++) {
				std::string whole_lp_name = lp_name + "_" + std::to_string(i);
				wholeloopName2loopBoundMapTy::iterator it_whole = wholeloopName2loopBoundMap.find(whole_lp_name);
				assert((it_whole!=wholeloopName2loopBoundMap.end()) && "Error: Can not find loop name in wholeloopName2loopBoundMap!\n");
				unsigned lp_bound = wholeloopName2loopBoundMap[whole_lp_name];
				if (lp_bound == 0) {
					VERBOSE_PRINT(std::cout << "DEBUG-INFO: [parsing_configuration-extraction] loop " << lp_name << " level " << i);
					VERBOSE_PRINT(std::cout << " has a variable loop bound, can not support in current version!" << std::endl);
				}
				whole_lpName2comp_unroll_factorMap.insert(std::make_pair(whole_lp_name, lp_bound));
			}
		}
		pipe_config.close();
	}

	if (unrolling_config.size() != 0)
	{
		string file_name(input_kernel);
		file_name += "_unrolling_config";
		//file_name += test;
		ofstream output;
		output.open(file_name);
		std::vector<std::string> unroll_wholelpName_str;
		for (unsigned i = 0; i < unrolling_config.size(); ++i) {
			std::string unrolling_conf = unrolling_config.at(i);
			char type[256];
			char funcName_char[256];
			unsigned loop_num, loop_level;
			unsigned line_num, unroll_factor;
			sscanf(unrolling_conf.c_str(), "%[^,],%[^,],%d,%d,%d,%d\n", type, funcName_char, &loop_num, &loop_level, &line_num, &unroll_factor);
			std::string func_name(funcName_char);
			std::string lp_name = func_name + "_loop-" + std::to_string(loop_num);
			std::string whole_lp_name = lp_name + "_" + std::to_string(loop_level);
			unroll_wholelpName_str.push_back(whole_lp_name);
			std::map<std::string, unsigned>::iterator it_whole = whole_lpName2comp_unroll_factorMap.find(whole_lp_name);
			if (it_whole == whole_lpName2comp_unroll_factorMap.end()) {
				output << unrolling_config.at(i) << endl;
			}
			else{
				unsigned static_bound = it_whole->second;
				if (static_bound == 0) {
					output << unrolling_config.at(i) << endl;
				}
				else {
					std::string new_unroll_conf = "unrolling," + func_name + "," + std::to_string(loop_num) + ",";
					new_unroll_conf += std::to_string(loop_level) + "," + std::to_string(line_num) + "," + std::to_string(static_bound);
					output << new_unroll_conf << endl;
				}
			}
		}

		// If we apply loop pipelining at the upper loop level, but we donot specify completely unrolling pragma at the inner loop
		// levels, we need to add it to unrolling configuration file.
		std::map<std::string, unsigned>::iterator it_pipe = whole_lpName2comp_unroll_factorMap.begin();
		std::map<std::string, unsigned>::iterator ie_pipe = whole_lpName2comp_unroll_factorMap.end();
		for (; it_pipe != ie_pipe; ++it_pipe) {
			std::string whole_lp_name = it_pipe->first;
			unsigned lp_bound = it_pipe->second;
			std::vector<std::string>::iterator it_unr = unroll_wholelpName_str.begin();
			std::vector<std::string>::iterator ie_unr = unroll_wholelpName_str.end();
			std::vector<std::string>::iterator not_found = std::find(it_unr, ie_unr, whole_lp_name);
			if (not_found == unroll_wholelpName_str.end()) {
				std::size_t pos = whole_lp_name.find("_loop-");
				std::string func_name = whole_lp_name.substr(0, pos);
				std::string rest_str = whole_lp_name.substr(pos+6);
				pos = rest_str.find("_");
				unsigned loop_num = std::stoi(rest_str.substr(0, pos));
				unsigned loop_level = std::stoi(rest_str.substr(pos+1));
				int line_num = -1;
				
				std::string new_unroll_conf = "unrolling," + func_name + "," + std::to_string(loop_num) + ",";
				new_unroll_conf += std::to_string(loop_level) + "," + std::to_string(line_num) + "," + std::to_string(lp_bound);
				output << new_unroll_conf << endl;
			}
		}

		output.close();
	}

	if (array_info.size() != 0) {
		string file_name(input_kernel);
		file_name += "_array_info";
		//file_name += test;
		ofstream output;
		output.open(file_name);
		for (unsigned i = 0; i < array_info.size(); ++i)
			output << array_info.at(i) << endl;
		output.close();
	}
	else {
		assert(false && "Error: please provide array information for this kernel!\n");
	}

	if (partition_config.size() != 0)
	{
		string partition(input_kernel);
		partition += "_partition_config";
		//partition += test;
		ofstream part_config;
		part_config.open(partition);
		for (unsigned i = 0; i < partition_config.size(); ++i)
			part_config << partition_config.at(i) << endl;
		part_config.close();
	}

	if (comp_partition_config.size() != 0)
	{
		string complete_partition(input_kernel);
		complete_partition += "_complete_partition_config";
		//complete_partition += test;
		ofstream comp_config;
		comp_config.open(complete_partition);
		for (unsigned i = 0; i < comp_partition_config.size(); ++i)
			comp_config << comp_partition_config.at(i) << endl;
		comp_config.close();
	}

	increase_load_latency = false;
	if (pipelining_config.size() != 0) {
		increase_load_latency = false;
	}
	else {
		if (partition_config.size() != 0) {
			for (int i = 0; i < partition_config.size(); i++) {
				std::string  part_str = partition_config.at(i);
				unsigned size, p_factor, wordsize;
				char config_type[256];
				char type[256];
				char base_addr[256];
				sscanf(part_str.c_str(), "%[^,],%[^,],%[^,],%d,%d,%d\n", config_type, type, base_addr, &size, &wordsize, &p_factor);
				if (p_factor > 1) {
					increase_load_latency = true;
					break;
				}
			}
		}
		else {
			increase_load_latency = false;
		}
	}
#endif
}

void InstrumentForDDDG::getUnrollingConfiguration(lpNameLevelPair2headBBnameMapTy &lpNameLvPair2headerBBMap) {
#if 0
	// Initialize loopName2levelUnrollPairListMap, all unrolling factors are set to 1 as default.
	loopName2levelUnrollVecMap.clear();
	lpNameLevelPair2headBBnameMapTy::iterator it = lpNameLvPair2headerBBMap.begin();
	lpNameLevelPair2headBBnameMapTy::iterator ie = lpNameLvPair2headerBBMap.end();
	for (; it != ie; ++it) {
		std::string loopName = it->first.first;
		unsigned loop_level = std::stoul(it->first.second);
		loopName2levelUnrollVecMapTy::iterator itVec = loopName2levelUnrollVecMap.find(loopName);
		if (itVec != loopName2levelUnrollVecMap.end()) {
			itVec->second.push_back(1);
		}
		else {
			std::vector<unsigned> levelUnrollPairVec;
			levelUnrollPairVec.push_back(1);
			loopName2levelUnrollVecMap.insert(std::make_pair(loopName, levelUnrollPairVec));
		}
	}

	// Read unrolling configuration and update loopName2levelUnrollPairListMap and 
	unrollingConfig.clear();
	bool succeed_or_not = readUnrollingConfig(loopName2levelUnrollVecMap, unrollingConfig);
#endif
}

bool InstrumentForDDDG::readUnrollingConfig(loopName2levelUnrollVecMapTy &lpName2levelUrPairVecMap, std::unordered_map<int, int> &unrollingConfig) {
#if 0
	//loopName2levelUnrollPairListMapTy vector is better
	std::string kernel_name = kernel_names.at(0);
	ifstream config_file;
	//std::string file_name(inputPath + kernel_name);
	std::string file_name(outputPath + kernel_name);
	file_name += "_unrolling_config";
	config_file.open(file_name.c_str());
	if (!config_file.is_open())
		return 0;
	while (!config_file.eof())
	{
		std::string wholeline;
		getline(config_file, wholeline);
		if (wholeline.size() == 0)
			break;
		char func[256];
		char config_type[256];
		unsigned ith_loop;
		unsigned loop_level;
		int line_num, factor;
		sscanf(wholeline.c_str(), "%[^,],%[^,],%d, %d, %d,%d\n", config_type, func, &ith_loop, &loop_level, &line_num, &factor);
		unrolling_config[line_num] = factor;
		std::string loop_name = std::string(func) + "_loop-" + std::to_string(ith_loop);
		loopName2levelUnrollVecMapTy::iterator it = lpName2levelUrPairVecMap.find(loop_name);
		if (it != lpName2levelUrPairVecMap.end()) {
			unsigned innermost_level = it->second.size();
			if ((loop_level != innermost_level) && factor > 1) {
				//std::cout << "DEBUG-INFO: [parsing_read-unrolling-configuration] For FPGA implementation, the unrolling for the innermost loop level is more interesting, we only consider this level loop." << std::endl;
				assert(loop_level<=innermost_level && "Error: Loop level information inside the unrolling_config file exceeds number of levels in this loop!\n");
				it->second[loop_level-1] = factor;
			}
			else {
				std::string whole_loop_name = loop_name + "_" + std::to_string(loop_level);
				unsigned lp_bound = wholeloopName2loopBoundMap.at(whole_loop_name);
				if (lp_bound == 0) {
					// Need to get loop bound at runtime
					it->second.at(loop_level - 1) = factor;
				}
				else {
					if ((unsigned)factor > lp_bound) {
						it->second.at(loop_level - 1) = lp_bound;
						//assert((factor<lp_bound) && "Error: Loop unrolling factor is larger than loop bound! Please use smaller loop bound\n");
					}
					else {
						it->second.at(loop_level - 1) = factor;
					}
				}

			}
		}
		else {
			std::cout << "DEBUG-INFO: [parsing_read-unrolling-configuration] Warning: Can not find the loop name inside configuration files" << std::endl;
			assert(false && "Please check whether configuration file is written in a correct way!\n");
		}
	}
	config_file.close();
	return 1;
#endif
}

void InstrumentForDDDG::loopBasedTraceAnalysis() {
	VERBOSE_PRINT(errs() << "[][loopBasedTraceAnalysis] Loop-based trace analysis started\n");

	std::string traceFileName = args.workDir + "dynamic_trace.gz";
	std::string kernelName = args.kernelNames.at(0);

	VERBOSE_PRINT(errs() << "[][loopBasedTraceAnalysis] Writing header of summary file\n");
	openSummaryFile(kernelName); 

	VERBOSE_PRINT(errs() << "[][loopBasedTraceAnalysis] Parsing configuration file\n");
	removeConfig(kernelName);
	parseConfig(kernelName);

#if 0
	VERBOSE_PRINT(errs() << "[][loopBasedTraceAnalysis] Summary file closed\n");
#endif
#if 0
	errs() << "DEBUG-INFO: [trace-analysis_loop-based-trace-analysis] Analysis loops' IL and II inside the kernel\n";
	/// Create Dynamic Data Dependence Graph
	std::string trace_file_name = inputPath + "dynamic_trace.gz";
	//std::string trace_file_name = "dynamic_trace";
	std::string kernel_name = kernel_names.at(0);

	/// Open summary file
	open_summary_file(summary, kernel_name);

	/// Remove previous configuration files
	remove_config(kernel_name, inputPath);

	/// Generate configuration files
	parse_config(kernel_name, inputPath);

	// Get unrolling configuration
	getUnrollingConfiguration(lpNameLevelPair2headBBnameMap);

	loopName2levelUnrollVecMapTy::iterator it = loopName2levelUnrollVecMap.begin();
	loopName2levelUnrollVecMapTy::iterator ie = loopName2levelUnrollVecMap.end();
	for (; it != ie; ++it) {
		bool enable_pipelining = false;
		bool skip_loop_analysis = false;
		std::string loop_name = it->first;

		if (target_loops.size() != 0) {
			skip_loop_analysis = true;
			size_t pos_index = loop_name.find("-");
			if (pos_index != std::string::npos) {
				std::string loop_index = loop_name.substr(pos_index + 1);
				std::vector<std::string>::iterator found_lp_index = std::find(target_loops.begin(), target_loops.end(), loop_index);
				if (found_lp_index != target_loops.end()) {
					skip_loop_analysis = false;
				}
			}

			if (skip_loop_analysis == true) {
				// Do not analyze this loop
				continue;
			}
		}

		std::vector<unsigned> levelUnrollVec = it->second;
		unsigned level_size = levelUnrollVec.size();

		unsigned target_loop_level = 1;
		unsigned target_unroll_factor = 1;

		for (int i = (int) level_size-1; i >= 0; i--) {
			unsigned unroll_factor = levelUnrollVec.at(i);
			std::string whole_lp_name = loop_name + "_" + std::to_string(i+1);
			wholeloopName2loopBoundMapTy::iterator it_found = wholeloopName2loopBoundMap.find(whole_lp_name);
			if (it_found != wholeloopName2loopBoundMap.end()) {
				unsigned level_bound = it_found->second;
				bool exit_flag = false;
				if (unroll_factor != level_bound) {
					target_loop_level = i + 1;
					exit_flag = true;
					//break;
				}
				target_unroll_factor = unroll_factor;

				std::vector<std::string>::iterator found_pipe = std::find(pipeline_loop_levelVec.begin(), pipeline_loop_levelVec.end(), whole_lp_name);
				if (found_pipe != pipeline_loop_levelVec.end()) {
					enable_pipelining = true;
				}
				else {
					enable_pipelining = false;
				}

				if (exit_flag) {
					break;
				}
			}
			else {
				assert(false && "Error: Can not find loop name in wholeloopName2loopBoundMap!\n");
			}
		}

		unsigned target_loop_bound;
		std::string whole_target_name = loop_name + "_" + std::to_string(target_loop_level);
		wholeloopName2loopBoundMapTy::iterator found_it = wholeloopName2loopBoundMap.find(whole_target_name);
		if (found_it != wholeloopName2loopBoundMap.end()) {
			target_loop_bound = found_it->second;
		}
		else {
			assert(false && "Error: Cannot find loop name in wholeloopName2loopBoundMap!\n");
		}

		unsigned unroll_factor = 1;
		/// Used to get recII
		unsigned IL_asap_rec = 0;
		if (enable_pipelining == true) {
			DynamicDatapath* datapath_tmp;
			unsigned target_factor = target_unroll_factor * 2;
			unroll_factor = ((target_loop_bound < target_factor) && (target_loop_bound != 0)) ? target_loop_bound : target_factor;
			datapath_tmp = new DynamicDatapath(kernel_name, trace_file_name, inputPath, loop_name, target_loop_level, unroll_factor , enable_pipelining, 0);
			IL_asap_rec = datapath_tmp->getIL_asap_ii();
			delete datapath_tmp;
		}

		errs() << "DEBUG-INFO: [trace-analysis_loop-based-trace-analysis] Building Dynamic Datapath for loop " << loop_name <<"\n";
		unroll_factor = ((target_loop_bound < target_unroll_factor) && (target_loop_bound != 0)) ? target_loop_bound : target_unroll_factor;
		DynamicDatapath DynDatapath(kernel_name, trace_file_name, inputPath, loop_name, target_loop_level, unroll_factor, false, IL_asap_rec);
		VERBOSE_PRINT(errs() << "DEBUG-INFO: [trace-analysis_loop-based-trace-analysis] Finished dynamic trace analysis for loop " << loop_name << "\n");
		VERBOSE_PRINT(errs() << "-------------------\n");
	}
	VERBOSE_PRINT(errs() << "DEBUG-INFO: [trace-analysis_loop-based-trace-analysis] Finished\n");
	close_summary_file(summary);
	errs() << "-------------------\n";
#endif
}

void InstrumentForDDDG::openSummaryFile(std::string kernelName) {
	std::string fileName(args.outWorkDir + kernelName + "_summary.log");
	if(summaryFile.is_open())
		summaryFile.close();
	summaryFile.open(fileName);

	assert(summaryFile.is_open() && "Could not open memory trace output file");

	summaryFile << "==========================\n";
	summaryFile << "   Lin-analyzer summary\n";
	summaryFile << "==========================\n";
	summaryFile << "Function name: " << kernelName << "\n";
}

void InstrumentForDDDG::closeSummaryFile() {
	summaryFile.close();
}

ProfilingEngine::ProfilingEngine(Module &M, TraceLogger &TL) : M(M), TL(TL) {
}

void ProfilingEngine::runOnProfiler() {
	VERBOSE_PRINT(errs() << "[][profilingEngine] Profiling engine started\n");

	VERBOSE_PRINT(errs() << "[][profilingEngine] Creating context\n");
	ProfilingJITSingletonContext JSC(this);
	EngineBuilder builder(&M);
	ExecutionEngine *EE = builder.setEngineKind(EngineKind::JIT).create();

	assert(EE && "ExecutionEngine was not constructed");

	VERBOSE_PRINT(errs() << "[][profilingEngine] Mapping trace logger functions\n");
	EE->addGlobalMapping(TL.log0, reinterpret_cast<void *>(trace_logger_log0));
	EE->addGlobalMapping(TL.logInt, reinterpret_cast<void *>(trace_logger_log_int));
	EE->addGlobalMapping(TL.logDouble, reinterpret_cast<void *>(trace_logger_log_double));
	EE->addGlobalMapping(TL.logIntNoReg, reinterpret_cast<void *>(trace_logger_log_int_noreg));
	EE->addGlobalMapping(TL.logDoubleNoReg, reinterpret_cast<void *>(trace_logger_log_double_noreg));

	Function *entryF = M.getFunction("main");

	assert(entryF && "Input code has no main() function, cannot trace");

	VERBOSE_PRINT(errs() << "[][profilingEngine] Executing code\n");
	errs() << "********************************************************\n";
	errs() << "********************************************************\n";
	errs() << "************** See ya in a minute or so! ***************\n";
	errs() << "********************************************************\n";

	EE->runStaticConstructorsDestructors(false);
	EE->getPointerToFunction(entryF);
	std::vector<std::string> argv;
	argv.push_back(M.getModuleIdentifier());
	int retc = EE->runFunctionAsMain(entryF, argv, nullptr);

	errs() << "********************************************************\n";
	errs() << "********************* Hello back! **********************\n";
	errs() << "********************************************************\n";
	errs() << "********************************************************\n";

	assert(0 == retc && "Code executed on profiler returned non-zero");

	VERBOSE_PRINT(errs() << "[][profilingEngine] Code executed. It's good to be back\n");
	VERBOSE_PRINT(errs() << "[][profilingEngine] Performing cleanup\n");

	trace_logger_fin();
	EE->runStaticConstructorsDestructors(true);
}

ProfilingJITContext::ProfilingJITContext() : P(nullptr) {
	InitializeNativeTarget();
	InitializeNativeTargetAsmPrinter();
	InitializeNativeTargetAsmParser();
}


ProfilingJITSingletonContext::ProfilingJITSingletonContext(ProfilingEngine *P) {
	gJITContext->P = P;
}

ProfilingJITSingletonContext::~ProfilingJITSingletonContext() {
	gJITContext->P = nullptr;

	// TODO: Other cleanup?
}

#ifdef DBG_PRINT_ALL
void InstrumentForDDDG::printDatabase(void) {
	errs() << "-- staticInstID2OpcodeMap\n";
	for(auto const &x : staticInstID2OpcodeMap)
		errs() << "-- " << x.first << ": " << x.second << "\n";
	errs() << "-- ----------------------\n";

	errs() << "-- instName2bbNameMap\n";
	for(auto const &x : instName2bbNameMap)
		errs() << "-- " << x.first << ": " << x.second << "\n";
	errs() << "-- ------------------\n";

	errs() << "-- headerBBFuncNamePair2lastInstMap\n";
	for(auto const &x : headerBBFuncNamePair2lastInstMap)
		errs() << "-- <" << x.first.first << ", " << x.first.second << ">: " << x.second << "\n";
	errs() << "-- --------------------------------\n";

	errs() << "-- exitingBBFuncNamePair2lastInstMap\n";
	for(auto const &x : exitingBBFuncNamePair2lastInstMap)
		errs() << "-- <" << x.first.first << ", " << x.first.second << ">: " << x.second << "\n";
	errs() << "-- ---------------------------------\n";
}
#endif

char InstrumentForDDDG::ID = 0;

INITIALIZE_PASS(
	InstrumentForDDDG,
	"instrumentCodeforDDDG",
	"This pass is used to instrument code for building DDDG",
	false,
	true /* false We need to modify the code later. In initial step, we just check the unique ID first*/
)

ModulePass *llvm::createInstrumentForDDDGPass() {
	return new InstrumentForDDDG();
}
