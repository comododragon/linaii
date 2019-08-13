# Lina

(yet another) High Level Analysis Tool for FPGA Accelerators

## Introduction

Lina is a pre-HLS performance estimator for C/C++ codes targetting Xilinx FPGAs with Vivado HLS or its variants. Given a C/C++ code, it generates the Dynamic Data Dependence Graph (DDDG) and schedules its nodes to estimate the performance (cycle count) of the code for the Vivado HLS toolchain. Several optimisation directives (e.g. loop unroll, pipelining) can be provided and Lina estimates the performance according to those constraints. Lina enables fast design space exploration by providing results in significantly less time than FPGA synthesis or even the HLS compilation process.

Lina is an expansion of the Lin-Analyzer project (see https://github.com/zhguanw/lin-analyzer). It uses the same traced execution and (pretty much the same) DDDG scheduling. However, Lina adds Timing-Constrained Scheduling (TCS) and Non-Perfect Loop Analysis (NPLA). The TCS provides a more precise estimation considering a user-provided operational frequency for the design, while NPLA generalises the estimation to non-perfect loop nests. Moreover, Lina has some improved logic to estimate certain border cases where Lin-Analyzer did not properly estimate.

For more information regarding Lina, please refer to our paper (see Section ***Publications***).

## Licence

Lina is distributed under the GPL-3.0 licence, so as Lin-Analyzer.

According to Lin-Analyzer repository: *"Dynamic data graph generation (DDDG) and few of optimization functions in Lin-Analyzer are modified from Aladdin project and Lin-Analyzer is also followed Aladdin's license. For more information, please visit Aladdin's website: https://github.com/ysshao/ALADDIN"*.

The ```common.h``` header is distributed under the GPL-3.0 licence. See the following repo for details: https://github.com/comododragon/commonh

The kernels in folder ```misc/kernels``` are adapted from PolyBench/C, which follows the GPL-2.0 licence. Please see: https://sourceforge.net/projects/polybench/

## Setup

Lina makes use of the LLVM Compiler Framework 3.5.0. In order to use Lina, it must be compiled together with ```llvm``` and ```clang```.

Compilation of Lina was tested in the following systems:
* Arch Linux with Linux kernel 5.2.0-arch2-1-ARCH;
* Ubuntu 18.04 LTS, clean install.

Before proceeding to compilation, you must ensure that the following packages are installed:

* GNU Compiler Collection. For Ubuntu, run:

```$ sudo apt-get install build-essential```
* ZLIB development libraries. For Ubuntu, run:

```$ sudo apt-get install zlib1g-dev```
* GIT. For Ubuntu, run:

```$ sudo apt-get install git```
* CMAKE. For Ubuntu, run:

```$ sudo apt-get install cmake```

*You can also compile LLVM using other toolchains, such as ```clang```. Please see https://releases.llvm.org/3.5.0/docs/GettingStarted.html*

At last, the relation between ```cmake``` and ```zlib``` is quite tricky, mainly in Ubuntu. We are assuming here that installing the ```zlib``` package will put the shared library ```zlib.so``` at ```/usr/lib```. Ubuntu puts ```zlib``` on a different location and this breaks compilation. ***Please make sure that you have a valid library (or a link to) at the /usr/lib/libz.so location.*** You can do that by listing the file:
```
$ ls /usr/lib/libz.so
```

If it shows ```no such file or directory``` or it lists a broken link, please (re-)create the soft link pointing to the correct library. In Ubuntu 18.04 for example, it is located at ```/lib/x86_64-linux-gnu/libz.so.1```. In this case the soft-link should be created as:
```
$ cd /usr/lib
$ ln -s /lib/x86_64-linux-gnu/libz.so.1 libz.so
```

Alternatively, you can change the compiler script (or the ```cmake``` command if compiling manually) to point to the correct ```zlib``` position. You can do that by modifying the value passed to the ```-DZLIB_LIBRARY=``` flag of the ```cmake``` command. In this case you don't need to perform any soft-linking.

## Compilation

You can either compile Lina by using the automated compiling script, or manually by following the instructions presented in Section ***Manual Compilation***.

### Automatic Compilation

We have provided an automated BASH compilation script at ```misc/compiler.sh```. It simply follows the instructions from ***Manual Compilation*** and applies patches automatically if necessary.

To use automatic compilation:

1. Make sure you read and understood the Section ***Setup*** (in other words, make sure that you have ```gcc```, ```zlib```, ```git``` and ```cmake```);
2. Download the automated compilation script at ```misc/compiler.sh```(https://raw.githubusercontent.com/comododragon/lina/master/misc/compiler.sh);
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
	* It will ask before compiling if you want to apply some patches. Please read Section ***Troubleshooting*** for better understanding. In doubt, just press ENTER and the patches will be ignored. If compilation fails, you will have another chance to apply the patches;
	* ***Every time the script is executed, the whole operation is re-executed (i.e. no incremental compilation with the script!);***
	* Right after ```cmake``` and before starting the whole compilation process, the script will give you the option to abort the script and leave the project as is. At this point you will have the project ready to be compiled, where you can insert your modifications or fix some system-related problems regarding dependencies. Then, simply follow Section ***Manual Compilation*** from step ***9***;
	* If everything goes right, you should have the ```lina``` binary at ```/path/to/lina/build/bin/lina```.

### Manual Compilation

The script simply executes the following steps automatically:

1. Make sure you read and understood the Section ***Setup*** (in other words, make sure that you have ```gcc```, ```zlib```, ```git``` and ```cmake```);
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
5. Clone Lina to folder ```llvm/tools/lina```:
```
$ git clone https://github.com/comododragon/lina.git
$ mv lina llvm/tools/lina
```
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
6. Add the line ```set(LLVM_REQUIRES_RTTI 1)``` at the end of file ```llvm/CMakeLists.txt```;
7. Download BOOST and extract to folder ```boost```:
```
$ wget "http://sourceforge.net/projects/boost/files/boost/1.57.0/boost_1_57_0.tar.gz"
$ tar xvf boost_1_57_0.tar.gz
$ mv boost_1_57_0 boost
```
8. Create build folder and run ```cmake```:
```
$ mkdir build
$ cd build
$ cmake ../llvm -DBOOST_INCLUDE_DIR=../boost -DZLIB_INCLUDE_DIRS=/usr/include -DZLIB_LIBRARY=/usr/lib/libz.so -DLLVM_ENABLE_RTTI:BOOL=ON
```
9. Now you can finally make:
```
$ make
```
10. If everything goes well, you will have the Lina binary at ```path/to/lina/build/bin```. If not, please see Section ***Troubleshooting***.

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
* ```-c FILE``` or ```--config-file=FILE```: use ```FILE``` as the configuration file. See Section ***Configuration File***;
* ```-m MODE``` or ```--mode=MODE```: set ```MODE``` as the execution mode of Lina. Three values are possible:
	* ```all```: execute traced execution and performance estimation;
	* ```trace```: execute only traced execution, generating the dynamic trace. Lina hangs before performance estimation;
	* ```estimation```: execute only performance estimation. The dynamic trace must be already generated;
* ```-t TARGET``` or ```--target=TARGET```: select the FPGA to perform cycle estimation:
	* ```ZC702:``` Xilinx Zynq-7000 SoC (DEFAULT);
	* ```ZCU102:``` Xilinx Zynq UltraScale+ ZCU102 kit;
	* ```VC707:``` Xilinx Virtex-7 FPGA;
* ```-v``` or ```--verbose```: show more details about the estimation process and the results;
* ```-f FREQ``` or ```--frequency=FREQ```: specify the target clock, in MHz;
* ```-u UNCTY``` or ```--uncertainty=UNCTY```: specify the clock uncertainty, in percentage;
* ```-l LOOPS``` or ```--loops=LOOPS```: specify which top-level loops should be analised, starting from 0;
* ```--f-npla```: activate non-perfect loop analysis (disabled by default);
* ```--f-notcs```: deactivate timing-constrained scheduling (enabled by default).

### Use by Example

Let's estimate the cycle count for a simple matrix vector and product:
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
3. No we have to create a configuration file for this project, which will inform Lina the size of each array and also optimisation directives. Let's create a file ```config.cfg``` and enable partial loop unrolling with factor 4 on both loops (please see Section ***Configuration File*** for more information about this file):
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
lina --config-file=config.cfg --target=ZCU102 --loops=0 test_opt.bc mvp
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
Counting number of top-level loops in "_Z3mvpPfS_S_"
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

We can compare the results against the reports from Vivado. In this case, we used Vivado HLS 2018.2 for the ZCU102 Xilinx UltraScale+ Board.

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
set_part {xczu9eg-ffvb1156-2-e}
create_clock -period 10 -name default
set_clock_uncertainty 27.0%
csynth_design
exit
```
3. Then we run Vivado and wait for the HLS process to finish:
```
$ vivado_hls -f script.tcl
```
4. Then you can find at ```mvp/solution1/syn/report/mvp_csynth.rpt``` the cycle count for the loop:
```
+-------------+------+------+----------+-----------+-----------+------+----------+
|             |   Latency   | Iteration|  Initiation Interval  | Trip |          |
|  Loop Name  |  min |  max |  Latency |  achieved |   target  | Count| Pipelined|
+-------------+------+------+----------+-----------+-----------+------+----------+
|- Loop 1     |  5672|  5672|       709|          -|          -|     8|    no    |
+-------------+------+------+----------+-----------+-----------+------+----------+
```

### Configuration File

Lina uses a configuration file to inform array information and optimisation directives. This file is only necessary to the estimation mode of Lina. The configuration file is parsed line by line. See the following configuration file as an example of supported directives:

```
# Any line starting with a "#" is ignored by the parser
array,A,4096,4
unrolling,mvp,0,1,4,4
pipeline,mvp,0,2
partition,block,A,4096,4,8
```

Now line by line:

* ```# Any line starting with a "#" is ignored by the parser```
	* Pretty self-explanatory;
* ```array,A,4096,4```
	* Declare an array. The arguments are the array name, total size in bytes and word size in bytes, respectively;
	* Equivalent to ```uint32_t A[1024];```
	* All arrays of the kernel must be declared here;
* ```unrolling,mvp,0,1,4,4```
	* Set unroll. The arguments are the kernel name, the top-level loop ID (starts from 0), the loop level (1 is the top-level). the loop header line number and the unroll factor;
	* Equivalent to ```#pragma HLS unroll factor=4```
* ```pipeline,mvp,0,2```
	* Set pipeline. The arguments are the kernel name, the top-level loop ID and the loop level;
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
$ lina --mode=estimation --config-file=config.cfg --target=ZCU102 --loops=0 test_opt.bc mvp
```
5. Repeat from step ***3***.

## Files Description

TODO

## Supported Platforms

TODO

### Adding a New Platform

TODO

## Troubleshooting

TODO

## TODOs

TODO (lol)

## Acknowledgements

The project author would like to thank São Paulo Research Foundation (FAPESP), who funds the research where this project is inserted (Grant 2016/18937-7).

The opinions, hypotheses, conclusions or recommendations contained in this material are the sole responsibility of the author(s) and do not necessarily reflect FAPESP opinion.
