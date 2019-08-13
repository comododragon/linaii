.PHONY: vivado
vivado: <KERN>/solution1/syn/report/<KERN>_csynth.rpt

<KERN>/solution1/syn/report/<KERN>_csynth.rpt: src/main.cpp src/<KERN>.cpp include/<KERN>.h include/common.h
	vivado_hls -f script.tcl

.PHONY: clean
clean:
	rm -rf <KERN> vivado_hls.log