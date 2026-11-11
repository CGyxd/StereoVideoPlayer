# StereoVideoPlayer

基于 OpenGL Quad Buffer 的左右分屏立体视频播放器。

## 功能

- 读取左右分屏 `mkv` 视频（左眼画面在左半部分，右眼画面在右半部分）。
- 使用 OpenGL Quad Buffer 分别渲染到左、右后缓冲。
- 在支持 Quad Buffer 的显卡/驱动上，配合主动立体显示器或快门眼镜即可看到立体效果。
- 普通显卡上无立体效果，画面看起来与普通视频一致。

## 目录结构

```text
StereoVideoPlayer/
├── src/
│   ├── main.cpp              # Windows 入口：Win32 + WGL
│   ├── main_linux.cpp        # Linux 入口：X11 + GLX
│   └── FFmpegVideoReader.*   # 跨平台视频读取（FFmpeg）
├── ThirdParty/
│   ├── GLEW/                 # Windows GLEW 库
│   └── FFmpeg/               # Windows FFmpeg 4.2 库
├── CMakeLists.txt
└── AGENTS.md
```

## Windows 构建

要求：

- Visual Studio 2022 Professional
- CMake >= 3.20

步骤：

```powershell
cd StereoVideoPlayer
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

构建后运行所需文件（均位于 `build/Release/` 或 `bin/Release/`）：

```text
StereoVideoPlayer.exe
glew32.dll
avcodec-58.dll
avformat-58.dll
avutil-56.dll
swscale-5.dll
swresample-3.dll
左右立体片源.mkv
```

## Linux 构建

要求：

- Ubuntu / Debian 或兼容发行版
- X11、OpenGL、GLEW、FFmpeg 4.2+ 开发库

安装依赖（以 Debian/Ubuntu 为例）：

```bash
sudo apt update
sudo apt install -y cmake g++ libglew-dev libgl1-mesa-dev libx11-dev \
    libavformat-dev libavcodec-dev libavutil-dev libswscale-dev
```

编译：

```bash
cd StereoVideoPlayer
mkdir build && cd build
cmake ..
make -j$(nproc)
```

运行：

```bash
./StereoVideoPlayer
# 或指定视频路径
./StereoVideoPlayer /path/to/左右立体片源.mkv
```

## 使用说明

- 默认读取与可执行文件同目录下的 `左右立体片源.mkv`。
- Windows 可通过命令行传入路径：
  ```powershell
  StereoVideoPlayer.exe "D:\\Videos\\左右立体片源.mkv"
  ```
- 按 `Esc` 退出播放。

## Linux 部署打包

可将程序打包为自带依赖库的便携包，在未安装 FFmpeg/GLEW/X11 开发库的其他 X11 环境中运行。
详见 [`deploy/README.md`](deploy/README.md)：

```bash
cd StereoVideoPlayer
bash deploy/package_linux.sh          # 生成 deploy/StereoVideoPlayer-Linux.tar.gz
# 或仅用 CMake/CPack 打包可执行文件与启动脚本（不内置系统依赖库）
cd build && cpack
```

## 注意事项

- Quad Buffer 立体输出依赖显卡驱动支持：
  - Windows 需要支持 WGL_STEREO / PFD_STEREO 的驱动与显卡。
  - Linux 需要 X11 + GLX 支持 `GLX_STEREO`；Wayland 不支持 OpenGL 立体。
- 普通消费级显卡通常不支持 Quad Buffer：
  - Windows 下驱动通常会忽略 `PFD_STEREO` 标志，程序以普通平面视频方式继续运行。
  - Linux 版本会自动检测 `GLX_STEREO` 支持情况；不支持时回退到普通双缓冲，程序仍可运行。
