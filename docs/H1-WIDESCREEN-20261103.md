# H1 480×272 宽屏适配

本文记录汉化前英文宽屏版本；历史真机结果见 `verification/history/native-en-wide-20261103.json`。当前中文成品、资源和验证范围见 [汉化记录](H1-ZH-CN-20261103.md)。

运行版采用 480×272 物理画布和 1060×600 布局坐标，人物、植物、字形和原版按钮保持原有比例。九列草坪保留 800×600 的游戏坐标，显示时居中；种植、命中与 V4 存档中的实体坐标没有横向缩放。

启动背景使用 GPT Image 扩图；主菜单使用从已确认预览提取的纯背景，叠加原版木牌、墓碑、按钮、动画和动态用户名。右侧按钮及其动画轨道、点击区域一同右移。原尺寸输入和生成提示保存在 `private/widescreen/`。

关卡优先利用原版 1400×600 宽背景。开场和选卡镜头按新视野限制平移边界，避免移出背景；选卡、种植预览和顶部菜单的点击坐标同步适配。扩宽后的图片、主菜单绘制指令在电脑离线生成。画布保留、文字复用及 LCD 局部刷新沿用上一版优化，并统一为新的行跨度与损坏区域尺寸。

## 构建与资源

程序：`dist/hardware-widescreen-complete/H1PVZPortable.bda`。

配套资源树：`private/resources-h1-wide/`，由 `python tools/prepare_widescreen_assets.py` 创建。原 `private/resources-h1-full/` 保留。

相对于上一版，需要更新所选 PVZ 资源根目录下的七个文件：

- `images/titlescreen.h1i`
- `images/awardscreen_back.h1i`
- `reanim/selectorscreen_bg.h1i`
- `reanim/selectorscreen.reanim`
- `reanim/zombie_hand.reanim`
- `h1menu/selector-wide.h1mb`
- `image-metadata.h1x`

资源仍放在 `A:\应用\数据\游戏\PVZ` 或 `B:\应用\数据\游戏\PVZ`；整次运行的资源和存档继续使用同一个根目录。宽屏程序应配套上述资源，不能混用旧的菜单动画与索引。

原生构建在上一版优化参数之外增加 `--define PVZH1_WIDESCREEN`，记录于 BDA 旁的 `.build.json`。主机构建使用 `tools/build_host.py --widescreen`；菜单包使用 `tools/bake_selector.py --resources private/resources-h1-wide` 和该主机构建生成。

## 主机验证

- 完整游戏通过普通输入完成首关四波、真实投射物命中、暂停恢复、领取奖励并进入第二关；新画布为 480×272，原始像素回退计数和图片读取失败均为零。记录：`build/host-widescreen/run-first-final/`。
- 7 种关卡背景 × 3 个镜头位置，21 个实际光栅化覆盖检查通过，无两侧空白条。记录：`build/wide-backdrops-engine/`。
- 323 帧 LCD 局部刷新与完整呈现逐像素一致，覆盖两侧边缘、指针、表面切换和分配失败。原 360×270 路径的 244 帧回归也通过。
- 菜单 48 帧画布与 LCD 对照、关卡 64 帧保留画布对照、文字 240 帧对照通过，包含移动阳光、透明文字、裁剪变化、鼠标悬停和菜单回退。
- 绘制计划的 586 项生命周期与分配检查通过。宽屏下修正了“绘制片段未按横向顺序到达时，错误地禁用背景保留”的判定；完整覆盖采用有界临时位图证明，逐帧绘制沿用原有快速路径。

奖励页采用 GPT Image 扩展背景并居中摆放原版卡牌、文字与按钮，渐变遮罩覆盖全屏。放大卡牌价格的原版矩阵绘制遗漏了父控件平移，宽屏分支补上此偏移。最终完整游戏主机验证位于 `build/host-widescreen/run-wide-complete/`，加载改版前的 H1 V4 存档，通过真实种植、命中、触摸暂停、恢复、奖励页点击和进入第二关。

菜单在保留画布的基础上只恢复被云层、花朵等动态内容影响的区域；附件按原始前后顺序重建前景前缀，背景和静态前景的跳过范围有逐像素全帧对照。包含活动保留路径和跨区域花朵附件的 48 帧对照通过，附件前缀恢复实际执行 65 次。当前对照记录：`build/wide-retained-menu-attachments-stress/`、`build/wide-retained-board-engine/`、`build/wide-retained-text-engine/`。

## H1 真机测量

最终 BDA SHA256：`bb339ae62fa9fdab6596e570c4a1320724d93b3a822adc286c8a1ba4fe03021d`，3,306,052 字节。设备端完整文件 CRC 校验通过，运行时四处代码指纹与对应 ELF 一致。程序位于 A 盘，七个宽屏资源文件位于 B 盘，设备端校验通过。

| 场景 | 实际呈现帧数 | PC 端计时间隔 | FPS |
| --- | ---: | ---: | ---: |
| 宽屏主菜单 | 437 | 20.024 秒 | 21.82 |
| 首关旧存档继续、教学草坪 | 568 | 20.032 秒 | 28.35 |

两个窗口都以实际呈现计数增量计算，窗口内无输入、RPC 或截图。原生计时各为 801 个 raw tick，当前校准为 40 Hz，即 20.025 秒。窗口内文件打开、图片载入和图片淘汰增量均为零。未修改 CPU 时钟，也未用跳帧提高数值。最初未经静态层保留优化的宽屏菜单约 16.33 FPS；优化后以本表的最终程序数据为准。

真机已验证右上角触摸 Menu 能打开选项，返回主菜单后保存。普通输入通关、奖励页和下一关流程在完整主机运行版验证；真机帧率范围限于上述菜单和首关场景，不代表所有关卡与高密度战斗。原尺寸真机截图和原始结果在 `build/hardware-test-20261103-wide/`，汇总为 `final-native-verification.json`。

B 盘新存档已读回验证：33,773 字节，V4 长度及内部 CRC32 正确，SHA256 为 `dd83fbd9b6d4cace42ef61e7c216e72040b1a9eb9de70dc0c0d3eb10bdff972e`，与更新前内容不同。退出阶段为 9，本程序原生堆占用、离线菜单驻留、LCD 影子画布和文字缓存均归零。退出后原生资源管理器能够打开 B 盘并列出目录。最后仅关闭电脑串口客户端，H1Debug 驻留服务保持运行。

## 扩图来源

使用内置 GPT Image 工具编辑原背景，最终图片位于 `private/widescreen/source/title-outpaint-v1.png`、`menu-clean-background-v1.png` 和 `award-outpaint-v1.png`。原版按钮、动态文字与人物素材继续独立绘制。提示及来源分别见 `private/widescreen/title-generation.json`、`menu-clean-generation.json`、`award-generation.json`；主菜单的含文字布局预览不作为运行素材。

奖励页提示：将原 800×600 背景左右各扩展 130 像素为 1060×600，保持中央卡牌框、标题木牌的原始尺寸和居中位置，仅延展棕色斑驳墙面和外框，不增加文字或控件；随后离线缩为 480×272 RGB，逻辑尺寸记录为 1060×600。
