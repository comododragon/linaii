# Lina

(yet another) High Level Analysis Tool for FPGA Accelerators

## Note

This is a side-repository from Lina. It contains the project snapshot from a stable version after code refactoring from Lin-Analyzer (commit e325c6f from the original repo). However, some bug fixes and patches were inserted to fix some errors and some logic was changed to the original values from Lin-Analyzer:

* Patch for ```partitionCfgMapTy``` variable, changing the element from pointer to static container (see commit 5d0810d from original repo);
* Bug fix for incorrect handling of mangled names in ```auxiliary.cpp``` (see commit 1972e5c from original repo);
* Patch for release of unconstrained functional units (see commit 8b6e6a4 from original repo);
* Bug fix for ordering logic of ResIIMem (see commit 22157cb from original repo);
* ```EXTRA_ENTER_EXIT_LOOP_LATENCY``` at ```BaseDatapath.h``` is set to 3, as in the original Lin-Analyzer code;
* ```BaseDatapath.cpp```, around line 1490, removed the ```else if(1 == i) { ... }``` block;
* ```BaseDatapath.cpp```, around line 1746, changed from ```cycleTick + 1 : cycleTick``` to ```cycleTick + 1 : cycleTick - 1```.

The remaining useful information you can find in the README of the main branch.
