include_directories( ${ZLIB_INCLUDE_DIRS} )

add_llvm_library(BuildDDDGlib
	NodeLatency.cpp
	ColorWriter.cpp
	DynamicDatapath.cpp
	BaseDatapath.cpp
	DDDGBuilder.cpp
	SlotTracker.cpp
	TraceFunctions.cpp
	file_func.cpp
	Registers.cpp
	opcodes.cpp
	generic_func.cpp
	fpga_resources.cpp
	
	LINK_LIBS
	${ZLIB_LIBRARY}
	)
	
#target_link_libraries(BuildDDDGlib ${ZLIB_LIBRARY})
