# NOTE
**本工程适用于 Kernel-5.10.110 版本的 luckfox-pico SDK，目前本仓库不再维护迁移到 https://github.com/LuckfoxTECH/luckfox_pico_rkmpi_example.**

**This project is suitable for the luckfox-pico SDK based on Kernel version 5.10.110. The current repository is no longer maintained. Please migrate to https://github.com/LuckfoxTECH/luckfox_pico_rkmpi_example.**

**Read this in other languages: [English](README.md), [中文](README_CN.md).**
# luck_pico_rtsp_retinaface

This project aims to test the use of RKNN for retinaface inference on luckfox-pico plus. It annotates the inference results on the video stream through On-Screen Display (OSD). It utilizes multithreading to handle RKNN inference and RTSP streaming, ensuring the frame rate of streaming while performing model inference.

## Development Environment
+ Luckfox-pico SDK

## Compilation
```bash
export LUCKFOX_SDK_PATH=<Your Luckfox-pico Sdk Path>
mkdir build
cd build
cmake ..
make && make install
```

## Execution
Upload the compiled `luckfox_rtsp_retinaface_demo` to luckfox-pico, navigate to the folder, and run:
```bash
./luckfox_rtsp_retinaface_osd
```
**Note**: Before running, please close the default system `rkipc` program by executing `RkLunch-stop.sh`. Currently, it only supports detecting a single face.
