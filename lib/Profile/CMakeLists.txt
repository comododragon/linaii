add_llvm_library(LLVMLinProfiler
	InstrumentForDDDGPass.cpp
	LoopNumberPass.cpp
	AssignBasicBlockID.cpp
	AssignLoadStoreID.cpp
	ExtractLoopInfoPass.cpp
	FunctionNameMapperPass.cpp
	)

target_link_libraries(LLVMLinProfiler
	Auxlib
	)

llvm_config(LLVMLinProfiler jit)

#MESSAGE(STATUS "add llvm library LLVMLinProfiler")
