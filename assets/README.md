# 原版完整资源与 H1 转换

资源输入锁定到 `FregD156/PvZ_Assets` 的提交 `8ba94e95cfe7d0de5682f8258d3b932dbdac885c`。该仓库说明资源由 macOS 版 `main.pak` 提取。源仓库没有给出数字版本；本项目没有把资源 ID 匹配等同于完全验证 GOTY 版本。

`private/source/` 保留 2898 个资源输入及其原始字节。所有文件以 Git blob SHA-1 验证，另外记录 SHA-256。`private/resources/` 是完整安装资源树，不是精简动画贴图集：

- 2480 个原大小图像：195 MB 磁盘数据，RGB24 / RGBA32 / 字体 alpha mask。
- 456 个定义、字体指标、属性及原音频文件：约 18.8 MB；全部 reanim 轨道、变换和时间序列均保留。
- 169 个原音效与片尾歌曲：转换为 16 kHz 单声道 H1PC，共 13,754,332 字节。
- 两个完整 XM 音乐模块：约 5.73 MB 主模块及 328 KB hi-hat 模块，保留原 236 个 orders、200 个 patterns、30 个声道及全部音符/效果。当前上游使用主模块的两个播放上下文和独立声道音量。

这份原尺寸基线没有缩放图像，XML 的尺寸、动画列/行数及字形矩形均保持原值。另行生成的 H1 缩放树见下节，基线始终保留。LawnStrings 的两处 MacRoman 注册商标字符显式转为 UTF-8；原始文件仍在 `private/source/`，输出哈希另记。

## H1 图像格式与接口

`.h1i` 原图、`.h1a` 自动合成外置 alpha 图。文件头为 24 字节：`H1I1`，后跟五个 LE uint32：宽、高、每像素通道数、flags、像素字节数。通道为 RGB24、RGBA32，或 `flags=1` 的单字节 alpha；后者使用调用时的 `gAlphaComposeColor`。

转换器保留原 alpha 前缀/后缀规则。例如 `images/pool_base_` 是原灰度 mask 的 RGB24 文件，而 `images/pool_base` 的 `.h1a` 才是原色 JPEG 加 mask。

`platform/resources/h1_resources.h` 提供不分配像素的 `h1_image_probe`、行片段读取及完整读取。`ImageLib.cpp` 提供上游兼容的真实 `ImageLib::GetImage`，输出原尺寸、未预乘的 `0xAARRGGBB` 像素。需要按需加载和逐帧缓存；全图同时展开约 200 MB，不能在 H1 全部常驻。

可选 `image-metadata.h1x` 索引为 137,073 字节，包含全部 2480 个图像流的原始头字段。首次 probe 校验版本、长度、FNV-1a、路径排序及尺寸后载入一次，二分查找尺寸；切换资源根目录即释放。实际像素读取始终打开并校验 H1I1 文件，整图使用连续分块读取。索引缺失、损坏或查不到条目时回退原文件头查询，不以索引漏项假报文件缺失。

索引头为 28 字节：`H1X1`，再接六个 LE uint32：版本 1、条目数、条目宽度 24、字符串区字节数、主体字节数、主体 FNV-1a32。每个条目为六个 LE uint32：字符串偏移、宽、高、通道数、flags、像素字节数，后接按路径字节序排序的 NUL 结尾相对路径。`image-metadata-verification.json` 包含全条目与原头字段比对，以及无原图的索引查询、缺索引、损坏、截断、非法尺寸、漏项回退和根目录切换测试。

唯一图片缺口是可选发行商 `properties/partner_logo`。平台为该精确路径提供带 `H1_IMAGE_OPTIONAL_TRANSPARENT` 标记的透明占位并记录一次日志；其他缺失文件正常失败。真实 `IMAGE_BLANK` 来自原 `images/blank_.gif`，并非合成图片。

## H1 离线缩放资源

`tools/prepare_h1_assets.py` 从已校验基线生成独立资源树。菜单原型为 `private/resources-h1/`，只缩放三张 Selector 背景及其 alpha 变体；完整暂存树为 `private/resources-h1-full/`。原尺寸 `private/resources/` 和所有声音、模块、字体指标、XML 动画定义均不修改。

缩放率为 `9/20`，实际尺寸使用 `max(1,(n*9+10)//20)`。RGBA 先转预乘 alpha，再用 Lanczos 缩小并转回直通 RGBA，避免透明边缘颜色渗入。动画按每个 cel、字体按每个 glyph rectangle 独立过滤；物理矩形两端统一为 `floor(logicalBound*storageSize/logicalSize)`，渲染器必须使用相同映射并在该矩形内取样。

H1I2 文件头为 32 字节：`<4s7I>`，依次为 magic、逻辑宽高、通道数、flags、物理像素字节数、存储宽高。H1X2 保留 28 字节索引头，版本为 2、条目宽度为 32；每条是原六个字段再加存储宽高。H1I1/H1X1 仍可读。旧 logical span 接口按逻辑坐标读取；新增 storage 接口直接读取物理像素，避免为渲染重新展开原尺寸。

