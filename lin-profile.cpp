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

#define DEBUG_TYPE "lin-analyzer"

using namespace llvm;

const std::string helpMessage =
	"Lin-analyzer: A High Level Analysis Tool for FPGA Accelerators\n"
	"Usage: lin-analyzer [OPTION]... BYTECODEFILE KERNELNAME\n"
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
	"                                            VC707 : Xilinx Virtex-7 FPGA\n"
	"        -v       , --verbose          : be verbose, print a lot of information\n"
	"        -x       , --compressed       : use compressed files to reduce memory footprint\n"
#ifdef PROGRESSIVE_TRACE_CURSOR
	"        -p       , --progressive      : use progressive trace cursor when trace is\n"
	"                                        analysed, reducing estimation time when several\n"
	"                                        top loops are analysed. Loops defined with\n"
	"                                        -l|--loops flag must be in crescent order\n"
#endif
	"        -f FREQ  , --frequency=FREQ   : specify the target clock (in MHz)\n"
	"        -u UNCTY , --uncertainty=UNCTY: specify the clock uncertainty (in %)\n"
	"        -l LOOPS , --loops=LOOPS      : specify loops to be analysed comma-separated (e.g.\n"
	"                                        --loops=2,3 only analyse loops 2 and 3)\n"
	"                   --mem-trace        : obtain memory trace for access pattern analysis.\n"
	"                                        Ignored if -m estimation | --mode=estimation is\n"
	"                                        set.\n"
	"                                        Can only be used with -m trace | --mode=trace\n"
	"                   --show-cfg         : show CFG with basic blocks\n"
	"                   --show-detail-cfg  : show detailed CFG with instructions\n"
	"                   --show-pre-dddg    : show DDDG before optimisation\n"
	"                   --show-post-dddg   : show DDDG after optimisation\n"
	"                   --fno-tcs          : disable timing-constrained scheduling\n"
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
	"Report bugs to abperina<abperina@usp.br>\n"
	"or guanwen<guanwen@comp.nus.edu.sg>\n";

ArgPack args;
#ifdef PROGRESSIVE_TRACE_CURSOR
long int progressiveTraceCursor = 0;
uint64_t progressiveTraceInstCount = 0;
#endif

int main(int argc, char **argv) {
	parseInputArguments(argc, argv);

	errs() << "********************************************************\n";
	errs() << "********************************************************\n\n";
	errs() << "     Lin-analyzer: An FPGA High-Level Analysis Tool\n\n";
	errs() << "********************************************************\n";
	errs() << "********************************************************\n";

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
	args.frequency = 100.0;
	args.uncertainty = 27;
	args.verbose = false;
	args.memTrace = false;
	args.showCFG = false;
	args.showCFGDetailed = false;
	args.showPreOptDDDG = false;
	args.showPostOptDDDG = false;
	args.fNoTCS = false;
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
	args.fILL = false;

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
			{"frequency", required_argument, 0, 'f'},
			{"uncertainty", required_argument, 0, 'u'},
			{"loops", required_argument, 0, 'l'},
			{"mem-trace", no_argument, 0, 0xF00},
			{"show-cfg", no_argument, 0, 0xF01},
			{"show-detail-cfg", no_argument, 0, 0xF02},
			{"show-pre-dddg", no_argument, 0, 0xF03},
			{"show-post-dddg", no_argument, 0, 0xF04},
			{"fno-tcs", no_argument, 0, 0xF05},
			{"fno-sb", no_argument, 0, 0xF06},
			{"f-slr", no_argument, 0, 0xF07},
			{"fno-slr", no_argument, 0, 0xF08},
			{"fno-rsr", no_argument, 0, 0xF09},
			{"f-thr-float", no_argument, 0, 0xF0A},
			{"f-thr-int", no_argument, 0, 0xF0B},
			{"f-md", no_argument, 0, 0xF0C},
			{"fno-ft", no_argument, 0, 0xF0D},
			{"f-es", no_argument, 0, 0xF0E},
			{"f-rwrwm", no_argument, 0, 0xF0F},
			{0, 0, 0, 0}
		};
		int optionIndex = 0;

#ifdef PROGRESSIVE_TRACE_CURSOR
		c = getopt_long(argc, argv, "+hi:o:c:m:t:vxpf:u:l:", longOptions, &optionIndex);
#else
		c = getopt_long(argc, argv, "+hi:o:c:m:t:vxf:u:l:", longOptions, &optionIndex);
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
				args.fNoTCS = true;
				break;
			case 0xF06:
				args.fSBOpt = false;
				break;
			case 0xF07:
				args.fSLROpt = true;
				break;
			case 0xF08:
				args.fNoSLROpt = true;
				break;
			case 0xF09:
				args.fRSROpt = false;
				break;
			case 0xF0A:
				args.fTHRFloatOpt = true;
				break;
			case 0xF0B:
				args.fTHRIntOpt = true;
				break;
			case 0xF0C:
				args.fMemDisambuigOpt = true;
				break;
			case 0xF0D:
				args.fNoFPUThresOpt = true;
				break;
			case 0xF0E:
				args.fExtraScalar = true;
				break;
			case 0xF0F:
				args.fRWRWMem = true;
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

	if(args.uncertainty < 0.0 || args.uncertainty > 100.0) {
		errs() << "Uncertainty must be between 0.0 and 100.0 %\n";
		exit(-1);
	}
	if(args.frequency < 0.0) {
		errs() << "Target frequency must be positive\n";
		exit(-1);
	}
	if(args.frequency > 500.0) {
		errs() << "Lin-analyzer does not support estimation with target frequency above 500 Mhz\n";
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
				errs() << "Xilinx Zynq UltraScale+ SoC\n";
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
	);

	// Loading kernel names into kernel_names vector
	DEBUG(dbgs() << "We only focus on the kernel for this application: \n");
	DEBUG(dbgs() << "\tKernel name: " << args.kernelNames[0] << "\n");

	DEBUG(dbgs() << "Please make sure all functions within a kernel function are included.");
	DEBUG(dbgs() << "We also need to consider these functions. Otherwise, the tool will ");
	DEBUG(dbgs() << "ignore all these functions and cause problems!\n");
}
