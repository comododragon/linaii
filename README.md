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
$ ln-s /lib/x86_64-linux-gnu/libz.so.1 libz.so
```

Alternatively, you can change the compiler script (or the ```cmake``` command if compiling manually) to point to the correct ```zlib``` position. Ou can do that by modifying the value passed to the ```-DZLIB_LIBRARY=``` flag of the ```cmake``` command. In this case you don't need to perform any soft-linking.

## Compilation

You can either compile Lina manually by following the instructions presented in Section ***Manual Compilation*** below or by using the automated compiling script.

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
	* Right after ```cmake``` and before starting the whole compilation process, the script will give you the option to abort the script and leave the project as is. At this point you will have the project ready to be compiled, where you can insert your modifications or fix some system-related problems regarding dependencies. Then, simply follow Section ***Manual Compilation*** from step ***TODOOOOOOOOOOOOOOOOOO***;
	* If everything goes right, you should have the ```lina``` binary at ```/path/to/lina/build/bin/lina```.

### Manual Compilation

TODO

## Example of Use

TODO

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
