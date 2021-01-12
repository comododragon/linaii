#include "profile_h/InstrumentForDDDGPass.h"

#define DEBUG_TYPE "instrument-code-for-building-dddg"

#ifdef FUTURE_CACHE
FutureCache futureCache;
#endif
staticInstID2OpcodeMapTy staticInstID2OpcodeMap;
instName2bbNameMapTy instName2bbNameMap;
headerBBFuncNamePair2lastInstMapTy headerBBFuncNamePair2lastInstMap;
headerBBFuncNamePair2lastInstMapTy exitingBBFuncNamePair2lastInstMap;
loopName2levelUnrollVecMapTy loopName2levelUnrollVecMap;
memoryTraceMapTy memoryTraceMap;
bool memoryTraceGenerated = false;

using namespace llvm;

static ManagedStatic<ProfilingJITContext> gJITContext;

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

int64_t Injector::getMemSizeInBits(Type *T) {
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

		for(unsigned i = 0; i < S->getNumElements(); i++)
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
		return (int64_t) A->getNumElements() * A->getElementType()->getPrimitiveSizeInBits();
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
	IRBuilder<> IRB(it);

	// Create LLVM values for the provided arguments
	Value *vLineNo = ConstantInt::get(IRB.getInt64Ty(), lineNo);
	Value *vOpcode = ConstantInt::get(IRB.getInt64Ty(), opcode);

	// Create global variables with strings and getelementptr calls
	Constant *vvFuncID = createGlobalVariableAndGetGetElementPtr(funcID);
	Constant *vvBB = createGlobalVariableAndGetGetElementPtr(bbID);
	Constant *vvInst = createGlobalVariableAndGetGetElementPtr(instID);

	// Call trace_logger_log0 with the aforementioned values
	IRB.CreateCall5(TL->log0, vLineNo, vvFuncID, vvBB, vvInst, vOpcode);

	// Update databases
	staticInstID2OpcodeMap.insert(std::make_pair(instID, opcode));
	instName2bbNameMap.insert(std::make_pair(instID, bbID));

	// This instruction is a branch. If this is a loop header BB or exiting BB, save its ID as the last inst of this BB
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
	IRBuilder<> IRB(it);
	int type = T->getTypeID();

	// Create LLVM values for the provided arguments
	Value *vLineNo = ConstantInt::get(IRB.getInt64Ty(), lineNo);
	Value *vDataSize = ConstantInt::get(IRB.getInt64Ty(), getMemSizeInBits(T));
	Value *vIsReg = ConstantInt::get(IRB.getInt64Ty(), isReg);
	Value *vValue;

	if(isReg) {
		Constant *vvRegOrFuncID = createGlobalVariableAndGetGetElementPtr(regOrFuncID);

		if(value) {
			if(llvm::Type::IntegerTyID == type) {
				vValue = IRB.CreateZExt(value, IRB.getInt64Ty());
				IRB.CreateCall5(TL->logInt, vLineNo, vDataSize, vValue, vIsReg, vvRegOrFuncID);
			}
			else if(type >= llvm::Type::HalfTyID && type <= llvm::Type::PPC_FP128TyID) {
				vValue = IRB.CreateFPExt(value, IRB.getDoubleTy());
				IRB.CreateCall5(TL->logDouble, vLineNo, vDataSize, vValue, vIsReg, vvRegOrFuncID);
			}
			else if(llvm::Type::PointerTyID == type) {
				vValue = IRB.CreatePtrToInt(value, IRB.getInt64Ty());
				IRB.CreateCall5(TL->logInt, vLineNo, vDataSize, vValue, vIsReg, vvRegOrFuncID);
			}
			else {
				assert(false && "Value is of unsupported type");
			}
		}
		else {
			vValue = ConstantInt::get(IRB.getInt64Ty(), 0);
			IRB.CreateCall5(TL->logInt, vLineNo, vDataSize, vValue, vIsReg, vvRegOrFuncID);
		}
	}
	else {
		if(value) {
			if(llvm::Type::IntegerTyID == type) {
				vValue = IRB.CreateZExt(value, IRB.getInt64Ty());
				IRB.CreateCall4(TL->logIntNoReg, vLineNo, vDataSize, vValue, vIsReg);
			}
			else if(type >= llvm::Type::HalfTyID && type <= llvm::Type::PPC_FP128TyID) {
				vValue = IRB.CreateFPExt(value, IRB.getDoubleTy());
				IRB.CreateCall4(TL->logDoubleNoReg, vLineNo, vDataSize, vValue, vIsReg);
			}
			else if(llvm::Type::PointerTyID == type) {
				vValue = IRB.CreatePtrToInt(value, IRB.getInt64Ty());
				IRB.CreateCall4(TL->logIntNoReg, vLineNo, vDataSize, vValue, vIsReg);
			}
			else {
				assert(false && "Value is of unsupported type");
			}
		}
		else {
			vValue = ConstantInt::get(IRB.getInt64Ty(), 0);
			IRB.CreateCall4(TL->logIntNoReg, vLineNo, vDataSize, vValue, vIsReg);
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

#ifdef CUSTOM_OPS
	std::unordered_map<std::string, unsigned>::const_iterator found = customOpsMap.find(call);
	if(found != customOpsMap.end())
		return TRACE_CUSTOM_OP;
#endif

	return NOT_TRACE;
}

bool InstrumentForDDDG::getInstID(Instruction *I, std::string bbID, unsigned &instCnt, std::string &instID) {
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
#ifdef LEGACY_SEPARATOR
		instID = bbID + "-" + std::to_string(instCnt++);
#else
		instID = bbID + GLOBAL_SEPARATOR + std::to_string(instCnt++);
#endif
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

	std::string fileName = args.workDir + FILE_MEM_TRACE;
	std::string traceFileName = args.workDir + FILE_DYNAMIC_TRACE;
	std::ofstream memTraceFile;
	gzFile traceFile;
	bool traceEntry = false;
	int opcode = -1;

	memTraceFile.open(fileName);
	assert(memTraceFile.is_open() && "Could not open memory trace output file");

	traceFile = gzopen(traceFileName.c_str(), "r");
	assert(traceFile != Z_NULL && "Could not open trace input file");

	std::pair<std::string, std::string> wholeLoopNameInstNamePair;

	// Walk through the dynamic trace
	while(!gzeof(traceFile)) {
		char buffer[BUFF_STR_SZ];
		if(Z_NULL == gzgets(traceFile, buffer, sizeof(buffer)))
			continue;

		std::string line(buffer);
		size_t tagPos = line.find(",");

		if(std::string::npos == tagPos)
			continue;

		std::string tag = line.substr(0, tagPos);
		std::string rest = line.substr(tagPos + 1);

		// If no load or store is in process and this is a log0 line
		if(!traceEntry && !(tag.compare("0"))) {
			int lineNo;
			char buffer1[BUFF_STR_SZ];
			char buffer2[BUFF_STR_SZ];
			char buffer3[BUFF_STR_SZ];
			int count;
			sscanf(rest.c_str(), "%d,%[^,],%[^,],%[^,],%d,%d\n", &lineNo, buffer1, buffer2, buffer3, &opcode, &count);
			std::string funcName(buffer1);
			std::string bbName(buffer2);
			std::string instName(buffer3);

			// Trace is a load or store
			if(isStoreOp(opcode) || isLoadOp(opcode)) {
				traceEntry = true;

				// Load or store is inside a known loop, print this information
				bbFuncNamePair2lpNameLevelPairMapTy::iterator it = bbFuncNamePair2lpNameLevelPairMap.find(std::make_pair(bbName, funcName));
				if(it != bbFuncNamePair2lpNameLevelPairMap.end()) {
					lpNameLevelPairTy lpNameLevelPair = it->second;
					std::string loopName = lpNameLevelPair.first;
					std::string wholeLoopName = appendDepthToLoopName(loopName, lpNameLevelPair.second);
					unsigned numLevels = LpName2numLevelMap.at(loopName);

					memTraceFile << wholeLoopName << "," << numLevels << "," << instName << ",";

					wholeLoopNameInstNamePair = std::make_pair(wholeLoopName, instName);

					if(isLoadOp(opcode))
						memTraceFile << "load,";
					else if(isStoreOp(opcode))
						memTraceFile << "store,";

					memTraceFile << count << ",";
				}
			}
		}
		// Print operands of this load/store
		else if(traceEntry && ((!(tag.compare("1")) && isLoadOp(opcode)) || (!(tag.compare("2")) && isStoreOp(opcode)))) {
			uint64_t addr;

			sscanf(rest.c_str(), "%*d,%lu,%*d,%*s\n", &addr);
			memTraceFile << addr << ",";

			memoryTraceMap[wholeLoopNameInstNamePair].push_back(addr);
		}
		// Print result value of this load/store
		else if(traceEntry && ((!(tag.compare("r")) && isLoadOp(opcode)) || (!(tag.compare("1")) && isStoreOp(opcode)))) {
			float value;

			sscanf(rest.c_str(), "%*d,%f,%*d,%*s\n", &value);
			memTraceFile << value << "\n";
			traceEntry = false;
		}
	}

	gzclose(traceFile);
	memTraceFile.close();

	memoryTraceGenerated = true;

	VERBOSE_PRINT(errs() << "[][memoryTrace] Memory trace finished\n");
}

bool InstrumentForDDDG::runOnModule(Module &M) {
	errs() << "========================================================\n";
	errs() << "Starting code instrumentation for DDDG generation\n";

	bool result = false;
	for(Module::iterator FI = M.begin(); FI != M.end(); FI++) {
		if(isFunctionOfInterest(FI->getName())) {
			VERBOSE_PRINT(errs() << "[instrumentForDDDG] Injecting trace code in \"" + demangleFunctionName(FI->getName()) + "\"\n");
			for(Function::iterator BI = FI->begin(); BI != FI->end(); BI++)
				result += performOnBasicBlock(*BI);
		}
	}

	if((args.MODE_TRACE_AND_ESTIMATE == args.mode || args.MODE_TRACE_ONLY == args.mode) && (args.fNoMMA || args.mmaMode != ArgPack::MMA_MODE_USE)) {
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

	// Perform the cycle estimation
	loopBasedTraceAnalysis();

	VERBOSE_PRINT(errs() << "[instrumentForDDDG] Finished\n");

#ifdef DBG_PRINT_ALL
	printDatabase();
#endif

	return result;
}

bool InstrumentForDDDG::performOnBasicBlock(BasicBlock &BB) {
	Function *F = BB.getParent();
	unsigned instCnt = 0;
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
		for(; dyn_cast<PHINode>(it); it++) {
			Value *currOperand = nullptr;
			bool isReg = false;
			int opcode;
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

			// Create calls to trace logger informing the inputs for phi
			int numOfOperands = it->getNumOperands();
			for(int i = numOfOperands - 1; i >= 0; i--) {
				currOperand = it->getOperand(i);
				isReg = currOperand->hasName();

				// Input to phi is an instruction
				if(Instruction *I = dyn_cast<Instruction>(currOperand)) {
					unsigned flag = 0;
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
					// XXX: put a silent warning or fail assert?
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
		int opcode;
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

#ifdef CUSTOM_OPS
			if(TRACE_CUSTOM_OP == stRes)
				opcode = customOpsMap.at(I->getCalledFunction()->getName());
#endif
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
			int callID = 0;

			for(Function::ArgumentListType::const_iterator argIt = AL.begin(); argIt != AL.end(); argIt++) {
				std::string currArgName = argIt->getName();

				currOperand = it->getOperand(callID);
				isReg = currOperand->hasName();

				// Argument to call is an instruction
				if(Instruction *I = dyn_cast<Instruction>(currOperand)) {
					unsigned flag = 0;
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

#ifdef CUSTOM_OPS
					// If this operand is a custom op call, we have different handling
					if(TRACE_CUSTOM_OP == stRes) {
						// The last argument is the function called, not interesting for us
						if(numOfOperands - 1 == i)
							continue;
					}
#endif

					// Input is an instruction
					if(Instruction *I = dyn_cast<Instruction>(currOperand)) {
						unsigned flag = 0;
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
				// XXX: put a silent warning or fail assert?
			}
			else {
				IJ.injectTrace(nextIt, RESULT_LINE, instID, it->getType(), it, isReg);
			}
		}
	}

	return true;
}

void InstrumentForDDDG::updateUnrollingDatabase(const std::vector<ConfigurationManager::unrollingCfgTy> &unrollingCfg) {
	loopName2levelUnrollVecMap.clear();

	// Check how many levels inside a loop was asked to be unrolled, and also initialise unrolling factors vector
	for(auto &it : lpNameLevelPair2headBBnameMap) {
		std::string loopName = it.first.first;

		loopName2levelUnrollVecMapTy::iterator found = loopName2levelUnrollVecMap.find(loopName);
		if(found != loopName2levelUnrollVecMap.end()) {
			found->second.push_back(1);
		}
		else {
			std::vector<unsigned> levelUnrollPairVec;
			levelUnrollPairVec.push_back(1);
			loopName2levelUnrollVecMap.insert(std::make_pair(loopName, levelUnrollPairVec));
		}
	}

	// Update unroll factors
	for(const ConfigurationManager::unrollingCfgTy &cfg : unrollingCfg) {
		assert(cfg.loopLevel > 0 && "Invalid loop level passed, it must be > 0");

		std::string loopName = constructLoopName(cfg.funcName, cfg.loopNo);
		loopName2levelUnrollVecMapTy::iterator found2 = loopName2levelUnrollVecMap.find(loopName);

		assert(found2 != loopName2levelUnrollVecMap.end() && "Loop was not found inside internal databases, please check configuration file");

		unsigned innermostLevel = found2->second.size();

		// Not the innermost level AND partial unroll requested
		if(cfg.loopLevel != innermostLevel && cfg.unrollFactor > 1) {
			assert(cfg.loopLevel <= innermostLevel && "Loop level is larger than number of levels");
			// XXX: Shouldn't we also check here if the factor is bigger than the loop bound if it is known?
			found2->second[cfg.loopLevel - 1] = cfg.unrollFactor;
		}
		// Innermost level OR full unroll requested
		else {
			std::string wholeLoopName = appendDepthToLoopName(loopName, cfg.loopLevel);
			uint64_t loopBound = wholeloopName2loopBoundMap.at(wholeLoopName);
			found2->second.at(cfg.loopLevel - 1) = (loopBound && cfg.unrollFactor > loopBound)? loopBound : cfg.unrollFactor;
		}
	}
}

void InstrumentForDDDG::loopBasedTraceAnalysis() {
	VERBOSE_PRINT(errs() << "[][loopBasedTraceAnalysis] Loop-based trace analysis started\n");

	std::string traceFileName = args.workDir + FILE_DYNAMIC_TRACE;
	std::string kernelName = mangleFunctionName(args.kernelNames.at(0));

	VERBOSE_PRINT(errs() << "[][loopBasedTraceAnalysis] Writing header of summary file\n");
	openSummaryFile(kernelName); 

	VERBOSE_PRINT(errs() << "[][loopBasedTraceAnalysis] Parsing configuration file\n");
	ConfigurationManager CM(kernelName);
	CM.parseAndPopulate(pipelineLoopLevelVec);
	updateUnrollingDatabase(CM.getUnrollingCfg());

#ifdef FUTURE_CACHE
	if(args.futureCache) {
		VERBOSE_PRINT(errs() << "[][loopBasedTraceAnalysis] Use of future cache enabled\n");

		if(futureCache.load())
			VERBOSE_PRINT(errs() << "[][loopBasedTraceAnalysis] Future cache file found.\n");
		else
			VERBOSE_PRINT(errs() << "[][loopBasedTraceAnalysis] Future cache file not found or is corrupt. Starting from scratch\n");
	}
#endif

	ContextManager CtxM;
	if(!(args.fNoMMA)) {
		if(ArgPack::MMA_MODE_GEN == args.mmaMode) {
			CtxM.openForWrite();
		}
		else if(ArgPack::MMA_MODE_USE == args.mmaMode) {
			CtxM.openForRead();

			VERBOSE_PRINT(errs() << "[][loopBasedTraceAnalysis] Recovering context from previous execution\n");
			CtxM.getLoopBoundInfo(&wholeloopName2loopBoundMap);
			CtxM.getGlobalDDRMap(&globalDDRMap);
			CtxM.getGlobalOutBurstsInfo(&globalOutBurstsInfo);
			if(args.fVec) {
				assert(1 == loopName2levelUnrollVecMap.size() && "Vectorisation analysis is only possible when only one loop nest is explored (run with \"--fno-vec\") or select only one loop for analysis");
				CtxM.getGlobalPackInfo(&globalPackInfo);
			}
		}
	}

	for(auto &it : loopName2levelUnrollVecMap) {
		std::string loopName = it.first;
		std::string loopIndex = std::to_string(std::get<1>(parseLoopName(loopName)));

		// Skip loop if it is not of interest
		std::vector<std::string>::iterator found = std::find(args.targetLoops.begin(), args.targetLoops.end(), loopIndex);
		if(args.targetLoops.end() == found)
			continue;

		std::vector<unsigned> &levelUnrollVec = it.second;
		int targetLoopLevel = 1;
		unsigned targetUnrollFactor = 1;
		unsigned targetLoopBound;
		std::string targetWholeLoopName = appendDepthToLoopName(loopName, targetLoopLevel);
		bool enablePipelining = false;

		// Acquire target unroll factors, loop bound and pipelining flag
		for(int i = (int) (levelUnrollVec.size() - 1); i >= 0 && 1 == targetLoopLevel; i--) {
			// This value is always > 0
			targetUnrollFactor = levelUnrollVec.at(i);
			std::string wholeLoopName = appendDepthToLoopName(loopName, i + 1);

			wholeloopName2loopBoundMapTy::iterator found2 = wholeloopName2loopBoundMap.find(wholeLoopName);
			assert(found2 != wholeloopName2loopBoundMap.end() && "Could not find loop in wholeloopName2loopBoundMap");
			targetLoopBound = found2->second;

			/**
			 * If loop bound is not known statically (0 == targetLoopBound), this "if" always executes (targetUnrollFactor is always > 0)
			 * If loop bound is known (targetLoopBound > 1), this "if" will execute in the following situations:
			 * - No unroll was specified
			 * - Unroll was specified, but the unroll factor is different from the loop bound
			 * The first execution of this "if" breaks the "for" loop.
			 */
			if(targetUnrollFactor != targetLoopBound) {
				targetLoopLevel = i + 1;
				targetWholeLoopName = wholeLoopName;
			}

			std::vector<std::string>::iterator found3 = std::find(pipelineLoopLevelVec.begin(), pipelineLoopLevelVec.end(), wholeLoopName);
			enablePipelining = found3 != pipelineLoopLevelVec.end();
		}

		unsigned firstNonPerfectLoopLevel = 1;

		VERBOSE_PRINT(errs() << "[][loopBasedTraceAnalysis] Target loop: " << targetWholeLoopName << "\n");
		VERBOSE_PRINT(errs() << "[][][" << targetWholeLoopName << "] Target unroll factor: " << targetUnrollFactor << "\n");
		VERBOSE_PRINT(errs() << "[][][" << targetWholeLoopName << "] Target loop bound: " << targetLoopBound << "\n");
		VERBOSE_PRINT(errs() << "[][][" << targetWholeLoopName << "] Pipelining: " << (enablePipelining? "enabled" : "disabled") << "\n");

		unsigned unrollFactor = (targetLoopBound < targetUnrollFactor && targetLoopBound)? targetLoopBound : targetUnrollFactor;

		// There used to be logic to control NPLA here, but for now it is always active as long --f-npla is set
		if(args.fNPLA) {
			VERBOSE_PRINT(errs() << "[][][" << targetWholeLoopName << "] Non-perfect loop analysis triggered: building multipaths\n");

			if(enablePipelining) {
				unsigned actualUnrollFactor = (targetLoopBound < (targetUnrollFactor << 1) && targetLoopBound)? targetLoopBound : (targetUnrollFactor << 1);

				Multipath MD(kernelName, CM, CtxM, &summaryFile, loopName, targetLoopLevel, firstNonPerfectLoopLevel, unrollFactor, levelUnrollVec, actualUnrollFactor);
				if(args.fNoMMA || ArgPack::MMA_MODE_GEN != args.mmaMode)
					errs() << "[][][" << targetWholeLoopName << "] Estimated cycles: " << std::to_string(MD.getCycles()) << "\n";
			}
			else {
				Multipath MD(kernelName, CM, CtxM, &summaryFile, loopName, targetLoopLevel, firstNonPerfectLoopLevel, unrollFactor, levelUnrollVec);
				if(args.fNoMMA || ArgPack::MMA_MODE_GEN != args.mmaMode)
					errs() << "[][][" << targetWholeLoopName << "] Estimated cycles: " << std::to_string(MD.getCycles()) << "\n";
			}
		}
		else {
			unsigned recII = 0;

			// Get recurrence-constrained II
			if(enablePipelining) {
				VERBOSE_PRINT(errs() << "[][][" << targetWholeLoopName << "] Building dynamic datapath for recurrence-constrained II calculation\n");

				unsigned actualUnrollFactor = (targetLoopBound < (targetUnrollFactor << 1) && targetLoopBound)? targetLoopBound : (targetUnrollFactor << 1);
				DynamicDatapath DD(kernelName, CM, CtxM, &summaryFile, loopName, targetLoopLevel, actualUnrollFactor);
				recII = DD.getASAPII();

				if(args.fNoMMA || ArgPack::MMA_MODE_GEN != args.mmaMode)
					VERBOSE_PRINT(errs() << "[][][" << targetWholeLoopName << "] Recurrence-constrained II: " << recII << "\n");
			}

			VERBOSE_PRINT(errs() << "[][][" << targetWholeLoopName << "] Building dynamic datapath\n");
			DynamicDatapath DD(kernelName, CM, CtxM, &summaryFile, loopName, targetLoopLevel, unrollFactor, enablePipelining, recII);

			if(args.fNoMMA || ArgPack::MMA_MODE_GEN != args.mmaMode)
				errs() << "[][][" << targetWholeLoopName << "] Estimated cycles: " << std::to_string(DD.getCycles()) << "\n";
		}
	}

	if(!(args.fNoMMA)) {
		if(ArgPack::MMA_MODE_GEN == args.mmaMode) {
			VERBOSE_PRINT(errs() << "[][loopBasedTraceAnalysis] Saving context for later use\n");
			CtxM.saveLoopBoundInfo(wholeloopName2loopBoundMap);
			CtxM.saveGlobalDDRMap(globalDDRMap);
			CtxM.saveGlobalOutBurstsInfo(globalOutBurstsInfo);
			if(args.fVec)
				CtxM.saveGlobalPackInfo(globalPackInfo);
		}

		CtxM.close();
	}

#ifdef FUTURE_CACHE
	if(args.futureCache) {
		futureCache.dumpSummary(&summaryFile);

		VERBOSE_PRINT(errs() << "[][loopBasedTraceAnalysis] Saving future cache\n");
		futureCache.save();
	}
#endif

	closeSummaryFile();
	VERBOSE_PRINT(errs() << "[][loopBasedTraceAnalysis] Summary file closed\n");

	VERBOSE_PRINT(errs() << "[][loopBasedTraceAnalysis] Finished\n");

#ifdef DBG_PRINT_ALL
	CM.parseToFiles();
#endif
}

void InstrumentForDDDG::openSummaryFile(std::string kernelName) {
	std::string fileName(args.outWorkDir + demangleFunctionName(kernelName) + FILE_SUMMARY_SUFFIX);
	if(summaryFile.is_open())
		summaryFile.close();
	summaryFile.open(fileName);

	assert(summaryFile.is_open() && "Could not open summary output file");

	summaryFile << "=======================================================================\n";
	summaryFile << "Lina summary\n";
	summaryFile << "=======================================================================\n";
	summaryFile << "Function name: " << demangleFunctionName(kernelName) << "\n";
}

void InstrumentForDDDG::closeSummaryFile() {
	summaryFile << "=======================================================================\n";
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

	// XXX: Other cleanup?
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

	errs() << "-- pipelineLoopLevelVec\n";
	for(auto const &x : pipelineLoopLevelVec)
		errs() << "-- " << x << "\n";
	errs() << "-- --------------------\n";

	errs() << "-- loopName2levelUnrollVecMap\n";
	for(auto const &x : loopName2levelUnrollVecMap) {
		errs() << "-- " << x.first << ": (";
		if(x.second.size() > 0)
			errs() << x.second[0];
		bool skipFirst = true;
		for(auto const &y : x.second) {
			if(skipFirst)
				skipFirst = false;
			else
				errs() << ", " << y;
		}
		errs() << ")\n";
	}
	errs() << "-- --------------------------\n";
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
