#include <getopt.h>

#include "llvm/PassManager.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Bitcode/BitcodeWriterPass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/ToolOutputFile.h"

#include "profile_h/lin-profile.h"

#define DEBUG_TYPE "lina"

using namespace llvm;

const std::string helpMessage =
	"Lina: (yet another) High Level Analysis Tool for FPGA Accelerators\n"
	"    an expansion of the Lin-Analyzer High Level Analysis Tool for FPGA Accelerators\n"
	"    visit: https://github.com/zhguanw/lin-analyzer\n"
	"\n"
	"Usage: lina [OPTION]... BYTECODEFILE KERNELNAME\n"
	"Where:\n"
	"    BYTECODEFILE is the optimised .bc file generated with the LLVM toolchain\n"
	"    KERNELNAME is the kernel name (i.e. function name) to be analysed\n"
	"    OPTION may be:\n"
	"        -h       , --help             : this message\n"
	"        -i PATH  , --workdir=PATH     : input working directory where trace should happen\n"
	"                                        (e.g. containing files used by the application).\n"
	"                                        Default is $CWD\n"
	"        -o PATH  , --out-workdir=PATH : output working directory where temporary files\n"
	"                                        will be written. Default is $CWD\n"
	"        -c FILE  , --config-file=FILE : use FILE as the configuration file for this\n"
	"                                        application. Default is workdir/config.cfg\n"
	"        -m MODE  , --mode=MODE        : set execution mode to MODE, where MODE may be:\n"
	"                                            all       : perform dynamic trace and cycle\n"
	"                                                        estimation (DEFAULT)\n"
	"                                            trace     : perform dynamic trace only,\n"
	"                                                        generating dynamic_trace.gz\n"
	"                                            estimation: perform cycle estimation only,\n"
	"                                                        using provided dynamic_trace.gz\n"
	"        -t TARGET, --target=TARGET    : select TARGET FPGA platform, where TARGET may be:\n"
	"                                            ZC702 : Xilinx Zynq-7000 SoC (DEFAULT)\n"
	"                                            ZCU102: Xilinx Zynq UltraScale+ SoC\n"
	"                                            ZCU104: Xilinx Zynq UltraScale+ SoC\n"
	"                                            VC707 : Xilinx Virtex-7 FPGA\n"
	"        -v       , --verbose          : be verbose, print a lot of information\n"
	"        -x       , --compressed       : use compressed files to reduce memory footprint\n"
#ifdef PROGRESSIVE_TRACE_CURSOR
	"        -p       , --progressive      : use progressive trace cursor when trace is\n"
	"                                        analysed, reducing estimation time when several\n"
	"                                        top loops are analysed. Loops defined with\n"
	"                                        -l|--loops flag must be in crescent order\n"
#endif
#ifdef FUTURE_CACHE
	"        -C       , --future-cache     : use future cache. DDDG positions in the dynamic_trace.gz\n"
	"                                        file are cached to be used in successive executions of Lina\n"
	"                                        saving seek time. Only supported when progressive trace\n"
	"                                        cursor is active with -p | --progressive. Future cache is\n"
	"                                        disabled when runtime loop bound analysis is required.\n"
