#ifndef __SHAREDDYNAMICTRACE_H__
#define __SHAREDDYNAMICTRACE_H__

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

#include <string>
#include <zlib.h>

// TODO Improve this
#define SHARED_MEMORY_NAME "LinadSharedMemory."
#define PIPE_PATH "/tmp/linad.pipe."
#define DYNAMIC_TRACE_KEY_SEPARATOR "?"

#define DYNAMIC_TRACE_DEFAULT_BUFFER_SIZE 131072
#define DYNAMIC_TRACE_GET0_ISINIT(traceRegion) (((bool *) (traceRegion->data()))[0])
#define DYNAMIC_TRACE_GET1_EOFFOUND(traceRegion) (((bool *) (traceRegion->data()))[1])
#define DYNAMIC_TRACE_GET2_LOWER(traceRegion) *((z_off_t *) (&(traceRegion->data())[sizeof(bool) + sizeof(bool)]))
#define DYNAMIC_TRACE_GET3_HIGHER(traceRegion) *((z_off_t *) (&(traceRegion->data())[sizeof(bool) + sizeof(bool) + sizeof(z_off_t)]))
#define DYNAMIC_TRACE_GET4_DATA(traceRegion) (&(traceRegion->data())[sizeof(bool) + sizeof(bool) + sizeof(z_off_t) + sizeof(z_off_t)])
#define DYNAMIC_TRACE_SET0_ISINIT(traceRegion, isInit) do {\
	bool _isInit = isInit;\
	memcpy(traceRegion->data(), &_isInit, sizeof(bool));\
} while(0)
#define DYNAMIC_TRACE_SET1_EOFFOUND(traceRegion, eofFound) do {\
	bool _eofFound = eofFound;\
	memcpy(&(traceRegion->data())[sizeof(bool)], &_eofFound, sizeof(bool));\
} while(0)
#define DYNAMIC_TRACE_SET2_LOWER(traceRegion, lower) do {\
	z_off_t _lower = lower;\
	memcpy(&(traceRegion->data())[sizeof(bool) + sizeof(bool)], &_lower, sizeof(z_off_t));\
} while(0)
#define DYNAMIC_TRACE_SET3_HIGHER(traceRegion, higher) do {\
	z_off_t _higher = higher;\
	memcpy(&(traceRegion->data())[sizeof(bool) + sizeof(bool) + sizeof(z_off_t)], &_higher, sizeof(z_off_t));\
} while(0)
#define DYNAMIC_TRACE_SET4_DATA(traceRegion, data, size) do {\
	memcpy(&(traceRegion->data())[sizeof(bool) + sizeof(bool) + sizeof(z_off_t) + sizeof(z_off_t)], data, size);\
} while(0)
// XXX Using RESIZE on this side of the shared memory region creates some nasty C++ warnings. So I've removed this functionality on this side
// (i.e. available only via linad!)
#define DYNAMIC_TRACE_RESIZE(traceRegion, len)
#define DYNAMIC_TRACE_INVALIDATE(traceRegion) do {\
	DYNAMIC_TRACE_RESIZE(traceRegion, 0);\
	DYNAMIC_TRACE_SET0_ISINIT(traceRegion, false);\
	DYNAMIC_TRACE_SET1_EOFFOUND(traceRegion, false);\
} while(0)

class SharedDynamicTrace {

	typedef boost::interprocess::allocator<char, boost::interprocess::managed_shared_memory::segment_manager> ByteShmAllocator;
	typedef boost::interprocess::vector<char, ByteShmAllocator> ConnectionRegionTy;
	typedef boost::interprocess::vector<char, ByteShmAllocator> DynamicTraceRegionTy;

	gzFile uncachedFile;
	static unsigned transactionID;
	std::string futureCacheRealName;
	std::ofstream linadPipe;
	boost::interprocess::managed_shared_memory segment;
	ConnectionRegionTy *connection;
	std::string curCacheKey;
	DynamicTraceRegionTy *dynamicTrace;
	z_off_t curCursor;
	z_off_t proposedCursor;

	char *_gets(char *buff, int len);

public:

	SharedDynamicTrace(std::string dynamicTraceFileName, std::string futureCacheFileName);
	~SharedDynamicTrace();

	void attach(std::string cacheKey, long int baseCursor);
	void release();
	z_off_t seek(z_off_t offset, int whence);
	int rewind();
	int eof();
	z_off_t tell();
	char *gets(char *buff, int len);

};

#endif
