/// Format of the NamedMDNode:
/// !lia.kernelloopnumber = !{!n, !n+1, !n+2, ...}
/// ...
/// !n = metadata !{metadata !"kernel1", metadata !"loop~1"}
/// !n+1 = metadata !{metadata !"kernel1", metadata !"loop~2"}
/// !n+2 = metadata !{metadata !"kernel2", metadata !"loop~1"}
/// ...

/// First element: kernel name
/// Second element: auto-generated loop ID

/// Therefore, to get loop number in a function, we need to check the number of 
/// metadata node that contains the function name.

#include "profile_h/LoopNumberPass.h"

#define DEBUG_TYPE "loop-number"

using namespace llvm;

LoopNumber::LoopNumber() : LoopPass(ID) {
	firstRun = true;
	loopCounter = 0;
	LoopsMetadataNode.clear();
	DEBUG(dbgs() << "Initialize LoopNumber pass\n");
	initializeLoopNumberPass(*PassRegistry::getPassRegistry());
}

bool LoopNumber::doInitialization(Loop *L, LPPassManager &LPM) {
	Module *M = L->getHeader()->getParent()->getParent();
	if(!M->getNamedMetadata(loopNumberMDKindName))
		NMD = M->getOrInsertNamedMetadata(loopNumberMDKindName);

	return true;
}

void LoopNumber::getAnalysisUsage(AnalysisUsage &AU) const {
	AU.addRequired<LoopInfo>();
	AU.addPreserved<LoopInfo>();

	AU.addRequiredID(LoopSimplifyID);
	AU.addPreservedID(LoopSimplifyID);

	AU.setPreservesCFG();
	AU.setPreservesAll();

	// FIXME: Loop unroll requires LCSSA. And LCSSA requires dom info.
	// If loop unroll does not preserve dom info then LCSSA pass on next
	// loop will receive invalid dom info.
	// For now, recreate dom info, if loop is unrolled.
	//AU.addPreserved<DominatorTreeWrapperPass>();
}

bool LoopNumber::runOnLoop(Loop *L, LPPassManager &LPM) {
	/// Check whether loop is in a simplify form
	assert(L->isLoopSimplifyForm() && "Loop is not in a simplify form!\n");

	Function *F = L->getHeader()->getParent();
	/// Ignore uninterested functions
	if(!isFunctionOfInterest(F->getName()))
		return false;

	if(firstRun) {
		errs() << "========================================================\n";
		errs() << "Counting number of top-level loops in \"" << demangleFunctionName(F->getName()) << "\"\n";
		firstRun = false;
	}

	LLVMContext &Context = F->getContext();

	std::vector<Function *>::iterator it;
	it = std::find(exploredFunc.begin(), exploredFunc.end(), F);
	if(it == exploredFunc.end()) {
		// Reset loop counter for a new function.
		firstRun = false;
		loopCounter = 0;
		exploredFunc.push_back(F);
	}

	unsigned depth = L->getLoopDepth();
	/// Trace the top-level loop of a loop
	if(1 == depth) {
		MDString *funcName = MDString::get(Context, F->getName());
		LoopsMetadataNode.push_back(funcName);
#ifdef LEGACY_SEPARATOR
		std::string loopName = "loop" + std::to_string(loopCounter++);
#else
		std::string loopName = "loop" GLOBAL_SEPARATOR + std::to_string(loopCounter++);
#endif
		LoopsMetadataNode.push_back(MDString::get(Context, loopName));
		MDNode *MD = MDNode::getWhenValsUnresolved(Context, ArrayRef<Value *>(LoopsMetadataNode), false);
		NMD->addOperand(MD);
		LoopsMetadataNode.clear();
	}

	return true;
}

char LoopNumber::ID = 0;
INITIALIZE_PASS_BEGIN(
	LoopNumber,
	"LoopNumber",
	"This pass is used to calculate the number of top-level loops inside functions.",
	false,
	true
)
INITIALIZE_PASS_DEPENDENCY(LoopInfo)
INITIALIZE_PASS_DEPENDENCY(LoopSimplify)
INITIALIZE_PASS_END(
	LoopNumber,
	"LoopNumber",
	"This pass is used to calculate the number of top-level loops inside functions.",
	false,
	true
)

Pass *llvm::createLoopNumberPass() {
	return new LoopNumber();
}
