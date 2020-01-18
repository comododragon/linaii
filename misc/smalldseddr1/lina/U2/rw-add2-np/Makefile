.PHONY: profile
profile: linked_opt.bc
	lina -p -c config.cfg -v -l 0 -t ZCU104 -f 100 -u 27 --f-npla --show-pre-dddg --show-post-dddg --show-scheduling --mem-trace linked_opt.bc test
	rm -f test.log
	mv *.log test.log

linked_opt.bc: linked.bc
	opt -mem2reg -instnamer -lcssa -indvars linked.bc -o linked_opt.bc

linked.bc: main.bc test.bc
	llvm-link main.bc test.bc -o linked.bc

main.bc: test_tb.cpp test.h test_tb.h
	clang -g -O1 -emit-llvm -c test_tb.cpp -o main.bc

test.bc: test.cpp test.h
	clang -g -O1 -emit-llvm -c test.cpp -o test.bc

.PHONY: clean
clean:
	rm -rf *.bc *.log *.dot *.rpt *.gz
