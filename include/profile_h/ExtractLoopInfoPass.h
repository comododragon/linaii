#ifndef EXTRACTLOOPINFOPASS_H
#define EXTRACTLOOPINFOPASS_H

#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/CFGPrinter.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Transforms/Scalar.h"

#include "profile_h/DDDGBuilder.h"
#include "profile_h/LoopNumberPass.h"
#include "profile_h/Passes.h"
#include "profile_h/auxiliary.h"

namespace llvm {

/// Load/Store ID --> (Basic Block, Loop Level Map) Map
typedef std::map<unsigned, std::pair<BasicBlock*, unsigned>> LSID2BBLoopLevelPairMapTy;
/// Loop Name (NOT loop level) --> LSID2BBLoopLevelPairMapTy Map
typedef std::map<std::string, LSID2BBLoopLevelPairMapTy> LoopID2LSInfoMapTy;
/// Function Name --> vector<LoopID2LSInfoMapTy> Map
typedef std::map<std::string, LoopID2LSInfoMapTy> Func2LoopInfoMapTy;

class ExtractLoopInfo : public LoopPass {
	bool firstRun;

	LSID2BBLoopLevelPairMapTy LSID2BB2LoopLevelMap;
	LoopID2LSInfoMapTy LoopID2LSInfoMap;
	Func2LoopInfoMapTy Func2LoopInfoMap;

	std::vector<Function *> exploredFunc;
	unsigned loopID;
	unsigned depth;
	NamedMDNode *NMD;
	NamedMDNode *loopNumNMD;
	unsigned countNumLoopInAFunc;
	std::vector<Value *> LoopsMetadataNode;

	/// Load/Store Instruction -> Load/StoreID
	std::map<Instruction *, std::pair<unsigned, unsigned>> LoadStoreIDMap;
	std::map<unsigned, Instruction *> LineID2LSMap;
	NamedMDNode *recordLSMDNode;
	bool alreadyCheck;

	typedef std::map<BasicBlock *, uint64_t> BB2BBidMapTy;
	BB2BBidMapTy BB2BBidMap;

	unsigned depthRecord;

	std::vector<Loop *> exploredLoop;

#ifdef DBG_PRINT_ALL
	void printLineID2LSMapDatabase(void);
	void printDoInitializationDatabase(void);
	void printRunOnLoopDatabase(void);
#endif

public:
	static char ID;

	ExtractLoopInfo();
	bool doInitialization(Loop *L, LPPassManager &LPM);
	void getAnalysisUsage(AnalysisUsage &AU) const;
	bool runOnLoop(Loop *L, LPPassManager &LPM);
	bool isPerfectNest(Loop *L, LoopInfo *LI);
	bool hasNoMemoryOps(BasicBlock *BB);
	LoopID2LSInfoMapTy getFunc2LoopInfoMap(std::string funcName) const;
};

} // End namespace LLVM

#endif // End of definition of EXTRACTLOOPINFOPASS_H
