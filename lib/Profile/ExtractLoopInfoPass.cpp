/// Format of the NamedMDNode:
/// !lia.kernelloopinfo = !{!n, !n+1, ...}
/// ...
/// !n = metadata !{metadata !"kernel1", metadata !"kernel1_loop2_2", i32 2, i32 0, i32 12, metadata !"kernel1_loop1_1", i32 1, i32 0, i32 10, ...}
/// !n+1 = metadata !{metadata !"kernel2", ...}
/// ...

/// Structure of n-th metadata:
/// First element: kernel name
/// All other elements: several tuples in the following form:
///     First tuple element: auto-generated loop label in the form of "<KERNELNAME><LOOPID>_<LOOPDEPTH>"
///     Second tuple element: loop depth
///     Third tuple element: loop bound if applicable (or 0 otherwise)
///     Fourth tuple element: basic block ID of loop header

#include "profile_h/ExtractLoopInfoPass.h"
#include "profile_h/auxiliary.h"

#define DEBUG_TYPE "extract-loopinfo"

namespace llvm {

fnlpNamePair2BinaryOpBBidMapTy fnlpNamePair2BinaryOpBBidMap;
funcName2loopNumMapTy funcName2loopNumMap;
BB2loopNameMapTy BB2loopNameMap;
#ifdef BUILD_DDDG_H
bbFuncNamePair2lpNameLevelPairMapTy bbFuncNamePair2lpNameLevelPairMap;
bbFuncNamePair2lpNameLevelPairMapTy headerBBFuncnamePair2lpNameLevelPairMap;
bbFuncNamePair2lpNameLevelPairMapTy exitBBFuncnamePair2lpNameLevelPairMap;
#endif // End of BUILD_DDDG_H
LpName2numLevelMapTy LpName2numLevelMap;

}

lpNameLevelPair2headBBnameMapTy lpNameLevelPair2headBBnameMap;
lpNameLevelPair2headBBnameMapTy lpNameLevelPair2exitingBBnameMap;
wholeloopName2loopBoundMapTy wholeloopName2loopBoundMap;
wholeloopName2perfectOrNotMapTy wholeloopName2perfectOrNotMap;

using namespace llvm;

ExtractLoopInfo::ExtractLoopInfo() : LoopPass(ID) {
	DEBUG(dbgs() << "Initialize ExtractLoopInfo pass\n");
	firstRun = true;
	loopID = 0;
	depth = 0;
	depthRecord = 0;
	countNumLoopInAFunc = 0;
	alreadyCheck = false;
	LoopsMetadataNode.clear();

	initializeExtractLoopInfoPass(*PassRegistry::getPassRegistry());
}

