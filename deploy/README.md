# Linux 部署打包

本目录提供将 `StereoVideoPlayer` 打包为**可移植 Linux 部署包**的脚本与说明。
打包产物自带运行所需的第三方动态库（FFmpeg、GLEW、X11、Mesa/OpenGL 等），
可在未安装这些开发库的其他 X11 环境中直接运行。

## 目录内容

- `run.sh`：便携启动脚本。将自身所在目录加入 `LD_LIBRARY_PATH` 后启动播放器，
  因此只要 `.so` 与可执行文件在同一目录即可运行。
- `package_linux.sh`：一键构建并收集依赖库、生成 `StereoVideoPlayer-Linux.tar.gz`。

## 一键打包（推荐）

在 Linux 机器上（已安装 cmake、g++ 及 FFmpeg/GLEW/X11 开发库）：

```bash
cd StereoVideoPlayer
bash deploy/package_linux.sh
```

脚本会：

1. 在 `build/` 下执行 `cmake .. && make` 构建可执行文件（若已构建则跳过）；
2. 将 `StereoVideoPlayer` 与 `run.sh` 复制到 `deploy/StereoVideoPlayer-Linux/`；
3. 用 `ldd` 解析依赖，复制除 glibc 核心库与动态链接器以外的 `.so`；
4. 打包为 `deploy/StereoVideoPlayer-Linux.tar.gz`。

## 运行部署包

```bash
tar -xzf StereoVideoPlayer-Linux.tar.gz
cd StereoVideoPlayer-Linux
# 片源 左右立体片源.mkv 放在本目录，或通过参数指定路径
./run.sh
# 或：./run.sh /path/to/左右立体片源.mkv
```

## 使用 CPack（仅打包可执行文件与启动脚本）

项目 CMake 已内置 CPack 配置（仅 Linux 生效）。构建后执行：

```bash
cd build
cpack
```

将生成 `StereoVideoPlayer-1.0.0-Linux.tar.gz`，内含 `StereoVideoPlayer` 与 `run.sh`。
该包**不内置系统依赖库**，目标机需自行安装 FFmpeg/GLEW/OpenGL/X11 开发库；
若需自带依赖的自包含包，请使用上面的 `package_linux.sh`。

## 说明

- 打包时跳过了 glibc 核心库（libc/libm/libpthread 等）、`ld-linux` 与 NSS 模块，
  以避免与目标机系统的 C 运行时冲突。
- `libstdc++.so.6` 会被打包，以兼容 gcc 版本略低的目标机。
- 片源 `*.mkv` 体积较大且未被纳入版本控制（见根目录 `.gitignore`），请单独分发。
