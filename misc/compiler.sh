#!/bin/bash

# Patch function
function applyPatches {
	echo -e "\u001b[43;1mApplying patches...\u001b[0m"
	sed -i "s/\\(    IntrusiveRefCntPtr(IntrusiveRefCntPtr<X>&& S) : Obj(S.get()) {\\)/    friend class IntrusiveRefCntPtr;\n\n    template <class X>\n\\1/g" llvm/include/llvm/ADT/IntrusiveRefCntPtr.h
}

# Exit if any command fails
set -e

echo -e "\u001b[43;1mStarting LINA compiler...\u001b[0m."

echo -e "\u001b[43;1mThis will compile Lina on your machine and will take some time\u001b[0m"
echo -e "\u001b[41;1mAny previous compile attempt in this folder will be removed!\u001b[0m"
echo -e "\u001b[41;1mPlease confirm that you have GCC/GLIBC/etc, GIT, ZLIB and CMAKE installed!\u001b[0m"
echo -ne "\u001b[43;1mPress ENTER to proceed or CTRL+C to abort... \u001b[0m"
read

echo -e "\u001b[43;1mCleaning up any previous compile...\u001b[0m"
rm -rf llvm-3.5.0.src.tar.xz
rm -rf llvm-3.5.0.src
rm -rf llvm
rm -rf cfe-3.5.0.src.tar.xz
rm -rf cfe-3.5.0.src
rm -rf llvm/tools/clang
rm -rf lina
rm -rf llvm/tools/lina
rm -rf boost_1_57_0.tar.gz
rm -rf boost_1_57_0
rm -rf boost
rm -rf build

echo -e "\u001b[43;1mDownloading LLVM 3.5.0...\u001b[0m"
wget "http://releases.llvm.org/3.5.0/llvm-3.5.0.src.tar.xz"
echo -e "\u001b[43;1mExtracting LLVM...\u001b[0m"
tar xvf llvm-3.5.0.src.tar.xz
mv llvm-3.5.0.src llvm
rm llvm-3.5.0.src.tar.xz

echo -e "\u001b[43;1mDownloading CLANG 3.5.0...\u001b[0m"
wget "http://releases.llvm.org/3.5.0/cfe-3.5.0.src.tar.xz"
echo -e "\u001b[43;1mExtracting CLANG...\u001b[0m"
tar xvf cfe-3.5.0.src.tar.xz
mv cfe-3.5.0.src llvm/tools/clang
rm cfe-3.5.0.src.tar.xz

echo -e "\u001b[43;1mCloning LINA...\u001b[0m"
git clone https://github.com/comododragon/lina.git
mv lina llvm/tools/lina

echo -e "\u001b[43;1mAdapting CMAKE files...\u001b[0m"
sed -i "s/add_llvm_tool_subdirectory(opt)/add_llvm_tool_subdirectory(lina)\nadd_llvm_tool_subdirectory(opt)/g" llvm/tools/CMakeLists.txt
echo -e "\nset(LLVM_REQUIRES_RTTI 1)" >> llvm/CMakeLists.txt

echo -e "\u001b[43;1mDownloading BOOST...\u001b[0m"
wget "http://sourceforge.net/projects/boost/files/boost/1.57.0/boost_1_57_0.tar.gz"
echo -e "\u001b[43;1mExtracting BOOST...\u001b[0m"
tar xvf boost_1_57_0.tar.gz
mv boost_1_57_0 boost
rm boost_1_57_0.tar.gz

echo -e "\u001b[43;1mPreparing build folder...\u001b[0m"
mkdir build

echo -e "\u001b[43;1mRunning CMAKE...\u001b[0m"
cd build
cmake ../llvm -DBOOST_INCLUDE_DIR=../boost -DZLIB_INCLUDE_DIRS=/usr/include -DZLIB_LIBRARY=/usr/lib/libz.so -DLLVM_ENABLE_RTTI:BOOL=ON
cd ..

echo -e "\u001b[43;1mApplying patches...\u001b[0m"
echo -e "New versions of GCC can have some trouble compiling old codes such as LLVM 3.5.0"
echo -e "We can apply some patches to make the compilation go smooth"
echo -e "(if you are unsure, just press ENTER to not patch. We will ask again if compilation goes wrong)"
echo -ne "\u001b[43;1mApply patches? [y/N] \u001b[0m"
read INPUT

PATCHESAPPLIED=false
if [ "y" == "$INPUT" ]; then
	applyPatches
	PATCHESAPPLIED=true
else
	echo -e "\u001b[43;1mPatch aborted\u001b[0m"
fi

echo -e "\u001b[41;1mEverything is set!\u001b[0m"
echo -e "\u001b[41;1mYou can quit now and manually compile using \"make\" or you can proceed to automatic compilation\u001b[0m"
echo -ne "\u001b[43;1mPress ENTER to proceed or CTRL+C to stop here... \u001b[0m"
read

# Deactivate exiting for any error, we will manually do this from now
set +e

while true; do
	echo -e "\u001b[43;1mRunning make...\u001b[0m"
	make -C build

	if [ $? -ne 0 ]; then
		echo -e "\u001b[41;1mIt looks like that something went wrong.\u001b[0m"

		if $PATCHESAPPLIED; then
			echo -e "\u001b[43;1mCompile aborted :(\u001b[0m"
			echo -e "Since our patches were already applied, there isn't much that we can do from this script"
			echo -e "You can try compiling on your own (the folders are left intact)"
			echo -e "or you can ask for help on the great internet community or file a issue on the github repo"

			break
		else
			echo -e "New versions of GCC can have some trouble compiling old codes such as LLVM 3.5.0"
			echo -e "We can apply some patches to make the compilation go smooth"
			echo -ne "\u001b[43;1mPress ENTER to apply and re-run make or CTRL+C to abort \u001b[0m"
			read

			applyPatches
			PATCHESAPPLIED=true
		fi
	else
		echo -e "\u001b[41;1mDone!\u001b[0m"
		echo -e "\u001b[43;1mBinaries are at build/bin \u001b[0m"

		break
	fi
done

echo -e "\u001b[41;1mSee ya!\u001b[0m"