bool ExtractLoopInfo::doInitialization(Loop *L, LPPassManager &LPM) {
	Function *F = L->getHeader()->getParent();
	// We do not care of uninteresting functions
	if(!isFunctionOfInterest(F->getName()))
		return false;

	Module *M = L->getHeader()->getParent()->getParent();
	if(!M->getNamedMetadata(extractLoopInfoMDKindName))
		NMD = M->getOrInsertNamedMetadata(extractLoopInfoMDKindName);

	if(!alreadyCheck) {
		unsigned lineID = 0;
		unsigned loadStoreID = 0;
		Module::iterator FI, FE;

		/// Generate lineID and loadStoreID
		for(FI = M->begin(), FE = M->end(); FI != FE; ++FI) {
			if(FI->isDeclaration())
				continue;

			// Ignore uninteresting functions
			if(!isFunctionOfInterest(FI->getName()))
				continue;

			funcName2loopNumMap.insert(std::make_pair(FI->getName(), 0));
			// Check Load/Store Instruction's ID
			for(inst_iterator I = inst_begin(FI), E = inst_end(FI); I != E; ++I) {
				Instruction *inst = &*I;
				if(isa<LoadInst>(inst) || isa<StoreInst>(inst)) {
					LoadStoreIDMap.insert(std::make_pair(inst, std::make_pair(lineID, loadStoreID)));
					LineID2LSMap.insert(std::make_pair(lineID, inst));
					loadStoreID++;
				}

				lineID++;
			}
		}

		/// Check the ids against the annotations performed by AssignLoadStoreIDPass
		if(M->getNamedMetadata(assignLoadStoreIDMDKindName)) {
			recordLSMDNode = M->getNamedMetadata(assignLoadStoreIDMDKindName);
			unsigned sizeLSMDNode = recordLSMDNode->getNumOperands();
			assert(sizeLSMDNode == LoadStoreIDMap.size() && "IR has changed, Load/Store lineID mismatch!\n");

			for(unsigned i = 0; i < sizeLSMDNode; i++) {
				MDNode* tempMDNode = recordLSMDNode->getOperand(i);
				ConstantInt *lineIDPtr = dyn_cast<ConstantInt>(tempMDNode->getOperand(1));
				ConstantInt *loadStoreIDPtr = dyn_cast<ConstantInt>(tempMDNode->getOperand(2));
				uint64_t lineID2 = lineIDPtr->getZExtValue();
				uint64_t loadStoreID2 = loadStoreIDPtr->getZExtValue();
				Instruction *Inst = LineID2LSMap.at(lineID2);
				if(loadStoreID2 != LoadStoreIDMap.at(Inst).second)
					assert(sizeLSMDNode == LoadStoreIDMap.size() && "IR has changed, Load/Store ID mismatch!\n");
			}
		}

#ifdef DBG_PRINT_ALL
		printLineID2LSMapDatabase();
#endif
		// clear LineID2LSMap
		LineID2LSMap.clear();
		/// End checking

		/// Generate ids for all basic blocks inside the functions of interest
		uint64_t bbCounter = 0;
		Function::iterator BI, BE;
		for(FI = M->begin(), FE = M->end(); FI != FE; ++FI) {
			if(FI->isDeclaration())
				continue;

			// Ignore uninteresting functions
			if(!isFunctionOfInterest(FI->getName()))
				continue;

			for(BI = FI->begin(), BE = FI->end(); BI != BE; ++BI) {
				BB2BBidMap.insert(std::make_pair(BI, bbCounter));
				bbCounter++;
			}
		}

		/// Get the number of functions have loops in the module and 
		/// the number of loops in a function here
		if(M->getNamedMetadata(loopNumberMDKindName)) {
			loopNumNMD = M->getNamedMetadata(loopNumberMDKindName);
			unsigned sizeLoopNumNMD = loopNumNMD->getNumOperands();
			for(unsigned i = 0; i < sizeLoopNumNMD; i++) {
				MDNode *tempMDNode = loopNumNMD->getOperand(i);
				MDString *funcNameMDStr = dyn_cast<MDString>(tempMDNode->getOperand(0));
				std::string funcNameStr = funcNameMDStr->getString();
				funcName2loopNumMapTy::iterator it;
				it = funcName2loopNumMap.find(funcNameStr);
				if(it != funcName2loopNumMap.end())
					it->second++;
			}
		}

		// Flag as checked
		alreadyCheck = true; 
	}

#ifdef DBG_PRINT_ALL
	printDoInitializationDatabase();
#endif

	/// We have changed IR
	return true;
}

void ExtractLoopInfo::getAnalysisUsage(AnalysisUsage &AU) const {
	AU.addRequired<LoopInfo>();
	AU.addPreserved<LoopInfo>();
	AU.addRequiredID(LoopSimplifyID);
	AU.addPreservedID(LoopSimplifyID);
	AU.addRequiredID(LCSSAID);
	AU.addPreservedID(LCSSAID);
	AU.addRequired<ScalarEvolution>();
	AU.addPreserved<ScalarEvolution>();

	// FIXME: Loop unroll requires LCSSA. And LCSSA requires dom info.
	// If loop unroll does not preserve dom info then LCSSA pass on next
	// loop will receive invalid dom info.
	// For now, recreate dom info, if loop is unrolled.
	AU.addPreserved<DominatorTreeWrapperPass>();
}

