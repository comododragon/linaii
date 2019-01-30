#include "profile_h/InstrumentForDDDGPass.h"

#define DEBUG_TYPE "instrument-code-for-building-dddg"

staticInstID2OpcodeMapTy staticInstID2OpcodeMap;
instName2bbNameMapTy instName2bbNameMap;
headerBBFuncNamePair2lastInstMapTy headerBBFuncNamePair2lastInstMap;
headerBBFuncNamePair2lastInstMapTy exitingBBFuncNamePair2lastInstMap;
loopName2levelUnrollVecMapTy loopName2levelUnrollVecMap;
std::ofstream summary;

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
			NULL
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
			NULL
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
			NULL
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
			NULL
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
			NULL
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
	currFunction = NULL;

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

#if 0
/// Function used to instrument LLVM-IR
void InstrumentForDDDG::printLine(
	BasicBlock::iterator it, int line, int lineNo, std::string funcOrRegID,
	std::string bbID, std::string instID, int opcodeOrType, int dataSize, Value *value, 
	bool isReg
) {
	CallInst *tlCall;
	IRBuilder<> IRB(it);

	// Create LLVM values for the provided arguments
	Value *vLine, *vOpcodeOrType, *vValue, *vLineNo;
	vLine = ConstantInt::get(IRB.getInt64Ty(), line);
	vOpcodeOrType = ConstantInt::get(IRB.getInt64Ty(), opcodeOrType);
	vLineNo = ConstantInt::get(IRB.getInt64Ty(), lineNo);

	// Print line 0
	if(!line) {
		// Create global variables with strings and getelementptr calls
		Constant *vvFuncID = createGlobalVariableAndGetGetElementPtr(funcOrRegID);
		Constant *vvBB = createGlobalVariableAndGetGetElementPtr(bbID);
		Constant *vvInst = createGlobalVariableAndGetGetElementPtr(instID);

		// TODO: entender o que realmente signfica uma call para trace_logger_log0
		// Call trace_logger_log0 with the aforementioned values
		tlCall = IRB.CreateCall5(TL.log0, vLineNo, vvFuncID, vvBB, vvInst, vOpcodeOrType);

		// Update databases
		insertInstID(instID, opcodeOrType);
		insertInstid2bbName(instID, bbID);

		// This instruction is a branch
		if(LLVM_IR_Br == (unsigned) opcodeOrType) {
			bbFuncNamePairTy bbFnName = std::make_pair(bbID, funcOrRegID);
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
	else {
		Value *vSize = ConstantInt::get(IRB.getInt64Ty(), dataSize);
		Value *vIsReg = ConstantInt::get(IRB.getInt64Ty(), isReg);

		if(isReg) {
			Constant *vvRegID = createGlobalVariableAndGetGetElementPtr(funcOrRegID);

			if(value) {
				if(llvm::Type::IntegerTyID == opcodeOrType) {
					vValue = IRB.CreateZExt(value, IRB.getInt64Ty());
					tlCall = IRB.CreateCall5(TL.logInt, vLine, vSize, vValue, vIsReg, vvRegID);
				}
				else if(opcodeOrType >= llvm::Type::HalfTyID && opcodeOrType <= llvm::Type::PPC_FP128TyID) {
					vValue = IRB.CreateFPExt(value, IRB.getDoubleTy());
					tlCall = IRB.CreateCall5(TL.logDouble, vLine, vSize, vValue, vIsReg, vvRegID);
				}
				else if(llvm::Type::PointerTyID == opcodeOrType) {
					vValue = IRB.CreatePtrToInt(value, IRB.getInt64Ty());
					tlCall = IRB.CreateCall5(TL.logInt, vLine, vSize, vValue, vIsReg, vvRegID);
				}
				else {
					// TODO: assert mesmo ou apenas um silent error?
					assert(false && "Invalid type provided for trace generation");
				}
			}
			else {
				vValue = ConstantInt::get(IRB.getInt64Ty(), 0);
				tlCall = IRB.CreateCall5(TL.logInt, vLine, vSize, vValue, vIsReg, vvRegID);
			}
		}
		else {
			if(value) {
				if(llvm::Type::IntegerTyID == opcodeOrType) {
					vValue = IRB.CreateZExt(value, IRB.getInt64Ty());
					tlCall = IRB.CreateCall4(TL.logIntNoReg, vLine, vSize, vValue, vIsReg);
				}
				else if(opcodeOrType >= llvm::Type::HalfTyID && opcodeOrType <= llvm::Type::PPC_FP128TyID) {
					vValue = IRB.CreateFPExt(value, IRB.getDoubleTy());
					tlCall = IRB.CreateCall4(TL.logDoubleNoReg, vLine, vSize, vValue, vIsReg);
				}
				else if(llvm::Type::PointerTyID == opcodeOrType) {
					vValue = IRB.CreatePtrToInt(value, IRB.getInt64Ty());
					tlCall = IRB.CreateCall4(TL.logIntNoReg, vLine, vSize, vValue, vIsReg);
				}
				else {
					// TODO: assert mesmo ou apenas um silent error?
					assert(false && "Invalid type provided for trace generation");
				}
			}
			else {
				vValue = ConstantInt::get(IRB.getInt64Ty(), 0);
				tlCall = IRB.CreateCall4(TL.logIntNoReg, vLine, vSize, vValue, vIsReg);
			}
		}
	}
}

void InstrumentForDDDG::insertInstID(std::string instID, unsigned opcode) {
	staticInstID2OpcodeMap.insert(std::make_pair(instID, opcode));
}

void InstrumentForDDDG::insertInstid2bbName(std::string instID, std::string bbName) {
	instName2bbNameMap.insert(std::make_pair(instID, bbName));
}
#endif

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
		return NULL;
}

void InstrumentForDDDG::extractMemoryTraceForAccessPattern() {
#if 0
	std::string file_name = inputPath + "mem_trace.txt";
	std::ofstream mem_trace;
	mem_trace.open(file_name);
	if (mem_trace.is_open()) {
		std::cout << "DEBUG-INFO: [Mem-trace] Start" << std::endl;
	}
	else {
		assert(false && "Error: Cannot open mem_trace file!\n");
	}

	//FILE *tracefile;
	gzFile tracefile;
	//tracefile = fopen(trace_name.c_str(), "r");
	std::string tracefile_name = inputPath + "dynamic_trace.gz";
	tracefile = gzopen(tracefile_name.c_str(), "r");
	if (tracefile == Z_NULL) {
		std::string err_str = "Error! gzfile " + tracefile_name + " can not open!";
		assert(false && err_str.c_str());
	}

	char buffer[256];
	char curr_static_function[256];
	char instid[256], bblockid[256];
	int microop;
	int line_num;
	int count;
	bool trace_entry = false;
	while (!gzeof(tracefile)) {
		if (gzgets(tracefile, buffer, sizeof(buffer)) == Z_NULL)
			continue;
		std::string wholeline(buffer);
		size_t pos_end_tag = wholeline.find(",");

		if (pos_end_tag == std::string::npos) {
			continue;
		}

		std::string tag = wholeline.substr(0, pos_end_tag);
		std::string line_left = wholeline.substr(pos_end_tag + 1);
		if ((trace_entry == false) && (tag.compare("0") == 0)){
			//parse_instruction_line(line_left);
			sscanf(line_left.c_str(), "%d,%[^,],%[^,],%[^,],%d,%d\n", &line_num, curr_static_function, bblockid, instid, &microop, &count);
			std::string func_name(curr_static_function);
			std::string bb_name(bblockid);
			std::string inst_name(instid);
			// Get loop name
			if (microop == 27 || microop == 28) {
				// Only consider load and store
				trace_entry = true;
				bbFuncNamePair2lpNameLevelPairMapTy::iterator it_found = bbFuncNamePair2lpNameLevelPairMap.find(std::make_pair(bb_name, func_name));
				if (it_found != bbFuncNamePair2lpNameLevelPairMap.end()) {
					llvm::lpNameLevelPairTy lpNamelpLevelPair = it_found->second;
					std::string loop_name = lpNamelpLevelPair.first;
					std::string whole_loop_name = loop_name + "-" + to_string(lpNamelpLevelPair.second);
					unsigned int num_levels = LpName2numLevelMap.at(loop_name);
					mem_trace << whole_loop_name << "," << num_levels << "," << instid << ",";
					if (microop == 27) {
						// Load instruction
						mem_trace << "load,";
					}
					else if (microop == 28) {
						mem_trace << "store,";
					}
					else {
						// Do nothing
					}
					mem_trace << count << ",";
				}
			}

		}
		else if ((trace_entry == true) && (((tag.compare("1") == 0) && microop == 27) || ((tag.compare("2") == 0) && microop == 28))) {
			// Load instruction for obtaining load/store addresses
			unsigned int tmp1_v = 0;
			unsigned int tmp2_v = 0;
			unsigned long int addr_v = 0;
			char predecessor_name[256];
			sscanf(line_left.c_str(), "%d,%ld,%d,%s\n", &tmp1_v, &addr_v, &tmp2_v, predecessor_name);
			mem_trace << addr_v << ",";
		}
		else if ((trace_entry == true) && (((tag.compare("r") == 0) && microop == 27) || ((tag.compare("1") == 0) && microop == 28))) {
			// Load instruction for obtaining load/store values
			unsigned int tmp1_v = 0;
			unsigned int tmp2_v = 0;
			float its_value = 0.0f;
			char its_name[256];
			sscanf(line_left.c_str(), "%d,%f,%d,%s\n", &tmp1_v, &its_value, &tmp2_v, its_name);
			mem_trace << its_value << std::endl;
			trace_entry = false;
		}
		else {
			// Do nothing here
		}

	}

	gzclose(tracefile);
	mem_trace.close();
	std::cout << "DEBUG-INFO: [Mem-trace] Finished" << std::endl;
#endif
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

	// TODO: parei aqui

	if(args.MODE_TRACE_AND_ESTIMATE == args.mode || args.MODE_TRACE_ONLY == args.mode) {
		VERBOSE_PRINT(errs() << "[instrumentForDDDG] Starting profiling engine\n");

		/// Integrate JIT profiling engine and run the embedded profiler
		ProfilingEngine P(M, TL);
		P.runOnProfiler();

		/// Finished Profiling
		VERBOSE_PRINT(errs() << "[instrumentForDDDG] Profiling finished\n");

		if(args.memTrace) {
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
			Value *currOperand = NULL;
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
						IJ.injectTrace(insIt, i + 1, operR, currOperand->getType(), NULL, isReg);
					else
						IJ.injectTrace(insIt, i + 1, operR, I->getType(), NULL, isReg);

					//if(currOperand->getType()->isVectorTy())
					//	printLine(insIt, i + 1, -1, operR, "phi", "", currOperand->getType()->getTypeID(), getMemSize(currOperand->getType()), NULL, isReg);
					//else
					//	printLine(insIt, i + 1, -1, operR, "phi", "", I->getType()->getTypeID(), getMemSize(I->getType()), NULL, isReg);
				}
				// Input to phi is of vector type
				else if(currOperand->getType()->isVectorTy()) {
					IJ.injectTrace(insIt, i + 1, currOperand->getName(), currOperand->getType(), NULL, isReg);
					//printLine(insIt, i + 1, -1, currOperand->getName(), "phi", "", currOperand->getType()->getTypeID(), getMemSize(currOperand->getType()), NULL, isReg);
				}
				// Input to phi is none of the above
				else {
					IJ.injectTrace(insIt, i + 1, currOperand->getName(), currOperand->getType(), currOperand, isReg);
					//printLine(insIt, i + 1, -1, currOperand->getName(), "phi", "", currOperand->getType()->getTypeID(), getMemSize(currOperand->getType()), currOperand, isReg);
				}
			}

			// Inject phi node result trace
			if(!(it->getType()->isVoidTy())) {
				isReg = true;
				if(it->getType()->isVectorTy()) {
					IJ.injectTrace(insIt, RESULT_LINE, instID, it->getType(), NULL, isReg);
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
		Value *currOperand = NULL;
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

				// Input to phi is an instruction
				if(Instruction *I = dyn_cast<Instruction>(currOperand)) {
					int flag = 0;
					isReg = getInstID(I, "", flag, operR);
					assert(0 == flag && "Unnamed instruction with no local slot found");

					if(currOperand->getType()->isVectorTy()) {
						IJ.injectTrace(it, callID + 1, operR, currOperand->getType(), NULL, isReg);
						IJ.injectTrace(it, FORWARD_LINE, currArgName, currOperand->getType(), NULL, true);
					}
					else {
						IJ.injectTrace(it, callID + 1, operR, I->getType(), currOperand, isReg);
						IJ.injectTrace(it, FORWARD_LINE, currArgName, I->getType(), currOperand, true);
					}
				}
				else {
					if(currOperand->getType()->isVectorTy()) {
						IJ.injectTrace(it, callID + 1, currOperand->getName(), currOperand->getType(), NULL, isReg);
						IJ.injectTrace(it, FORWARD_LINE, currArgName, currOperand->getType(), NULL, true);
					}
					else if(currOperand->getType()->isLabelTy()) {
						IJ.injectTrace(it, callID + 1, getBBID(currOperand), currOperand->getType(), NULL, isReg);
						IJ.injectTrace(it, FORWARD_LINE, currArgName, currOperand->getType(), NULL, true);
					}
					else if(2 == currOperand->getValueID()) {
						IJ.injectTrace(it, callID + 1, currOperand->getName(), currOperand->getType(), NULL, isReg);
						IJ.injectTrace(it, FORWARD_LINE, currArgName, currOperand->getType(), NULL, true);
					}
					else {
						IJ.injectTrace(it, callID + 1, currOperand->getName(), currOperand->getType(), currOperand, isReg);
						IJ.injectTrace(it, FORWARD_LINE, currArgName, currOperand->getType(), currOperand, true);
					}
				}

				callID++;
			}
		}
		else {
			IJ.injectTraceHeader(it, lineNumber, funcName, bbID, instID, opcode);

			if(numOfOperands > 0) {
				for(int i = numOfOperands - 1; i >= 0; i--) {
					currOperand = it->getOperand(i);
					isReg = currOperand->hasName();

					// Input to phi is an instruction
					if(Instruction *I = dyn_cast<Instruction>(currOperand)) {
						int flag = 0;
						isReg = getInstID(I, "", flag, operR);
						assert(0 == flag && "Unnamed instruction with no local slot found");

						if(currOperand->getType()->isVectorTy())
							IJ.injectTrace(it, i + 1, operR, currOperand->getType(), NULL, isReg);
						else
							IJ.injectTrace(it, i + 1, operR, I->getType(), currOperand, isReg);
					}
					else {
						if(currOperand->getType()->isVectorTy())
							IJ.injectTrace(it, i + 1, currOperand->getName(), currOperand->getType(), NULL, isReg);
						else if(currOperand->getType()->isLabelTy())
							IJ.injectTrace(it, i + 1, getBBID(currOperand), currOperand->getType(), NULL, isReg);
						else if(2 == currOperand->getValueID())
							IJ.injectTrace(it, i + 1, currOperand->getName(), currOperand->getType(), NULL, isReg);
						else
							IJ.injectTrace(it, i + 1, currOperand->getName(), currOperand->getType(), currOperand, isReg);
					}
				}
			}
		}

		// Inject phi node result trace
		if(!(it->getType()->isVoidTy())) {
			isReg = true;
			if(it->getType()->isVectorTy()) {
				IJ.injectTrace(nextIt, RESULT_LINE, instID, it->getType(), NULL, isReg);
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

void InstrumentForDDDG::removeConfig(std::string kernelName, std::string inputPath) {
#if 0
	//std::string input_kernel = input_path + kernel_name;
	std::string input_kernel = outputPath + kernel_name;
	std::string pipelining(input_kernel + "_pipelining_config");
	std::string unrolling(input_kernel + "_unrolling_config");
	std::string array_info(input_kernel + "_array_info");
	std::string partition(input_kernel + "_partition_config");
	std::string comp_partition(input_kernel + "_complete_partition_config");

	ifstream pipe(pipelining);
	if (pipe.is_open()) {
		pipe.close();
		if ( remove(pipelining.c_str()) != 0) {
			assert(false && "Error: deleting loop pipelining configuration file!\n");
		}
	}

	ifstream unroll(unrolling);
	if (unroll.is_open()) {
		unroll.close();
		if (remove(unrolling.c_str()) != 0) {
			assert(false && "Error: deleting loop unrolling configuration file!\n");
		}
	}

	ifstream arrayInfo(array_info);
	if (arrayInfo.is_open()) {
		arrayInfo.close();
		if (remove(array_info.c_str()) != 0) {
			assert(false && "Error: deleting array information file!\n");
		}
	}

	ifstream part(partition);
	if (part.is_open()) {
		part.close();
		if (remove(partition.c_str()) != 0) {
			assert(false && "Error: deleting array partitioning configuration file!\n");
		}
	}

	ifstream comp_part(comp_partition);
	if (comp_part.is_open()) {
		comp_part.close();
		if (remove(comp_partition.c_str()) != 0) {
			assert(false && "Error: deleting completely array partitioning configuration file!\n");
		}
	}
#endif
}

void InstrumentForDDDG::parseConfig(std::string kernelName, std::string inputPath) {
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

void InstrumentForDDDG::openSummaryFile(std::ofstream &summaryFile, std::string kernelKame) {
#if 0
	errs() << "DEBUG-INFO: [Lin-Analyzer summary] Writing summary into log file\n";
	//std::string file_name(inputPath+kernel_name+"_summary.log");
	std::string file_name(outputPath + kernel_name + "_summary.log");
	summary_file.open(file_name);
	if (summary_file.is_open()) {
		summary_file << "==========================" << std::endl;
		summary_file << "   Lin-analyzer summary" << std::endl;
		summary_file << "==========================" << std::endl;
		summary_file << "function name: " << kernel_name << std::endl;
	}
	else {
		assert(false && "Error: Cannot open summary file!\n");
	}
#endif
}

void InstrumentForDDDG::closeSummaryFile(std::ofstream &summaryFile) {
#if 0
	summary_file.close();
	VERBOSE_PRINT(errs() << "DEBUG-INFO: [Lin-Analyzer summary] Summary file generated\n");
#endif
}

ProfilingEngine::ProfilingEngine(Module &M, TraceLogger &TL) : M(M), TL(TL) {
}

void ProfilingEngine::runOnProfiler() {
#if 0
	errs() << "DEBUG-INFO: [profiling_profiling-engine] Running on Profiler\n";
	ProfilingJITSingletonContext JTSC(this);
	//std::unique_ptr<Module> M(generateInstrumentedModule());
	//ValueToValueMapTy V2VMap;
	//Module* M = CloneModule(&Mod, V2VMap);
	//std::unique_ptr<Module> M(newM);
	Module *M = (&Mod);

	// TODO: Insert instrumental code to extract the trace.
	EngineBuilder builder(M);
	ExecutionEngine *EE = builder.setEngineKind(EngineKind::JIT).create();
	// Where is getBBFreqInc()?
	//EE->addGlobalMapping(getBBFreqInc(), reinterpret_cast<void*>(IncreaseBBCounter));
	EE->addGlobalMapping(log0_Fn, reinterpret_cast<void*>(trace_logger_log0));
	EE->addGlobalMapping(log_int_Fn, reinterpret_cast<void*>(trace_logger_log_int));
	EE->addGlobalMapping(log_double_Fn, reinterpret_cast<void*>(trace_logger_log_double));
	EE->addGlobalMapping(log_int_noreg_Fn, reinterpret_cast<void*>(trace_logger_log_int_noreg));
	EE->addGlobalMapping(log_double_noreg_Fn, reinterpret_cast<void*>(trace_logger_log_double_noreg));

	if (!EE) {
		assert(false && "Error: Failed to construct ExecutionEngine\n");
	}

	Function *EntryFn = M->getFunction("main");

	// Nothing we can do if we cannot find the entry function of the module.
	if (EntryFn == nullptr){
		assert(false && "Error: EntryFn is equal to nullptr!\n");
	}


	// Here we go. Run the module.
	EE->runStaticConstructorsDestructors(false);

	// Trigger compilation separately so code regions that need to be
	// invalidated will be known.
	(void)EE->getPointerToFunction(EntryFn);

	// TODO: Accept argv from user script?
	// FIXME: Support passing arguments
	std::vector<std::string> Argv;
	Argv.push_back(M->getModuleIdentifier());
	Argv.push_back(inputPath);

	// Run main.
	VERBOSE_PRINT(errs() << "DEBUG-INFO: [profiling_profiling-engine] Running main()\n");
	int Result = EE->runFunctionAsMain(EntryFn, Argv, nullptr);

	if (Result != 0) {
		errs() << "DEBUG-INFO: [profiling_profiling-engine] Module return nonzero result during trace generation: "<< Result << '\n';
		assert(false && "Error: Trace generation failed!\n");
	}
	
	trace_logger_fin();

	// Run static destructors.
	EE->runStaticConstructorsDestructors(true);

	//VerifyProfile();

	//delete M
	errs() << "DEBUG-INFO: [profiling_profiling-engine] Finished profiling: Status = " << Result << "\n";
#endif
}

ProfilingJITContext::ProfilingJITContext() : P(nullptr) {
#if 0
	// If we have a native target, initialize it to ensure it is linked in and
	// usable by the JIT.
	InitializeNativeTarget();
	InitializeNativeTargetAsmPrinter();
	InitializeNativeTargetAsmParser();
#endif
}


ProfilingJITSingletonContext::ProfilingJITSingletonContext(ProfilingEngine *P) {
#if 0
	GlobalContextDDDG->P = P;
#endif
}

ProfilingJITSingletonContext::~ProfilingJITSingletonContext() {
#if 0
	GlobalContextDDDG->P = nullptr;
	//TODO: Other clearup?
#endif
}

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
