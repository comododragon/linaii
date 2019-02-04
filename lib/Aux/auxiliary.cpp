#include "profile_h/auxiliary.h"

#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

const std::string loopNumberMDKindName = "lia.kernelloopnumber";
const std::string assignBasicBlockIDMDKindName = "lia.kernelbbid";
const std::string assignLoadStoreIDMDKindName = "lia.loadstoreid"; 
const std::string extractLoopInfoMDKindName = "lia.kernelloopinfo";

bool isFunctionOfInterest(std::string key) {
	std::vector<std::string>::iterator it;
	it = std::find(args.kernelNames.begin(), args.kernelNames.end(), key);
	return (it != args.kernelNames.end());
}

bool verifyModuleAndPrintErrors(Module &M) {
	std::string errorStr;
	raw_string_ostream OS(errorStr);
	if(verifyModule(M, &OS)) {
		errs() << OS.str() << "\n";
		return false;
	}

	return true;
}

std::string constructLoopName(std::string funcName, int loopNo, int depth) {
	std::string loopName = funcName + "_loop" + std::to_string(loopNo);

	if(-1 == depth)
		return loopName;

	return appendDepthToLoopName(loopName, depth);
}

std::string appendDepthToLoopName(std::string loopName, int depth) {
	return loopName + "_" + std::to_string(depth);
}
