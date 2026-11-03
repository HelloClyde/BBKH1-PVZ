# H1 真机绘图优化记录：2026-11-03

测试设备为 H1/Y100 V1.41，驻留 H1Debug；程序位于 `A:\应用\程序\H1PVZPortable.bda`，资源与存档使用 `B:\应用\数据\游戏\PVZ`。画布为 360×270，LCD 为 480×272。以下 FPS 均为物理 H1 的已呈现帧计数与电脑单调时钟之比，测量等待区间没有截图、输入或其他 RPC。

| 场景 | 原优化起点 | 当前版本 | 当前测量 |
| --- | ---: | ---: | --- |
| 主菜单，云层及按钮动画 | 10.13 FPS | 20.88 FPS | 419 帧 / 20.070 秒 |
| 第一关教学画面，1 株豌豆射手、阳光及提示文字 | 11.02 FPS | 29.74 FPS | 595 帧 / 20.0075 秒 |

这些是各 20 秒窗口的平均值，不是最低帧率保证。尚未验证多僵尸、后期关卡及所有模式的性能。当前第一关文字复用缓存约 1.01 MB，缓存已热身后的测量期间没有资源重读或缓存淘汰；分配失败计数为零。

## 改动

- 将不可见的成就页面及时裁掉，主菜单静态图层使用离线素材和可见区域计划。
- 保留物理画布，对背景只恢复被动态对象覆盖的区域，并只向 LCD 写入发生变化的区域；指针覆盖、裁剪变化及不支持的绘制有完整回退。
- 缓存实际光栅化得到的文字像素操作及字体宽度，重复绘制时复用。透明字形仍按原顺序与当前背景混合，保留原混色舍入，缓存从堆分配并随字体或画布生命周期释放。
- 减少重复变换、三角形裁剪和图像缓存扫描，优化不透明填充与连续像素复制。

## 可核对证据

当前程序：`dist/hardware-over20-text/H1PVZPortable.bda`，3,302,988 字节。

主菜单配套离线资源为 `private/resources-h1-full/h1menu/selector.h1mb`，1,891,444 字节，安装到所选 PVZ 根目录的 `h1menu/selector.h1mb`。SHA256 为 `589bb2aad6b8d06d1804b02a1ca5b8ba89f1efc45f4dfc33f499a0836dba8b1b`；本次测试沿用已安装并校验的 B 盘资源。

SHA256：`b9728c52b6b08a144d581fa2d0498986fdd0bba81706df71667ead3b78879702`。

ELF SHA256：`c51e38a3267ff9df41e457490b49de06c47f19bc09c3a1805a85f518d95d9415`。

真机命令、原始读取和截图保存在 `build/hardware-test-20261103-over20/`：

- `079-deploy-text-result.json`、`text-program-deploy.json`：完整上传与原生文件 CRC 校验。
- `080-launch-text-result.json`、`native-code-fingerprints.json`：四处运行代码指纹与该 ELF 一致。
- `084-text-menu-result.json`：菜单 20 秒独立计数与资源读写计数。
- `087-text-game-independent-FPS-result.json`、`087-independent-FPS.json`：第一关独立帧计数。
- `084-text-menu.png`、`086-text-game.png`、`087-text-game.png`：原尺寸 LCD 截图。

`086-text-first-level-FPS-result.json` 的后一组分阶段计时读取跨越帧边界，未通过严格一致性检查，因此不作为各阶段耗时依据。第一关最终 FPS 采用独立的 `087` 两次有界帧计数读取，不依赖该分阶段计时。

主机像素对照测试已通过：240 个文字完整画面对照、64 个保留画布完整画面对照、48 个菜单画布与 LCD 对照。覆盖移动阳光、变化的文字透明度、随机背景、裁剪、菜单滑动、按钮状态、指针边缘、分配失败及资源释放；对照要求所有像素一致。另有 192 个几何变换与裁剪对照通过。结果分别保存在 `build/text-replay-engine-probe/run/`、`build/dirty-canvas-engine-probe/run/`、`build/dirty-menu-engine-probe/run/`、`build/geometry-engine-probe/`。

原生构建及源代码身份由 `build/hardware-test-20261103-over20/verify_handoff.py` 核验。该程序保留性能诊断计数，所列 SHA256 就是真机已运行的构建。

## 保存与退出

正常返回菜单并退出后，程序的原生堆记账、LCD 影子画布和文字缓存均归零，桌面任务完成且 `menu_idle=1`。`093-text-exit-save-check-result.json` 记录了保存后的 B 盘文件查询与下载；回读 `userdata/game1_0.v4` 为 35,112 字节，V4 文件头、长度与内置 CRC32 均通过，SHA256 为 `f1abdd682464d6ecada91485f9830e8a50216de20d12bf1b148a3bb06dab79bb`。目录读取正常。该下载接口未独立核验设备整文件 CRC，存档验证采用其内部 CRC32。

汇总证据为 `build/hardware-test-20261103-over20/final-native-verification.json`，可由同目录 `summarize_final.py` 离线重算。旧存档和此前的完整卡镜像继续保留。

退出后又用 H1 原生资源管理器成功打开 B 盘根目录，截图为 `094-native-B-root.png`，随后回到桌面。电脑串口会话已释放，H1Debug 驻留服务保持运行；此次没有切换到 USB 磁盘模式。
