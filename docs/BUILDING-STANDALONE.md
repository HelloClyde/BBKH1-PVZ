# 独立仓库构建

主入口为 `python tools/build_widescreen.py`，应用源代码、上游快照、zlib、libxmp、图标与 SDK 子模块均位于仓库内。`tools/prepare.py` 按 `pinned-source-manifest.json` 验证上游文件后生成隔离的 `build/native-widescreen/source/`，不会修改 `upstream/`。

## 首次安装编译环境

1. Windows 上安装 Python 3.11 与 MSYS2。默认 MSYS2 路径为 `C:/msys64`，可通过环境变量 `MSYS2_ROOT` 更改。
2. 在 MSYS2 UCRT64 终端安装 `base-devel` 与 `mingw-w64-ucrt-x86_64-gcc`。
3. 回到 PowerShell，在仓库根目录运行 README 的构建命令。工具链安装器核验固定 SHA-256，遇到现有目录会保留它，不自动覆盖。H1 SDK 本身没有下载器，因此这里提供应用侧安装器。

运行库默认构建目录为系统临时目录下的 `pvz-h1-cxx/`，可设置 `H1_CXX_WORK` 为无空格目录；`H1_CXX_PREFIX` 可指定安装完成的运行库。GNU configure/make 的运行库构建目录必须没有空格。运行库从官方 Newlib/GCC 归档按脚本中的哈希下载、构建，不随 Git 提交。

## 已有编译环境

```powershell
git submodule update --init sdk
python -m pip install -r requirements-build.txt
python tools/verify_repository.py
python tools/build_widescreen.py --toolchain 'D:/toolchain/bin' --prefix 'D:/pvz-cxx/install' --jobs 6
```

参数路径由调用位置解析。也可使用 `H1_TOOLCHAIN`（包含 bin 的工具链根目录）、`H1_CXX_PREFIX`、`H1_SDK_ROOT` 环境变量。默认 SDK 为仓库的 `sdk/`，固定提交记录在 `SDK.json`；不自动升级。可选的 `tools/build.py --kernel ...` 只用于用户本地复核固件 ABI，正常编译不需要固件文件。

构建维持 `-G0`、软浮点、异常与 RTTI，执行所有实际链接输入的 GP/TLS 审计及 BDA 标题、图标、分类、入口边界和 checksum 验证。英文宽屏版本的干净检出验证保存在 `docs/verification/history/standalone-en-wide-20261103.json`，对应其中明确记录的程序哈希。当前中文版本从本仓库编译，使用既有编译器和已验证运行库；其程序与真机证据以 `docs/verification/final-native-verification.json` 为准，没有重新引导构建全部编译器依赖。

## 资源

常规开发直接使用 `private/resources-h1-wide/`，无需生成资源。原始图片、字体、XML 与音频输入位于 `private/source/`，扩图位于 `private/widescreen/`。重新转换资源需 FFmpeg/FFprobe；MO3 转 XM 还需用户自行提供官方 UNMO3，仓库不附带该工具。

资源流水线入口依次为 `tools/prepare_assets.py`、`assets/prepare_modules.py`、`tools/prepare_h1_assets.py`、`tools/prepare_widescreen_assets.py`；菜单离线绘制包由完整主机运行器配合 `tools/bake_selector.py` 生成。各工具的 `--help` 给出实际参数。重新生成后必须复核文件清单、像素对照并更新 `provenance/`；不要把旧清单当作新资源的验证结果。

中文图片、文案和共享点阵字体由 `tools/build_zh_cn.py` 生成，随后重新烘焙中文菜单包，见 [汉化记录](H1-ZH-CN-20261103.md)。源字体仅用于电脑端生成，不进入 H1 运行目录。

动画、粒子、轨迹采用 `.h1d` 离线定义。重新生成时，先用 `tools/build_host.py --widescreen` 构建完整主机运行器，再执行 `python tools/bake_definitions.py --host build/host/pvz-portable-host.exe --resources private/resources-h1-wide --output build/definitions-new`。脚本调用原版 XML 解析器，对全部实际使用的粒子、全部动画及轨迹做二进制往返校验；输出位于 `definitions-new/definitions/`，按相同相对路径放入运行资源。它不读取个人存档，包含独立的临时测试用户目录。Linux 主机需用 `--runtime-dir` 指定本机 C++ 运行库路径，Windows 默认 MSYS2 UCRT64。

定义格式 `H1D1` 逐字段保存 32 位数值、字符串表和资源名称，避免序列化宿主机指针与结构体布局；外层 `H1DZ` 使用 zlib 压缩。H1 使用 4 KiB 页流式解压，两遍读取：先验证长度、CRC、类型结构和分配上限，再建立精确大小的数组，避免保留整份解压副本。未安装预编译文件时保留原 XML 兼容路径。当前导出清单为 `provenance/offline-definitions.json`。

历史开发工具中部分模拟器/原生采样脚本保留了工作区配套探针路径，它们不属于上述独立构建和 CI 入口。这里保留相关源代码用于研究，不把不存在的模拟器依赖描述为可直接运行。

## 包与验证范围

`python tools/package_install.py` 生成包含预验证 BDA 与全部 B 盘运行资源的安装 ZIP；指定 `--bda dist/H1PVZPortable.bda` 可使用本次编译结果。生成物不会覆盖已有同名 ZIP，且每个成员都读回核验 SHA-256。

CI 仅做快照完整性与成品格式校验，使用固定 SDK、固定官方 Actions 提交及只读权限。本次只建立私有仓库、推送 main，不创建 tag 或 Release。
