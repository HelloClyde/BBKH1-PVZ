# 草皮展开的离线合成

早期记录将开场静止预览误标为镜头平移，约 40 FPS 的结果不用于横移性能结论。5a677de1 补测实际横移仅 11.38 FPS。草皮展开也曾只有约 9 FPS：该阶段反复将大块透明草皮混到裸土地面上，角色离线回放不能消除这部分工作。

`early-lawns.h1lb` 保存原绘制器生成的六张 480×272 不透明端点画面，分别为第一、二/三、四关展开前后的底图。16×8 像素块去重后为 2,353 块、1,217,040 字节；全部 783,360 个解码像素与原端点一致。

运行时只按当前展开边界复制原端点像素，不再为草皮逐像素混合。原三角形绘制器会随裁剪宽度改变 UV 舍入，产生内部纹理采样抖动；这里固定端点采样，边界按 LCD 像素前进。因此中间帧不宣称与旧绘制器逐位一致。草皮卷轴、粒子、游戏更新和动画时长保留。镜头移动另用下述全景；特殊裁剪与其他背景使用原路径。

整份数据进入关卡时预载到普通堆页。稳定草坪按已有脏区恢复，滚动时复制不透明画面；绘制期间不读盘或分配内存。文件缺失或校验失败时使用原路径。

生成顺序：

```powershell
python tools/build_host.py --widescreen --build-dir build/lawn-compiler --cxx C:/msys64/ucrt64/bin/g++.exe --cc C:/msys64/ucrt64/bin/gcc.exe
python tools/tests/run_engine_probe.py --host build/lawn-compiler --probe tools/tests/early_lawn_export.cpp --entry pvzh1_early_lawn_export --output build/lawn-export
python tools/pack_early_lawns.py build/lawn-export/run private/resources-h1-wide/early-lawns.h1lb --report build/lawn-export/early-lawns.json
python tools/bake_file_index.py
```

使用新的输出目录。部署先传 `early-lawns.h1lb`，最后传 `resource-files.h1fi`。当前运行时回放已通过 176 次完整画面比较、脏区恢复和镜头/裁剪守卫检查；真机性能另见运行时验收记录。

## 镜头横移全景

`camera-lawns.h1cp` 保存三张 634×272 不透明全景，对应首次冒险白天第 1、2/3、4 关。屏幕只显示其中 480×272，额外 154 列覆盖横移边缘。离线调用原 `DrawBackdrop` 合成，原背景存储尺寸为 630×270；导出画布最右列/底行的 905 个光栅边界像素从相同背景取样补齐。整体 alpha 固定为不透明。原相机曲线与时间保持，移动量取整到 LCD 像素，故不宣称每个亚像素位置与原三角形采样逐位一致。

运行时仅提前读入当前关卡的一张图，像素及页表共 690,468 字节，移动时直接复制视口；草皮开始翻卷即释放。超出适用条件或文件缺失/损坏则回到原绘制。2,720 次视口与脏区恢复比较、四种异常文件回退和释放检查通过。64dd50b6 真机实际横移为 27.03 FPS，翻卷为 26.99 FPS；完整计时口径见[运行时记录](H1-RUNTIME-PERFORMANCE-20261104.md)。

```powershell
python tools/tests/run_engine_probe.py --host build/lawn-compiler --probe tools/tests/camera_lawn_export.cpp --entry pvzh1_camera_lawn_export --output build/camera-export
python tools/pack_camera_lawns.py --input build/camera-export/run
python tools/bake_file_index.py
```

使用新的输出目录，部署先传全景、再传索引。文件 SHA、逐帧 CRC 和生成器身份位于 `provenance/offline-camera-lawns.json`。
