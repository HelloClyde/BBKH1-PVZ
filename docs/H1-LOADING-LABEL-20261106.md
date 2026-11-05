# 关卡 Loading 文字位置

“读取中…”放在进度条内部，按 314 逻辑像素的填充宽度水平居中；文字基线为 560。实际可见字形框为 `[222,244,257,256]`，中心为 `(239.5,250)`，位于进度条条身中间。

通过原游戏渲染器重新离线生成 `level-loading.h1ll`。空进度与满进度均绘制文字，部分填充时从完成态补丁中取像素，因此不会把文字擦掉。0% 和 100% 可见字形边界一致，50% 预览也保持完整。

画面仍为 480×272 无损像素；进度条补丁区域 `[167,232,142,15]`、内存 8,520 字节。新版资源为 361,765 字节，并同步更新资源文件索引。

真机 Loading 截图与新离线画面一致。使用既有 BDA `92a0178133c6568ee7b6186e660740d10c2cc7b9125dc4afd41b76c9ec4c73cf`；本轮验证离线像素、资源传输 CRC 和原尺寸截图，已有游戏逻辑验证继续复用。

![H1 实际 Loading 画面](verification/loading-center/fresh-loading.png)

[位置与进度检查](verification/loading-center/layout-verification.json) · [真机与存档恢复](verification/loading-center/native.json)
