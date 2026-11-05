# 来源与许可记录

## PvZ-Portable

- 来源：<https://github.com/wszqkzqk/PvZ-Portable>
- 提交：`98b659380014c89c3553fbb2839f5e5a84d6df3d`
- Copyright (C) 2026 Zhou Qiankang <wszqkzqk@qq.com>
- 许可：LGPL-3.0-or-later，文本保存在本目录 `LICENSE`、`COPYING`。

上游 `icon.png` 由 Zhou Qiankang 借助图像生成工具制作，并非 PopCap/EA 官方标识。本工程当前 BDA 图标按 H1 原生桌面风格经 GPT Image 重新生成，生成提示、参考用途和转换记录见 `assets/icon.provenance.json`。

H1 平台层、资源加载适配、运行库桥接和软件绘图修复随本工程提供。原始上游源码在 `upstream/`，修改由 `platform/source_patches.py`、`platform/graphics/memory_patches.py` 与替换源文件描述。构建不修改上游 checkout。

保留上游框架声明：

> This product includes portions of the PopCap Games Framework, © 2005-2009 PopCap Games, Inc. All rights reserved. (http://popcapframework.sourceforge.net/).

框架原许可在 `licenses/PopCap-Framework.txt`。

## 原版美术、字体与声音

Plants vs. Zombies 及其原版图像、动画、美术、音乐、声音和字体属于各自权利人 PopCap / Electronic Arts；代码的开源许可不授予这些资源的使用权。

本地资源输入：<https://github.com/FregD156/PvZ_Assets>，固定提交 `8ba94e95cfe7d0de5682f8258d3b932dbdac885c`。该仓库说明资源从 macOS 版 `main.pak` 提取，未提供可据此认定资源自由再分发的独立许可证。本工程记录此来源和转换哈希，不把资源视为 LGPL 内容。

按用户要求，原始输入、转换后的完整运行资源和 GPT Image 扩图一并收录在本私有仓库的 `private/` 中。它们不因收录而取得代码的 LGPL 许可；上游资源仓库没有提供可据此认定原版素材自由再分发的独立许可证。扩图提示与源文件保留在 `private/widescreen/`。

SDK 独立位于 `sdk/` 子模块，来源与固定提交见 `SDK.json`，沿用其自身许可证和 NOTICE。本仓库的独立路径入口与资源清单不包含固件/NAND、个人存档或工具链二进制。

## 简体中文文本和界面资源

- 主要来源：[Bamcane/re-plants-vs-zombies](https://github.com/Bamcane/re-plants-vs-zombies)，固定提交 `0f6bbd39302acf69484ba8b3e071724e35cfba17`，`pak/properties/LawnStrings.txt` 和同目录树的中文界面图片。
- 补充来源：[gonewx/pvz](https://github.com/gonewx/pvz)，固定提交 `897a279082c5e5549b6959eab7f4ae82e8b696b1`，中文文本及带透明通道的原始界面 PNG。
- 逐图 Git blob 标识、来源路径和文本输入保存在 `private/zh-cn/source-upstream/`；移植中补充和修订的文案位于 `localization/overrides.zh_CN.json`。

这些原版中文资源仍属于其各自权利人，不因来源仓库开放或本项目收录而取得本项目代码的 LGPL 许可。仓库保持私有。

中文点阵字形使用 [Noto CJK](https://github.com/notofonts/noto-cjk) 的 `NotoSansCJKsc-Regular.otf`，固定提交 `f8d157532fbfaeda587e826d4cd5b21a49186f7c`，原始路径 `Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf`。字体文件存于 `assets/fonts/`，遵循 SIL Open Font License 1.1，全文见 `licenses/NotoSansCJK-OFL.txt`。字形离线栅格化后收入共享图集，H1 无需加载 OTF 或运行 FreeType。

## libxmp

- 来源：<https://github.com/libxmp/libxmp>
- 提交：`06efa94d463d6202e9d553ab45aa797ad6b7a0a3`
- Extended Module Player, Copyright (C) 1996-2026 Claudio Matsuoka and Hipolito Carraro Jr.
- MIT 许可全文：`licenses/libxmp.txt`。源码位于 `vendor/libxmp/`。

## zlib

- 来源：<https://zlib.net/>
- 版本：1.3.1。源码位于 `third_party/zlib-1.3.1/`。
- Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler.
- 许可全文：`licenses/zlib.txt`。

## C / C++ 运行库

Newlib 4.6.0.20260123 与 GCC 15.2.0 的 libstdc++、libsupc++、libgcc 由官方源代码构建。下载地址、源归档哈希及选项记录在 `build/cxx-toolchain.json`；H1 修改记录在 `tools/bootstrap_cxx.py`，本地源代码缓存位于 `C:\temp\h1-pvz-cxx\`。运行库适用各自的许可，包括 GCC Runtime Library Exception；保留的文本位于 `licenses/`。

MO3 解包使用 Un4seen 官方 UNMO3 工具，仅在本机离线准备时调用，不随 H1 安装包分发。原工具声明保留在本地工具缓存中。
