#ifndef GLOBALCFGPARAMS_CPP
#define GLOBALCFGPARAMS_CPP

#include "profile_h/auxiliary.h"

using namespace llvm;

const std::unordered_map<unsigned, unsigned> ConfigurationManager::globalCfgTy::globalCfgTypeMap = {
	{GLOBAL_DDRBANKING, GLOBAL_TYPE_BOOL}
};
const std::unordered_map<std::string, unsigned> ConfigurationManager::globalCfgTy::globalCfgRenamerMap = {
	{"ddrbanking", GLOBAL_DDRBANKING}
};

#endif
