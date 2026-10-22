# C SDK 垂直切片（未授权参考）

本目录是 2026-10-22 开发暂停前写的一份 C SDK 参考实现，用于验证 `04-c-abi.md` / `07-offline-package.md` 的接口在 Linux 假运行时下能否走通。

**它不是 B 轨的交付物，也不代表 B 轨负责人的决定。** B 轨负责人可以：

- 采用其中的部分实现或测试；
- 按 04/05/07 重写；
- 直接丢弃。

它不在 `08-work-split.md` 的工作包、M1/M2/M3 验收门或 G1/G2 证据内；不要把它当作 B 的基线或进度。

## 可选构建（Linux）

```bash
cmake -S prototypes/native-c-sdk -B <build-dir>
cmake --build <build-dir> -j4
ctest --test-dir <build-dir> --output-on-failure
```

- 依赖冻结头文件 `native/include/bole_agent.h`。
- Windows 平台层未实现；CMake 在 WIN32 上会明确报错。
- 示例中的 `<build-dir>` 由使用者自定，不写死路径。
