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

// If using GCC, these pragmas will stop GCC from outputting the thousands of warnings generated by boost library (WHICH IS EXTREMELY ANNOYING)
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma GCC diagnostic ignored "-Wdeprecated-copy"
#pragma GCC diagnostic ignored "-Wparentheses"
#endif
#include <boost/algorithm/string/predicate.hpp>
#include <boost/core/demangle.hpp>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#define DEBUG_TYPE "assign-bb-id"

using namespace llvm;

funcBBNmPair2numInstInBBMapTy funcBBNmPair2numInstInBBMap;
getElementPtrName2arrayNameMapTy getElementPtrName2arrayNameMap;
std::map<std::string, std::string> arrayName2MangledNameMap;
std::map<std::string, std::string> mangledName2ArrayNameMap;

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
					std::string mangledName = getElePtrInst->getOperand(0)->getName().str();

					// Atempt to get demangled name, if it doesn't work, the mangled named is used anyway
					// Assuming here that every mangled name starts with _Z, see https://itanium-cxx-abi.github.io/cxx-abi/abi.html#demangler
					std::string demangledName = boost::algorithm::starts_with(mangledName, "_Z")? boost::core::demangle(mangledName.c_str()) : mangledName;

					// Remove full-qualified from the demangled name (if applicable)
					size_t colonPos = demangledName.find_last_of(':');
					if(colonPos != std::string::npos)
						demangledName.erase(0, colonPos + 1);

					// When demangling a name, ambiguity might occur. Currently not supported
					std::map<std::string, std::string>::iterator found = mangledName2ArrayNameMap.find(mangledName);
					std::map<std::string, std::string>::iterator found2 = arrayName2MangledNameMap.find(demangledName);
					// If a same demangled name is found and if they refer to different mangled arrays, this is
					// currently not supported (i.e. it is ambiguous)
					if(found != mangledName2ArrayNameMap.end())
						assert(arrayName2MangledNameMap.end() != found2 && "Already found an array with the same demangled name (variables with same names under different scopes?). Currently not supported");

					arrayName2MangledNameMap.insert(std::make_pair(demangledName, mangledName));
					mangledName2ArrayNameMap.insert(std::make_pair(mangledName, demangledName));

					getElementPtrName2arrayNameMapTy::iterator itArrayName = getElementPtrName2arrayNameMap.find(getElementPtrName);
					if(itArrayName == getElementPtrName2arrayNameMap.end())
						getElementPtrName2arrayNameMap.insert(std::make_pair(getElementPtrName, mangledName));
				}
			}

			funcBBNmPair2numInstInBBMap.insert(std::make_pair(std::make_pair(funcName, BBName), numInst));
		}
	}

#ifdef DBG_PRINT_ALL
	printDatabase();
#endif

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

#ifdef DBG_PRINT_ALL
void AssignBasicBlockID::printDatabase(void) {
	errs() << "-- getElementPtrName2arrayNameMap\n";
	for(auto const &x : getElementPtrName2arrayNameMap)
		errs() << "-- " << x.first << ": " << x.second << "\n";
	errs() << "-- ------------------------------\n";

	errs() << "-- funcBBNmPair2numInstInBBMap\n";
	for(auto const &x : funcBBNmPair2numInstInBBMap)
		errs() << "-- <" << x.first.first << ", " << x.first.second << ">: " << x.second << "\n";
	errs() << "-- ---------------------------\n";
}
#endif

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
