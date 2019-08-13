#!/bin/bash

# XXX: Maybe some of the latency and Rec/Res values should be tail -n 1 instead of head -n 1 as well

grep "Estimated cycles" $1 | tail -n 1 | sed "s/.* Estimated cycles: \\(.*\\)/\\1,/g" | tr -d "\n"

grep "Target loop:" $1 | sed "s/.*_\\([0-9]*\\)/\\1,/g" | tr -d "\n"
grep "Target unroll factor:" $1 | sed "s/.* Target unroll factor: \\(.*\\)/\\1,/g" | tr -d "\n"
grep "Number of nodes:" $1 | head -n 1 | sed "s/\t\tNumber of nodes: \\(.*\\)/\\1,/g" | tr -d "\n"
grep "Number of edges:" $1 | head -n 1 | sed "s/\t\tNumber of edges: \\(.*\\)/\\1,/g" | tr -d "\n"
grep "Number of register dependencies:" $1 | head -n 1 | sed "s/\t\tNumber of register dependencies: \\(.*\\)/\\1,/g" | tr -d "\n"
grep "Number of memory dependencies:" $1 | head -n 1 | sed "s/\t\tNumber of memory dependencies: \\(.*\\)/\\1,/g" | tr -d "\n"

grep "		Latency:" $1 | head -n 1 | sed "s/\t\tLatency: \\(.*\\)/\\1,/g" | tr -d "\n"
grep "		DSPs:" $1 | head -n 1 | sed "s/\t\tDSPs: \\(.*\\)/\\1,/g" | tr -d "\n"
grep "		FFs:" $1 | head -n 1 | sed "s/\t\tFFs: \\(.*\\)/\\1,/g" | tr -d "\n"
grep "		LUTs:" $1 | head -n 1 | sed "s/\t\tLUTs: \\(.*\\)/\\1,/g" | tr -d "\n"
grep "		fAdd units:" $1 | head -n 1 | sed "s/\t\tfAdd units: \\(.*\\)/\\1,/g" | tr -d "\n"
grep "		fSub units:" $1 | head -n 1 | sed "s/\t\tfSub units: \\(.*\\)/\\1,/g" | tr -d "\n"
grep "		fMul units:" $1 | head -n 1 | sed "s/\t\tfMul units: \\(.*\\)/\\1,/g" | tr -d "\n"
grep "		fDiv units:" $1 | head -n 1 | sed "s/\t\tfDiv units: \\(.*\\)/\\1,/g" | tr -d "\n"

grep "^	IL:" $1 | head -n 1 | sed "s/\tIL: \\(.*\\)/\\1,/g" | tr -d "\n"
grep "^	DSPs:" $1 | tail -n 1 | sed "s/\tDSPs: \\(.*\\)/\\1,/g" | tr -d "\n"
grep "^	BRAM18k:" $1 | tail -n 1 | sed "s/\tBRAM18k: \\(.*\\)/\\1,/g" | tr -d "\n"
grep "^	FFs:" $1 | tail -n 1 | sed "s/\tFFs: \\(.*\\)/\\1,/g" | tr -d "\n"
grep "^	LUTs:" $1 | tail -n 1 | sed "s/\tLUTs: \\(.*\\)/\\1,/g" | tr -d "\n"
grep "^	fAdd units:" $1 | tail -n 1 | sed "s/\tfAdd units: \\(.*\\)/\\1,/g" | tr -d "\n"
grep "^	fSub units:" $1 | tail -n 1 | sed "s/\tfSub units: \\(.*\\)/\\1,/g" | tr -d "\n"
grep "^	fMul units:" $1 | tail -n 1 | sed "s/\tfMul units: \\(.*\\)/\\1,/g" | tr -d "\n"
grep "^	fDiv units:" $1 | tail -n 1 | sed "s/\tfDiv units: \\(.*\\)/\\1,/g" | tr -d "\n"
grep "^	II:" $1 | head -n 1 | sed "s/\tII: \\(.*\\)/\\1,/g" | tr -d "\n"
grep "^	RecII:" $1 | head -n 1 | sed "s/\tRecII: \\(.*\\)/\\1,/g" | tr -d "\n"
grep "^	ResIIMem:" $1 | head -n 1 | sed "s/\tResIIMem: \\(.*\\) constrained.*/\\1,/g" | tr -d "\n"
grep "^	ResIIOp:" $1 | head -n 1 | sed "s/\tResIIOp: \\(.*\\) constrained.*/\\1,/g" | tr -d "\n"

if [ $(grep -c "Performing final latency calculation" $1) == "0" ]; then
	grep "		Number of partitions for array" $1 | sed "s/\t\tNumber of partitions for array \"\\(.*\\)\": \\(.*\\)/\\1,\\2,/g" | tr -d "\n"
	# Fill with remaining tabs for alignment
	for i in `seq $2 4`; do echo -ne " , ,"; done
	grep "^	Number of partitions for array" $1 | sed "s/\tNumber of partitions for array \"\\(.*\\)\": \\(.*\\)/\\1,\\2,/g" | tr -d "\n"
	# Fill with remaining tabs for alignment
	for i in `seq $2 4`; do echo -ne " , ,"; done
	grep "^	Used BRAM18k for array" $1 | sed "s/\tUsed BRAM18k for array \"\\(.*\\)\": \\(.*\\)/\\1,\\2,/g" | tr -d "\n"
	# Fill with remaining tabs for alignment
	for i in `seq $2 4`; do echo -ne " , ,"; done
	grep "^	Memory efficiency for array" $1 | sed "s/\tMemory efficiency for array \"\\(.*\\)\": \\(.*\\)/\\1,\\2,/g" | tr -d "\n"
	# Fill with remaining tabs for alignment
	for i in `seq $2 4`; do echo -ne " , ,"; done
else
	grep -A 99 "Performing final latency calculation" $1 | grep "	Number of partitions for array" | sed "s/\tNumber of partitions for array \"\\(.*\\)\": \\(.*\\)/\\1,\\2,/g" | tr -d "\n"
	# Fill with remaining tabs for alignment
	for i in `seq $2 4`; do echo -ne " , ,"; done
	grep -A 99 "Performing final latency calculation" $1 | grep "^	Number of partitions for array" | sed "s/\tNumber of partitions for array \"\\(.*\\)\": \\(.*\\)/\\1,\\2,/g" | tr -d "\n"
	# Fill with remaining tabs for alignment
	for i in `seq $2 4`; do echo -ne " , ,"; done
	grep -A 99 "Performing final latency calculation" $1 | grep "^	Used BRAM18k for array" | sed "s/\tUsed BRAM18k for array \"\\(.*\\)\": \\(.*\\)/\\1,\\2,/g" | tr -d "\n"
	# Fill with remaining tabs for alignment
	for i in `seq $2 4`; do echo -ne " , ,"; done
	grep -A 99 "Performing final latency calculation" $1 | grep "^	Memory efficiency for array" | sed "s/\tMemory efficiency for array \"\\(.*\\)\": \\(.*\\)/\\1,\\2,/g" | tr -d "\n"
	# Fill with remaining tabs for alignment
	for i in `seq $2 4`; do echo -ne " , ,"; done
fi

echo ""
