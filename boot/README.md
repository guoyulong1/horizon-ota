# boot

U-Boot 启动脚本、分区布局、A/B 与 recovery 安装方案。

## 职责

- `boot.scr` 源文件（选槽、bootcount、失败切回）
- 分区表（GPT、PARTUUID）
- recovery 触发逻辑

## 状态

待实现（M3–M4）。

## 参考

- 安装方案：`docs/OTA.md` 4.8
- 确认与回滚：`docs/OTA.md` 4.9
