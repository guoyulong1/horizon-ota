# pack

打包 + 离线签名工具：把一次构建的产物整理成可发布的目录。

## 职责

- 收集组件（内核、dtb、rootfs），计算 SHA-256 与 `release_id`
- 生成发布清单 `manifest.json`（字段见 `docs/OTA.md` 2.3）
- 校验输入合法性（版本号、硬件兼容 ID、必填字段）
- 输出发布目录：`releases/<型号>/<版本>/<release_id>/`
- 签名由离线机完成，产出 `manifest.json.sig`（Ed25519，64 字节）

## 状态

待实现（M1）。

## 参考

- 流程与契约：`docs/OTA.md` 第 2 章
- 下载端参考实现：[fsd](../fsd/)
