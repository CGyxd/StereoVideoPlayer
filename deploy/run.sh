#!/bin/bash
# 便携启动脚本：以脚本所在目录作为库搜索路径，
# 确保在缺少 FFmpeg/GLEW/X11 等开发库的其他 X11 环境中也可运行。
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="${SCRIPT_DIR}:${LD_LIBRARY_PATH}"
exec "${SCRIPT_DIR}/StereoVideoPlayer" "$@"
