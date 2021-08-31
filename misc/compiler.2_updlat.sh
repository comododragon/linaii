#!/bin/bash

# Patch function
function applyPatches {
	echo -e "\u001b[1m\u001b[92m--> Applying patches...\u001b[0m"
	sed -i "s/\\(    IntrusiveRefCntPtr(IntrusiveRefCntPtr<X>&& S) : Obj(S.get()) {\\)/    friend class IntrusiveRefCntPtr;\n\n    template <class X>\n\\1/g" llvm/include/llvm/ADT/IntrusiveRefCntPtr.h
}

# Exit if any command fails
set -e

echo -e "\u001b[1m\u001b[92m--> Starting LINA compiler...\u001b[0m."

echo -e "\u001b[1m\u001b[92m--> This will compile Lina on your machine and will take some time\u001b[0m"
echo -e "\u001b[1m\u001b[93m--> Any previous compile attempt in this folder will be removed!\u001b[0m"
echo -e "\u001b[1m\u001b[93m--> Please confirm that you have GCC/GLIBC/etc, GIT, ZLIB and CMAKE installed!\u001b[0m"
echo -ne "\u001b[1m\u001b[92m--> Press ENTER to proceed or CTRL+C to abort... \u001b[0m"
read

echo -e "\u001b[1m\u001b[92m--> Cleaning up any previous compile...\u001b[0m"
rm -rf llvm-3.5.0.src.tar.xz
rm -rf llvm-3.5.0.src
rm -rf llvm
rm -rf cfe-3.5.0.src.tar.xz
rm -rf cfe-3.5.0.src
rm -rf llvm/tools/clang
rm -rf lin-analyzer
rm -rf llvm/tools/lin-analyzer
rm -rf boost_1_57_0.tar.gz
rm -rf boost_1_57_0
rm -rf boost
rm -rf build

echo -e "\u001b[1m\u001b[92m--> Downloading LLVM 3.5.0...\u001b[0m"
wget "http://releases.llvm.org/3.5.0/llvm-3.5.0.src.tar.xz"
echo -e "\u001b[1m\u001b[92m--> Extracting LLVM...\u001b[0m"
tar xvf llvm-3.5.0.src.tar.xz
mv llvm-3.5.0.src llvm
rm llvm-3.5.0.src.tar.xz

echo -e "\u001b[1m\u001b[92m--> Downloading CLANG 3.5.0...\u001b[0m"
wget "http://releases.llvm.org/3.5.0/cfe-3.5.0.src.tar.xz"
echo -e "\u001b[1m\u001b[92m--> Extracting CLANG...\u001b[0m"
tar xvf cfe-3.5.0.src.tar.xz
mv cfe-3.5.0.src llvm/tools/clang
rm cfe-3.5.0.src.tar.xz

echo -e "\u001b[1m\u001b[92m--> Cloning LINA...\u001b[0m"
git clone -b 2_updlat https://github.com/comododragon/lina.git
mv lina llvm/tools/lin-analyzer

echo -e "\u001b[1m\u001b[92m--> Adapting CMAKE files...\u001b[0m"
sed -i "s/add_llvm_tool_subdirectory(opt)/add_llvm_tool_subdirectory(lin-analyzer)\nadd_llvm_tool_subdirectory(opt)/g" llvm/tools/CMakeLists.txt
echo -e "\nset(LLVM_REQUIRES_RTTI 1)" >> llvm/CMakeLists.txt

echo -e "\u001b[1m\u001b[92m--> Downloading BOOST...\u001b[0m"
wget "http://sourceforge.net/projects/boost/files/boost/1.57.0/boost_1_57_0.tar.gz"
echo -e "\u001b[1m\u001b[92m--> Extracting BOOST...\u001b[0m"
tar xvf boost_1_57_0.tar.gz
mv boost_1_57_0 boost
rm boost_1_57_0.tar.gz

echo -e "\u001b[1m\u001b[92m--> Preparing build folder...\u001b[0m"
mkdir build

ZLIBPATH=/usr/lib/libz.so
if [ ! -e $ZLIBPATH ]; then
	echo -ne "\u001b[1m\u001b[92m--> ZLIB not found at /usr/lib/libz.so. Please set an alternate path: \u001b[0m"
	read ZLIBPATH
fi

echo -e "\u001b[1m\u001b[92m--> Running CMAKE...\u001b[0m"
cd build
cmake ../llvm -DBOOST_INCLUDE_DIR=../boost -DZLIB_INCLUDE_DIRS=/usr/include -DZLIB_LIBRARY=$ZLIBPATH -DLLVM_ENABLE_RTTI:BOOL=ON
cd ..

echo -e "\u001b[1m\u001b[92m--> Applying patches...\u001b[0m"
echo -e "New versions of GCC can have some trouble compiling old codes such as LLVM 3.5.0"
echo -e "We can apply some patches to make the compilation go smooth"
echo -e "(you will probably need these patches if you are using an updated operating system, however you may"
echo -e "skip if you are unsure. We will ask again if compilation goes wrong)"
echo -ne "\u001b[1m\u001b[92m--> Apply patches? [Y/n] \u001b[0m"
read INPUT

PATCHESAPPLIED=true
if [ "n" == "$INPUT" ]; then
	echo -e "\u001b[1m\u001b[92m--> Patch aborted\u001b[0m"
	PATCHESAPPLIED=false
else
	applyPatches
fi

echo -e "\u001b[1m\u001b[93m--> Everything is set!\u001b[0m"
echo -e "\u001b[1m\u001b[93m--> You can quit now and manually compile using \"make\" or you can proceed to automatic compilation\u001b[0m"
echo -ne "\u001b[1m\u001b[92m--> Press ENTER to proceed or CTRL+C to stop here... \u001b[0m"
read

# Deactivate exiting for any error, we will manually do this from now
set +e

while true; do
	echo -e "\u001b[1m\u001b[92m--> Running make...\u001b[0m"
	make -C build

	if [ $? -ne 0 ]; then
		echo -e "\u001b[1m\u001b[93m--> It looks like that something went wrong.\u001b[0m"

		if $PATCHESAPPLIED; then
			echo -e "\u001b[1m\u001b[92m--> Compile aborted :(\u001b[0m"
			echo -e "Since our patches were already applied, there isn't much that we can do from this script"
			echo -e "You can try compiling on your own (the folders are left intact)"
			echo -e "or you can ask for help on the great internet community or file a issue on the github repo"

			break
		else
			echo -e "New versions of GCC can have some trouble compiling old codes such as LLVM 3.5.0"
			echo -e "We can apply some patches to make the compilation go smooth"
			echo -ne "\u001b[1m\u001b[92m--> Press ENTER to apply and re-run make or CTRL+C to abort \u001b[0m"
			read

			applyPatches
			PATCHESAPPLIED=true
		fi
	else
		echo -e "\u001b[1m\u001b[93m--> Done!\u001b[0m"
		echo -e "\u001b[1m\u001b[92m--> Binaries are at build/bin \u001b[0m"

		break
	fi
done

echo -e "\u001b[1m\u001b[93m--> See ya!\u001b[0m"
