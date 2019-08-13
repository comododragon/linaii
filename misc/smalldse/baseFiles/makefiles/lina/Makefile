.PHONY: profile
profile: prof/linked_opt.bc
	cd prof; lina -p -c ../src/config.cfg -v -l 0 <ARGS> linked_opt.bc <KERN>

.PHONY: profile_old
profile_old: prof/linked_opt.bc
	cd prof; lin-analyzer -p -c ../src/config.cfg -v -l 0 <ARGS> linked_opt.bc <KERN>

prof/linked_opt.bc: prof/linked.bc
	opt -mem2reg -instnamer -lcssa -indvars prof/linked.bc -o prof/linked_opt.bc

prof/linked.bc: obj/main.bc obj/<KERN>.bc
	mkdir -p prof/
	llvm-link obj/main.bc obj/<KERN>.bc -o prof/linked.bc

obj/main.bc: src/main.cpp include/<KERN>.h include/common.h
	mkdir -p obj/
	clang -g -O1 -Iinclude -emit-llvm -c src/main.cpp -o obj/main.bc

obj/<KERN>.bc: src/<KERN>.cpp include/<KERN>.h include/common.h
	mkdir -p obj/
	clang -g -O1 -Iinclude -emit-llvm -c src/<KERN>.cpp -o obj/<KERN>.bc

.PHONY: run
run: bin/run

bin/run: obj/main.o obj/<KERN>.o
	mkdir -p bin/
	clang obj/main.o obj/<KERN>.o -o bin/run

obj/main.o: src/main.cpp include/<KERN>.h include/common.h
	mkdir -p obj/
	clang -Iinclude -c src/main.cpp -o obj/main.o

obj/<KERN>.o: src/<KERN>.cpp include/<KERN>.h include/common.h
	mkdir -p obj/
	clang -Iinclude -c src/<KERN>.cpp -o obj/<KERN>.o

.PHONY: clean
clean:
	rm -rf bin/ obj/ prof/
