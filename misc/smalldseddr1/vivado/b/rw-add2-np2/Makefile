# Default tools
CC=aarch64-linux-gnu-gcc
VIVADO=$(XILINX_VIVADO)/bin/vivado
XOCC=$(XILINX_SDX)/bin/xocc

# sanitize_dsa: create a filesystem-friendly name from DSA name
#               $(1): name of DSA
COLON=:
PERIOD=.
UNDERSCORE=_
sanitize_dsa = $(strip $(subst $(PERIOD),$(UNDERSCORE),$(subst $(COLON),$(UNDERSCORE),$(1))))
device2dsa = $(if $(filter $(suffix $(1)),.xpfm),$(shell $(COMMON_REPO)/utility/parsexpmf.py $(1) dsa 2>/dev/null),$(1))
device2sandsa = $(call sanitize_dsa,$(call device2dsa,$(1)))
device2dep = $(if $(filter $(suffix $(1)),.xpfm),$(dir $(1))/$(shell $(COMMON_REPO)/utility/parsexpmf.py $(1) hw 2>/dev/null) $(1),)

# Standard variable. Run "make TARGET=... PLATFORM=... CLKID=..." to customise
TARGET=hw
PLATFORM=zcu102
CLKID=0
DSA = $(call device2sandsa, $(PLATFORM))

# CC compile and link flags
CCFLAGS=-fPIC -DCOMMON_COLOURED_PRINTS -Iinclude -I../../base/include -O2 -Wall -I$(XILINX_SDX)/runtime/include/1_2/ -I/$(XILINX_SDX)/Vivado_HLS/include/
CCLINKFLAGS=-lm -lxilinxopencl -lpthread -lrt -ldl -lcrypt -L$(XILINX_SDX)/runtime/lib/aarch64

# XOCC compile flags
XOCCFLAGS=-t $(TARGET) --platform $(PLATFORM) -Iinclude -I../../base/include --save-temps --clkid $(CLKID)
BANKFLAGS=--max_memory_ports test --sp test_1.m_axi_gmem0:bank0 --sp test_1.m_axi_gmem1:bank1 --sp test_2.m_axi_gmem2:bank2 --sp test_3.m_axi_gmem3:bank3

# By default, auto-pipelining is disabled
AUTOPIPELINING=no
ifeq ($(AUTOPIPELINING), no)
    PIPELININGFLAG=--xp param:compiler.enableAutoPipelining=false
else
    PIPELININGFLAG=
endif

# checkForVivado: check if vivado binary is reachable
define checkForVivado
    $(if $(wildcard $(VIVADO)), , $(error vivado binary not found, please check your Xilinx installation and/or the init script, e.g. /path/to/xilinx/SDx/20xx.x/settings64.sh))
endef

# checkForXilinxVivado: check if XILINX_VIVADO is set
define checkForXilinxVivado
	$(if $(XILINX_VIVADO), , $(error XILINX_VIVADO is not set or empty, perhaps forgot to source the init script, e.g. /path/to/xilinx/SDx/20xx.x/settings64.sh?))
endef

# checkForPlatform: check if PLATFORM is set
define checkForPlatform
	$(if $(PLATFORM), , $(error PLATFORM is empty))
endef

# checkForTarget: check if TARGET is set
define checkForTarget
	$(if $(TARGET), , $(error TARGET is empty. Please set to a compatible target: hw, hw_emu or sw_emu))
endef

# checkForXOCC: check if xocc binary is reachable
define checkForXOCC
    $(if $(wildcard $(XOCC)), , $(error xocc binary not found, please check your Xilinx installation and/or the init script, e.g. /path/to/xilinx/SDx/20xx.x/settings64.sh))
endef

# checkForXilinxSDx: check if XILINX_SDX is set
define checkForXilinxSDx
    $(if $(XILINX_SDX), , $(error XILINX_SDX is not set or empty, perhaps forgot to source the init script, e.g. /path/to/xilinx/SDx/20xx.x/settings64.sh?))
