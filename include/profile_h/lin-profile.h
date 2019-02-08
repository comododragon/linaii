#ifndef LIN_PROFILE_H
#define LIN_PROFILE_H

#define BUILD_DDDG_H

// If using GCC, these pragmas will stop GCC from outputting the annoying misleading indentation warning for this include
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#include "llvm/Support/CommandLine.h"
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

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
