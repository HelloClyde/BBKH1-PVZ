# 来源与许可记录

## PvZ-Portable

- 来源：<https://github.com/wszqkzqk/PvZ-Portable>
- 提交：`98b659380014c89c3553fbb2839f5e5a84d6df3d`
- Copyright (C) 2026 Zhou Qiankang <wszqkzqk@qq.com>
- 许可：LGPL-3.0-or-later，文本保存在本目录 `LICENSE`、`COPYING`。

BDA 图标沿用该提交的 `icon.png`。上游说明此图标由 Zhou Qiankang 借助图像生成工具制作，并非 PopCap/EA 官方标识。

H1 平台层、资源加载适配、运行库桥接和软件绘图修复随本工程提供。原始上游源码在 `upstream/`，修改由 `platform/source_patches.py`、`platform/graphics/memory_patches.py` 与替换源文件描述。构建不修改上游 checkout。

保留上游框架声明：

> This product includes portions of the PopCap Games Framework, © 2005-2009 PopCap Games, Inc. All rights reserved. (http://popcapframework.sourceforge.net/).

框架原许可在 `licenses/PopCap-Framework.txt`。

## 原版美术、字体与声音

Plants vs. Zombies 及其原版图像、动画、美术、音乐、声音和字体属于各自权利人 PopCap / Electronic Arts；代码的开源许可不授予这些资源的使用权。

本地资源输入：<https://github.com/FregD156/PvZ_Assets>，固定提交 `8ba94e95cfe7d0de5682f8258d3b932dbdac885c`。该仓库说明资源从 macOS 版 `main.pak` 提取，未提供可据此认定资源自由再分发的独立许可证。本工程记录此来源和转换哈希，不把资源视为 LGPL 内容。

按用户要求，原始输入、转换后的完整运行资源和 GPT Image 扩图一并收录在本私有仓库的 `private/` 中。它们不因收录而取得代码的 LGPL 许可；上游资源仓库没有提供可据此认定原版素材自由再分发的独立许可证。扩图提示与源文件保留在 `private/widescreen/`。

SDK 独立位于 `sdk/` 子模块，来源与固定提交见 `SDK.json`，沿用其自身许可证和 NOTICE。本仓库的独立路径入口与资源清单不包含固件/NAND、个人存档或工具链二进制。

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