完整暂存树目前包含 2448 张 H1I2 和 32 张逐字节保留的 H1I1，图像约从 195 MB 降为 49 MB。保留项包括三个尺寸不整除的粒子 sheet、两个重叠字体图层、两个会出现零宽物理 glyph 的小字体图层，以及尚需逻辑坐标合成的 alpha-grid 图片和 mask。具体每个文件的原因、原新哈希和全部 cel/glyph 映射在 `h1-scale-manifest.json`。

`h1-offline-scale-verification.json` 覆盖 4443 个非空物理矩形、相邻格和字形的颜色隔离、零 texel 拒绝、2480 个实际文件头及 629 个未修改的非图文件。菜单三背景原型已在自有完整固件模拟器验证：同一返回菜单路径的暖窗口约 5.05 FPS，未产生新图片读取、缓存换出或内存回收；原尺寸基线同路径约 0.126 FPS。该结果只覆盖菜单原型，不代表完整缩放树或真机性能已经验证。

```powershell
python h1-pvz-portable/tools/prepare_h1_assets.py --profile menu
python h1-pvz-portable/tools/prepare_h1_assets.py --profile full
python h1-pvz-portable/assets/test_h1_scale.py
```

## 音频

H1PC 头部为 16 字节：`H1PC`，LE uint32 版本 `1`、采样率 `16000`、单声道样本数，随后为 LE signed int16 PCM。声音可按块流式读取。

音乐通过 [Un4seen 官方 UNMO3](https://www.un4seen.com/mo3.html) 离线解回 XM，工具保留在私有构建缓存，不安装、不随 H1 安装资源分发。包内 MO3.TXT 声明非商业使用免费。`private/music/*.original.xm` 保留未适配输出；安装模块仅将样本带限降采样 2:1、量化为 8 bit，同时逐样本将 `relativeNote` 减 12，循环点相应折半，保持音高与时长。所有 orders/patterns 前缀逐字节相同，仪器映射/包络不变。

`module-index.json` 记录每个样本的原/新长度、音高、循环和哈希。原始 hi-hat MO3 与官方 UNMO3 输出 XM 的“全部子曲扫描”时长存在差异；这在 H1 样本适配前已出现。适配前后 XM 的同模式时长精确一致。实际游戏以原 order/row 跳转，不以一个默认子曲的连续 PCM 冒充完整音乐。

运行时主 XM 的事件轨道通过逐字节相同内容去重，从 4,605,636 降至 1,568,344 字节；5,014,597 字节样本保持原安装数据。MAIN / DRUMS 共享这一份模块，仍有两个独立的播放器、order / row、声道音量及暂停状态。共享和轨道去重不增加音频损失。主机验证覆盖两个播放器的独立控制、先释放一方、另一方继续，以及最终释放；不把这个结果当作 H1 实机音频验证。

## 审计与验证

`resource-audit.json`：879 个静态资源 ID、251 个实际 reanim/particle 定义路径及全部动画图片引用均覆盖。源资源未提供 `Properties/ZombatarTOS.txt` 中的 `ZOMBATAR_TOS` / `ZOMBATAR_TOS_HEADER`，当前未编造文本；该可选条款页仍有缺口。

运行时轨道存在已知 GOTY / macOS 差异：上游 `Zombie.cpp` 的 `SetupWaterTrack("Zombie_whitewater2")` 请求在原 `reanim/Zombie.reanim` 中没有对应名称；源文件有两个同名 `Zombie_whitewater` 轨道。保留源轨道原名，没有擅自重命名或补造数据。静态文件和图片引用覆盖并不代表所有运行时轨道完全兼容。

`loader-verification.json`：C/C++ loader 输出与从原始 PNG/JPEG/mask 解码的 ARGB 哈希比对，包含字体动态着色、灰度 alpha grid、大小写、行读取和越界拒绝；所有转换图片文件哈希校验。

`module-verification.json`：十秒新旧 XM 音频比较。主模块波形相关 0.99013、RMS 包络相关 0.999957，五个低频主谱峰相同。音频具有明确的 H1 采样适配损失，不宣称与原始 PCM 逐样本一致。

可重复准备：

先安装 Python/Pillow，将 FFmpeg 与 FFprobe 加入 PATH，并将官方 `unmo3.exe` 放在 `h1-pvz-portable/private/mo3-tools/un4seen/`。也可在模块命令中用 `--unmo3` 指向其他位置。UNMO3 不随源码包提供。

```powershell
python h1-pvz-portable/tools/prepare_assets.py --download
python h1-pvz-portable/tools/prepare_assets.py --audio-only
python h1-pvz-portable/assets/prepare_modules.py
python h1-pvz-portable/assets/verify_resources.py
python h1-pvz-portable/assets/verify_metadata.py
```

完整源输入缺失时 `--download` 才会联网；旧 `h1-pvz` 缓存只读复用。以上验证属于离线资源和主机验证，不代表 H1 真机或全游戏通关验证。
