# Lina (Mark 2)

(yet another) High Level Analysis Tool for FPGA Accelerators

**MARK 2**


## Introduction

Lina is a pre-HLS performance estimator for C/C++ codes targetting Xilinx FPGAs with Vivado HLS or its variants. Given a C/C++ code, it generates the Dynamic Data Dependence Graph (DDDG) and schedules its nodes to estimate the performance (cycle count) of the code for the Vivado HLS toolchain. Several optimisation directives (e.g. loop unroll, pipelining) can be provided and Lina estimates the performance according to those constraints. Lina enables fast design space exploration by providing results in significantly less time than FPGA synthesis or even the HLS compilation process.

Lina is an expansion of the Lin-Analyzer project (see https://github.com/zhguanw/lin-analyzer). It uses the same traced execution and (pretty much the same) DDDG scheduling.

Lina currently has two big checkpoints, Mark 1 and Mark 2. See [Versions](#versions) for an explanation of differences.


## Table of Contents

1. [Versions](#versions)
1. [Licence](#licence)
1. [Publications](#publications)
1. [Setup](#setup)
1. [Compilation](#compilation)
	1. [Automatic Compilation](#automatic-compilation)
	1. [Manual Compilation](#manual-compilation)
1. [Differences Between Mark 1 and 2](#differences-between-mark-1-and-2)
	1. [Command-line Arguments](#command-line-arguments)
1. [Usage](#usage)
	1. [Use by Example](#use-by-example)
	1. [Configuration File](#configuration-file)
	1. [Enabling Design Space Exploration](#enabling-design-space-exploration)
		1. [Trace Cache](#trace-cache)
1. [Perform an Exploration](#perform-an-exploration)
	1. [The Script](#the-script)
	1. [Setting Up a New Exploration](#setting-up-a-new-exploration)
	1. [JSON Description File](#json-description-file)
1. [Supported Platforms](#supported-platforms)
	1. [Adding a New Platform](#adding-a-new-platform)
1. [Files Description](#files-description)
1. [Troubleshooting](#troubleshooting)
	1. ["is private within this context"](#is-private-within-this-context)
	1. [Problems with Parallel Compilation](#problems-with-parallel-compilation)
	1. [subprocess.CalledProcessError: Command ... returned non-zero exit status 2](#subprocesscalledprocesserror-command--returned-non-zero-exit-status-2)
	1. [Lina's bundled clang fails to find basic headers (e.g. "fatal error: 'cstdlib' file not found")](#linas-bundled-clang-fails-to-find-basic-headers-eg-fatal-error-cstdlib-file-not-found)
1. [Acknowledgments](#acknowledgments)


## Versions

### Mark 1

Mark 1 is the first consistent version of Lina. Results with this version were published on the IEEE Transactions on Computers paper of 2021 (see [Publications](#publications)).

This version is available on a separate repository: https://github.com/comododragon/lina.

This version includes:

* A Timing-Constrained Scheduler (TCS) that supports a continuous interval of operating clock frequencies;
* A Non-Perfect Loop Analysis (NPLA), which allows a finer estimation of non-perfect loop nests by generating DDDGs for different segments of the loop nest;
* A comprehensive resource estimation that considers the usage of integer FUs, floating-point FUs, supporting computations (e.g. loop/array indexing), intermediate registers, multiplexing and memory related resources (e.g. distributed RAM or completetly partitioned arrays).

### Mark 2

Mark 2 includes all the features from Mark 1, but it also includes the following features:

* A **memory model** that is able to estimate the latency of off-chip transactions, including common memory patterns or features such as coalescing, data packing and memory banking;
* A new **caching logic** that reduces the amount of I/O accesses during parallel exploration. That allows multiple parallel exploration threads to be executed with significantly less I/O bottlenecking.
	* Feature available in the `cachedaemon` git branch of this repository.


## Licence

Lina is distributed under the GPL-3.0 licence, so as Lin-Analyzer.

According to Lin-Analyzer repository: *"Dynamic data graph generation (DDDG) and few of optimization functions in Lin-Analyzer are modified from Aladdin project and Lin-Analyzer is also followed Aladdin's license. For more information, please visit Aladdin's website: https://github.com/ysshao/ALADDIN"*.

The ```common.h``` header is distributed under the GPL-3.0 licence. See the following repo for details: https://github.com/comododragon/commonh

PolyBench/C kernels are used in ```misc/smalldse``` and ```misc/largedse```, which follows the GPL-2.0 licence. Please see: https://sourceforge.net/projects/polybench/


## Publications

Refer to our paper for a detailed description of the contributions presented in Lina (also if you use Lina in your research, please kindly cite this paper):

* A. Bannwart Perina, A. Silitonga, J. Becker and V. Bonato, "Fast Resource and Timing Aware Design Optimisation for High-Level Synthesis," in IEEE Transactions on Computers, doi: [10.1109/TC.2021.3112260](https://doi.org/10.1109/TC.2021.3112260).

A much simpler validation with an early version of Lina was presented in our FPT-2019 paper (see [Versions](#versions) for more information):

* A. Bannwart Perina, J. Becker and V. Bonato, "Lina: Timing-Constrained High-Level Synthesis Performance Estimator for Fast DSE," 2019 International Conference on Field-Programmable Technology (ICFPT), 2019, pp. 343-346, doi: [10.1109/ICFPT47387.2019.00063](https://doi.org/10.1109/ICFPT47387.2019.00063).

Several parts of Lina were inherited from Lin-analyzer. For more information about Lin-analyzer, please see:

* G. Zhong, A. Prakash, Y. Liang, T. Mitra and S. Niar, "Lin-Analyzer: A high-level performance analysis tool for FPGA-based accelerators," 2016 53nd ACM/EDAC/IEEE Design Automation Conference (DAC), 2016, pp. 1-6, doi: [10.1145/2897937.2898040](https://doi.org/10.1145/2897937.2898040).


## Setup

Lina makes use of the LLVM Compiler Framework 3.5.0. In order to use Lina, it must be compiled together with ```llvm``` and ```clang```.

Compilation of Lina was tested in the following systems:
* Arch Linux with Linux kernel 5.2.0-arch2-1-ARCH;
* Ubuntu 18.04 LTS, clean install.

Before proceeding to compilation, you must ensure that the following packages are installed:

* GNU Compiler Collection. For Ubuntu, run:
	```
	$ sudo apt-get install build-essential
	```
* ZLIB development libraries. For Ubuntu, run:
	```
	$ sudo apt-get install zlib1g-dev
	```
* GIT. For Ubuntu, run:
	```
	$ sudo apt-get install git
	```
* CMAKE. For Ubuntu, run:
	```
	$ sudo apt-get install cmake
	```
*You can also compile LLVM using other toolchains, such as ```clang```. Please see https://releases.llvm.org/3.5.0/docs/GettingStarted.html*

**We assume here that ZLIB is located at /usr/lib.libz.so.** Note that certain distributions (e.g. Ubuntu) might place it in a different path. You can check that by listing the file:
```
$ ls /usr/lib/libz.so
```

If ```no such file or directory``` appears, there are a couple of options:
* If manually compiling Lina, modify the value passed to the ```-DZLIB_LIBRARY=``` flag of the ```cmake``` command;
* If using the compiler script, a notification will pop up if ZLIB is not found and you will have the chance to set a custom path;
* You can (re-)create the soft link pointing to the correct library (**this is not a recommended approach**). In Ubuntu 18.04 for example, it is located at ```/lib/x86_64-linux-gnu/libz.so.1```. In this case the soft-link should be created as:
	```
	$ cd /usr/lib
	$ ln -s /lib/x86_64-linux-gnu/libz.so.1 libz.so
	```

### Linad

If the `cachedaemon` version is selected, you must also compile the Lina Daemon (linad). Along the packages cited above, you will also need:
* `librt`
* `libpthread`


## Compilation

You can either compile Lina by using the automated compiling script, or manually by following the instructions presented in [Manual Compilation](#manual-compilation).

### Automatic Compilation

We have provided an automated BASH compilation script at ```misc/compiler.sh```. It simply follows the instructions from **Manual Compilation** and applies patches automatically if necessary.

To use automatic compilation:

1. Make sure you read and understood the [Setup](#setup) (in other words, make sure that you have ```gcc```, ```zlib```, ```git``` and ```cmake```);
2. Download the automated compilation script at ```misc/compiler.sh```(https://raw.githubusercontent.com/comododragon/linaii/master/misc/compiler.sh);
3. Create a folder where you wish to compile everything (for example purposes, we will refer this path as ```/path/to/lina```) and place the compiler script inside;
4. Give execution permission to ```compiler.sh```:
	```
	$ chmod +x compiler.sh
	```
5. Execute the script:
	```
	$ ./compiler.sh
	```
6. Follow the instructions on-screen;
	* The script will download LLVM, CLANG, BOOST and Lina, prepare the folders and execute ```cmake```;
	* It will ask before compiling if you want to apply some patches. Please read [Troubleshooting](#troubleshooting) for better understanding. In doubt, you can refuse the patches. If compilation fails, you will have another chance to apply the patches;
	* **Every time the script is executed, the whole operation is re-executed (i.e. no incremental compilation with the script!);**
	* Right after ```cmake``` and before starting the whole compilation process, the script will give you the option to abort the script and leave the project as is. At this point you will have the project ready to be compiled, where you can insert your modifications or fix some system-related problems regarding dependencies. Then, simply follow [Manual Compilation](#manual-compilation) from step **10**;
	* If everything goes right, you should have the ```lina``` binary at ```/path/to/lina/build/bin/lina```;
	* If using the `cachedaemon` version, ```linad``` will be built at ```/path/to/lina/llvm/tools/lina/misc/linad/linad```.

### Manual Compilation

The automatic compilation script simply executes the following steps:

1. Make sure you read and understood the [Setup](#setup) (in other words, make sure that you have ```gcc```, ```zlib```, ```git``` and ```cmake```);
2. Create your compilation folder and ```cd``` to it:
	```
	$ mkdir /path/to/lina
	$ cd /path/to/lina
	```
3. Download LLVM 3.5.0 and extract to folder ```llvm```:
	```
	$ wget "http://releases.llvm.org/3.5.0/llvm-3.5.0.src.tar.xz"
	$ tar xvf llvm-3.5.0.src.tar.xz
	$ mv llvm-3.5.0.src llvm
	```
4. Download CLANG 3.5.0 and extract to folder ```llvm/tools/clang```:
	```
	$ wget "http://releases.llvm.org/3.5.0/cfe-3.5.0.src.tar.xz"
	$ tar xvf cfe-3.5.0.src.tar.xz
	$ mv cfe-3.5.0.src llvm/tools/clang
	```
5. Clone Lina Mark 2 to folder ```llvm/tools/lina```:
	```
	$ git clone https://github.com/comododragon/linaii.git
	$ mv linaii llvm/tools/lina
	```
	* **NOTE:** For `cachedaemon` finish this step by running: ```cd llvm/tools/lina; git checkout cachedaemon``` (see [Versions](#versions) for more information);
6. Add the line ```add_llvm_tool_subdirectory(lina)``` right before ```add_llvm_tool_subdirectory(opt)``` at file ```llvm/tools/CMakeLists.txt```. It should look something like:
	```
	...
	else(WITH_POLLY)
	  list(APPEND LLVM_IMPLICIT_PROJECT_IGNORE "${LLVM_MAIN_SRC_DIR}/tools/polly")
	endif(WITH_POLLY)

	add_llvm_tool_subdirectory(lina)
	add_llvm_tool_subdirectory(opt)
	add_llvm_tool_subdirectory(llvm-as)
	...
	```
7. Add the line ```set(LLVM_REQUIRES_RTTI 1)``` at the end of file ```llvm/CMakeLists.txt```;
8. Download BOOST and extract to folder ```boost```:
	```
	$ wget "http://sourceforge.net/projects/boost/files/boost/1.57.0/boost_1_57_0.tar.gz"
	$ tar xvf boost_1_57_0.tar.gz
	$ mv boost_1_57_0 boost
	```
9. Create build folder and run ```cmake```:
	```
	$ mkdir build
	$ cd build
	$ cmake ../llvm -DBOOST_INCLUDE_DIR=../boost -DZLIB_INCLUDE_DIRS=/usr/include -DZLIB_LIBRARY=/usr/lib/libz.so -DLLVM_ENABLE_RTTI:BOOL=ON
	```
10. Now you can finally make:
	```
	$ make
	```
11. If everything goes well, you will have the Lina binary at ```path/to/lina/build/bin```. If not, please see [Troubleshooting](#troubleshooting).

#### Linad

If using `cachedaemon`, the Lina Daemon must also be built. After running all steps above:

1. Cd to the `linad` folder:
	```
	$ cd /path/to/lina/llvm/tools/lina/misc/linad
	```
2. Make:
	```
	$ make
	```
3. If build went well, ```linad``` will be present at ```/path/to/lina/llvm/tools/lina/misc/linad/linad```.


## Differences between Mark 1 and 2

*TODO: this section is still being populated*

Basic usage of Lina is explained in detail on the Mark 1 README. See [here](https://github.com/comododragon/lina/blob/master/README.md).

This README will focus on the differences between Mark 1 and 2.

### Command-line arguments

This version of Lina includes the arguments in Mark 1, along these additional:

* ```--mem-trace```: depends on mode `-m`:
	* if `trace`, a textual memory trace is generated with name `mem_trace.txt`;
	* if `estimation`, the file `mem_trace.txt` is loaded to the off-chip memory model;
* ```--short-mem-trace```: depends on mode `-m`:
	* if `trace`, a binary memory trace is generated with name `mem_trace_short.bin`;
	* if `estimation`, the file `mem_trace_short.bin` is loaded to the off-chip memory model;
	* *It is recommended to use this argument instead of* `--mem-trace`, *since the binary trace is often more efficient to be parsed*;
* ```--fno-mma```: disable off-chip memory model analysis **(DEFAULT IS ENABLED)**;
* ```--f-burstaggr```: enable burst aggregation: sequential off-chip operations inside a DDDG are grouped together to form coalesced bursts;
	* *Only groups operations from same array*;
	* *This argument has no effect if* `--fno-mma` *is set*;
* ```--f-burstmix```: enable burst aggregation: coalesced bursts can be mixed between arrays;
	* *This argument has no effect if* `--fno-mma` *is set*;
	* *This argument requires* `--f-burstaggr`;
	* *This argument has no effect if global parameter* `ddrbanking` *is set*;
	* *This feature is almost in deprecated state*;
* ```--f-vec```: enable array vectorisation analysis, which tries to find a suitable SIMD type for the off-chip arrays;
	* *This argument has no effect if* `--fno-mma` *is set*;
	* *This argument requires* `--f-burstaggr`;
	* *This argument has no effect if* `--mma-mode` *is* `off`;
* ```-d LEVEL``` or ```--ddrsched=LEVEL```: specify the DDR scheduling policy:
	* ```0```: DDR transactions of same type (read/write) cannot overlap (i.e. once a transaction starts, it must end before others of same type can start **(DEFAULT)**;
	* ```1```: DDR transactions can overlap if their memory spaces are disjoint;
	* See [DDR Scheduling Policies](#ddr-scheduling-policies);
* ```--mma-mode=MODE```: select Lina execution model according to MMA mode:
	* ```off```: run Lina normally **(DEFAULT)**;
	* ```gen```: perform memory model analysis, generate a context import file and stop execution;
	* ```use```: skip profiling and DDDG generation, proceed directly to memory model analysis;
		* In this mode context import file is used to generate the DDDG and other data;
		* The context-import should have been generated with a previous execution of Lina with ```--mma-mode=gen```, otherwise Lina fails;
	* *This argument has no effect if* `--mode` *is* `trace`;
	* See [Context-mode Execution](#context-mode-execution);







*TODO: README is still being populated from this point onwards*

## Usage

After compiled, you can install Lina by using ```make install``` or you can simply put Lina somewhere and update ```PATH```:
```
$ export PATH=/path/to/lina/build/bin:$PATH
```

*Please note that ```/path/to/lina/build/bin``` has also the LLVM and CLANG binaries. Exporting this ```PATH``` will override your default LLVM/CLANG configuration.*

You should now be able to use the ```lina``` executable. You can test by requesting the help text:
```
$ lina -h
```

Calling ```lina -h``` will show you the help, which is pretty self-explanatory. The most important flags are:

* ```-h```: to show you the pretty help text;
* ```-c FILE``` or ```--config-file=FILE```: use ```FILE``` as the configuration file. See [Configuration File](#configuration-file);
* ```-m MODE``` or ```--mode=MODE```: set ```MODE``` as the execution mode of Lina. Three values are possible:
	* ```all```: execute traced execution and performance estimation;
	* ```trace```: execute only traced execution, generating the dynamic trace. Lina hangs before performance estimation;
	* ```estimation```: execute only performance estimation. The dynamic trace must be already generated;
* ```-t TARGET``` or ```--target=TARGET```: select the FPGA to perform cycle estimation:
	* ```ZC702```: Xilinx Zynq-7000 SoC (DEFAULT);
	* ```ZCU102```: Xilinx Zynq UltraScale+ ZCU102 kit;
	* ```ZCU104```: Xilinx Zynq UltraScale+ ZCU104 kit;
	* ```VC707```: Xilinx Virtex-7 FPGA;
* ```-v``` or ```--verbose```: show more details about the estimation process and the results;
* ```-C``` or ```--future-cache```: use cache file to save trace cursors and speed up further executions of Lina (see **Enabling Design Space Exploration**);
* ```-f FREQ``` or ```--frequency=FREQ```: specify the target clock, in MHz;
* ```-u UNCTY``` or ```--uncertainty=UNCTY```: specify the clock uncertainty, in percentage;
* ```-l LOOPS``` or ```--loops=LOOPS```: specify which top-level loops should be analysed, starting from 0;
* ```--f-npla```: activate non-perfect loop analysis (disabled by default);
* ```--f-notcs```: deactivate timing-constrained scheduling (enabled by default);
* ```--f-argres```: make Lina count BRAM usage of kernel arguments, which is by default disabled (see **Configuration File** for information on how arrays are described for Lina).

### Use by Example

Let's estimate the cycle count and resources for a simple matrix vector and product:
```
#define SIZE 32

void mvp(float A[SIZE * SIZE], float x[SIZE], float y[SIZE]) {
	for(int i = 0; i < SIZE; i++)
		for(int j = 0; j < SIZE; j++)
			x[i] += A[i * SIZE + j] * y[j];
}

int main(void) {
	float A[SIZE * SIZE], x[SIZE], y[SIZE];

	for(int j = 0; j < SIZE; j++) {
		y[j] = j;

		for(int i = 0; i < SIZE; i++)
			A[i * SIZE + j] = i + j;
	}

	mvp(A, x, y);

	return 0;
}
```

1. First, make sure you are using LLVM 3.5.0 binaries, as other versions may have incompatible intermediate representations. You can find the 3.5.0 binaries together with the compilation of Lina:
	```
	$ export PATH=/path/to/lina/build/bin:$PATH
	```
2. Create a folder for your Lina project, create a file ```test.cpp``` and insert the contents of the code above;
3. Now we create a configuration file for this project, which will inform Lina the size of each array and also optimisation directives. Let's create a file ```config.cfg``` and enable partial loop unrolling with factor 4 on both loops (please see [Configuration File](#configuration-file) for more information about this file):
	```
	array,A,4096,4
	array,x,128,4
	array,y,128,4
	unrolling,mvp,0,1,4,4
	unrolling,mvp,0,2,5,4
	```
4. We must compile ```test.cpp``` to the LLVM intermediate representation and also apply some optimisations before using Lina:
	```
	$ clang -O1 -emit-llvm -c test.cpp -o test.bc
	$ opt -mem2reg -instnamer -lcssa -indvars test.bc -o test_opt.bc
	```
5. Then, we can run Lina. You must pass the ```test_opt.bc``` file and the kernel name ```mvp``` as the two last positional arguments of Lina:
	```
	lina --config-file=config.cfg --target=ZCU104 --loops=0 test_opt.bc mvp
	```
6. Lina should give you a report of the cycle counts, similar to:
	```
	░░░░░░░░░░░░░░░░░░░░░░░░░░░░▒▒
	░░▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▒▒
	░░▓░░▓▓▓▓░░▓▓░░▓▓░░▓▓▓░░░░▓▓▒▒
	░░▓░░▓▓▓▓░░▓▓░░░▓░░▓▓░░▓▓░░▓▒▒
	░░▓░░▓▓▓▓░░▓▓░░░░░░▓▓░░░░░░▓▒▒
	░░▓░░▓▓▓▓░░▓▓░░▓░░░▓▓░░▓▓░░▓▒▒
	░░▓░░░░▓▓░░▓▓░░▓▓░░▓▓░░▓▓░░▓▒▒
	░░▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▒▒
	▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒
	========================================================
	Building mangled-demangled function name map
	========================================================
	Counting number of top-level loops in "mvp"
	========================================================
	Assigning IDs to BBs, acquiring array names
	========================================================
	Assigning IDs to load and store instructions
	========================================================
	Extracting loops information
	========================================================
	Starting code instrumentation for DDDG generation
	********************************************************
	********************************************************
	************** See ya in a minute or so! ***************
	********************************************************
	********************************************************
	********************* Hello back! **********************
	********************************************************
	********************************************************
	[][][_Z3mvpPfS_S__loop-0_2] Estimated cycles: 5672
	```
7. A more detailed report (including resources) can be found in the log file ```mvp_summary.log```:
	```
	================================================
	Lina summary
	================================================
	Function name: mvp
	================================================
	Target clock: 100.000000 MHz
	Clock uncertainty: 27.000000 %
	Target clock period: 10.000000 ns
	Effective clock period: 7.300000 ns
	Achieved clock period: 7.010000 ns
	Loop name: _Z3mvpPfS_S__loop-0
	Loop level: 2
	DDDG type: full loop body
	Loop unrolling factor: 4
	Loop pipelining enabled? no
	Total cycles: 5672
	------------------------------------------------
	Number of repeated stores detected: 3
	------------------------------------------------
	Ideal iteration latency (ASAP): 21
	Constrained iteration latency: 22
	Initiation interval (if applicable): 2
	resII (mem): 2
	resII (op): 1
	recII: 1
	Limited by memory, array name: A
	------------------------------------------------
	Units limited by DSP usage: none
	------------------------------------------------
	DSPs: 5
	FFs: 2215
	LUTs: 1486
	BRAM18k: 0
	fAdd units: 1
	fSub units: 0
	fMul units: 1
	fDiv units: 0
	add units: 0
	sub units: 0
	mul units: 0
	udiv units: 0
	sdiv units: 0
	shl units: 0
	lshr units: 0
	ashr units: 0
	and units: 0
	or units: 0
	xor units: 0
	Number of partitions for array "A": 1
	Number of partitions for array "x": 1
	Number of partitions for array "y": 1
	Memory efficiency for array "A": 0.000000
	Memory efficiency for array "x": 0.000000
	Memory efficiency for array "y": 0.000000
	Used BRAM18k for array "A": 0
	Used BRAM18k for array "x": 0
	Used BRAM18k for array "y": 0
	================================================
	```
	* **NOTE:** No BRAM usage was reported, since arguments are by default not resource-counted (so as Vivado HLS does). To enable resource count for arguments, please either set your array to be ```rwvar``` or ```rovar``` in the ```config.cfg``` file **OR** use ```--f-argres```;
	* **NOTE2:** Lina will output resource estimates to console when running with ```-v```. **Please disregard these values and prefer to use the values shown in the summary file as above.**

We can compare the results against the reports from Vivado. In this case, we used Vivado HLS 2018.2 for the ZCU104 Xilinx UltraScale+ Board.

1. We have to modify ```test.cpp``` to insert the unrolling pragmas for Vivado:
	```
	#define SIZE 32

	void mvp(float A[SIZE * SIZE], float x[SIZE], float y[SIZE]) {
		for(int i = 0; i < SIZE; i++) {
	#pragma HLS unroll factor=4
			for(int j = 0; j < SIZE; j++) {
	#pragma HLS unroll factor=4
				x[i] += A[i * SIZE + j] * y[j];
			}
		}
	}

	int main(void) {
		float A[SIZE * SIZE], x[SIZE], y[SIZE];

		for(int j = 0; j < SIZE; j++) {
			y[j] = j;

			for(int i = 0; i < SIZE; i++)
				A[i * SIZE + j] = i + j;
		}

		mvp(A, x, y);

		return 0;
	}
	```
2. Then, we can create a script ```script.tcl``` for Vivado HLS with the same configurations as Lina:
	```
	open_project mvp
	set_top mvp
	add_files test.cpp
	open_solution "solution1"
	set_part {xczu7ev-ffvc1156-2-e}
	create_clock -period 10 -name default
	set_clock_uncertainty 27.0%
	csynth_design
	exit
	```
3. Run Vivado and wait for the HLS process to finish:
	```
	$ vivado_hls -f script.tcl
	```
4. You can find at ```mvp/solution1/syn/report/mvp_csynth.rpt``` the cycle count for the loop:
	```
	+-------------+------+------+----------+-----------+-----------+------+----------+
	|             |   Latency   | Iteration|  Initiation Interval  | Trip |          |
	|  Loop Name  |  min |  max |  Latency |  achieved |   target  | Count| Pipelined|
	+-------------+------+------+----------+-----------+-----------+------+----------+
	|- Loop 1     |  5672|  5672|       709|          -|          -|     8|    no    |
	+-------------+------+------+----------+-----------+-----------+------+----------+
	```
5. A resource count estimate is available in the same file:
	```
	+-----------------+---------+-------+--------+--------+-----+
	|       Name      | BRAM_18K| DSP48E|   FF   |   LUT  | URAM|
	+-----------------+---------+-------+--------+--------+-----+
	|DSP              |        -|      -|       -|       -|    -|
	|Expression       |        -|      -|       0|     239|    -|
	|FIFO             |        -|      -|       -|       -|    -|
	|Instance         |        -|      5|     355|     349|    -|
	|Memory           |        -|      -|       -|       -|    -|
	|Multiplexer      |        -|      -|       -|     654|    -|
	|Register         |        -|      -|     368|       -|    -|
	+-----------------+---------+-------+--------+--------+-----+
	|Total            |        0|      5|     723|    1242|    0|
	+-----------------+---------+-------+--------+--------+-----+
	|Available        |      624|   1728|  460800|  230400|   96|
	+-----------------+---------+-------+--------+--------+-----+
	|Utilization (%)  |        0|   ~0  |   ~0   |   ~0   |    0|
	+-----------------+---------+-------+--------+--------+-----+
	```
	* ***NOTE:*** These are Vivado HLS pre-synthesis estimates for resource. For a final resource count, the kernel must be fully synthesised.

### Configuration File

Lina uses a configuration file to inform array information and optimisation directives. This file is only necessary to the estimation mode of Lina. The configuration file is parsed line by line. See the following configuration file as an example of supported directives:

```
# Any line starting with a "#" is ignored by the parser
array,A,4096,4,arg
unrolling,mvp,0,1,4,4
pipeline,mvp,0,2
partition,block,A,4096,4,8
```

Now line by line:

* ```# Any line starting with a "#" is ignored by the parser```
	* Pretty self-explanatory;
* ```array,A,4096,4,arg```
	* Declare an array. The arguments are the array name, total size in bytes, word size in bytes and an optional scope indicator;
	* Equivalent to ```uint32_t A[1024];```
	* All arrays of the kernel must be declared here;
	* The last argument indicates whether and how Lina should count the resources for the array:
		* ```arg```: array is considered an argument (DEFAULT if omitted). No resource count is performed for this array unless Lina is executed with ```--f-argres```;
		* ```rovar```: array is considered a read-only memory block and Lina will consider its resource count;
		* ```rwvar```: array is considered a read-write memory block and Lina will consider its resource count;
		* ```nocount```: force this array to be never counted, regardless of ```--f-argres``` being present or not;
* ```unrolling,mvp,0,1,4,4```
	* Set unroll. The arguments are the kernel name, the top-level loop ID (starts from 0), the loop depth (1 is the top-level), the loop header line number and the unroll factor;
	* Equivalent to ```#pragma HLS unroll factor=4```
* ```pipeline,mvp,0,2```
	* Set pipeline. The arguments are the kernel name, the top-level loop ID and the loop depth;
	* Equivalent to ```#pragma HLS pipeline```
* ```partition,block,A,4096,4,8```
	* Set array partitioning. The arguments are the partitioning type, the array name, total size in bytes, word size in bytes and partition factor;
	* Types of partitioning include ```block```, ```cyclic``` or ```complete```;
	* If ```complete``` is used, you can omit the last argument (partition factor);
	* Equivalent to ```#pragma HLS array_partition variable=A block factor=8```

### Enabling Design Space Exploration

The idea of Lina is to provide fast estimations for design space exploration. Lina is divided in two stages: trace and estimation. The first is the most time-consuming, however it has to be performed only once. Therefore, general flow for DSE would be:

1. Follow the steps above to generate the ```*.bc``` file for input to Lina;
2. Perform traced execution:
	```
	$ lina --mode=trace test_opt.bc mvp
	```
3. Create a ```config.cfg``` with the optimisation configurations that you wish to test:
4. Perform estimation:
	```
	$ lina --mode=estimation --config-file=config.cfg --target=ZCU104 --loops=0 test_opt.bc mvp
	```
5. Repeat from step ***3***.

#### Trace Cache

At every run of Lina, a traversal is performed on the dynamic trace file to generate the DDDGs. This traversal can slow the exploration down significantly if for many design points a significant amount of instructions are traversed prior to DDDG generation. Lina implements a trace cursor cache that saves information about the trace cursor for future executions of Lina for the same exploration. To activate the cache, simply use ```-C```:

```
$ lina --mode=estimation --config-file=config.cfg --target=ZCU104 --loops=0 -C test_opt.bc mvp
```

When cache is active, Lina will search for a file named ```<WORKDIR>/futurecache.db```, where ```<WORKDIR>``` is the working directory as defined by the ```-i``` argument (DEFAULT to ```.```). If this file is found, Lina will use it as a trace cursor cache, and save further cache hits after execution. If the file is not found, Lina generates a new empty one. This cache can significantly improve the performance of Lina by reducing the amount of redundant computations.

**Please note that prior to execution of a new exploration involving different kernels or different parameters, the file should be deleted first.**

***This access is not thread-safe! For parallel executions of Lina, please use separate cache files (i.e. by soft-links).***

## Perform an Exploration

***NOTE: This section describes how to perform an exploration using a newer DSE infrastructure. To use the small DSE tools from the FPT-2019 paper, please see [this](https://github.com/comododragon/lina/blob/d85c4a49019027a41970b5e11aa14558951efe35/README.md#perform-a-small-exploration) section from the older README.md (https://github.com/comododragon/lina/blob/d85c4a49019027a41970b5e11aa14558951efe35/README.md).***

We made available in folder ```misc/largedse``` the DSE infrastructure that we used to validate Lina. The PolyBench/C kernels used are available in the folder ```misc/largedse/hls``` and ```misc/largedse/fullsyn```. The ```hls``` experiment holds kernels that are created to be compared against Vivado HLS results, whereas the ```fullsyn``` kernels are tailored to be compared against SDSoC results.

Some kernels were slightly modified in order to be compatible with Lina. See ```misc/largedse/CHANGES``` for more details (due to non-disclosure properties of the AES kernels, they are currently not available in this repository).

Lets perform an exploration on the ```bicg``` kernel from ```hls``` as an example:

1. Compile Lina following the instructions from [Setup](#setup) and [Compilation](#compilation);
	* We will refer to the path for this version as ```/path/to/lina/build/bin```;
2. You will also need Python 3. Please install it using your OS repository. For example in Ubuntu:
	```
	$ sudo apt-get install python3
	```
3. Generate the exploration workspace. This will create the folder ```workspace/hls/bicg``` where all design points are generated as separate Lina projects:
	```
	$ ./run.py PATH=/path/to/lina/build/bin generate hls bicg
	INFO: Option PATH set to /path/to/lina/build/bin
	===========================================================================
	Generating design points for bicg                                                                                                      
	100% ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓
	Done generating for bicg!                                                                                                              
	===========================================================================
	```
	* The LLVM bitcode files are compiled and stored at ```workspace/hls/bicg/base```;
	* The compilation stdout/stderr can be found at ```workspace/hls/bicg/base/make.out```;
4. Generate the trace file for this kernel:
	```
	$ ./run.py PATH=/path/to/lina/build/bin trace hls bicg   
	INFO: Option PATH set to /path/to/lina/build/bin
	===========================================================================
	Generating trace for bicg                                                                                                              
	100% ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓
	Done generating trace for bicg! Elapsed time: 1.1e+07us                                                                                
	===========================================================================
	```
	* The trace file ```dynamic_trace.gz``` is generated and stored at ```workspace/hls/bicg/base```. Each design point has a soft-link to this trace;
	* The trace generation time can be found at ```workspace/hls/bicg/base/trace.time```;
	* The trace generation stdout/stderr can be found at ```workspace/hls/bicg/base/lina.trace.out```;
5. Now run the exploration. In this case we will use the trace cache and 4 parallel threads:
	```
	$ ./run.py PATH=/path/to/lina/build/bin JOBS=4 explore hls bicg
	INFO: Option PATH set to /path/to/lina/build/bin
	INFO: Option JOBS set to 4
	===========================================================================
	Exploring bicg (job 1)                                                                                                                 
	100% ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓
	Elapsed time for this job: 4.6e+07us; Total: 4.67e+07us                                                                                
	===========================================================================
	Exploring bicg (job 2)                                                                                                                 
	100% ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓
	Done exploring bicg! Elapsed time for this job: 4.6e+07us                                                                              
	===========================================================================
	Exploring bicg (job 3)                                                                                                                 
	100% ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓
	Done exploring bicg! Elapsed time for this job: 4.6e+07us                                                                              
	===========================================================================
	Exploring bicg (job 4)                                                                                                                 
	100% ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓
	Done exploring bicg! Elapsed time for this job: 4.59e+07us                                                                             
	===========================================================================
	```
	* Script ```run.py``` generates a trace cache file for each thread and dynamically creates the soft-links for each design point to one of the files. These files are stored as ```workspace/hls/bicg/base/futurecache.X.db```, where ```X``` is a thread ID;
	* The exploration time can be found at ```workspace/hls/bicg/base/explore.time```;
	* The exploration stdout/stderr can be found at ```workspace/hls/bicg/base/lina.explore.X.out```, where ```X``` is a thread ID;
6. Finally generate a csv file containing the results:
	```
	$ ./run.py PATH=/path/to/lina/build/bin collect hls bicg 
	INFO: Option PATH set to /path/to/lina/build/bin
	===========================================================================
	Collecting bicg                                                                                                                        
	100% ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓
	Done collecting bicg! Data saved to csvs/hls/bicg.csv                                                                                  
	===========================================================================
	```
	* The generated csv file is saved to ```csvs/hls/bicg.csv```. You can then use this file as an input to a Pareto set generator or use your favourite multi-objective optimiser;

### The Script

Run ```run.py``` with no arguments in order to understand its usage:
```
Usage: run [OPTION=VALUE]... COMMAND EXPERIMENT [KERNELS]...
    [OPTION=VALUE]... change options (e.g. UNCERTAINTY=27.0):
                          SILENT      all subprocesses spawned will output
                                      to /dev/null instead of the *.out files
                                      (use SILENT=yes to enable)
                                      DEFAULT: no
                          PATH        the path to Lina and its LLVM bins
                                      (will use $PATH when omitted)
                                      DEFAULT: empty
                          JOBS        set the number of threads to spawn
                                      lina
                                      DEFAULT: 1
                          CACHE       toggle cache use (use CACHE=no to disable)
                                      DEFAULT: yes
                          UNCERTAINTY (in %, only applicable to "explore")
                                      DEFAULT: 27.0
                          LOOPID      the loop nest number to be analysed by
                                      Lina
                                      DEFAULT: 0
    COMMAND           may be
                          generate
                          trace
                          explore
                          collect
    EXPERIMENT        the experiment to run (e.g. "hls" or "fullsyn")
    [KERNELS]...      the kernels to run the command on
                      (will consider all in the experiment when omitted)
```

The options can be used to change the DSE exploration (e.g. number of threads to run Lina, clock uncertainty, etc.). An experiment is defined as a set of kernels that Lina should explore at once. Each experiment is composed of a folder located in the same folder as ```run.py```, and it is named after the folder name (e.g. the ```hls``` and ```fullsyn``` folders). If no kernel name is explicitly passed to ```run.py```, it will perform the command on all kernels within that experiment.

### Setting Up a New Exploration

In order to explore a kernel not included in the repository, do as follows (please use the existing experiments ```hls``` and ```fullsyn``` as a reference):

1. Set up a new experiment. We will name it ```test```:
	```
	mkdir test
	```
	* **NOTE:** Ensure that the ```run.py``` is in the same folder as this experiment.
2. Set up a new kernel. We will use the ```mvp``` kernel tested above:
	```
	mkdir test/mvp
	```
3. Prepare the kernel. Five files are required (unused files should be created and left empty):
	* ```test/mvp/mvp.cpp```: source code containing the kernel to be tested:
		```
		#include "mvp.h"

		void mvp(float A[SIZE * SIZE], float x[SIZE], float y[SIZE]) {
			for(int i = 0; i < SIZE; i++)
				for(int j = 0; j < SIZE; j++)
					x[i] += A[i * SIZE + j] * y[j];
		}
		```
	* ```test/mvp/mvp.h```: kernel header file, with constants and so:
		```
		#ifndef MVP_H
		#define MVP_H

		#define SIZE 32

		void mvp(float A[SIZE * SIZE], float x[SIZE], float y[SIZE]);

		#endif
		```
	* ```test/mvp/mvp_tb.cpp```: testbench to execute the kernel:
		```
		#include "mvp.h"

		int main(void) {
			float A[SIZE * SIZE], x[SIZE], y[SIZE];

			for(int j = 0; j < SIZE; j++) {
				y[j] = j;

				for(int i = 0; i < SIZE; i++)
					A[i * SIZE + j] = i + j;
			}

			mvp(A, x, y);

			return 0;
		}
		```
	* ```test/mvp/mvp_tb.h```: testbench header file. We will leave empty in this case;
	* ```test/mvp/mvp.json```: a JSON description file that describes the exploration knobs that the DSE should use, platform and array information. For more information please see **JSON Description File** below. An example of such file:
		```
		{
			"platform": "zcu104",
			"periods": [10, 20],
			"loops": [
				{
					"line": 6,
					"bound": 32,
					"unrolling": [],
					"pipelining": false,
					"nest": {
						"line": 9,
						"bound": 32,
						"unrolling": [2],
						"pipelining": true,
						"nest": {}
					}
				}
			],
			"arrays": {
				"A": {"words": 1024, "size": 4, "block": [2], "cyclic": [2], "complete": false},
				"x": {"words": 32, "size": 4, "block": [], "cyclic": [], "complete": true},
				"y": {"words": 32, "size": 4, "block": [], "cyclic": [], "complete": true}
			},
			"inarrays": {}
		}
		```
4. Then finally follow the instructions as in **Performing an Exploration** to generate the results for ```mvp```.

### JSON Description File

The ```run.py``` script uses a JSON description file to define several exploration parameters for one kernel. It defines the platform to be used, description of loops, arrays and which knobs should be explored. See the [previous](#setting-up-a-new-exploration) section (step 3) for an example of such file.

The required information are:

* ```"platform"```: the target FPGA platform. Examples: "zcu102", "zcu104";
* ```"periods"```: an array containing which periods to explore in nanosseconds (can be replaced by ```"frequencies"```, see below);
* ```"frequencies"```: an array containing which frequencies to explore in MHz (can be replaced by ```"periods"```, see above);
* ```"loops"```: an array containing description of the loop nests in the kernel (currently only one loop nest supported!). Each loop nest is composed of a recursive dictionary with the following elements:
	* ```"line"```: the line number of the header;
	* ```"bound"```: the loop trip count;
	* ```"unrolling"```: a list of all unroll factors to explore;
		* For none, simply use ```[]```;
		* The factor 1 (no unroll) is added automatically;
		* For complete unroll, use the value of ```"bound"```;
	* ```"pipelining"```: set to ```true``` in order to allow this loop level to be pipelined, or ```false``` otherwise;
	* ```"nest"```: is this loop level contains a sub-loop nest, insert it here. The sub-loop elements are the same as this loop (i.e. recursive);
		* If there are no sub-loops, simply use ```{}```;
* ```"arrays"```: a dictionary of all arrays present in the kernel code that should be partitioned. The key of each element is the name of the array. Each value must have the following elements:
	* ```"words"```: number of words in the array;
	* ```"size"```: size (in bytes) of each word (for now only 4 is completely supported);
	* ```"block"```: a list of all partitioning factors to be used with the block partitioning type;
		* For none, simply use ```[]```;
	* ```"cyclic"```: a list of all partitioning factors to be used with the cyclic partitioning type;
		* For none, simply use ```[]```;
	* ```"complete"```: set to ```true``` in order to allow this array to be completely partitioned, or ```false``` otherwise;
	* ```"forcescope"```: ```run.py``` executes Lina with the ```--f-argres``` flag active. You can force different scopes for this array by setting ```"arg"```, ```"rovar"```, ```"rwvar"``` or ```"nocount"``` to ```"forcescope"```;
		* This argument is optional;
* ```"inarrays"```: a dictionary of all arrays present in the kernel that should not be part of the partitioning exploration (e.g. internal arrays). They are defined similar to ```"arrays"```, however without ```"block"```, ```"cyclic"```, ```"complete"``` and ```"forcescope"``` elements. Specific elements to this type are:
	* ```"readonly"```: by default this array is set with scope ```"rwvar"```. Set ```"readonly"``` to true to force the scope to ```"rovar"```.

## Supported Platforms

Currently three platforms are supported:
* ```ZC702```: Xilinx Zynq-7000 SoC (DEFAULT);
* ```ZCU102```: Xilinx Zynq UltraScale+ ZCU102 kit;
* ```ZCU104```: Xilinx Zynq UltraScale+ ZCU104 kit;
* ```VC707```: Xilinx Virtex-7 FPGA.

You can select one by specifying the ```-t``` or ```--target``` option.

*ZC702 and VC707 use the legacy hardware profile library from Lin-Analyzer, therefore timing-constrained scheduling is not supported for these platforms and must be disabled with ```--f-notcs```.*

### Adding a New Platform

Several points of the code must be adjusted if you want to insert a new platform. For example, additional logic has to be created for selecting the new platform and so on. However, the most important part of adding a new platform is defining the hardware profile library. This library is composed of three data structures:
* Total resource count for the platform;
	* Check the enums with ```MAX_DSP```, ```MAX_FF``` and so on at ```include/profile_h/HardwareProfile.h```;
* Timing-constrained latencies;
	* Check the variable ```timeConstrainedLatencies``` at ```lib/Build_DDDG/HardwareProfileParams.cpp```;
* Timing-constrained resources;
	* Check the variables ```timeConstrainedDSPs```, ```timeConstrainedFFs``` and ```timeConstrainedLUTs``` at ```lib/Build_DDDG/HardwareProfileParams.cpp```.

## Files Description

* ***include/profile_h***;
	* ***ArgPack.h:*** struct with the options passed by command line to Lina;
	* ***AssignBasicBlockIDPass.h:*** pass to assign ID to basic blocks;
	* ***AssignLoadStoreIDPass.h:*** pass to assign ID to load/stores;
	* ***auxiliary.h:*** auxiliary functions and variables;
	* ***BaseDatapath.h:*** base class for DDDG estimation;
	* ***boostincls.h:*** the BOOST includes used by Lina;
	* ***colors.h:*** colour definitions used to generate the DDDGs as DOT files;
	* ***DDDGBuilder.h:*** DDDG builder;
	* ***DynamicDatapath.h:*** extended class from BaseDatapath, simply coordinates some BaseDatapath calls;
	* ***ExtractLoopInfoPass.h:*** pass to extract loop information;
	* ***FunctionNameMapperPass.h:*** pass to map mangled/demangled function names;
	* ***HardwareProfile.h:*** hardware profile library, characterising resources and latencies;
	* ***InstrumentForDDDGPass.h:*** pass to instrument and execute the input code;
	* ***lin-profile.h:*** main function;
	* ***LoopNumberPass.h:*** pass to number loops;
	* ***Multipath.h:*** class to handle a set of datapaths (non-perfect loop analysis);
	* ***opcodes.h:*** LLVM opcodes;
	* ***Passes.h:*** declaration of all passes;
	* ***SlotTracker.h:*** slot tracker used by InstrumentForDDDGPass;
	* ***TraceFunctions.h:*** trace functions used by InstrumentForDDDGPass;
* ***lib***;
	* ***Aux:*** auxiliary library;
		* ***auxiliary.cpp:*** auxiliary functions and variables;
	* ***Build_DDDG:*** (part of) trace and estimation library;
		* ***BaseDatapath.cpp:*** base class for DDDG estimation;
		* ***DDDGBuilder.cpp:*** DDDG builder;
		* ***DynamicDatapath.cpp:*** extended class from BaseDatapath, simply coordinates some BaseDatapath calls;
		* ***HardwareProfile.cpp:*** hardware profile logic;
		* ***HardwareProfileParams.cpp:*** hardware profile library with all latencies and resources;
		* ***Multipath.cpp:*** class to handle a set of datapaths (non-perfect loop analysis);
		* ***opcodes.cpp:*** LLVM opcodes;
		* ***SlotTracker.cpp:*** slot tracker used by InstrumentForDDDGPass;
		* ***TraceFunctions.cpp:*** trace functions used by InstrumentForDDDGPass;
	* ***Profile:*** LLVM passes that compose Lina;
		* ***AssignBasicBlockIDPass.cpp:*** pass to assign ID to basic blocks;
		* ***AssignLoadStoreIDPass.cpp:*** pass to assign ID to load/stores;
		* ***ExtractLoopInfoPass.cpp:*** pass to extract loop information;
		* ***FunctionNameMapperPass.cpp:*** pass to map mangled/demangled function names;
		* ***InstrumentForDDDGPass.cpp:*** pass to instrument and execute the input code;
		* ***lin-profile.cpp:*** main function;
* ***misc***;
	* ***largedse:*** updated DSE tool;
		* ***csvs:*** folder where the results for each experiment/kernel are saved as CSV;
		* ***fullsyn:*** the ```fullsyn``` experiment;
		* ***hls:*** the ```hls``` experiment;
		* ***run.py:*** the DSE tool;
		* ***workspace:*** exploration workspace for the experiments/kernels;
	* ***smalldse:*** small exploration with 9 PolyBench-based kernels (FPT-2019 PAPER);
		* ***baseFiles:*** common files that are shared among design points;
			* ***makefiles:*** Makefiles used by Lina or Vivado;
			* ***parsers:*** parser scripts used by the main python script;
			* ***project:*** C projects prepared for Lina;
			* ***traces:*** folder where all traces should be placed (please see [Perform a Small Exploration](https://github.com/comododragon/lina/blob/d85c4a49019027a41970b5e11aa14558951efe35/README.md#perform-a-small-exploration) from old README.md);
			* ***vivadoprojs:*** C projects prepared for Vivado;
		* ***csvs:*** folder where the results for each design point is saved as CSV;
		* ***processedResults:*** automatic spreadsheets that provide results according to the CSV files;
		* ***workspace:*** exploration workspace;
		* ***vai.py:*** main python script for one design point with Lina
		* ***vivai.py:*** main python script for one design point with Vivado
		* ***runall.sh:*** call ```vai.py``` several times to perform small exploration with Lina;
		* ***runallvivado.sh:*** call ```vivai.py``` several times to perform small exploration with Vivado;
	* ***compiler.sh:*** automatic Lina compiler script (please see [Automatic Compilation](#automatic-compilation));
	* ***compiler.2_updlat.sh:*** automatic compiler script for the ```2_updlat``` version of Lina (older version from branch ```2_updlat```, see [Perform a Small Exploration](https://github.com/comododragon/lina/blob/d85c4a49019027a41970b5e11aa14558951efe35/README.md#perform-a-small-exploration) from old README.md).

## Troubleshooting

Some problems may arise during compilation or runtime, since LLVM 3.5.0 was designed to be compiled by older versions of GCC or C/C++ standards.

### "is private within this context"

If you get the following error:
```
error: ‘{anonymous}::ChainedIncludesSource* llvm::IntrusiveRefCntPtr<{anonymous}::ChainedIncludesSource>::Obj’ is private within this context
```

This is caused by newer versions of GCC being more sensitive to certain C++ syntaxes. To solve this problem:

1. At file ```llvm/include/llvm/ADT/IntrusiveRefCntPtr.h```, find the following line:
	```
		template <class X>
		IntrusiveRefCntPtr(IntrusiveRefCntPtr<X>&& S) : Obj(S.get()) {
			S.Obj = 0;
		}
	```
2. Right before, add the following lines:
	```
		template <class X>
		friend class IntrusiveRefCntPtr;
	```
3. Resume compilation.

Source: http://lists.llvm.org/pipermail/llvm-bugs/2014-July/034874.html

### Problems with Parallel Compilation

After setting up all files and folder, you can compile using parallel compilation like:
```
make -j3
```

However, compilation might fail at some point due to broken dependencies with TableGen:
```
fatal error: llvm/IR/Intrinsics.gen: No such file or directory
```

One workaround for this is to compile for the first time without parallel compilation:
```
make
```

Then, the following compilations will work normally with ```-j2```, ```-j3```, etc. There should be no problems if you only manipulate the source files inside Lina.

### subprocess.CalledProcessError: Command ... returned non-zero exit status 2

The run.py script may throw exceptions if the spawned subprocesses fail. For specific information, one can check the output files that are located at ```workspace/<EXPERIMENT>/<KERNEL>/base```. **Note that these are only generated if run.py is not running in silent mode (i.e. not running with **```SILENT=yes```** argument.** The files are:

* **make.out:** generated during compilation of the input kernel codes through **generate** command;
* **lina.trace.out:** output from Lina during trace;
* **lina.explore.X.out:** output from Lina during exploration, where X is the job ID (1, 2, 3, ...).

### Lina's bundled clang fails to find basic headers (e.g. "fatal error: 'cstdlib' file not found")

Since Lina is build on an old version of LLVM, operating systems that are up to date may not provide libraries or headers in the outdated paths that LLVM 3.5.0 expect. To solve this issue, the user must point CLANG to the include folders that actually contain the content that CLANG is expecting. This is done using environment variables:

* **C_INCLUDE_PATH:** to set additional header paths (C);
* **CPLUS_INCLUDE_PATH:** to set additional header paths (C++);
* **LD_LIBRARY_PATH:** to set additional linking libraries.

For example in Ubuntu 20.04, the CLANG bundled with Lina is not able to find the standard libraries (e.g. cstdio, cstdlib). One can check the paths used by ```g++``` running the following command:
```
$ g++ -E -xc++ - -v < /dev/null
```

The following lines show the include paths:
```
...
#include <...> search starts here:
 /usr/include/c++/9
 /usr/include/x86_64-linux-gnu/c++/9
 /usr/include/c++/9/backward
 /usr/lib/gcc/x86_64-linux-gnu/9/include
 /usr/local/include
 /usr/include/x86_64-linux-gnu
 /usr/include
...
```

In this case, including ```/usr/include/c++/9``` and ```/usr/include/x86_64-linux-gnu/c++/9``` as include paths was enough to solve the error:
```
export CPLUS_INCLUDE_PATH=/usr/include/c++/9:/usr/include/x86_64-linux-gnu/c++/9:$CPLUS_INCLUDE_PATH
```

You can insert this export at your rc file (e.g. ```.bashrc```, ```.zshrc```) to make it permanent.

## Acknowledgments

The project author would like to thank São Paulo Research Foundation (FAPESP), who funds the research where this project is inserted (Grant 2016/18937-7).

The opinions, hypotheses, conclusions or recommendations contained in this material are the sole responsibility of the author(s) and do not necessarily reflect FAPESP opinion.

