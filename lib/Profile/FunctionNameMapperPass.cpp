/// Format of the NamedMDNode:
/// !lia.functionnamemapper = !{!n, !n+1, !n+2, ...}
/// ...
/// !n = metadata !{metadata !"fooBar", metadata !"_z87788787dhfooBar_S_S__V_V_B_S"}
/// !n+1 = metadata !{metadata !"barFoo", metadata !"_z87788787dhbarFoo_S_S__V_V_B_S"}
/// !n+2 = metadata !{metadata !"fooFoo", metadata !"_z87788787dhfooFoo_S_S__V_V_B_S"}
/// ...

/// NOTE: function names here used are for illustration purposes only
/// First element: the demangled name
/// Second element: the mangled name

#include "profile_h/FunctionNameMapperPass.h"

#define DEBUG_TYPE "function-name-mapper"

namespace llvm {

std::map<std::string, std::string> functionName2MangledNameMap;
std::map<std::string, std::string> mangledName2FunctionNameMap;

}

using namespace llvm;

FunctionNameMapper::FunctionNameMapper() : FunctionPass(ID) {
	firstRun = true;
	functionName2MangledNameMap.clear();
	mangledName2FunctionNameMap.clear();
	metadataNode.clear();
	DEBUG(dbgs() << "Initialize FunctionNameMapper pass\n");
	initializeFunctionNameMapperPass(*PassRegistry::getPassRegistry());
}

bool FunctionNameMapper::doInitialization(Module &M) {
	if(!M.getNamedMetadata(functionNameMapperMDKindName))
		NMD = M.getOrInsertNamedMetadata(functionNameMapperMDKindName);

	return true;
}

bool FunctionNameMapper::runOnFunction(Function &F) {
	DEBUG(dbgs() << "\n\nBegin FunctionNameMapper Pass :\n");

	if(firstRun) {
		errs() << "========================================================\n";
		errs() << "Building mangled-demangled function name map\n";
		firstRun = false;
	}

	LLVMContext &Context = F.getContext();

	std::string mangledName = F.getName();
	// Atempt to get demangled name, if it doesn't work, the mangled named is used anyway
	int status = -1;
	std::unique_ptr<char, void(*)(void *)> _demangledName { abi::__cxa_demangle(mangledName.c_str(), NULL, NULL, &status), std::free };
	std::string demangledName = status? mangledName : _demangledName.get();

	// Remove argument list from the demangled name (if applicable)
	size_t parenthesisPos = demangledName.find_first_of('(');
	if(parenthesisPos != std::string::npos)
		demangledName.erase(parenthesisPos);

	// When demangling a name, ambiguity might occur caused by function overloading. Currently not supported
	std::map<std::string, std::string>::iterator found = functionName2MangledNameMap.find(demangledName);
	assert(functionName2MangledNameMap.end() == found && "Already found a function with the same demangled name. Function overloading is currently not supported");

	functionName2MangledNameMap.insert(std::make_pair(demangledName, mangledName));
	mangledName2FunctionNameMap.insert(std::make_pair(mangledName, demangledName));

	metadataNode.push_back(MDString::get(Context, demangledName));
	metadataNode.push_back(MDString::get(Context, mangledName));
	MDNode *MD = MDNode::getWhenValsUnresolved(Context, ArrayRef<Value *>(metadataNode), false);
	NMD->addOperand(MD);
	metadataNode.clear();

#ifdef DBG_PRINT_ALL
	printDatabase();
#endif

	DEBUG(dbgs() << "End FunctionNameMapper Pass\n");
	return true;
}

#ifdef DBG_PRINT_ALL
void FunctionNameMapper::printDatabase(void) {
	errs() << "-- functionName2MangledNameMap\n";
	for(auto const &x : functionName2MangledNameMap)
		errs() << "-- " << x.first << ": " << x.second << "\n";
	errs() << "-- ---------------------------\n";

	errs() << "-- mangledName2FunctionNameMap\n";
	for(auto const &x : mangledName2FunctionNameMap)
		errs() << "-- " << x.first << ": " << x.second << "\n";
	errs() << "-- ---------------------------\n";
}
#endif

char FunctionNameMapper::ID = 0;
INITIALIZE_PASS(
	FunctionNameMapper,
	"FunctionNameMapper",
	"This pass is used to map the mangled and demangled name of functions.",
	false,
	true
)

Pass *llvm::createFunctionNameMapperPass() {
	return new FunctionNameMapper();
}
