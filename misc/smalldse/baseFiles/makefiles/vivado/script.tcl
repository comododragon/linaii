open_project <KERN>
set_top <KERN>
add_files src/<KERN>.cpp -cflags "-Iinclude"
add_files include/common.h
add_files -tb include/<KERN>.h
add_files -tb src/main.cpp -cflags "-Iinclude -lm"
open_solution "solution1"
set_part {<PART>}
create_clock -period 10 -name default
set_clock_uncertainty <UNC>%
csim_design
csynth_design
exit