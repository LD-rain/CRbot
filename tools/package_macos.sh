#!/usr/bin/env bash
# 构建并打包 macOS 版
#
# 用法：
#   tools/package_macos.sh                 # 构建 Release（build-release）并打包
#   tools/package_macos.sh --skip-build    # 只打包已有产物
#   BUILD_DIR=build tools/package_macos.sh --skip-build
#
#   --channel=stable                   # 正式版本不显示 Build 号
# 产物：dist/CRBot-<版本>(<Build>)-macos-<arch>.dmg 及 .sha256
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ARCH="$(uname -m)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-release}"
SKIP_BUILD=0
BUILD_NUMBER=AUTO
CHANNEL=prerelease

for arg in "$@"; do
    case "$arg" in
        --skip-build) SKIP_BUILD=1 ;;
        --build-dir=*) BUILD_DIR="${arg#*=}" ;;
        --build-number=*) BUILD_NUMBER="${arg#*=}" ;;
        --channel=*) CHANNEL="${arg#*=}" ;;
        -h|--help)
            sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *) echo "不认识的参数：$arg" >&2; exit 2 ;;
    esac
done

echo "==> CRBot (Release, $ARCH), build dir: $BUILD_DIR"

if [[ $SKIP_BUILD -eq 0 ]]; then
    cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
        -DCRBOT_BUILD_NUMBER="$BUILD_NUMBER" -DCRBOT_RELEASE_CHANNEL="$CHANNEL"
    cmake --build "$BUILD_DIR" -j"$(sysctl -n hw.ncpu)"
else
    echo "==> 跳过构建，直接打包已有产物"
fi

# Name packages from the metadata of the actual build, including --skip-build.
METADATA="$BUILD_DIR/build-info.json"
[[ -f "$METADATA" ]] || { echo "没有构建元数据 $METADATA，请先重新构建" >&2; exit 1; }
VERSION="$(plutil -extract version raw -o - "$METADATA")"
BUILD_NUMBER="$(plutil -extract build_number raw -o - "$METADATA")"
CHANNEL="$(plutil -extract channel raw -o - "$METADATA")"
DISPLAY_VERSION="$(plutil -extract display_version raw -o - "$METADATA")"
[[ "$VERSION" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ && "$BUILD_NUMBER" =~ ^[1-9][0-9]*$ ]] || { echo "非法版本元数据" >&2; exit 1; }
case "$CHANNEL" in
    prerelease) EXPECTED_VERSION="$VERSION($BUILD_NUMBER)" ;;
    stable) EXPECTED_VERSION="$VERSION" ;;
    *) echo "非法发布通道：$CHANNEL" >&2; exit 1 ;;
esac
[[ "$DISPLAY_VERSION" == "$EXPECTED_VERSION" ]] || { echo "构建版本不一致" >&2; exit 1; }
BIN="$BUILD_DIR/CRbot"
APP="$BUILD_DIR/CRBot.app"
[[ -x "$BIN" ]] || { echo "没有找到 $BIN（先构建，或用 BUILD_DIR 指到构建目录）" >&2; exit 1; }
[[ -x "$APP/Contents/MacOS/CRBot" ]] || { echo "没有找到 $APP（构建时须开启 CRBOT_BUILD_MACOS_GUI）" >&2; exit 1; }
# Use the compiled architecture, including when packaging a cross-build.
ARCH="$(lipo -archs "$APP/Contents/MacOS/CRBot")"
[[ "$ARCH" == arm64 || "$ARCH" == x86_64 ]] || { echo "不支持的应用架构：$ARCH" >&2; exit 1; }
[[ "$(lipo -archs "$BIN")" == "$ARCH" ]] || { echo "GUI 与 CLI 架构不一致" >&2; exit 1; }
PKG_NAME="CRBot-$DISPLAY_VERSION-macos-$ARCH"
PKG_DIR="$BUILD_DIR/installer-stage/$PKG_NAME"

# ---- 打包 ----
rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR/CLI/assets" "$ROOT/dist"
cp "$BIN" "$PKG_DIR/CLI/"
ditto "$APP" "$PKG_DIR/CRBot.app"
ln -s /Applications "$PKG_DIR/Applications"

# Sign the staged copy only. ad-hoc signing does not replace notarization.
codesign --force --sign - "$PKG_DIR/CLI/CRbot"
codesign --force --sign - "$PKG_DIR/CRBot.app"
codesign --verify --strict "$PKG_DIR/CRBot.app"

# 程序按 <exe>/assets/templates 找模板
cp -R "$ROOT/assets/templates" "$PKG_DIR/CLI/assets/"
cp "$ROOT/assets/README.md" "$PKG_DIR/CLI/assets/"
cp "$ROOT/README.md" "$ROOT/CHANGELOG.md" "$METADATA" "$PKG_DIR/"
printf '%s\n' "$VERSION" > "$PKG_DIR/VERSION"
# GPL-3.0 要求分发二进制时随附许可证与第三方声明
cp "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_NOTICES.md" "$PKG_DIR/"
mkdir -p "$PKG_DIR/docs"
cp "$ROOT/docs/圣水识别方案.md" "$PKG_DIR/docs/"

cat > "$PKG_DIR/INSTALL.txt" <<'EOF'
安装：将 CRBot.app 拖入 Applications，然后从「应用程序」启动。
更新前请退出正在运行的 CRBot；安装后可推出此磁盘映像。
命令行版：把 CLI 文件夹复制到可写目录，再在该目录运行 ./CRbot。
macOS 使用系统 ADB，安装方法见 README.md。
EOF

DMG="$ROOT/dist/$PKG_NAME.dmg"
rm -f "$DMG" "$DMG.sha256"
hdiutil create -volname "CRBot $DISPLAY_VERSION" -srcfolder "$PKG_DIR" \
    -format UDZO -fs HFS+ -ov "$DMG"
hdiutil verify "$DMG"
(cd "$ROOT/dist" && shasum -a 256 "$(basename "$DMG")" > "$(basename "$DMG").sha256")

echo ""
echo "==> 完成"
echo "    磁盘映像: $DMG"
echo "    打开后将 CRBot.app 拖入 Applications"
