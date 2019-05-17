/// This pass is used to map the mangled and demangled name of functions.

#ifndef FUNCTIONNAMEMAPPERPASS_H
#define FUNCTIONNAMEMAPPERPASS_H

#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_os_ostream.h"

#include "profile_h/Passes.h"
#include "profile_h/lin-profile.h"

namespace llvm {

class FunctionNameMapper : public FunctionPass {		
	bool firstRun;
	NamedMDNode *NMD;
	std::vector<Value *> metadataNode;

#ifdef DBG_PRINT_ALL
	void printDatabase(void);
#endif

public:
	static char ID;
	FunctionNameMapper();
	bool doInitialization(Module &M);
	bool runOnFunction(Function &F);
};

} // End of llvm namespace

#endif // End of definition of FUNCTIONNAMEMAPPERPASS_H
