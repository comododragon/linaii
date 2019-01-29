/// This pass is used to calculate the number of 
/// loops inside functions.

#ifndef LOOPNUMBERPASS_H
#define LOOPNUMBERPASS_H

#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Transforms/Scalar.h"

#include "profile_h/Passes.h"
#include "profile_h/lin-profile.h"

namespace llvm {

class LoopNumber : public LoopPass {		
	bool firstRun;
	unsigned loopCounter;
	NamedMDNode *NMD;
	std::vector<Value *> LoopsMetadataNode;
	std::vector<Function *> exploredFunc;

public:
	static char ID;
	LoopNumber();
	bool doInitialization(Loop *L, LPPassManager &LPM);
	void getAnalysisUsage(AnalysisUsage &AU) const;
	bool runOnLoop(Loop *L, LPPassManager &LPM);
};

} // End of llvm namespace

#endif // End of definition of LOOPNUMBERPASS_H
