#!/bin/bash

pwd=`pwd`

# Function to display help information
show_help() {
    echo "Usage: $0 [options] [project_name]"
    echo
    echo "Options:"
    echo "  -h, --help        Display this help message and exit"
    echo "  -p, --project     name and directory of the project to be built, defualt projcet is e100"
    echo "  -n, --nfs         directory of your nfs server"
    echo "  -s, --symbols     Keep debug symbols in outpt binary"
    echo
    echo "Examples:"
    echo "  $0"
    echo "  $0 -p e100"
}


# the default project
proj_dir=${proj_dir:-e100}
nfs_dir=""
strip_symbols="1"


# Parse options
TEMP=$(getopt -o hsp:n: --long help,symbols,project:,nfs: -n 'multi_options.sh' -- "$@")
if [ $? != 0 ]; then
    echo "Terminating..." >&2
    exit 1
fi

eval set -- "$TEMP"

while true; do
    case "$1" in
        -h|--help)
            show_help
            exit 0
            ;;
        -n|--nfs)
            nfs_dir="$2"
            shift 2
            ;;
        -p|--project)
            proj_dir="$2"
            shift 2
            ;;
        -s|--symbols)
            strip_symbols="0"
            shift
            ;;
        --)
            shift
            break
            ;;
        *)
            echo "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
done


echo "start to build ${proj_dir}"
# make & build the app
if [ $proj_dir = "e100" ]; then
    cp CMakeLists.txt.e100 CMakeLists.txt
    sed -i 's/#define CONFIG_BOARD_E100 .*/#define CONFIG_BOARD_E100 BOARD_E100/' jenkins.h
elif [ $proj_dir = "e100_lite" ]; then
    cp CMakeLists.txt.e100_lite CMakeLists.txt
    sed -i 's/#define CONFIG_BOARD_E100 .*/#define CONFIG_BOARD_E100 BOARD_E100_LITE/' jenkins.h
elif [ $proj_dir = "" ]; then
    cp CMakeLists.txt.e100 CMakeLists.txt
else
    echo "Unsupported project: ${proj_dir} "
    exit 1
fi

${pwd}/tools/tr/tr -g ${pwd}/resources/${proj_dir}/translation.csv ${pwd}/resources/${proj_dir}/translation.bin
${pwd}/tools/widget/widget -g ${pwd}/resources/${proj_dir}/widget.csv ${pwd}/resources/${proj_dir}/widget.bin

# Configure cross-compiler toolchain
. ${pwd}/arm-openwrt-linux-gnueabi-gcc/environment-arm-openwrt-linux-gnueabi || (echo "Need to clone toolchain in .., aborting..." && exit 1)

# Generate jenkins.h verion file from Git version info, hashes, tags etc.
./genjenkins.sh

# Actually build the app
mkdir ${pwd}/build
cd ${pwd}/build
cmake ${pwd}
if [ $strip_symbols = "1" ]; then
  make -j19 && cp app app-debug && arm-openwrt-linux-gnueabi-strip app
else
  make -j19
fi