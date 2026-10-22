# native/

本目录只放 C SDK 的**冻结接口**，不包含交付实现。

- `include/bole_agent.h`：C ABI 1.0，接口契约；改动需 A+B 双签（见 [04-c-abi.md](../docs/bole-sdk/04-c-abi.md) 与 [08-work-split.md](../docs/bole-sdk/08-work-split.md)）。
- `src/`、`tests/`、`CMakeLists.txt`、控制台/界面示例、离线包由 B 轨负责人决定与完成；本会话不代写、不代做决定。
- [`prototypes/native-c-sdk/`](../prototypes/native-c-sdk/README.md)：开发暂停前的未授权参考，不属于交付物；B 可决定采用、改写或丢弃。
