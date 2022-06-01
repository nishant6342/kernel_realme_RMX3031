#!/bin/bash

function compile() 
{
source ~/.bashrc && source ~/.profile
export LC_ALL=C && export USE_CCACHE=1
ccache -M 100G
export ARCH=arm64
export KBUILD_BUILD_HOST=origin
export KBUILD_BUILD_USER="nishant6342"
clangbin=clang/bin/clang
if ! [ -a $clangbin ]; then git clone --depth=1 https://github.com/kdrag0n/proton-clang clang
fi
gcc64bin=los-4.9-64/bin/aarch64-linux-android-as
if ! [ -a $gcc64bin ]; then git clone --depth=1 https://github.com/LineageOS/android_prebuilts_gcc_linux-x86_aarch64_aarch64-linux-android-4.9 los-4.9-64
fi
gcc32bin=los-4.9-32/bin/arm-linux-androideabi-as
if ! [ -a $gcc32bin ]; then git clone --depth=1 https://github.com/LineageOS/android_prebuilts_gcc_linux-x86_arm_arm-linux-androideabi-4.9 los-4.9-32
fi

[ -d "out" ] && rm -rf out || mkdir -p out

make O=out ARCH=arm64 mt6893_defconfig

PATH="${PWD}/clang/bin:${PATH}:${PWD}/los-4.9-32/bin:${PATH}:${PWD}/los-4.9-64/bin:${PATH}" \
make -j$(nproc --all) O=out \
                      ARCH=arm64 \
                      CC="clang" \
                      CLANG_TRIPLE=aarch64-linux-gnu- \
                      CROSS_COMPILE="${PWD}/los-4.9-64/bin/aarch64-linux-android-" \
                      CROSS_COMPILE_ARM32="${PWD}/los-4.9-32/bin/arm-linux-androideabi-" \
                      CONFIG_NO_ERROR_ON_MISMATCH=y
}

function zupload()
{
zimage=out/arch/arm64/boot/Image.gz-dtb
if ! [ -a $zimage ];
then
echo  " Failed to compile zImage, fix the errors first "
else
echo -e " Build succesful, generating flashable zip now "
anykernelbin=AnyKernel/anykernel.sh
if ! [ -a $anykernelbin ]; then git clone --depth=1 https://github.com/nishant6342/AnyKernel3 -b cupida  AnyKernel
fi
cp out/arch/arm64/boot/Image.gz-dtb AnyKernel
cd AnyKernel
zip -r9 ORIGIN-OSS-KERNEL-RMX3031.zip *
#curl --upload-file ORIGIN-OSS-KERNEL-RMX3031.zip https://transfer.sh/
curl -sL https://git.io/file-transfer | sh
./transfer wet ORIGIN-OSS-KERNEL-RMX3031.zip
cd ../
fi
}

compile
zupload