#endif
	"        -l LOOPS , --loops=LOOPS      : specify loops to be analysed comma-separated (e.g.\n"
	"                                        --loops=2,3 only analyse loops 2 and 3)\n"
	"                   --mem-trace        : obtain memory trace for access pattern analysis.\n"
	"                                        Ignored if -m estimation | --mode=estimation is\n"
	"                                        set.\n"
	"                                        Can only be used with -m trace | --mode=trace\n"
	"                   --show-cfg         : dump CFG with basic blocks\n"
	"                   --show-detail-cfg  : dump detailed CFG with instructions\n"
	"                   --show-pre-dddg    : dump DDDG before optimisation\n"
	"                   --show-post-dddg   : dump DDDG after optimisation\n"
	"                   --show-scheduling  : dump constrained-scheduling\n"
	"\n"
	"Analysis enable/disable flags:\n"
	"                   --f-npla           : enable non-perfect loop analysis\n"
	"                   --fno-tcs          : disable timing-constrained scheduling\n"
	"                   --fno-mma          : disable memory model analysis\n"
	"\n"
	"Timing-constrained flags (ignored if \"--fno-tcs\" is set):\n"
	"        -f FREQ  , --frequency=FREQ   : specify the target clock (in MHz)\n"
	"        -u UNCTY , --uncertainty=UNCTY: specify the clock uncertainty (in %)\n"
	"\n"
	"Memory model tuning flags (ignored if \"--fno-mma\" is set):\n"
	"                   --f-burstaggr      : enable burst aggregation: sequential operations inside a\n"
	"                                        DDDG are grouped together to form bursts.\n"
	"                   --f-burstmix       : if enabled, burst aggregation can mix arrays. Option ignored\n"
	"                                        if global parameter \"ddrbanking\" is enabled or if \"--f-burstaggr\"\n"
	"                                        is disabled\n"
	"                   --f-vec            : enable array vectorisation analysis, which tries to find a\n"
	"                                        suitable SIMD type for the off-chip arrays. This analysis is\n"
	"                                        automatically disabled when \"--mma-mode=off\". Requires\n"
	"                                        \"--f-burstaggr\"\n"
	"        -d LEVEL , --ddrsched=LEVEL   : specify the DDR scheduling policy:\n"
	"                                            0: DDR transactions of same type (read/write) cannot\n"
	"                                               overlap (i.e. once a transaction starts, it must end\n"
	"                                               before others of same type can start, DEFAULT)\n"
	"                                            1: DDR transactions can overlap if their memory spaces\n"
	"                                               are disjoint\n"
	"                   --mma-mode=MODE    : select Lina execution model according to MMA mode. MODE may\n"
	"                                        be:\n"
	"                                            off: run Lina normally (DEFAULT)\n"
	"                                            gen: perform memory model analysis, generate a context\n"
	"                                                 import file and stop execution\n"
	"                                            use: skip profiling and DDDG generation, proceed\n"
	"                                                 directly to memory model analysis. In this mode, a\n"
	"                                                 context import file is used to generate the DDDG\n"
	"                                                 and other data. The context-import should have been\n"
	"                                                 generated with a previous execution of Lina with\n"
	"                                                 \"--mma-mode=GEN\". Performing memory model analysis\n"
	"                                                 based on a context-import opens the possibility for\n"
	"                                                 improved memory optimisations. Please note that Lina\n"
	"                                                 fails if this mode is set without a present context\n"
	"                                                 import\n"
	"                                        Please note that this argument is ignored if \"-m trace\" |\n"
	"                                        \"--mode=trace\" is set\n"
	"\n"
	"Lin-Analyzer flags:\n"
	"                   --fno-sb           : disable store-buffer optimisation\n"
	"                   --f-slr            : enable shared-load-removal optimisation\n"
	"                   --fno-slr          : disable shared-load-removal optimisation. If\n"
	"                                        either --f-slr and --fno-slr are omitted, the\n"
	"                                        decision will be left to the estimator\n"
	"                   --fno-rsr          : disable repeated-store-removal optimisation\n"
	"                   --f-thr-float      : enable tree-height-reduction for floating point\n"
	"                                        operations\n"
	"                   --f-thr-int        : enable tree-height-reduction for integer\n"
	"                                        operations\n"
	"                   --f-md             : enable memory-disambiguation optimisation\n"
	"                   --fno-ft           : disable FPU threshold optimisation\n"
	"                   --f-es             : enable extra-scalar\n"
	"                   --f-rwrwm          : enable RWRW memory\n"
	"Other flags:\n"
	"                   --f-argres         : consider resource used by function arguments\n"
	"\n"
	"For bug reporting, please file a github issue at https://github.com/comododragon/linaii\n";