bool ExtractLoopInfo::runOnLoop(Loop *L, LPPassManager &LPM) {
	DEBUG(dbgs() << "\n\nBegin ExtractLoopInfo Pass :\n");
	LoopInfo *LI = &getAnalysis<LoopInfo>();
	ScalarEvolution *SE = &getAnalysis<ScalarEvolution>();

	/// Check whether loop is in a simplify form
	assert(L->isLoopSimplifyForm() && "Loop is not in a simplify form!\n");

	Function *func = L->getHeader()->getParent();
	std::string funcName = func->getName();
	// We do not consider uninteresting functions
	if(!isFunctionOfInterest(funcName))
		return false;

	if(firstRun) {
		errs() << "========================================================\n";
		errs() << "Extracting loops information\n";
		firstRun = false;
	}

	LLVMContext &Context = func->getContext();
	
	std::vector<Function *>::iterator it;
	it = std::find(exploredFunc.begin(), exploredFunc.end(), func);
	if(it == exploredFunc.end()) {
		// We only print the CFG of a function unexplored
		if(args.showCFGDetailed)
			func->viewCFG();
		else if(args.showCFG)
			func->viewCFGOnly();

		// New function, add function name to LoopsMetadataNode and reset loops counter
		MDString *funcName2 = MDString::get(Context, funcName);
		LoopsMetadataNode.push_back(funcName2);
		countNumLoopInAFunc = 0;

		exploredFunc.push_back(func);
	}

	unsigned numLoopInAFunc = funcName2loopNumMap.find(funcName)->second;

	depth = L->getLoopDepth();
	depthRecord++;

	// Add LoopiMetadataNode (i = 1,2,...,n) to LoopsMetadataNode. The first entry is the 
	// innermost loop
	std::string loopName = funcName + "_loop" + std::to_string(numLoopInAFunc - countNumLoopInAFunc - 1) + "_" + std::to_string(depth);
	std::string wholeLoopName = funcName + "_loop" + std::to_string(numLoopInAFunc - countNumLoopInAFunc - 1);
	LoopsMetadataNode.push_back(MDString::get(Context, loopName));
	LoopsMetadataNode.push_back(ConstantInt::get(Type::getInt32Ty(Context), depth));
	// Detect loop bound of this loop level if available. Otherwise, we set 0 at his field
	// FIXME: Current implementation, we just assign 0 to this field
	LoopsMetadataNode.push_back(ConstantInt::get(Type::getInt32Ty(Context), 0));
	// Add loop header id in this named_metadatanode
	BasicBlock *headerBB = L->getHeader();
	LoopsMetadataNode.push_back(ConstantInt::get(Type::getInt32Ty(Context), BB2BBidMap.at(headerBB)));

#ifndef ENABLE_INSTDISTRIBUTION
	DEBUG(dbgs() << "Analysing Loop " << wholeLoopName << " and its loop depth is: " << depth << "\n");
#endif // End of ENABLE_INSTDISTRIBUTION

	/// Try to get the loop bound of this loop
	BasicBlock* latchBB = L->getLoopLatch();
	unsigned tripCount = 0;
	if(latchBB) {
		tripCount = SE->getSmallConstantTripCount(L, latchBB);
		wholeloopName2loopBoundMap.insert(std::make_pair(loopName, tripCount));
	}

	wholeloopName2perfectOrNotMap.insert(std::make_pair(loopName, isPerfectNest(L, LI)));

	//exitBBFuncnamePair2lpNameLevelPairMap
	assert(L->getExitingBlock() != nullptr && "Could not find exiting basic block\n");
	bbFuncNamePairTy bbFuncPair = std::make_pair(L->getExitingBlock()->getName(), funcName);
	lpNameLevelPairTy lpNameLevelPair = std::make_pair(wholeLoopName, depth);
	std::pair<std::string, std::string> lpNameLevelStrPair = std::make_pair(wholeLoopName, std::to_string(depth));
	exitBBFuncnamePair2lpNameLevelPairMap.insert(std::make_pair(bbFuncPair, lpNameLevelPair));
	lpNameLevelPair2exitingBBnameMap.insert(std::make_pair(lpNameLevelStrPair, bbFuncPair.first));

	if(1 == depth) { // The top-level loop
		// Count loops inside the function
		countNumLoopInAFunc++;

		/// This loop level is the top level, we need to explore all its load/store instructions
		/// and construct a LSID2BB2LoopLevelMap.
		std::vector<BasicBlock *> BBVecTemp;
		BBVecTemp = L->getBlocks();
		for(unsigned i = 0; i < BBVecTemp.size(); i++) {
			BasicBlock *BB = BBVecTemp.at(i);
			unsigned loopLevel = LI->getLoopFor(BB)->getLoopDepth();
			BB2loopNameMap.insert(std::make_pair(BB, wholeLoopName));

			// Explore all load/store instructions inside Basic Blocks of a loop
			BasicBlock::iterator it, ie;
			for(it = BB->begin(), ie = BB->end(); it != ie; ++it) {
				if(isa<LoadInst>(it) || isa<StoreInst>(it)) {
					Instruction *instTemp = &*it;
					unsigned lsid = LoadStoreIDMap.at(instTemp).second;
					LSID2BB2LoopLevelMap.insert(std::make_pair(lsid, std::make_pair(BB, loopLevel)));
				}
			}
		}  // Now we have explored all load/store instructions inside a loop (whole loop) of a function

		/// Insert the Load/Store information (LSID2BB2LoopLevelMap) into LoopID2LSInfoMap
		std::string lpName = funcName + "_loop" + std::to_string(numLoopInAFunc - countNumLoopInAFunc);
		LoopID2LSInfoMap.insert(std::make_pair(lpName, LSID2BB2LoopLevelMap));
		/// Clear LSID2BB2LoopLevelMap in order to store new load/store info of other loops in the same function
		LSID2BB2LoopLevelMap.clear();
		
		/// Store LoopID2LSInfoMap into a Func2LoopInfoMap
		/// FIXME: Need a pass to calculate number of loops inside a function and number of functions inside a module,
		/// then we can use
		///		if (countNumLoopInaFunc == NumLoopInaFunc) {
		///			Func2LoopInfoMap.insert(...);
		///		}

		if(countNumLoopInAFunc == numLoopInAFunc) {
			Func2LoopInfoMap.insert(std::make_pair(funcName, LoopID2LSInfoMap));
			LoopID2LSInfoMap.clear();

			// Add Metadata into the global NamedMetadata Node
			MDNode *MD = MDNode::getWhenValsUnresolved(Context, ArrayRef<Value *>(LoopsMetadataNode), false);
			NMD->addOperand(MD);

			// We finish record one (or one nested) loop, need to clear contents in LoopsMetadataNode
			LoopsMetadataNode.clear();
		}

		// Calculate number of arithmetic operations for loops inside functions except for the "main" function
		// The map used to trace binary operators: (functionName, loopName) -> std::vector<BB id> > fnlpNamePair2BinaryOpBBidMap.
		// After we get the basic block frequency, we can get the total number of binary operations executed by
		// multiplication.
		std::vector<uint64_t> BBidVec;
		if(func->getName() != "main") {
			std::vector<BasicBlock *> BBVec = L->getBlocks();
			BasicBlock::iterator IT, IE;
			unsigned sizeBBVec = BBVec.size();
			unsigned arithmeticCounter = 0;
			for(unsigned i = 0; i < sizeBBVec; i++) {
				BasicBlock* BBTemp = BBVec.at(i);
				for(IT = BBTemp->begin(), IE = BBTemp->end(); IT != IE; ++IT) {
					// Find arithmetic instructions except for induction variables
					// Arithmetic instructions are called "Binary Operations" in LLVM

					// Should we include "Bitwise Binary Operations" as parts of 
					// arithmetic instructions?
					if(isa<BinaryOperator>(IT)) {
						// Ignore binary operations for induction variables
						if(IT->getName().find("indvars") == std::string::npos)
							BBidVec.push_back(BB2BBidMap.at(BBTemp));
					}
				}
			}

			// So far, we record all binary operators of a loop. We need to store it into fnlpNamePair2BinaryOpBBidMap
			std::pair<std::string, std::string> fnlpName = std::make_pair(funcName, lpName);
			fnlpNamePair2BinaryOpBBidMap.insert(std::make_pair(fnlpName, BBidVec));
			BBidVec.clear();

#ifdef BUILD_DDDG_H
			for(unsigned i = 0; i < sizeBBVec; i++) {
				BasicBlock *BBTemp = BBVec.at(i);
				unsigned lpLevel = LI->getLoopFor(BBTemp)->getLoopDepth();
				std::string lpLevelStr = std::to_string(lpLevel);
				std::string bbName = BBTemp->getName();
				bbFuncNamePairTy bbfnName = std::make_pair(bbName, funcName);
				lpNameLevelPairTy lpNameLevel = std::make_pair(lpName, lpLevel);
				
				if(LI->isLoopHeader(BBTemp)) {
					lpNameLevelPair2headBBnameMap.insert(std::make_pair(std::make_pair(lpName, lpLevelStr), bbName));
					headerBBFuncnamePair2lpNameLevelPairMap.insert(std::make_pair(bbfnName, lpNameLevel));
				}

				bbFuncNamePair2lpNameLevelPairMap.insert(std::make_pair(bbfnName, lpNameLevel));
			}

			LpName2numLevelMap.insert(std::make_pair(lpName, depthRecord));
			depthRecord = 0;
#endif // End of BUILD_DDDG_H
		}
	}

#ifdef DBG_PRINT_ALL
	printRunOnLoopDatabase();
#endif

	DEBUG(dbgs() << "End ExtractLoopInfo Pass\n");
	return true;
}

