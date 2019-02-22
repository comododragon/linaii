#ifndef ASSIGNLOADSTOREID_PASS_H
#define ASSIGNLOADSTOREID_PASS_H

#include "llvm/Pass.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Scalar.h"

#include "profile_h/Passes.h"
#include "profile_h/auxiliary.h"

namespace llvm {

class AssignLoadStoreID : public ModulePass, public InstVisitor<AssignLoadStoreID> {
	unsigned counter;
	unsigned instID;

	// Query ID of Load/Store instructions
	typedef std::map<Instruction *, unsigned> InstToIDMapTy;
	InstToIDMapTy Inst2IDMap;
	typedef std::map<unsigned, Instruction *> IDToInstMapTy;
	IDToInstMapTy ID2InstMap;

	NamedMDNode *NMD;

	MDNode *assignID(Instruction *I, unsigned id);

#ifdef DBG_PRINT_ALL
	void printDatabase(void);
#endif

public:
	static char ID;
	AssignLoadStoreID();

	void getAnalysisUsage(AnalysisUsage &AU) const;
	bool runOnModule(Module &M);
	void visitLoad(LoadInst &I);
	void visitStore(StoreInst &I);
};

} // End namespace LLVM

#endif // End ASSIGNLOADSTOREID_PASS_H
