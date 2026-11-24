# Git基线与新对话开发接续

更新日期：2026-11-24。本文记录目录清理后的开发入口，不授予硬件操作许可。

## 先读什么

1. [README](../README.md)：1.0.2基线、接线、构建及已解决问题。
2. [验证记录](validation.md)：区分2026-11-20实机验证与2026-11-24只编译/主机测试。
3. [设计文档第7节](../tourbox_lite_esp32_s3.md#7-验证结果故障归因与后续接续)：排查顺序、待验证目标与修改约束。
4. 协议或USB问题再读[采集索引](../captures/README.md)及[数据采集说明](../tourbox_lite_data_collection.md)。

当前源码：固件1.0.2，ESP-IDF **6.1.1**，ESP32-S3 N16R8，实测板RGB GPIO48。BLE名`TBE Lite`，仅对已核实`CAFE:4001`的94字节Lite配置进行适配。2026-11-24全新目录重编成功，应用622,384字节，ASan/UBSan主机测试通过；未进行新的烧录或USB/BLE实机测试。

## 目录与Git范围

建议纳入Git：

- 根目录的构建描述、分区表、[依赖锁](../dependencies.lock)、[默认配置](../sdkconfig.defaults)和文档。
- `main/`源码与头文件、`tests/`及协议夹具、`tools/`脚本源码。
- `docs/`及关键历史日志、`captures/`原始采集/分析/归档清单、`third_party/`许可证。
- [忽略规则](../.gitignore)本身。

本地保留、默认不提交：根目录生成的工程配置、`dist/`已验证发布制品、`.vscode/`本机SDK路径设置。它们不是纯Git克隆的必备文件；本机SDK安装也在工程目录外，不会随Git复制。

已删除：`build/`、`managed_components/`、整个`tmp/`、Python字节码缓存，包括本轮扫描JSON、检查报告、新构建镜像与测试/构建日志，以及早期临时参考资料、Python环境、Swift模块缓存和探针app。关键历史证据保留在正式文档、日志及采集目录，不依赖已删除文件。

本次没有初始化或提交Git。可由用户在工程根目录执行：

```sh
git init -b main
git add .
git status --short
git diff --cached --stat
git commit -m "Initial TourBox Lite firmware baseline"
```

提交前检查文件范围；不要使用强制添加把忽略的生成物带入基线，也不要笼统忽略所有日志或JSON，否则会丢失项目证据。若以后公开发布，先检查日志及采集中的设备/主机地址和其他本机信息。

## 清理后恢复开发环境

以下命令从工程根目录运行，会重新生成被忽略的目录。清理后没有为了验证而再次构建，以免重新留下中间文件。

本机固件构建：

```sh
zsh tools/idf.sh build
```

包装脚本只在未设置`IDF_PATH`时激活本机安装。其他电脑安装并激活ESP-IDF 6.1.1后用`idf.py build`；目标由默认配置指定为`esp32s3`。依赖锁固定CDC-ACM 2.4.1、USB Host 1.6.1、LED Strip 3.1.1。托管组件和项目缓存已清理，首次构建可能需要联网重新获取；不要为解决下载问题随意升级锁定版本。

本机保留的工程配置与原发布配置开启`TB_PROTO_HEXDUMP_DEBUG`；纯Git克隆会根据默认配置/Kconfig生成新配置，Hex日志默认关闭。如需复现联调日志，通过`menuconfig`显式开启该选项。配置副本并非Git依赖，新增长期必要的配置应落实到默认配置或明确的受版本控制配置文件。

主机核心测试不需要ESP-IDF：

```sh
cmake -S tests -B tmp/host-tests
cmake --build tmp/host-tests
ctest --test-dir tmp/host-tests --output-on-failure
```

Python BLE/串口工具的环境已清理；需要时再创建（安装命令本身不访问硬件）：

```sh
python3 -m venv .venv
.venv/bin/python -m pip install bleak==3.1.0 pyserial==3.5
```

macOS原生BLE探针也需重新生成：

```sh
zsh tools/build_macos_ble_probe.zsh
```

该脚本生成探针app和Swift缓存；运行扫描或连接前仍需本次用户授权及macOS蓝牙权限，不能沿用旧会话许可。自定义日志输出目录应先创建。串口名称使用前重新查询；打开串口可能复位开发板，完整BLE探针可能占用Console连接。

## 后续优先事项与边界

优先验证最终手动添加页面、USB热插拔及长时间高频输入。连续超时恢复、BOOT清绑定、完整组合键/Console配置、其他硬件批次、其他Mac/Console版本和Windows仍待实机验证。

不得凭早期故障回退已核实的广播身份、加密恢复和旋转配置；不得伪造握手应答、按包长猜型号丢包或添加旋转停止码。USB发送超时后不能盲目重发，不随意修改稳定BLE身份或擦除NVS。具体约束以README及设计文档为准。

新对话可使用以下提示，并补充本次实际开发目标：

```text
请先阅读 @README.md、@docs/development.md、@docs/validation.md
以及 @tourbox_lite_esp32_s3.md 第7节，检查当前目录与Git状态，
区分历史实机验证和最新编译结果。请先提出开发/验证计划，
不要自动烧录、复位、连接设备或清除绑定。
本次目标：……
```