bool ExtractLoopInfo::isPerfectNest(Loop *L, LoopInfo *LI) {
	//check to see if we're the innermost nest
	if(L->getBlocks().size() == 1) {
		std::vector<Loop *>::iterator found = std::find(exploredLoop.begin(), exploredLoop.end(), L);
		if(found == exploredLoop.end())
			exploredLoop.push_back(L);
		else
			exploredLoop.push_back(L->getParentLoop());

		return true;
	}
	else {
		//do we have a single subloop?
		if(L->getSubLoops().size() != 1) {
			exploredLoop.push_back(L);

			return false;
		}
		//make sure all our non-nested loop blocks are innocuous

		std::vector<Loop *>::iterator found = std::find(exploredLoop.begin(), exploredLoop.end(), L);
		if(found != exploredLoop.end()) {
			// We have checked this recursive loop level already, no need to check again. The upper loop
			// level is a perfect loop.
			exploredLoop.push_back(L->getParentLoop());

			return true;
		}

		for(Loop::block_iterator BB = L->block_begin(), BE = L->block_end(); BB != BE; BB++) {
			BasicBlock *block = *BB;
			if(LI->getLoopFor(block) == L) {
				if(!hasNoMemoryOps(block)) {
					exploredLoop.push_back(L);

					return false;
				}
			}
		}

		//recursively check subloops
		return isPerfectNest(*L->begin(), LI);
	}
}

