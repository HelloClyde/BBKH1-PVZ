# 植物大战僵尸：H1 / Y100 移植

基于 [PvZ-Portable](https://github.com/wszqkzqk/PvZ-Portable) 固定提交 `98b659380014c89c3553fbb2839f5e5a84d6df3d`，目标为步步高 H1 / Y100 V1.41 的原生 BDA。原 Lawn 游戏逻辑、关卡、菜单、动画定义和存档实现参与编译；H1 平台层接管窗口、绘图、输入、文件和声音。

此前的五关简化游戏在相邻的 `h1-pvz` 中保留。本目录是独立的上游移植工程。

H1 宽屏适配版使用 480×272 实际画布和 1060×600 界面布局，九列草坪保留原 800×600 游戏坐标并居中。植物、僵尸、字体和按钮保持比例，图片在电脑离线缩放。启动、菜单和奖励背景扩图，关卡使用原版宽背景。实现与测试范围见 [宽屏记录](docs/H1-WIDESCREEN-20261103.md)。此前 360×270 版的 [性能记录](docs/H1-PERFORMANCE-20261103.md) 另行保留；尚未逐项验证全部关卡和模式。

## 安装与操作

宽屏程序为 `dist/hardware-widescreen-complete/H1PVZPortable.bda`。将 `private/resources-h1-wide/` 完整目录树复制到 `A:\应用\数据\游戏\PVZ\`，也支持 `B:\应用\数据\游戏\PVZ\`。目录下应直接存在 `properties\resources.xml`、`images\`、`data\`、`reanim\`、`particles\`、`trails\`、`levels\` 和 `sounds\`，资源总量约 90 MiB。原始 PNG/OGG 或 `main.pak` 不能直接替代转换资源；请配套更新程序和资源。已安装上一版完整资源时，只需替换宽屏记录列出的七个资源文件；保留现有 `userdata`。

程序启动先检查 A 盘新目录的 `properties\resources.xml`，找不到时检查 B 盘同目录。整次运行使用同一个 PVZ 根目录，存档 `userdata/`、缓存和日志均写到选中的资源盘；不逐文件跨盘查找。

迁移旧存档时，将原 `A:\PVZSAVE\userdata` 复制到选中的新 PVZ 目录内，保留旧目录备份；如新目录已有存档，先备份并避免覆盖。程序包和资源包均不携带测试存档。

触摸笔直接点击。方向键移动黄色指针，确认键或 Z 点击，Enter 对应原版回车，返回键或 Esc 对应原版 Esc。输入玩家名时使用字母键和删除键。

宽屏界面完整覆盖 480×272 LCD，触摸与键盘指针使用对应映射。九列草坪的种植、命中和 V4 存档坐标保持原值。当前资源文字为英文。声音输出为 16 kHz 单声道混音，再送入 H1 的 32 kHz PCM 接口。

## 构建

需要 Python 3、Pillow、Windows curl、MSYS2 GNU make/bash，以及工作区 `.tools/toolchain/bin` 下的 `mipsel-none-elf` GCC 15.2。资源准备还需要 PATH 中的 FFmpeg/FFprobe，以及 [Un4seen 官方 UNMO3](https://www.un4seen.com/mo3.html)。将 `unmo3.exe` 放在 `private/mo3-tools/un4seen/`，或通过 `--unmo3` 指定路径。固件 SDK 来自相邻 `vendor/h1-sdk`。完整 hosted C++ 标准库与 Newlib 由脚本单独构建，原交叉编译器保持不变。

```powershell
New-Item -ItemType Directory -Force build | Out-Null
python tools/bootstrap_cxx.py
python tools/prepare_assets.py --download --audio
python assets/prepare_modules.py
python tools/prepare_h1_assets.py --profile full --output private/resources-h1-full
python tools/build.py --build-dir build/native-physical-paths --output-dir dist/physical-paths --define PVZH1_PHYSICAL_SCREEN
```

`bootstrap_cxx.py` 默认使用 `C:\temp\h1-pvz-cxx`，GNU 构建路径不能含空格。脚本记录源代码下载地址、哈希、编译选项和 H1 所需补丁。主程序保留 C++ 异常及 RTTI。

`upstream/` 保持固定版本的原始源码。`tools/prepare.py` 将其复制到 `build/source/`，再应用 `platform/source_patches.py` 和绘图后端中的有上下文检查的补丁。主机验证使用独立的 `build/host/source/`。

H1 固件的中断处理会覆盖 `$gp`，因此原生程序及 C/C++ 运行库统一使用 `-G0` 并去掉 Newlib 强制的 `.sdata` 属性。链接还检查程序、BSS 和栈的总地址边界；越界时不生成 BDA。

可通过 `python tools/build.py --kernel <V1.41-project.bin路径>` 对固定固件独立复核中断和分配器 ABI。未提供固件时，构建使用记录的版本证据；程序的分配兼容路径还会在运行时检查关键指令和堆结构。固件文件不随源码包分发。

离线转换分离逻辑尺寸与像素存储尺寸，按帧格和字形隔离缩放；动画轨道和字体指标保留原值。图片首次使用时直接缓存缩图像素，采用 7 MiB 常驻缓存预算，此预算不是程序总内存上限。缩图资源模式在启动时固定停用原逻辑尺寸动画图集构建，走原无图集绘制分支，避免重新生成大图。调用栈固定为 128 KiB，只承载有界临时数据；贴图与画布从共享堆分配。原 MO3 音乐离线解回完整 XM，缩减采样数据后由 libxmp 实时播放，保留原音乐调度及声道控制。详见 [assets/README.md](assets/README.md)。

## 验证与范围

构建报告、主机运行记录和独立固件模拟器记录保存在 `build/`。资源转换及音乐接口的验证记录在 `assets/`。当前验证范围见 [docs/VALIDATION.md](docs/VALIDATION.md)，平台设计见 [docs/PORTING.md](docs/PORTING.md)。主机运行通过并不等同于 H1 固件运行通过；最终交付说明应以实际报告为准。

已知资源差异：资源输入来自 macOS 版本提取包；可选发行商 logo 缺失，使用透明占位并写日志；Zombatar 条款文件缺失，泳圈僵尸另有水花轨道名称差异。音频进行了采样与位深适配。平台尚未接入 PNG/JPEG/TGA 图像输出接口。没有完成全部关卡和所有模式的逐项测试。

实体存储按 32 个槽位分块分配，保留上游的总容量、ID 与遍历顺序。主机集成测试已验证当前 v4 字段存档以及分页前生成的 v4 存档；旧版直接保存 C++ 内存布局的格式未承诺兼容。

## 来源与许可

原版游戏资源和代码许可分别列在 [NOTICE.md](NOTICE.md)。PvZ-Portable 及本移植平台代码按 LGPL-3.0-or-later 提供；完整许可文本在 `LICENSE` 和 `COPYING`。本项目未获得 PopCap/EA 的官方授权。
