#ifndef __SHAREDMEMORYTRACEMAP_H__
#define __SHAREDMEMORYTRACEMAP_H__

// If using GCC, these pragmas will stop GCC from outputting the annoying misleading indentation warning for this include
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

// TODO Improve this
#ifndef SHARED_MEMORY_NAME_0
#define SHARED_MEMORY_NAME_0 "LinadSharedMemory.0"
#endif
#define KEY_SEPARATOR "?"

typedef boost::interprocess::allocator<uint64_t, boost::interprocess::managed_shared_memory::segment_manager> ShmAllocator;
typedef boost::interprocess::vector<uint64_t, ShmAllocator> MemoryTraceElementTy;

#endif

