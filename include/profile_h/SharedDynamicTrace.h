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

class SharedDynamicTrace {

	typedef boost::interprocess::allocator<char, boost::interprocess::managed_shared_memory::segment_manager> ByteShmAllocator;
	typedef boost::interprocess::vector<char, ByteShmAllocator> ConnectionRegionTy;

	gzFile uncachedFile;
	static unsigned transactionID;
	std::string futureCacheRealName;
	std::ofstream linadPipe;
	boost::interprocess::managed_shared_memory segment;
	ConnectionRegionTy *connection;
	std::string curCacheKey;

	void _attach(std::string cacheKey, long int *fallbackCursor);

public:

	SharedDynamicTrace(std::string dynamicTraceFileName, std::string futureCacheFileName);
	~SharedDynamicTrace();

	bool initialise(std::string dynamicTraceFileName, std::string futureCacheFileName);
	void attach(std::string cacheKey);
	void attach(std::string cacheKey, long int fallbackCursor);
	void release();
	z_off_t seek(z_off_t offset, int whence);
	int rewind();
	int eof();
	z_off_t tell();
	char *gets(char *buff, int len);

};

#endif