bool ExtractLoopInfo::hasNoMemoryOps(BasicBlock *BB) {
	for(BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E; I++) {
		switch (I->getOpcode()) {
			case Instruction::FAdd:
			case Instruction::FSub:
			case Instruction::FMul:
			case Instruction::FDiv:
			case Instruction::FCmp:
				return false;
		}
	}
	return true;
}

LoopID2LSInfoMapTy ExtractLoopInfo::getFunc2LoopInfoMap(std::string funcName) const {
	return Func2LoopInfoMap.at(funcName);
}

#ifdef DBG_PRINT_ALL
void ExtractLoopInfo::printLineID2LSMapDatabase(void) {
	errs() << "-- LineID2LSMap\n";
	for(auto const &x : LineID2LSMap) {
		if(x.second->hasName())
			errs() << "-- " << x.first << ": " << x.second->getName() << "\n";
		else
			errs() << "-- " << x.first << ": " << x.second->getOpcode() << "\n";
	}
	errs() << "-- ------------\n";
}

void ExtractLoopInfo::printDoInitializationDatabase(void) {
	errs() << "-- funcName2loopNumMap\n";
	for(auto const &x : funcName2loopNumMap)
		errs() << "-- " << x.first << ": " << x.second << "\n";
	errs() << "-- -------------------\n";

	errs() << "-- LoadStoreIDMap\n";
	for(auto const &x : LoadStoreIDMap) {
		if(x.first->hasName())
			errs() << "-- " << x.first->getName() << ": <" << x.second.first << ", " << x.second.second << ">\n";
		else
			errs() << "-- " << x.first->getOpcode() << ": <" << x.second.first << ", " << x.second.second << ">\n";
	}
	errs() << "-- --------------\n";

	errs() << "-- BB2BBidMap\n";
	for(auto const &x : BB2BBidMap) {
		if(x.first->hasName()) {
			errs() << "-- " << x.first->getName() << ": " << x.second << "\n";
		}
		else {
			std::string name;
			raw_string_ostream OS(name);
			x.first->printAsOperand(OS, false);
			errs() << "-- " << OS.str() << ": " << x.second << "\n";
		}
	}
	errs() << "-- ----------\n";
}

