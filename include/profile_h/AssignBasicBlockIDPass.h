#ifndef ASSIGNBASICBLOCKID_PASS_H
#define ASSIGNBASICBLOCKID_PASS_H

#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"

#include "profile_h/DDDG.h"
#include "profile_h/Passes.h"
#include "profile_h/auxiliary.h"

namespace llvm{

class AssignBasicBlockID : public ModulePass, public InstVisitor<AssignBasicBlockID> {
	unsigned int counter;
	MDNode *assignID(BasicBlock *BB, unsigned id);

public:
	static char ID;
	AssignBasicBlockID();

	void getAnalysisUsage(AnalysisUsage &AU) const;

	bool runOnModule(Module &M);
};

} // End namespace LLVM

#endif // End ASSIGNBASICBLOCKID_PASS_H