ArgPack args;
#ifdef PROGRESSIVE_TRACE_CURSOR
long int progressiveTraceCursor = 0;
uint64_t progressiveTraceInstCount = 0;
#endif

int main(int argc, char **argv) {
#ifdef DBG_FILE
	debugFile.open(DBG_FILE);
#endif

	parseInputArguments(argc, argv);

	errs() << "░░░░░░░░░░░░░░░░░░░░░░░░░░░░▒▒\n";
	errs() << "░░▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▒▒\n";
	errs() << "░░▓░░▓▓▓▓░░▓▓░░▓▓░░▓▓▓░░░░▓▓▒▒\n";
	errs() << "░░▓░░▓▓▓▓░░▓▓░░░▓░░▓▓░░▓▓░░▓▒▒\n";
	errs() << "░░▓░░▓▓▓▓░░▓▓░░░░░░▓▓░░░░░░▓▒▒\n";
	errs() << "░░▓░░▓▓▓▓░░▓▓░░▓░░░▓▓░░▓▓░░▓▒▒\n";
	errs() << "░░▓░░░░▓▓░░▓▓░░▓▓░░▓▓░░▓▓░░▓▒▒\n";
	errs() << "░░▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▒▒\n";
	errs() << "▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒\n";

	LLVMContext &Context = getGlobalContext();
	SMDiagnostic Err;

	std::unique_ptr<Module> M;
	M.reset(ParseIRFile(InputFilename, Err, Context));
	if(!M.get()) {
		Err.print(argv[0], errs());
		return 1;
	}

	Module &mod = *M.get();
	Triple TheTriple(mod.getTargetTriple());

	if(OutputFilename.empty()) {
		if(InputFilename == "-") {
			OutputFilename = "-";
		}
		else {
			const std::string &inputFilenameStr = InputFilename;
			// We are assuming that InputFilename ends with ".bc", a constraint detected when the arguments were collected
			OutputFilename = std::string(inputFilenameStr.begin(), inputFilenameStr.end() - 3) + FILE_TRACE_SUFFIX;
		}
	}

	// Figure out what stream we are supposed to write to...
	std::unique_ptr<tool_output_file> Out;
	std::string ErrorInfo;
	Out.reset(new tool_output_file(OutputFilename.c_str(), ErrorInfo, sys::fs::F_None));
	if(!ErrorInfo.empty()) {
		errs() << ErrorInfo << "\n";
		return 1;
	}

	PassManager Passes;
	// Identifying the mangled-demangled function names
	Passes.add(createFunctionNameMapperPass());
	// Counting number of top-level loops in each function of interest (and store in the form of metadata)
	Passes.add(createLoopNumberPass());
	// Assigning IDs to BBs, acquiring array names
	Passes.add(createAssignBasicBlockIDPass());
	// Assigning IDs to load and store instructions
	Passes.add(createAssignLoadStoreIDPass());
	// Extracting loops information, counting number of load/store instructions and arithmetic instructions inside specific loops
	Passes.add(createExtractLoopInfoPass());
	Passes.add(createInstrumentForDDDGPass());
	Passes.add(createBitcodeWriterPass(Out->os()));

	Passes.run(mod);

	// Declare success.
	Out->keep();

#ifdef DBG_FILE
	debugFile.close();
#endif

	return 0;
}