void ExtractLoopInfo::printRunOnLoopDatabase(void) {
	errs() << "-- wholeloopName2loopBoundMap\n";
	for(auto const &x : wholeloopName2loopBoundMap)
		errs() << "-- " << x.first << ": " << x.second << "\n";
	errs() << "-- --------------------------\n";

	errs() << "-- wholeloopName2perfectOrNotMap\n";
	for(auto const &x : wholeloopName2perfectOrNotMap)
		errs() << "-- " << x.first << ": " << x.second << "\n";
	errs() << "-- -----------------------------\n";

	errs() << "-- exitBBFuncnamePair2lpNameLevelPairMap\n";
	for(auto const &x : exitBBFuncnamePair2lpNameLevelPairMap)
		errs() << "-- <" << x.first.first << ", " << x.first.second << ">: <" << x.second.first << ", " << x.second.second << ">\n";
	errs() << "-- -------------------------------------\n";

	errs() << "-- lpNameLevelPair2exitingBBnameMap\n";
	for(auto const &x : lpNameLevelPair2exitingBBnameMap)
		errs() << "-- <" << x.first.first << ", " << x.first.second << ">: " << x.second << "\n";
	errs() << "-- --------------------------------\n";

	errs() << "-- BB2loopNameMap\n";
	for(auto const &x : BB2loopNameMap) {
		if(x.first->hasName()) {
			errs() << "-- " << x.first->getName() << ": " << x.second << "\n";
		}
		else {
			std::string name;
			raw_string_ostream OS(name);
			x.first->printAsOperand(OS, false);
			errs() << "-- " << OS.str() << ": " << x.second << "\n";
		}
	}
	errs() << "-- --------------\n";

	errs() << "-- Func2LoopInfoMap(LoopID2LSInfoMap(LSID2BB2LoopLevelMap))\n";
	for(auto const &x : Func2LoopInfoMap) {
		errs() << "-- " << x.first << ": \n";
		for(auto const &y : x.second) {
			errs() << "-- -- " << y.first << ": \n";
			for(auto const &z : y.second) {
				if(z.second.first->hasName()) {
					errs() << "-- -- -- " << z.first << ": <" << z.second.first->getName() << ", " << z.second.second << ">\n";
				}
				else {
					std::string name;
					raw_string_ostream OS(name);
					z.second.first->printAsOperand(OS, false);
					errs() << "-- -- -- " << z.first << ": <" << OS.str() << ", " << z.second.second << ">\n";
				}
			}
		}
	}
	errs() << "-- --------------------------------------------------------\n";

	errs() << "-- fnlpNamePair2BinaryOpBBidMap\n";
	for(auto const &x : fnlpNamePair2BinaryOpBBidMap) {
		errs() << "-- <" << x.first.first << ", " << x.first.second << ">: (";
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
	errs() << "-- ----------------------------\n";

	errs() << "-- lpNameLevelPair2headBBnameMap\n";
	for(auto const &x : lpNameLevelPair2headBBnameMap)
		errs() << "-- <" << x.first.first << ", " << x.first.second << ">: " << x.second << "\n";
	errs() << "-- -----------------------------\n";

	errs() << "-- headerBBFuncnamePair2lpNameLevelPairMap\n";
	for(auto const &x : headerBBFuncnamePair2lpNameLevelPairMap)
		errs() << "-- <" << x.first.first << ", " << x.first.second << ">: <" << x.second.first << "," << x.second.second << ">\n";
	errs() << "-- ---------------------------------------\n";

	errs() << "-- bbFuncNamePair2lpNameLevelPairMap\n";
	for(auto const &x : bbFuncNamePair2lpNameLevelPairMap)
		errs() << "-- <" << x.first.first << ", " << x.first.second << ">: <" << x.second.first << "," << x.second.second << ">\n";
	errs() << "-- ---------------------------------\n";

	errs() << "-- LpName2numLevelMap\n";
	for(auto const &x : LpName2numLevelMap)
		errs() << "-- " << x.first << ": " << x.second << "\n";
	errs() << "-- ------------------\n";
}
#endif

char ExtractLoopInfo::ID = 0;

INITIALIZE_PASS_BEGIN(
	ExtractLoopInfo,
	"extractLoopInfo",
	"This pass is used to extract basic loop info (e.g. loop bound, loop levels) and insert a function to trace loop iteration at runtime",
	false,
	true
)
INITIALIZE_PASS_DEPENDENCY(LoopNumber)
INITIALIZE_PASS_DEPENDENCY(LoopInfo)
INITIALIZE_PASS_DEPENDENCY(LoopSimplify)
INITIALIZE_PASS_DEPENDENCY(LCSSA)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolution)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(
	ExtractLoopInfo,
	"extractLoopInfo",
	"This pass is used to extract basic loop info (e.g. loop bound, loop levels) and insert a function to trace loop iteration at runtime",
	false,
	true
)

Pass* llvm::createExtractLoopInfoPass() {
	return new ExtractLoopInfo();
}
