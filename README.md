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
	1. [Configuration File](#configuration-file)
	1. [Memory Trace File](#memory-trace-file)
	1. [Context-based Dual Execution](#context-based-dual-execution)
	1. [Off-chip Memory Model](#off-chip-memory-model)
	1. [Different Dynamic Trace Format](#different-dynamic-trace-format)
	1. [Lina Daemon (linad)](#lina-daemon-linad)
1. [Usage](#usage)
1. [Perform an Exploration](#perform-an-exploration)
1. [Supported Platforms](#supported-platforms)
1. [Files Description](#files-description)
1. [Troubleshooting](#troubleshooting)
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

Refer to our papers for detailed descriptions of the contributions presented in Lina (also if you use Lina in your research, please kindly cite one or more papers below):

**Lina Mark 2 (Off-chip memory model):**

* *A link to this material will be added soon*

**Lina Mark 1:**

* A. Bannwart Perina, A. Silitonga, J. Becker and V. Bonato, "Fast Resource and Timing Aware Design Optimisation for High-Level Synthesis," in IEEE Transactions on Computers, doi: [10.1109/TC.2021.3112260](https://doi.org/10.1109/TC.2021.3112260).

**Thesis (contains most of Mark 2 except `cachedaemon`):**

* A. Bannwart Perina, "Lina: a fast design optimisation tool for software-based FPGA programming," doctoral dissertation, Universidade de São Paulo, 2022.
	* **URL:** https://www.teses.usp.br/teses/disponiveis/55/55134/tde-23082022-101507/en.php

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


## Differences Between Mark 1 and 2

Basic usage of Lina is explained in detail on the Mark 1 README. See [here](https://github.com/comododragon/lina/blob/master/README.md).

This README will focus on the differences between Mark 1 and 2.

### Command-line Arguments

Command-line arguments from Mark 1 (also functional here) are included [here](https://github.com/comododragon/lina/blob/master/README.md#usage).

These are the Mark 2 specific arguments:

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
	* See [Context-based Dual Execution](#context-based-dual-execution);

### Configuration File

Configuration file structure from Mark 1 (also applicable here) are included [here](https://github.com/comododragon/lina/blob/master/README.md#configuration-file).

The subsections below present the differences.

#### Mapping Arrays to Offchip Memory

On Mark 2, arrays can be declared as offchip:
```
array,foo,65536,8,offchip
```

On the example above, array `foo` is declared as offchip. This means that its scheduling will be handed to the Memory Model, which is reponsible for dealing with the estimation of offchip transactions.

When arrays are declared as in Mark 1, they are considered as onchip and behaviour is similar to Mark 1. The only difference is if you want to use scope arguments (e.g. `rwvar`). In this case, the `onchip` keyword must be added before the scope:
```
array,foo,65536,8,onchip,rwvar
```

#### Global parameters

On Mark 2, there is a new type of entry called `global`, which can be used to enable/disable global exploration parameters. The format is:
```
global,<NAME>,<VALUE>
# Example
global,ddrbanking,1
```

Currently, there is a single `global` parameter:

* ```ddrbanking```: toggles DDR banking:
	* ```0```: all off-chip arrays are considered in the same shared memory space **(DEFAULT)**;
	* ```1```: all off-chip arrays are considered in separate DDR banks. That means separate memory spaces.

### Memory Trace File

For offchip scheduling estimation, the address of the memory accesses must be known. This is needed to evaluate optimisations such as burst coalescing.

A memory trace can be generated by using the `--mem-trace` or `--short-mem-trace` options. The first option is also present on Mark 1 and even on Lin-analyzer, and it generates a textual file named `mem_trace.txt`.

However, this file can easily grow up in size if the kernel under test performs too many memory transactions, and this can degrade Lina's performance. In order to reduce its overhead, Mark 2 includes the `--short-mem-trace` option. This generates a binary memory trace called `memory_trace_short.bin`. Its structure is optimised to use less space as the textual representation and it does not require formatted parsing. This way, it can be loaded much faster. **It is recommended to use this option.**

### Context-based Dual Execution

On Mark 1, Lina is executed a single time for each design point to be explored. However, this limits the amount of offchip optimisations that Lina can perform. This limitation arises from the way Lina's code was constructed. More specifically, it is related to the original code construction of Lin-analyzer, that Lina inherited.

Changing this would require significant code rewrite and re-testing. For this reason, if you want to fully utilise the memory model capabilities, you will need to run Lina twice for each design point:

- The first execution (called **generate** mode) performs a first pass and DDDG generation. Execution is interrupted before actual estimation starts. Relevant data structures are saved to a context file named `context.dat`;
- The second execution (called **use** mode) uses the `context.dat` file to repopulate several data structures, and then memory model optimisation can proceed with much more useful information.

This mode is used to improve burst optimisation detection across different loop iterations. More specifically, it helps to filter out burst optimisations that are not possible when multiple DDDGs of the code are analysed. In short, certain burst optimisations can be detected if using single mode, but refused when the dual execution mode is used.

This dual mode is simply done by calling Lina twice for each design point. All arguments can be the same within calls, except for `--mma-mode`. On the first execution, it shall be `--mma-mode=gen`. On the second execution, it shall be `--mma-mode=use`.

You can disable this dual execution mode by either not providing the `--mma-mode` option, or by setting it as `--mma-mode=off`.

For detailed code information, you can take a look on the file `lib/Build_DDDG/MemoryModel.cpp`, at function `findOutBursts()`.

### Off-chip Memory Model

The greatest addition of Mark 2 is the off-chip memory model. It has several responsibilities:

- Analyses potential optimisations;
- Performs off-chip memory scheduling;
- Performs allocation of scheduled off-chip memory nodes;
- Generates the memory optimisation report.

Most of the memory model is implemented on the `MemoryModel` class. Take a look there for details.

Please see [Command-line Arguments](#command-line-arguments) for information about possible optimisations. More specifically, see the following arguments: `--fno-mma`, `--f-burstaggr`, `--f-burstmix`, `--f-vec`, `--ddrsched`, and `--mma-mode`.

Information about how the model works can be found in the latest publication or thesis. See [Publications](#publications).

#### DDR Scheduling Policies

Lina's off-chip memory model has different scheduling policies that affects how off-chip transactions may overlap. The policy is selected via the `--ddrsched` argument. These are the options:

* `0` (conservative): DDR transactions of same type (read/write) cannot overlap (i.e. once a transaction starts, it must end before others of same type can start;
* `1` (permissive): DDR transactions can overlap if their memory spaces are disjoint.

Please refer to the [thesis](#publications) or [last published paper](#publications) for actual details on the policies.

#### ResMIIMemRec Calculation

The memory model is also responsible for providing the calculation of ResMIIMemRec. This is the "Resource/Recurrence-Constrained Minimum Initiation Interval", constrained by dependent memory accesses. A better explanation for this calculation is presented on the latest publication. See [Publications](#publications).

The implementation can be found at `MemoryModel::calculateResIIMemRec()`.

### Different Dynamic Trace Format

Mark 1 and Mark 2 both use gzip compression to store the dynamic trace file, but they use a different compression ratio. The gzip files generated by both versions are interchangeable, however there might be differences in file sizes and performance (Mark 2 generates a larger file, but with faster processing due to reduced compression ratio).

### Lina Daemon (linad)

Mark 2 has a special variant present on [cachedaemon branch](https://github.com/comododragon/linaii/tree/cachedaemon) that uses shared memory and a daemon to reduce IO bottleneck during DSE.

When several `lina` instances are running in parallel, competition for IO resources increase (i.e. high demand on memory trace map and dynamic trace). But, it is not uncommon that several instances access - roughly - the same memory regions. Therefore, it is beneficial to cache these regions on memory, and provide access of these regions to the parallel `lina` instances.

This is achieved by using `linad`. It is a daemon that opens the dynamic trace and memory trace map, and caches accessed data to a shared memory region. The shared memory region is then accessible by the multiple `lina` calls, and thus IO accesses are greatly reduced.

Using `linad` is beneficial when `lina` accesses really large files during exploration. Further analysis and details can be found in the latest [publication](#publications).

Calling `linad` is simple. Only a single argument exists and it is optional:

```
linad [PIPEID]
```

The `PIPEID` argument is a positive number used to identify the IPC pipe, see [IPC](#IPC) below. If absent, `PIPEID` is 0. Setting `PIPEID` allows multiple instances of `linad` to be executed, each pointing to a different IPC pipe. This is useful on multi-node systems that uses a shared filesystem.

For the memory trace, the whole file is loaded to shared memory when a load command is specified. For dynamic traces, the functionality is quite different.

A single `lina` execution usually accesses very scattered regions of the dynamic trace, and it reads a small-to-moderate amount of data in each of those sparse points. On top of that, multiple `lina` calls share approximate regions. Without `linad`, explorations on really large dynamic trace files will incur in significant overheads when seeking the compressed file (i.e. `gzseek()` calls). If multiple dynamic trace file pointers are opened and seeked, one for each of the scattered regions accessed by the `lina` calls, it becomes way faster to read these regions multiple times. This eliminates multiple `gzseek()` calls that incurs in large IO overhead, by using the right files to the right scattered regions.

As an example, let's say that all `lina` calls for a single DSE results in accessing at most five scattered regions of the dynamic trace. Each of those regions are identified by a unique key inside the `lina` calls. These keys are passed to `linad`, and each key results in one dynamic trace file pointer being opened and seeked to the respective scattered region. Every time a new `lina` call wants to access something near these regions, it uses the unique keys to select the file pointer that is closest, potentially avoiding large seek calls.

Therefore, the logic for the shared dynamic trace works as follows:

1. A `lina` call creates a connection to the shared dynamic trace logic on `linad`;
	* This creates a shared memory region that is used as a buffer for the dynamic trace contents;
1. Lina performs internal calculations and decides which scattered region it must read for a certain DDDG generation;
	* This scattered region is identified by a unique key across `lina` calls;
1. Lina attaches to a dynamic trace file pointer using the unique key;
	* If there is already a dynamic trace file pointer using this unique key, `linad` uses it;
	* Otherwise, `linad` opens a new dynamic trace file pointer, and seeks to the proposed scattered region;
1. Lina then requests this region to be read from the attached dynamic file, and stored on the shared memory buffer;
1. Then, Lina uses the shared memory buffer to read the contents from the dynamic trace;
1. If more data is needed, Lina can request `linad` for further buffer reads from the attached dynamic trace file;
1. When Lina is done with this scattered region, it detaches from the current file;
	* The file is not closed by `linad`, but it is set aside, as it may be used on other attach calls pointing to this region (or a close point);
1. If another scattered region is needed (e.g. to generate another DDDG), Lina attaches to a new unique key;
1. This attach/detach process is repeated until Lina finishes processing all required DDDGs;
1. Then, the connection is closed;
	* That effectively deallocates the shared memory region allocated as buffer.

It is important to note that `linad` does not perform any analysis to decide which dynamic trace file pointer is best to use when `lina` asks for attachment. This decision is actually done by `lina` according to the provided unique key during attachment.

The daemon is mainly structured in two parts: the IPC and the shared memory manager.

#### IPC

The IPC is the inter-process communication part. This is used to control `linad`, set up the traces, attach/detach shared memory regions, etc.

Communication is done via named pipes. Linad uses the `PIPEID` argument to define the named pipe as follows: `/tmp/linad.pipe.<PIPEID>`. For example, a `linad` instance with `PIPEID` = 5 will open its pipe on `/tmp/linad.pipe.5`.

Commands are simple structured strings. Below is a list of supported commands:

* `l` command: Load memory trace;
	* **Format:** `lXXXYYYYYYYYYYY...`;
	* `XXX` is the size of `YYYYYYYYYYY...` string (3-digit positive integer);
	* `YYYYYYYYYYY...` is the memory trace file name;
	* **Example:** `l028/path/to/mem_trace_short.bin` would load `/path/to/mem_trace_short.bin` as the memory trace file;
	* *The memory trace file is fully loaded to shared memory when this command is issued*;
* `d` command: Load dynamic trace;
	* **Format:** `dXXXYYYYYYYYYYY...`;
	* `XXX` is the size of `YYYYYYYYYYY...` string (3-digit positive integer);
	* `YYYYYYYYYYY...` is the dynamic trace file name;
	* **Example:** `d025/path/to/dynamic_trace.gz` would load `/path/to/dynamic_trace.gz` as the dynamic trace file;
	* *Differently from the memory trace, the dynamic trace is not fully loaded at once*;
* `c` command: Create a connection;
	* **Format:** `cXXXYYYYYYYYYYY...`;
	* `XXX` is the size of `YYYYYYYYYYY...` string (3-digit positive integer);
	* `YYYYYYYYYYY...` is the connection name;
	* **Example:** `d015connectionName0` creates a connection with name `connectionName0`;
	* *When a connection is created, a shared memory region is created that is used as buffer for dynamic trace contents*;
* `x` command: Close a connection;
	* **Format:** `xXXXYYYYYYYYYYY...`;
	* `XXX` is the size of `YYYYYYYYYYY...` string (3-digit positive integer);
	* `YYYYYYYYYYY...` is the connection name;
	* **Example:** `x015connectionName0` closes the connection with name `connectionName0`;
	* *When a connection is closed, the related shared memory buffer is released*;
* `a` command: "Attach to / open" a cached dynamic trace file;
	* **Format:** `aXXXYYYYYYYYYY...ZZZZZZZZZZWWWKKKKKKKKKK...`;
	* `XXX` is the size of `YYYYYYYYYY...` string (3-digit positive integer);
	* `YYYYYYYYYYY...` is the connection name;
	* `ZZZZZZZZZZ` is the transaction ID (10-digit positive integer);
	* `WWW` is the size of `KKKKKKKKKK...` string (3-digit positive integer);
	* `KKKKKKKKKK...` is the cache entry name to attach to;
	* **Example:** `a015connectionName00000000010010midRegion2` attaches the `connectionName0` connection to a dynamic trace file pointer identified as `midRegion2`;
	* *The transaction ID is used to identify when the transaction has been completed and loaded to the shared memory buffer (see [shared memory manager](#shared-memory-manager) below);
	* When attaching, `linad` checks if the provided "cache entry name" (unique key) already exists;
		* If it exists, there is an associated open dynamic trace file, and this one is used;
		* If it doesn't exist, a new dynamic trace file pointer is opened, and associated with the provided unique key;
* `r` command: Release/Detach;
	* **Format:** `rXXXYYYYYYYYYYY...`;
	* `XXX` is the size of `YYYYYYYYYYY...` string (3-digit positive integer);
	* `YYYYYYYYYYY...` is the connection name;
	* **Example:** `r015connectionName0` detaches whatever is attached to the connection with name `connectionName0`;
	* *If there is any attached dynamic trace file, it is detached, but not closed. It is set aside and may be reused by other future attach calls*;
* `g` command: Refresh/get dynamic trace buffer;
	* **Format:** `gXXXYYYYYYYYYY...ZZZZZZZZZZWWWWWWWWWWLLLLLLLLLL`;
	* `XXX` is the size of `YYYYYYYYYYY...` string (3-digit positive integer);
	* `YYYYYYYYYYY...` is the connection name;
	* `ZZZZZZZZZZ` is the transaction ID (10-digit positive integer);
	* `WWWWWWWWWW` is the proposed file cursor (10-digit positive integer);
	* `LLLLLLLLLL` is the max. amount of data to be read starting from the proposed cursor (10-digit positive integer);
	* **Example:** `g015connectionName0000000001100000111110000010000`: 10000 bytes are read starting from file cursor 1111, and its content are stored on the shared memory buffer from `connectionName0`.

#### Shared Memory Manager

The shared memory manager is responsible for dealing with the shared memory regions.

For the memory trace map, the whole memory trace map file is inserted at once to a shared memory region once an `l` command is issued.

For the dynamic trace file, functionality is different. Shared memory regions are created when connections are created (i.e. `c` command) and are destroyed when connections are closed (i.e. `x` commands).

These shared memory regions are composed of two super-segments: the **connection** and the **buffer**.

* **Connection**:
	* `TRANSACTIONID` (unsigned): always points to the transaction ID from the latest processed command via IPC (see [IPC commands](#ipc) with transaction ID arguments);
		* This can be used on `lina` to identify when a command that uses transaction ID has been fully processed;
	* `SUCCESS` (1 byte): indicates if the last command (identified by the transaction ID) has been successfuly processed or not.
* **Buffer**:
	* `ISINIT` (1 byte): indicates if the shared memory region is initialised with valid data;
	* `EOFFOUND` (1 byte): indicates if `EOF` was reached during dynamic trace read;
	* `LOWER` (8 bytes): indicates the lower byte cursor of the shared memory buffer;
	* `HIGHER` (8 bytes): indicates the higher byte cursor of the shared memory buffer;
	* `DATA` (`HIGHER-LOWER` bytes): the shared memory buffer.

Usually, `lina` should issue commands with unique transaction IDs (can be sequential), and then wait on the connection segments for the respective transaction ID. When it matches, `lina` should check `SUCCESS` to see if the command has been successfuly processed or not.

These segments are updated when `g` commands are issued. Then, `lina` can use these segments to read the dynamic trace as if it were reading from the file itself. Please refer to `lib/Build_DDDG/SharedDynamicTrace.cpp` to see how this is actually implemented.


## Usage

Basic usage of Lina is explained in detail on the Mark 1 README. See [here](https://github.com/comododragon/lina/blob/master/README.md#usage).

After reading Mark 1's README, see the differences [here](#differences-between-mark-1-and-2).


## Perform an Exploration

There are three DSE infrastructures developed around Lina. See [Mark 1 README](https://github.com/comododragon/lina/blob/master/README.md#perform-an-exploration) for the first two versions.

The latest version is using the `cirith-fpga` framework. Please refer to [cirith repository](https://github.com/comododragon/cirith-fpga) for complete examples of DSE using the Mark 2 lina.


## Supported Platforms

Although lina supports multiple platforms as presented in Mark 1, this version was heavily optimised for the Zynq UltraScale+ architectures. Therefore, full support is only provided for the following platforms:
* ```ZCU102```: Xilinx Zynq UltraScale+ ZCU102 kit;
* ```ZCU104```: Xilinx Zynq UltraScale+ ZCU104 kit;

You can select one by specifying the ```-t``` or ```--target``` option.


## Files Description

See [here](https://github.com/comododragon/lina/blob/master/README.md#files-description) for a description on files common to Marks 1 and 2. Below only the files specific to Mark 2 are presented:

* ***include/profile_h***;
	* ***ContextManager.h:*** handles Lina's dual-mode execution, handling the context file;
	* ***MemoryModel.h:*** the off-chip memory model;
* ***lib***;
	* ***Aux:*** auxiliary library;
		* ***globalCfgParams.cpp:*** class containing the [global parameters](#global-parameters);
	* ***Build_DDDG:*** (part of) trace and estimation library;
		* ***ContextManager.cpp:*** handles Lina's dual-mode execution, handling the context file;
		* ***MemoryModel.cpp:*** the off-chip memory model;
* ***misc***;
	* ***smalldseddr1:*** small exploration that was used to elaborate the off-chip memory model. Kept only for historical reasons.

Below is a list of files specific to the `cachedaemon` version (i.e. [cachedaemon branch](https://github.com/comododragon/linaii/tree/cachedaemon)): 

* ***include/profile_h***;
	* ***SharedDynamicTrace.h:*** class that interfaces with the shared memory region for the dynamic trace;
	* ***SharedMemoryTraceMap.h:*** class that interfaces with the shared memory region for the memory trace map;
* ***lib***;
	* ***Build_DDDG:*** (part of) trace and estimation library;
		* ***SharedDynamicTrace.cpp:*** class that interfaces with the shared memory region for the dynamic trace;
* ***misc***;
	* ***linad:*** the lina shared memory daemon;
		* ***Makefile:*** self-explanatory;
		* ***linad.cpp:*** complete source code for daemon;
		* ***run.py:*** DSE run script adapted to use linad. It should replace the original `run.py` on the [Cirith framework](https://github.com/comododragon/cirith-fpga).


## Troubleshooting

So far there are no troubleshooting related specifically to Mark 2. For a general troubleshooting, see [here](https://github.com/comododragon/lina/blob/master/README.md#troubleshooting).


## Acknowledgments

The project author would like to thank São Paulo Research Foundation (FAPESP), who funded the research where this project was inserted (Grants 2016/18937-7 and 2018/22289-6).

The opinions, hypotheses, conclusions or recommendations contained in this material are the sole responsibility of the author(s) and do not necessarily reflect FAPESP opinion.

