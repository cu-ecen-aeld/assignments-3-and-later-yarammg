#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}
mkdir -p "${OUTDIR}/arm-cross-compiler"
cd "${OUTDIR}/arm-cross-compiler"
wget -O gcc-arm.tar.xz https://developer.arm.com/-/media/Files/downloads/gnu/13.3.rel1/binrel/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu.tar.xz
    mkdir -p install 
    tar x -C install -f gcc-arm.tar.xz 
    rm -r gcc-arm.tar.xz
    export PATH="$PATH:/${OUTDIR}/arm-cross-compiler/install/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/bin"
cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    sudo apt-get update && sudo apt-get install -y u-boot-tools kmod cpio flex bison libssl-dev psmisc libncurses-dev
    sudo apt-get install -y qemu-system-arm
    make defconfig
    make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- mrproper
    make ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- defconfig
    make -j4 ARCH=arm64 CROSS_COMPILE=aarch64-none-linux-gnu- all
    
fi

echo "Adding the Image in outdir"
echo "The current path is: $(pwd)"
cp "${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image" ${OUTDIR}
echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

mkdir -p "rootfs"
cd "rootfs"
mkdir -p bin dev etc home lib lib64 proc sbin tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log

cd "$OUTDIR"

if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
else
    cd busybox
fi

make distclean
make defconfig
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -j$(nproc)
make CONFIG_PREFIX="${OUTDIR}/rootfs"  ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install
sudo chmod u+s ${OUTDIR}/busybox/busybox
echo "Library dependencies"
${CROSS_COMPILE}readelf -a ${OUTDIR}/busybox/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a ${OUTDIR}/busybox/busybox | grep "Shared library"

cd ${OUTDIR}/rootfs
cp "${OUTDIR}/arm-cross-compiler/install/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/aarch64-none-linux-gnu/libc/lib/ld-linux-aarch64.so.1" "lib/"
cp "${OUTDIR}/arm-cross-compiler/install/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/aarch64-none-linux-gnu/libc/lib64/libm.so.6" "lib64/"
cp "${OUTDIR}/arm-cross-compiler/install/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/aarch64-none-linux-gnu/libc/lib64/libresolv.so.2" "lib64/"
cp "${OUTDIR}/arm-cross-compiler/install/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/aarch64-none-linux-gnu/libc/lib64/libc.so.6" "lib64/"

sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 666 dev/tty1 c 4 1

cd ${FINDER_APP_DIR}
make clean
${CROSS_COMPILE}gcc -o ${OUTDIR}/rootfs/home/writer writer.c
chmod 777 finder.sh
# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
#cp $FINDER_APP_DIR/writer ${OUTDIR}/rootfs/home
cp $FINDER_APP_DIR/finder.sh ${OUTDIR}/rootfs/home
cp $FINDER_APP_DIR/finder-test.sh ${OUTDIR}/rootfs/home
mkdir -p ${OUTDIR}/rootfs/home/conf
cp $FINDER_APP_DIR/conf/username.txt ${OUTDIR}/rootfs/home/conf/username.txt
cp $FINDER_APP_DIR/conf/assignment.txt  ${OUTDIR}/rootfs/home/conf/assignment.txt
cp $FINDER_APP_DIR/autorun-qemu.sh  ${OUTDIR}/rootfs/home

# TODO: Chown the root directory
sudo chown -R root:root ${OUTDIR}/rootfs

# TODO: Create initramfs.cpio.gz
cd ${OUTDIR}/rootfs
find . | cpio -H newc -o --owner root:root > ${OUTDIR}/initramfs.cpio
cd ${OUTDIR}
gzip -f initramfs.cpio
#cp "initramfs.cpio.gz" ${OUTDIR}


