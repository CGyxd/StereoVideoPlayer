#!/usr/bin/env bash
# 构建并打包为可移植 Linux 部署包（自带依赖 .so）。
# 用法：bash deploy/package_linux.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$ROOT/build"
BIN="$BUILD_DIR/StereoVideoPlayer"

# 1) 构建（若尚未构建）
if [ ! -x "$BIN" ]; then
  echo "==> 构建项目"
  mkdir -p "$BUILD_DIR"
  ( cd "$BUILD_DIR" && cmake .. && make -j"$(nproc)" )
fi
[ -x "$BIN" ] || { echo "错误：未找到可执行文件 $BIN"; exit 1; }

# 2) 准备打包目录
OUT="$SCRIPT_DIR/StereoVideoPlayer-Linux"
rm -rf "$OUT"
mkdir -p "$OUT"

# 3) 复制可执行文件与便携启动脚本
cp "$BIN" "$OUT/"
cp "$SCRIPT_DIR/run.sh" "$OUT/"
chmod +x "$OUT/StereoVideoPlayer" "$OUT/run.sh"

# 4) 收集依赖的动态库（排除 glibc 核心库与动态链接器，避免破坏目标机运行时）
echo "==> 收集依赖库"
skip_lib() {
  local b; b="$(basename "$1")"
  case "$b" in
    ld-linux*.so*|libc.so*|libm.so*|libmvec.so*|libpthread.so*|\
    libdl.so*|librt.so*|libutil.so*|libresolv.so*|libnsl.so*|\
    libnss_*.so*|libBrokenLocale.so*|libSegFault.so*|libpcprofile.so*|\
    libgcc_s.so*)
      return 0 ;;  # 跳过核心运行时
    *) return 1 ;;
  esac
}

while read -r line; do
  lib="$(printf '%s' "$line" | awk -F'=>' '{print $2}' | awk '{print $1}')"
  [ -n "$lib" ] && [ -e "$lib" ] || continue
  skip_lib "$lib" && continue
  cp -n "$lib" "$OUT/" && echo "    复制 $(basename "$lib")"
done < <(ldd "$BIN" 2>/dev/null)

# 5) 生成压缩包
cd "$SCRIPT_DIR"
tar -czf "StereoVideoPlayer-Linux.tar.gz" "$(basename "$OUT")"
echo "==> 已生成部署包：$SCRIPT_DIR/StereoVideoPlayer-Linux.tar.gz"
echo "    解压后执行 ./StereoVideoPlayer-Linux/run.sh 即可运行（片源放在同目录或用命令行传入）。"
