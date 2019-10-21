include_directories( ${ZLIB_INCLUDE_DIRS} )

add_llvm_library(BuildDDDGlib
	HardwareProfile.cpp
	MemoryModel.cpp
	Multipath.cpp
	DynamicDatapath.cpp
	BaseDatapath.cpp
	DDDGBuilder.cpp
	SlotTracker.cpp
	TraceFunctions.cpp
	opcodes.cpp
	
	LINK_LIBS
	${ZLIB_LIBRARY}
	)
	
#target_link_libraries(BuildDDDGlib ${ZLIB_LIBRARY})
