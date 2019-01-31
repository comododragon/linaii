/// Format of the NamedMDNode:
/// !lia.loadstoreid = !{!n, !n+1, !n+2, ...}
/// ...
/// !n = metadata !{metadata !"LoadInst", i32 16, i32 0}
/// !n+1 = metadata !{metadata !"LoadInst", i32 18, i32 1}
/// !n+2 = metadata !{metadata !"StoreInst", i32 20, i32 2}
/// ...

/// NOTE: line numbers and load/store IDs here used are for illustration purposes only
/// First element: LoadInst or StoreInst, identifying the type of memory access
/// Second element: line number of the instruction
/// Third element: auto-generated load/store ID

#include "profile_h/AssignLoadStoreIDPass.h"

#define DEBUG_TYPE "assign-loadstore-id"

using namespace llvm;

AssignLoadStoreID::AssignLoadStoreID() : ModulePass(ID) {
	DEBUG(dbgs() << "Initialize AssignLoadStoreID pass\n");
	counter = 0;
	initializeAssignLoadStoreIDPass(*PassRegistry::getPassRegistry());
}

void AssignLoadStoreID::getAnalysisUsage(AnalysisUsage &AU) const {
	AU.setPreservesCFG();
}

bool AssignLoadStoreID::runOnModule(Module &M) {
	errs() << "========================================================\n";
	errs() << "Assigning IDs to load and store instructions\n";

	DEBUG(dbgs() << "\n\nBegin AssignLoadStoreID Pass :\n");
	// Create a named Metadata node:
	NMD = M.getOrInsertNamedMetadata(assignLoadStoreIDMDKindName);
			
	// Initialize Load/Store ID
	counter = 0;
	instID = 0;

	Module::iterator FI, FE;
	for(FI = M.begin(), FE = M.end(); FI != FE; ++FI) {
		if(FI->isDeclaration())
			continue;

		if(!isFunctionOfInterest(FI->getName()))
			continue;

		for(inst_iterator I = inst_begin(FI), E = inst_end(FI); I != E; ++I) {
			Instruction *inst = &*I;
			Inst2IDMap.insert(std::make_pair(inst, instID));
			ID2InstMap.insert(std::make_pair(instID, inst));
			instID++;
		}
	}

#ifdef DBG_PRINT_ALL
	printDatabase();
#endif

	// Scan through all instructions in the module and assign unique ID
	visit(M);

	// Check how many basic blocks
	DEBUG(dbgs() << "\tLoad/Store instruction number: " << counter << "\n");

	// Verify the module
	assert(verifyModuleAndPrintErrors(M) && "Errors found in module\n");

	DEBUG(dbgs() << "End AssignLoadStoreID Pass\n\n");
	return true;
}

void AssignLoadStoreID::visitLoad(LoadInst &I) {
	Function *F = I.getParent()->getParent();
	// We only care of interesting kernel functions
	if(isFunctionOfInterest(F->getName())) {
		NMD->addOperand(assignID(&I, counter));
		counter++;
	}
}

void AssignLoadStoreID::visitStore(StoreInst &I) {
	Function *F = I.getParent()->getParent();
	// We only care of interesting kernel functions
	if(isFunctionOfInterest(F->getName())) {
		NMD->addOperand(assignID(&I, counter));
		counter++;
	}
}

MDNode *AssignLoadStoreID::assignID(Instruction *I, unsigned id) {
	// Fetch the context in which the enclosing module was defined
	LLVMContext &Context = I->getContext();

	// Get instruction's ID
	unsigned instid = Inst2IDMap.at(I);

	if(isa<LoadInst>(I)) {
		// Create a metadata node that contains ID as a constant:
		Value *lsID[3] = {
			MDString::get(Context, "LoadInst"),
			ConstantInt::get(Type::getInt32Ty(Context), instid),
			ConstantInt::get(Type::getInt32Ty(Context), id)
		};
		return MDNode::getWhenValsUnresolved(Context, lsID, false);
	}
	else if(isa<StoreInst>(I)) {
		// Create a metadata node that contains ID as a constant:
		Value *lsID[3] = {
			MDString::get(Context, "StoreInst"),
			ConstantInt::get(Type::getInt32Ty(Context), instid),
			ConstantInt::get(Type::getInt32Ty(Context), id)
		};
		return MDNode::getWhenValsUnresolved(Context, lsID, false);
	}
	else {
		assert(false && "assignID function was invoked for non-load/store instruction");
		return MDNode::get(Context, MDString::get(Context, "assignID function was invoked for non-load/store instruction"));
	}
}

#ifdef DBG_PRINT_ALL
void AssignLoadStoreID::printDatabase(void) {
	errs() << "-- Inst2IDMap\n";
	for(auto const &x : Inst2IDMap) {
		if(x.first->hasName())
			errs() << "-- " << x.first->getName() << ": " << x.second << "\n";
		else
			errs() << "-- " << x.first->getOpcode() << ": " << x.second << "\n";
	}
	errs() << "-- ----------\n";

	errs() << "-- ID2InstMap\n";
	for(auto const &x : ID2InstMap) {
		if(x.second->hasName())
			errs() << "-- " << x.first << ": " << x.second->getName() << "\n";
		else
			errs() << "-- " << x.first << ": " << x.second->getOpcode() << "\n";
	}
	errs() << "-- ----------\n";
}
#endif

char AssignLoadStoreID::ID = 0;

INITIALIZE_PASS(AssignLoadStoreID, "assignLoadStoreID",
	"This pass is used to assign unique id for each load/store instruction",
	false,
	true	/*false  We need to modify the code later. In initial step, we just check the unique ID first*/
)

ModulePass *llvm::createAssignLoadStoreIDPass() {
	return new 	AssignLoadStoreID();
}
