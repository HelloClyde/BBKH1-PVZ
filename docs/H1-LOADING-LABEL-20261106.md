# 关卡 Loading 文字位置

将“读取中…”的逻辑基线从 576 调整为 496，在 480×272 屏幕上上移约 36 像素，放到进度条上方并保持水平居中。通过原游戏渲染器重新离线生成 `level-loading.h1ll`。

画面仍为 480×272 无损像素。进度条补丁 `[167,232,142,15]`、8,520 字节内容逐字节一致；除新旧文字所在区域外，背景像素相同。资源缩小 102 字节至 361,531 字节；同时更新资源文件索引中的尺寸与校验清单。

真机 Loading 截图与新离线背景逐像素一致。程序仍是此前验证的 BDA `92a0178133c6568ee7b6186e660740d10c2cc7b9125dc4afd41b76c9ec4c73cf`；本轮验证离线像素、资源传输 CRC 和原尺寸截图；已有游戏逻辑验证继续复用。

![H1 实际 Loading 画面](verification/loading-label/fresh-loading.png)

[像素对比记录](verification/loading-label/layout-verification.json) · [真机与恢复记录](verification/loading-label/native.json)
