
**其他语言版本: [English](README.md), [中文](README_CN.md).**

# luck_pico_rtsp_retinaface
    测试 luckfox-pico plus 使用 rknn 推理 retinaface ，将推理结果通过打 OSD 的方式标记在视频流上。使用多线程处理 rknn 推理和 rtsp 推流，在实现模型推理的同时保证推流的帧率。

# 开发环境
+ luckfox-pico sdk

# 编译
```
export LUCKFOX_SDK_PATH=<Your Luckfox-pico Sdk Path>
mkdir build
cd build
cmake ..
make && make install
```

# 运行
将编译生成的`luckfox_rtsp_retinaface_demo`上传到 luckfox-pico 上，进入文件夹运行
```
./luckfox_rtsp_retinaface_osd
```
**注意**：运行前请关闭系统默认 rkipc 程序，执行 `RkLunch-stop.sh` 关闭；目前仅支持检测单个人脸。
