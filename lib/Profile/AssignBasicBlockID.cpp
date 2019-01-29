/// Format of the NamedMDNode:
/// !lia.kernelbbid = !{!n, !n+1, !n+2, ...}
/// ...
/// !n = metadata !{metadata !"kernel1", metadata !"entry", i32 0}
/// !n+1 = metadata !{metadata !"kernel1", metadata !"for.loop....", i32 0}
/// !n+2 = metadata !{metadata !"kernel2", metadata !"for.end....", i32 1}
/// ...

/// NOTE: basic block names here used are for illustration purposes only
/// First element: kernel name
/// Second element: basic block label
/// Third element: auto-generated basic block ID

#include "profile_h/AssignBasicBlockIDPass.h"

#define DEBUG_TYPE "assign-bb-id"

using namespace llvm;

funcBBNmPair2numInstInBBMapTy funcBBNmPair2numInstInBBMap;
getElementPtrName2arrayNameMapTy getElementPtrName2arrayNameMap;

AssignBasicBlockID::AssignBasicBlockID() : ModulePass(ID) {
	counter = 0;
	DEBUG(dbgs() << "Initialize AssignBasicBlockID pass\n");
	initializeAssignBasicBlockIDPass(*PassRegistry::getPassRegistry());
}

void AssignBasicBlockID::getAnalysisUsage(AnalysisUsage &AU) const {
	AU.setPreservesCFG();
}

bool AssignBasicBlockID::runOnModule(Module &M) {
	errs() << "========================================================\n";
	errs() << "Assigning IDs to BBs, acquiring array names\n";

	DEBUG(dbgs() << "\n\nBegin AssignBasicBlockID Pass :\n");
	// Create a named Metadata node:
	NamedMDNode *NMD = M.getOrInsertNamedMetadata(assignBasicBlockIDMDKindName);
			
	// Initialize BB ID
	counter = 0;

	// Scan through all basic block in the module and assign unique ID
	Module::iterator FI, FE;
	Function::iterator BI, BE;
	BasicBlock::iterator BBIt, BBIe;

	for(FI = M.begin(), FE = M.end(); FI != FE; ++FI) {
		if(FI->isDeclaration())
			continue;

		// We are not interested in this function
		if(!isFunctionOfInterest(FI->getName()))
			continue;

		std::string funcName = FI->getName();
		DEBUG(dbgs() << "Function name = " << funcName << "\n");
		for(BI = FI->begin(), BE = FI->end(); BI != BE; ++BI) {
			NMD->addOperand(assignID(BI, counter));
			counter++;
			
			// Count instructions inside a basic block
			unsigned numInst = 0;
			std::string BBName = BI->getName();
			DEBUG(dbgs() << "BB name = " << BBName << "\n");
			for(BBIt = BI->begin(), BBIe = BI->end(); BBIt != BBIe; ++BBIt) {
				if(CallInst *CI = dyn_cast<CallInst>(BBIt)) {
					if(!(CI->isTailCall())) {
						numInst++;
					}
					else {
						// Do not count for tail call instructions.
					}
				}
				else {
					numInst++;
				}

				if(GetElementPtrInst *getElePtrInst = dyn_cast<GetElementPtrInst>(BBIt)) {
					std::string getElementPtrName = getElePtrInst->getName().str();
					std::string arrayName = getElePtrInst->getOperand(0)->getName().str();
					getElementPtrName2arrayNameMapTy::iterator itArrayName = getElementPtrName2arrayNameMap.find(getElementPtrName);
					if(itArrayName == getElementPtrName2arrayNameMap.end())
						getElementPtrName2arrayNameMap.insert(std::make_pair(getElementPtrName, arrayName));
				}
			}

			funcBBNmPair2numInstInBBMap.insert(std::make_pair(std::make_pair(funcName, BBName), numInst));
		}
	}

	// Check how many basic blocks
	DEBUG(dbgs() << "\tBasic Block number: " << counter << "\n");

	// Verify the module
	assert(verifyModuleAndPrintErrors(M) && "Errors found in module\n");

	DEBUG(dbgs() << "End AssignBasicBlockID Pass\n\n");
	return true;
}

MDNode *AssignBasicBlockID::assignID(BasicBlock *BB, unsigned id) {
	// Fetch the context in which the enclosing module was defined
	LLVMContext &Context = BB->getContext();
	std::string BBName = BB->getName();
	std::string funcName = BB->getParent()->getName();

	// Create a metadata node that contains ID as a constant:
	Value *ID[] {
		MDString::get(Context, funcName),
		MDString::get(Context, BBName),
		ConstantInt::get(Type::getInt32Ty(Context), id)
	};

	return MDNode::getWhenValsUnresolved(Context, ArrayRef<Value *>(ID, 3), false);
}

char AssignBasicBlockID::ID = 0;

INITIALIZE_PASS(
	AssignBasicBlockID,
	"assignBBid",
	"This pass is used to assign unique id for each basic block",
	false,
	true
)

ModulePass *llvm::createAssignBasicBlockIDPass() {
	return new 	AssignBasicBlockID();
}
