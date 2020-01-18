# Get variables used by elaborate
set effort__ [get_config_compile -effort]
set skip_syncheck__ [get_config_compile -skip_syncheck]
set keep_printf__ [get_config_compile -keep_printf]
set lm__ [get_config_compile -lm]
set skip_cdt__ [get_config_compile -skip_cdt]
set skip_transform__ [get_config_compile -skip_transform]
set ng__ [get_config_compile -ng]
set g__ [get_config_compile -g]
set opt_fp__ [get_config_compile -opt_fp]
set db_path__ [auto_get_db]

config_compile -skip_cdt=$skip_cdt__
puts "Performing elaboration"
elaborate -effort=$effort__ -skip_syncheck=$skip_syncheck__ -keep_printf=$keep_printf__ -lm=$lm__ -skip_cdt=$skip_cdt__ -skip_transform=$skip_transform__ -ng=$ng__ -g=$g__ -opt_fp=$opt_fp__ -from_csynth_design=1

puts "Injecting ProfCounter calls"
disassemble $db_path__/a.o.3 $db_path__/temp
exec bash $::env(PROFCOUNTERSRCROOT)/transform.sh $db_path__/temp.ll

puts "Performing final transform"
transform -loop-bound -cdfg-build $db_path__/temp.ll -o $db_path__/a.o.3.bc -f -phase build-ssdm

puts "Performing HLS"
autosyn -from_csynth_design=1

puts "Exporting design"
export_design -format ipxact -kernel_drc -sdaccel -ipname bicg

# Override the rest of the normal RTL generation process
close_project
puts "Vivado HLS completed successfully"
exit
