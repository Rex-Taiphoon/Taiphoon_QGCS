#!/usr/bin/env bash
# =============================================================================
# @file build-host-idlc.sh
# @brief 建置主機版 CycloneDDS idlc，供 iOS 等交叉編譯產生 DDS 型別（ADR-0017）
# @author Taiphoon GCS Team
# @date 2026-10-15
#
# 交叉編譯時 CycloneDDS 不編 idlc，idlc_generate 會從 PATH 尋找主機版 idlc。
# CycloneDDS 版本取自 cmake/Dependencies.cmake，確保與目標平台的 ddsc 一致。
# ddsc 以靜態方式連結；IDL 剖析函式庫 libcycloneddsidl 在 CycloneDDS 中固定為動態函式庫，
# 因此一併安裝到 <安裝目錄>/lib，並以 CycloneDDS 的安裝 rpath（@loader_path/../lib、$ORIGIN/../lib）
# 直接建置 idlc：建置時即由連結器簽章，之後不再修改執行檔（Apple Silicon 修改後簽章失效會被終止）。
#
# 用法：tools/build-host-idlc.sh <安裝目錄>
#       完成後將 <安裝目錄>/bin 加入 PATH，再 configure 目標平台。
# =============================================================================
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "用法：$0 <安裝目錄>" >&2
    exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(dirname "${script_dir}")"
install_dir="$1"

version="$(sed -n 's/^set(GCS_CYCLONEDDS_VERSION "\(.*\)")$/\1/p' "${project_dir}/cmake/Dependencies.cmake")"
if [[ -z "${version}" ]]; then
    echo "無法從 cmake/Dependencies.cmake 取得 CycloneDDS 版本" >&2
    exit 1
fi

work_dir="$(mktemp -d)"
trap 'rm -rf "${work_dir}"' EXIT

git clone --quiet --depth 1 --branch "${version}" \
    https://github.com/eclipse-cyclonedds/cyclonedds.git "${work_dir}/src"

cmake -S "${work_dir}/src" -B "${work_dir}/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${install_dir}" \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
    -DBUILD_IDLC=ON \
    -DBUILD_TESTING=OFF \
    -DBUILD_DDSPERF=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_DOCS=OFF \
    -DENABLE_SSL=OFF \
    -DENABLE_SECURITY=OFF \
    -DENABLE_SHM=OFF \
    -DENABLE_ICEORYX=OFF
cmake --build "${work_dir}/build" --target idlc
install -d "${install_dir}/bin" "${install_dir}/lib"
install "$(find "${work_dir}/build" -type f -name idlc -perm -u+x | head -n 1)" "${install_dir}/bin/idlc"
# 保留版本符號連結（libcycloneddsidl.dylib → .0.dylib → .0.10.5.dylib）
cp -P "${work_dir}/build/lib/"libcycloneddsidl* "${install_dir}/lib/"

# 建置目錄會在結束時刪除；在此確認安裝後的 idlc 可以獨立執行
"${install_dir}/bin/idlc" -v

echo "主機版 idlc（CycloneDDS ${version}）：${install_dir}/bin/idlc"
