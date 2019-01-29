#ifndef LIN_PROFILE_H
#define LIN_PROFILE_H

#define BUILD_DDDG_H

#include "llvm/Support/CommandLine.h"

#include "profile_h/ArgPack.h"
#include "profile_h/AssignBasicBlockIDPass.h"
#include "profile_h/AssignLoadStoreIDPass.h"
#include "profile_h/ExtractLoopInfoPass.h"
#include "profile_h/InstrumentForDDDGPass.h"
#include "profile_h/LoopNumberPass.h"
#include "profile_h/auxiliary.h"

static llvm::cl::opt<std::string>
InputFilename(llvm::cl::Positional, llvm::cl::desc("<input bitcode file>"), llvm::cl::init("-"), llvm::cl::value_desc("filename"));

static llvm::cl::opt<std::string>
OutputFilename("o", llvm::cl::desc("<output bitcode file>"), llvm::cl::value_desc("filename"));

void parseInputArguments(int argc, char **argv);

#endif // End LIN_PROFILE_H