void parseInputArguments(int argc, char **argv) {
	char temp[PATH_MAX];
	if(NULL == getcwd(temp, PATH_MAX)) {
		errs() << "Error: " << strerror(errno) << "\n";
		exit(-1);
	}
	std::string optargStr;
	size_t commaPos;

	args.inputFileName = "";
	args.workDir = temp;
	args.outWorkDir = temp;
	args.configFileName = "config.cfg";
	args.mode = args.MODE_TRACE_AND_ESTIMATE;
	args.target = args.TARGET_XILINX_ZC702;
	args.compressed = false;
#ifdef PROGRESSIVE_TRACE_CURSOR
	args.progressive = false;
#endif
#ifdef FUTURE_CACHE
	args.futureCache = false;
#endif
	args.frequency = 100.0;
	args.uncertainty = 27;
	args.verbose = false;
	args.ddrSched = args.DDR_POLICY_CANNOT_OVERLAP;
	args.memTrace = false;
	args.showCFG = false;
	args.showCFGDetailed = false;
	args.showPreOptDDDG = false;
	args.showPostOptDDDG = false;
	args.showScheduling = false;
	args.fNPLA = false;
	args.fNoTCS = false;
	args.fNoMMA = false;
	args.fBurstAggr = false;
	args.fBurstMix = false;
	args.fVec = false;
	args.mmaMode = args.MMA_MODE_OFF;
	args.fSBOpt = true;
	args.fSLROpt = false;
	args.fNoSLROpt = false;
	args.fRSROpt = true;
	args.fTHRFloatOpt = false;
	args.fTHRIntOpt = false;
	args.fMemDisambuigOpt = false;
	args.fNoFPUThresOpt = false;
	args.fExtraScalar = false;
	args.fRWRWMem = false;
	args.fArgRes = false;
	// XXX: Does not seem to make sense for me right now to leave this deactivated
	// since according to Vivado reports, the load latency is in fact 2
	args.fILL = true;

	int c;
	while(true) {
		static struct option longOptions[] = {
			{"help", no_argument, 0, 'h'},
			{"workdir", required_argument, 0, 'i'},
			{"out-workdir", required_argument, 0, 'o'},
			{"config-file", required_argument, 0, 'c'},
			{"mode", required_argument, 0, 'm'},
			{"target", required_argument, 0, 't'},
			{"verbose", no_argument, 0, 'v'},
			{"compressed", no_argument, 0, 'x'},
#ifdef PROGRESSIVE_TRACE_CURSOR
			{"progressive", no_argument, 0, 'p'},
#endif
#ifdef FUTURE_CACHE
			{"future-cache", no_argument, 0, 'C'},
#endif
			{"frequency", required_argument, 0, 'f'},
			{"uncertainty", required_argument, 0, 'u'},
			{"loops", required_argument, 0, 'l'},
			{"ddrsched", required_argument, 0, 'd'},
			{"mem-trace", no_argument, 0, 0xF00},
			{"show-cfg", no_argument, 0, 0xF01},
			{"show-detail-cfg", no_argument, 0, 0xF02},
			{"show-pre-dddg", no_argument, 0, 0xF03},
			{"show-post-dddg", no_argument, 0, 0xF04},
			{"show-scheduling", no_argument, 0, 0xF05},
			{"f-npla", no_argument, 0, 0xF06},
			{"fno-tcs", no_argument, 0, 0xF07},
			{"fno-mma", no_argument, 0, 0xF08},
			{"f-burstaggr", no_argument, 0, 0xF09},
			{"f-burstmix", no_argument, 0, 0xF0A},
			{"f-vec", no_argument, 0, 0xF0B},
			{"mma-mode", required_argument, 0, 0xF0C},
			{"fno-sb", no_argument, 0, 0xF0D},
			{"f-slr", no_argument, 0, 0xF0E},
			{"fno-slr", no_argument, 0, 0xF0F},
			{"fno-rsr", no_argument, 0, 0xF10},
			{"f-thr-float", no_argument, 0, 0xF11},
			{"f-thr-int", no_argument, 0, 0xF12},
			{"f-md", no_argument, 0, 0xF13},
			{"fno-ft", no_argument, 0, 0xF14},
			{"f-es", no_argument, 0, 0xF15},
			{"f-rwrwm", no_argument, 0, 0xF16},
			{"f-argres", no_argument, 0, 0xF17},
			{0, 0, 0, 0}
		};
		int optionIndex = 0;

#ifdef PROGRESSIVE_TRACE_CURSOR
#ifdef FUTURE_CACHE
		c = getopt_long(argc, argv, "+hi:o:c:m:t:vxpCf:u:l:d:", longOptions, &optionIndex);
#else
		c = getopt_long(argc, argv, "+hi:o:c:m:t:vxpf:u:l:d:", longOptions, &optionIndex);
#endif
#else
		c = getopt_long(argc, argv, "+hi:o:c:m:t:vxf:u:l:d:", longOptions, &optionIndex);
#endif
		if(-1 == c)
			break;

		switch(c) {
			case 'h':
				errs() << helpMessage;
				exit(-1);
				break;
			case 'i':
				args.workDir = optarg;
				break;
			case 'o':
				args.outWorkDir = optarg;
				break;
			case 'c':
				args.configFileName = optarg;
				break;
			case 'm':
				optargStr = optarg;
				if(!optargStr.compare("trace"))
					args.mode = args.MODE_TRACE_ONLY;
				else if(!optargStr.compare("estimation"))
					args.mode = args.MODE_ESTIMATE_ONLY;
				break;
			case 't':
				optargStr = optarg;
				if(!optargStr.compare("VC707"))
					args.target = args.TARGET_XILINX_VC707;
				else if(!optargStr.compare("ZCU102"))
					args.target = args.TARGET_XILINX_ZCU102;
				else if(!optargStr.compare("ZCU104"))
					args.target = args.TARGET_XILINX_ZCU104;
				break;
			case 'v':
				args.verbose = true;
				break;
			case 'x':
				args.compressed = true;
				break;
#ifdef PROGRESSIVE_TRACE_CURSOR
			case 'p':
				args.progressive = true;
				break;
#endif
#ifdef FUTURE_CACHE
			case 'C':
				args.futureCache = true;
				break;
#endif
			case 'f':
				args.frequency = std::stof(optarg);
				break;
			case 'u':
				args.uncertainty = std::stof(optarg);
				break;
			case 'l':
				optargStr = optarg;
				commaPos = optargStr.find(",");
				if(std::string::npos == commaPos) {
					args.targetLoops.push_back(optargStr);
				}
				else {
					while(optargStr.find(",") != std::string::npos) {
						commaPos = optargStr.find(",");
						std::string loopIndex = optargStr.substr(0, commaPos);
						optargStr.erase(0, commaPos + 1);
						if(loopIndex != "")
							args.targetLoops.push_back(loopIndex);
					}
					if(optargStr != "")
						args.targetLoops.push_back(optargStr);
				}
				break;
			case 'd':
				optargStr = optarg;
				if(!optargStr.compare("1"))
					args.ddrSched = args.DDR_POLICY_CAN_OVERLAP;
				break;
			case 0xF00:
				args.memTrace = true;
				break;
			case 0xF01:
				args.showCFG = true;
				break;
			case 0xF02:
				args.showCFGDetailed = true;
				break;
			case 0xF03:
				args.showPreOptDDDG = true;
				break;
			case 0xF04:
				args.showPostOptDDDG = true;
				break;
			case 0xF05:
				args.showScheduling = true;
				break;
			case 0xF06:
				args.fNPLA = true;
				break;
			case 0xF07:
				args.fNoTCS = true;
				break;
			case 0xF08:
				args.fNoMMA = true;
				break;
			case 0xF09:
				args.fBurstAggr = true;
				break;
			case 0xF0A:
				args.fBurstMix = true;
				break;
			case 0xF0B:
				args.fVec = true;
				break;
			case 0xF0C:
				optargStr = optarg;
				if(!optargStr.compare("off"))
					args.mmaMode = args.MMA_MODE_OFF;
				else if(!optargStr.compare("gen"))
					args.mmaMode = args.MMA_MODE_GEN;
				else if(!optargStr.compare("use"))
					args.mmaMode = args.MMA_MODE_USE;
				break;
			case 0xF0D:
				args.fSBOpt = false;
				break;
			case 0xF0E:
				args.fSLROpt = true;
				break;
			case 0xF0F:
				args.fNoSLROpt = true;
				break;
			case 0xF10:
				args.fRSROpt = false;
				break;
			case 0xF11:
				args.fTHRFloatOpt = true;
				break;
			case 0xF12:
				args.fTHRIntOpt = true;
				break;
			case 0xF13:
				args.fMemDisambuigOpt = true;
				break;
			case 0xF14:
				args.fNoFPUThresOpt = true;
				break;
			case 0xF15:
				args.fExtraScalar = true;
				break;
			case 0xF16:
				args.fRWRWMem = true;
				break;
			case 0xF17:
				args.fArgRes = true;
				break;
		}
	}

	// Note: The path should include \5C in Unix or / in Windows in the end!
#ifdef _MSC_VER
	args.workDir += "\\";
	args.outWorkDir += "\\";
#else
	args.workDir += "/";
	args.outWorkDir += "/";
#endif

	if((argc - optind) != 2) {
		errs() << "Missing input arguments (run \"" << argv[0] << " --help\" for help)\n";
		exit(-1);
	}

	InputFilename = argv[optind];
	args.inputFileName = InputFilename.c_str();
	int len = args.inputFileName.length();
	if(len < 3 || args.inputFileName.substr(len - 3, len).compare(".bc")) {
		errs() << "Invalid bitcode filename (wrong extension)\n";
		exit(-1);
	}

	args.kernelNames.clear();
	args.kernelNames.push_back(argv[optind + 1]);

	if(!args.targetLoops.size())
		args.targetLoops.push_back("0");

#ifdef PROGRESSIVE_TRACE_CURSOR
	if(args.progressive) {
		int maxLoop = -1;

		for(auto &it : args.targetLoops) {
			int itInt = std::stoi(it);
			if(itInt <= maxLoop) {
				errs() << "Progressive trace cursor enabled, target loops must be provided in crescent order\n";
				exit(-1);
			}
			maxLoop = itInt;
		}
	}
#endif

#ifdef FUTURE_CACHE
	if(args.futureCache && !(args.progressive)) {
		errs() << "Please activate progressive trace cursor with -p | --progressive in order to use future cache\n";
		exit(-1);
	}
#endif

	if(args.uncertainty < 0.0 || args.uncertainty > 100.0) {
		errs() << "Uncertainty must be between 0.0 and 100.0 %\n";
		exit(-1);
	}
	if(args.frequency < 0.0) {
		errs() << "Target frequency must be positive\n";
		exit(-1);
	}
	if(args.frequency > 500.0) {
		errs() << "Lina does not support estimation with target frequency above 500 Mhz\n";
		exit(-1);
	}
	if(args.mmaMode != ArgPack::MMA_MODE_OFF && ArgPack::MODE_TRACE_AND_ESTIMATE == args.mode) {
		errs() << "When mma mode != \"off\", Lina mode must be either \"trace\" or \"estimation\"\n";
		exit(-1);
	}

	if(args.fVec && args.mmaMode != ArgPack::MMA_MODE_OFF && !(args.fBurstAggr)) {
		errs() << "\"--f-burstaggr\" is required for \"--f-vec\" to work\n";
		exit(-1);
	}

	VERBOSE_PRINT(
		errs() << "Input bitcode file: " << InputFilename << "\n";
		errs() << "Kernel name: " << args.kernelNames[0] << "\n";
		errs() << "Input working directory: " << args.workDir << "\n";
		errs() << "Output working directory: " << args.outWorkDir << "\n";
		errs() << "Configuration file: " << args.configFileName << "\n";
		errs() << "Mode: ";
		switch(args.mode) {
			case ArgPack::MODE_TRACE_ONLY:
				errs() << "dynamic trace only\n";
				break;
			case ArgPack::MODE_ESTIMATE_ONLY:
				errs() << "cycle estimation only\n";
				break;
			default:
				errs() << "dynamic trace and cycle estimation\n";
				break;
		}
		errs() << "Target: ";
		switch(args.target) {
			case ArgPack::TARGET_XILINX_VC707:
				errs() << "Xilinx Virtex-7 FPGA\n";
				break;
			case ArgPack::TARGET_XILINX_ZCU102:
				errs() << "Xilinx Zynq UltraScale+ SoC (ZCU102)\n";
				break;
			case ArgPack::TARGET_XILINX_ZCU104:
				errs() << "Xilinx Zynq UltraScale+ SoC (ZCU104)\n";
				break;
			case ArgPack::TARGET_XILINX_ZC702:
			default:
				errs() << "Xilinx Zynq-7000 SoC\n";
				break;
		}
		errs() << "Target clock: " << std::to_string(args.frequency) << ((args.fNoTCS)? " MHz (disabled)\n" : " MHz\n");
		errs() << "Clock uncertainty: " << std::to_string(args.uncertainty) << ((args.fNoTCS)? " % (disabled)\n" : " %\n");
		errs() << "Target clock period: " << std::to_string(1000 / args.frequency) << ((args.fNoTCS)? " ns (disabled)\n" : " ns\n");
		errs() << "Effective clock period: " << std::to_string((1000 / args.frequency) - (10 * args.uncertainty / args.frequency)) << ((args.fNoTCS)? " ns (disabled)\n" : " ns\n");
		errs() << "Target loops: " << args.targetLoops[0];
		for(unsigned int i = 1; i < args.targetLoops.size(); i++)
			errs() << ", " << args.targetLoops[i];
		errs() << "\n";
		errs() << "Memory model analysis for offchip transactions: " << (args.fNoMMA? "disabled" : "enabled") << "\n";
		if(!(args.fNoMMA)) {
			if(args.fBurstAggr) {
				errs() << "Burst aggregation active\n";
				if(args.fBurstMix)
					errs() << "Burst mix active (will be deactivated if global parameter \"ddrbanking\" is set)\n";
			}
			if(args.fVec) {
				if(ArgPack::MMA_MODE_OFF == args.mmaMode)
					errs() << "Array vectorisation analysis disabled due to \"--mma-mode=off\"\n";
				else
					errs() << "Array vectorisation analysis enabled\n";
			}
			errs() << "DDR scheduling policy: ";
			switch(args.ddrSched) {
				case ArgPack::DDR_POLICY_CANNOT_OVERLAP:
					errs() << "conservative\n";
					break;
				case ArgPack::DDR_POLICY_CAN_OVERLAP:
					errs() << "permissive\n";
					break;
			}
			errs() << "MMA mode: ";
			switch(args.mmaMode) {
				case ArgPack::MMA_MODE_OFF:
					errs() << "normal Lina execution\n";
					break;
				case ArgPack::MMA_MODE_GEN:
					errs() << "generate DDDG, run memory-model analysis and halt\n";
					break;
				case ArgPack::MMA_MODE_USE:
					errs() << "import DDDG and other info. from context-import instead of generating\n";
					break;
			}
		}
	);

	// Loading kernel names into kernel_names vector
	DEBUG(dbgs() << "We only focus on the kernel for this application: \n");
	DEBUG(dbgs() << "\tKernel name: " << args.kernelNames[0] << "\n");

	DEBUG(dbgs() << "Please make sure all functions within a kernel function are included.");
	DEBUG(dbgs() << "We also need to consider these functions. Otherwise, the tool will ");
	DEBUG(dbgs() << "ignore all these functions and cause problems!\n");
}