endef

# checkForCC: check if CC binary is reachable
define checkForCC
	$(if $(findstring 0, $(shell which $(CC) &> /dev/null; echo $$?)), , $(error CC binary not found, please check your Xilinx installation and/or the init script, e.g. /path/to/xilinx/SDx/20xx.x/settings64.sh))
endef

# checkForHostBinary: check if stuff for compiling host binary is properly set
define checkForHostBinary
	$(call checkForTarget)
	$(call checkForCC)
	$(call checkForXilinxSDx)
endef

# checkForXclbin: check if stuff for synthesising OpenCL kernels is properly set
define checkForXclbin
	$(call checkForXilinxSDx)
	$(call checkForXOCC)
	$(call checkForTarget)
	$(call checkForPlatform)
endef

# checkForXo: check if stuff for creating OpenCL objects is properly set
define checkForXo
	$(call checkForXilinxVivado)
	$(call checkForVivado)
	$(call checkForTarget)
	$(call checkForPlatform)
endef

# Top-level make command for hw generation
.PHONY: hw
hw: fpga/hw/$(DSA)/sd_card/execute

# Make command for host binary
.PHONY: host
host: fpga/$(TARGET)/$(DSA)/execute

# Make command for OpenCL synthesis
.PHONY: xclbin
xclbin: fpga/$(TARGET)/$(DSA)/program.xclbin

# Make command for OpenCL objects
.PHONY: xo
xo: fpga/$(TARGET)/$(DSA)/test.xo

# Copies host executable to SD folder
fpga/$(TARGET)/$(DSA)/sd_card/execute: fpga/$(TARGET)/$(DSA)/execute fpga/$(TARGET)/$(DSA)/program.xclbin
	$(call checkForTarget)
	cp fpga/$(TARGET)/$(DSA)/execute fpga/$(TARGET)/$(DSA)/sd_card/execute

# Compiles host executable
fpga/$(TARGET)/$(DSA)/execute: src/host.c include/common.h
	$(call checkForHostBinary)
	mkdir -p fpga/$(TARGET)/$(DSA)
	$(CC) src/host.c -o fpga/$(TARGET)/$(DSA)/execute $(CCFLAGS) $(CCLINKFLAGS)

# Synthesises OpenCL kernels
fpga/$(TARGET)/$(DSA)/program.xclbin: fpga/$(TARGET)/$(DSA)/test.xo
	$(call checkForXclbin)
	$(XOCC) $(XOCCFLAGS) $(XOCCLDFLAGS) -lo fpga/$(TARGET)/$(DSA)/program.xclbin fpga/$(TARGET)/$(DSA)/test.xo --sys_config ocl -R2
	# Append two lines to init.sh, responsible for writing onto /etc/profile the XILINX_OPENCL export and cd to /mnt
	echo -e "\necho -e \"\\\\nexport XILINX_OPENCL=/mnt/embedded_root\" >> /etc/profile" >> fpga/$(TARGET)/$(DSA)/sd_card/init.sh
	echo -e "echo -e \"cd /mnt\" >> /etc/profile" >> fpga/$(TARGET)/$(DSA)/sd_card/init.sh

# Compiles OpenCL object for test kernel
fpga/$(TARGET)/$(DSA)/test.xo: src/test.cl
	$(call checkForXo)
	mkdir -p fpga/$(TARGET)/$(DSA)
	$(XOCC) $(BANKFLAGS) $(XOCCFLAGS) -c --messageDb fpga/$(TARGET)/$(DSA)/test.mdb -Iinclude -I../../base/include --xp misc:solution_name=_xocc_compile --xp param:compiler.version=31 $(PIPELININGFLAG) src/test.cl -o fpga/$(TARGET)/$(DSA)/test.xo -R2

# Clean all
.PHONY: clean
clean:
	rm -rf .Xil _x vivado*.jou vivado*.log xocc*.log fpga

