#!/bin/bash

if [ $# -ne 1 ]; then
	echo "Usage: transform.sh BCFILE" 1>&2
	echo -e "\tBCFILE\tLLVM-bytecode file to be transformed" 1>&2
	exit
fi

# XXX: This script performs many runs in the ll file, which is not the most effective way of working.
# XXX: I acknowledge this "issue", but I have no intention in solving this right now, since the script just "works".

# Remove the alloca call for __PROFCOUNTER_COMM_DUMMY_VAR__
sed -i "/.*PROFCOUNTER_COMM_DUMMY_VAR.*alloca.*/d" $1

# Remove loads and stores to __PROFCOUNTER_COMM_DUMMY_VAR__
sed -i "/.*PROFCOUNTER_COMM_DUMMY_VAR.*load volatile i32.*/d" $1
sed -i "/store volatile i32.*PROFCOUNTER_COMM_DUMMY_VAR.*/d" $1

# Substitute the __PROFCOUNTER_COMM_DUMMY_VAR__ calls by write_pipe calls in a very very very hacky way
sed -i "s/  %.* = add i32 %.*PROFCOUNTER_COMM_DUMMY_VAR.*, \\([0-9]\\+\\), !dbg.*/  store i32 \\1, i32 addrspace(4)* %p0/g" $1
sed -i "s/  %.* = add i32 \\([0-9]\\+\\), %.*PROFCOUNTER_COMM_DUMMY_VAR.*, !dbg.*/  store i32 \\1, i32 addrspace(4)* %p0/g" $1

exit