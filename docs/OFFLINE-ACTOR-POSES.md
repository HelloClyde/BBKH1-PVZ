# H1 离线角色动画

`actor-poses.h1ap` 保存按 H1 的 480×272 画布生成的像素操作，覆盖豌豆射手身体、头部待机与射击、向日葵、普通僵尸待机/行走/啃食及草坪卷轴。动画每个原始关键帧采样两次。游戏更新、攻击、附着更新和动画时长使用原代码，绘制时选择最近的采样，并将位置对齐到 LCD 像素。

生成器链接完整的固定版本游戏，调用原来的 `Reanimation::DrawTrack` 和软件着色器，记录有序的不透明像素与透明混合操作。它不把多个透明层先合成一张 RGBA 图，因为那会改变原着色器的整数舍入。部件附着点划分独立片段，回放时仍在原顺序调用 `AttachmentDraw`。调色板和行程编码无损。

缓存只用于匹配的轨道状态、矩阵、普通颜色和混合模式。特殊变色、未预制的过渡、未知动作以及切过角色的自定义裁剪使用原渲染路径；LCD 外缘直接裁剪已采样的像素。首次眨眼留下的基准姿态字段不会永久禁用自身的像素回放，附着矩阵在单位值附近的极小舍入误差也会规范化。像素对齐和时间采样使任意亚像素位置/时间的结果可能与即时渲染相差一个像素；采样位置的混合结果通过不同底色的逐像素比较验证。

新增豌豆头部从待机到射击、射击回到待机的完整 20 次更新过渡。49 个待机半帧位置，配合两种实际回位起点，共 2,940 个姿态；使用 64 个静态记录保存原 `StartBlend` 的起始帧，绘制时不申请内存。导出阶段 8,820 次整帧比较一致，实际缓存路径另有 336 次比较。原植物自动更新连续 400 次，重复射击与回位都命中缓存，没有手工触发射击。

草坪翻卷及泥土粒子合为完整效果，从原 100 Hz 模拟导出 40 Hz、共 91 帧，保留 2,250 ms 尾迹及原有挖土声音起止。装饰粒子使用固定随机种子；第 1、2、4 关按原位置回放一组或两组，其余场景保留原路径。导出阶段 273 次、缓存路径 27 次整帧比较一致。

普通僵尸按部件保存 500 个半帧位置，覆盖待机、两种行走、啃食和两种常见死亡动作。普通画面与 25 档中弹亮度共用几何数据；头部和手臂的选择、断臂、无头状态仍由原轨道显隐与原断臂图片替换控制。字典复用相同部件的像素行程，不在 H1 上解压或重新采样。导出阶段 39,000 次整帧比较一致；六种头部/手臂组合、三种损伤状态及全部亮度在实际回放路径另有 22,464 次比较。

最终文件含 **3,837 个条目、5,882,613 字节**，包括 1,917 组亮度调色板与 7,666 份共用的部件像素流。包内 CRC、边界和引用在加载时校验；六种缺失/损坏输入均能回退并清理。确认返回主菜单后释放角色包与关卡草坪底图，选项弹窗期间继续保留它们。

小跨度的坐标、长度和不透明标志采用固定的三个字节，超出范围的段仍用原六字节格式。离线逐段展开后与原字段及像素索引完全一致，共省 1,594,527 字节；没有减少任何动画帧或像素。运行时只读取对应宽度的整数，不生成行程、不解压图片。读取器支持字段跨越 4 KiB 页边界。

进入关卡时一次读取、校验并预载整份文件。绘制期间不读盘、不分配内存，也不计算各部件的三角形。数据使用普通堆分配的 4 KiB 页，退出游戏统一释放。文件缺失、校验错误或分配失败时保留原绘制功能。

可复现生成顺序（先按仓库 README 配置主机编译器）：

```powershell
python tools/build_host.py --widescreen --build-dir build/actor-compiler --cxx C:/msys64/ucrt64/bin/g++.exe --cc C:/msys64/ucrt64/bin/gcc.exe
python tools/tests/run_engine_probe.py --host build/actor-compiler --probe tools/tests/actor_pose_export.cpp --entry pvzh1_actor_pose_export --output build/actor-export
python tools/tests/run_engine_probe.py --host build/actor-compiler --probe tools/tests/sod_roll_export.cpp --entry pvzh1_sod_roll_export --output build/sod-export
python tools/tests/run_engine_probe.py --host build/actor-compiler --probe tools/tests/pea_blend_export.cpp --entry pvzh1_pea_blend_export --output build/blend-export
python tools/tests/run_engine_probe.py --host build/actor-compiler --probe tools/tests/zombie_flash_export.cpp --entry pvzh1_zombie_flash_export --output build/zombie-export
python -c "from pathlib import Path; Path('build/actor-all.raw').write_bytes(b''.join(Path(p).read_bytes() for p in ['build/actor-export/run/actor-poses.raw','build/sod-export/run/sod-roll.raw','build/blend-export/run/pea-blends.raw','build/zombie-export/run/zombie-flash.raw']))"
python tools/pack_actor_poses.py build/actor-all.raw private/resources-h1-wide/actor-poses.h1ap
python tools/bake_file_index.py
```

`--output` 必须指向新目录。生成前应按 README 准备其余离线资源。最后更新资源清单；部署时先传动画文件，再传 `resource-files.h1fi`。目标目录为 `B:\应用\数据\游戏\PVZ`，也支持 A 盘。
