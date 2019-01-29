add_llvm_library(LLVMLinProfiler
	InstrumentForDDDGPass.cpp
	#SlotTracker.cpp
	#TraceFunctions.cpp
	#BaseDatapath.cpp
	#DDDG.cpp
	#DynamicDatapath.cpp
	#file_func.cpp
	#opcode_func.cpp
	#generic_func.cpp
	LoopNumberPass.cpp
	AssignBasicBlockID.cpp
	AssignLoadStoreID.cpp
	ExtractLoopInfoPass.cpp
	TraceDataStructure.cpp
	)

target_link_libraries(LLVMLinProfiler
	Auxlib
	)

llvm_config(LLVMLinProfiler jit)

#MESSAGE(STATUS "add llvm library LLVMLinProfiler")
