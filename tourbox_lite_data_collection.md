# TourBox Lite 实物数据采集

本说明及配套材料已于2026-11-20导入当前项目。原始采集发生于2026-11-17，文件、三轮分析和原始profile完整保存在 [captures归档](captures/README.md)，复测工具为本地`tools/tourbox_probe.py`。本文的项目入口及当前状态已修订；原始JSON/JSONL保持内容不变，分析报告仅修订本地文档链接，校验记录见 [归档清单](captures/archive-manifest.json)。后续无需访问原项目目录。

第2至7节用于复现**TourBox Lite直接连接Mac**的USB采集流程，不能把Lite串口与ESP32调试UART混用。当前1.0.2桥接固件已完成USB/BLE联调、四方向输入及自动重连验证，结论见 [README](README.md)、[系统设计](tourbox_lite_esp32_s3.md)和 [验证记录](docs/validation.md)；原始采集报告中的“尚未验证”描述只适用于当时的直接USB采集阶段。

## 1. 已取得的数据

已经完成只读 USB 配置描述符采集，结果保存在 [描述符 JSON](captures/usb-descriptors-20261117-213903-999905.json)。本次实物为 `CAFE:4001`，CDC 控制接口 0、数据接口 1。

| 端点 | 类型 / 方向 | 最大包长 |
| --- | --- | --- |
| `0x81` | Interrupt IN，CDC 通知 | 8 字节 |
| `0x02` | Bulk OUT，电脑 → 设备 | 64 字节 |
| `0x82` | Bulk IN，设备 → 电脑 | 64 字节 |

配置总长度 75 字节；CDC 版本 `0x0120`；Union 描述符绑定接口 0→1；ACM capabilities=`0x02`。配置声明功耗为100mA，**不是实际电流测量结果**。

后续可用 `python3 tools/tourbox_probe.py descriptors` 再次只读采集。脚本使用本机已有的 libusb，不需要 pip 安装；它不打开/占用设备接口。若系统报告可见而 libusb 返回零设备，可能是运行环境的沙箱限制，需在本机终端或获准的环境运行，不代表设备没有连接。

## 2. 先准备一轮干净的串口测试

1. 彻底退出 **TourBox Console**，包括菜单栏后台程序，并关闭其他串口工具。
2. 拔掉 TourBox USB，等待几秒后重新插入。测试完成前保持 Console 退出，避免它自动初始化设备。
3. 打开独立终端，进入项目目录并运行：

```sh
# 在包含本说明及tools/目录的当前项目根目录运行
python3 tools/tourbox_probe.py capture
```

脚本默认使用 `/dev/cu.usbmodemTourBox1`、115200/8N1，并尝试设置DTR/RTS=true。打开串口本身可能改变设备控制线状态。它会先检查串口占用，持续保存收到的原始字节；只有你输入 `unlock` 后才发送一次已知解锁指令。默认不发送配置，仅在你另外输入 `config-zero` 时发送第6节的94字节对照配置。

## 3. 获取解锁回复

先不要触碰任何控件。启动脚本后空闲约2秒，再输入：

```text
unlock
```

按回车，继续保持不操作至少2秒，观察是否出现 `RX ... bytes:`。脚本发送的是二进制 `55 00 07 88 94 00 1A FE`，不会自动重复发送。

如果没有回复，先不要追加其他配置指令。仍可按一次按钮观察是否有输入，随后输入 `quit` 保存本轮数据；反馈日志及是否能在官方Console中正常工作，再判断是命令差异、控制线或初始化需求。

## 4. 分别记录控件

每项操作前输入 `mark 操作名称` 并回车。每次按下后保持约1秒，释放后再等约1秒，先分开各项，避免初始样本混淆。

| 先输入的标记 | 随后执行的操作 |
| --- | --- |
| `mark tall` | Tall按下/释放3次 |
| `mark top` | Top按下/释放3次 |
| `mark short` | Short按下/释放3次 |
| `mark c1` | C1按下/释放3次 |
| `mark c2` | C2按下/释放3次 |
| `mark tour` | Tour按下/释放3次 |
| `mark scroll_click` | 滚轮按下/释放3次 |
| `mark knob_click` | 旋钮按下/释放3次 |
| `mark scroll_up` | 滚轮向上缓慢转动，停止后等待1秒 |
| `mark scroll_down` | 滚轮向下缓慢转动，停止后等待1秒 |
| `mark knob_cw` | 旋钮顺时针缓慢转动，停止后等待1秒 |
| `mark knob_ccw` | 旋钮逆时针缓慢转动，停止后等待1秒 |
| `mark idle` | 完全不操作10秒，观察是否有空闲上报 |

只有输入稳定时，再增加 `mark tall_knob`：按住Tall，旋钮分别顺/逆时针转动，最后释放Tall。不要把此前解锁回复中的字节解释为输入事件；脚本只保存原始数据，不做这一猜测。

## 5. 保存并反馈

输入 `quit` 并回车，或按Ctrl+C。结果自动保存在 `captures/serial-session-时间戳.jsonl`，包括打开时的数据、控制线请求结果、TX/RX原始十六进制、操作标记与时间戳。

回复采集文件名和实际现象即可，文件已在共享项目目录中，无需手动复制大量十六进制。若错误提示串口不存在，可运行 `python3 -c 'import glob; print(glob.glob("/dev/cu.usbmodem*"))'`，然后用 `capture --port 实际串口路径` 重试。

完成记录、退出脚本后，可以重新打开官方Console测试正常使用。需要重复解锁样本时，再次退出Console、拔插设备、创建新的capture会话，不在同一个会话中重复发送。

本轮目标是确认解锁回复和输入码；官方Console完整命令序列、配置包、控制线是否必需及USB实际传输时序仍需后续专项采集。日志中的RX分块是macOS串口读取分块，**不是USB包边界**；时间戳也不是USB总线抓包时间戳。

## 6. 第二轮：旋转无输入时测试配置

首次采集已经收到解锁26字节回复和全部8个按钮的正确输入，四个旋转方向无数据。分析见 [首次串口采集分析](captures/首次串口采集分析.md)。idle无RX本身不异常；本轮重点是验证是否需要配置才能输出旋转事件。

1. 退出Console和旧脚本，拔插设备，再运行第2节的capture命令。
2. 输入 `unlock`，确认26字节回复，保持不操作至少2秒。
3. 输入 `config-zero` 并回车。脚本手动发送`tuxbox v3.8.1 haptic.py`的94字节默认配置，全部强度/速度参数为零；是否适用于Lite、是否能开启旋转仍是本轮测试内容。脚本不会自动重复配置。
4. 再保持不操作至少2秒，保存可能出现的配置回复。**没有配置回复不等于配置未生效**，随后以真实旋转输出验证。
5. 依次输入 `mark scroll_up`、`mark scroll_down`、`mark knob_cw`、`mark knob_ccw`，每次标记后只操作对应方向、停止并等待1秒，再进行下一项。
6. 输入 `mark tall`，按下/释放Tall一次，检查配置后按钮仍可输出。最后输入 `quit`。

脚本的配置前检查只根据本次已测Lite的回复形态，防止未解锁就发送配置，不能当作通用认证解析器。日志会区分`unlock`和`config-zero`的TX，并保存每个实际写入片段；不会把配置回复解读成按键。

如果配置后仍没有旋转输入，停止追加未知命令，保留新日志并确认官方Console下旋钮/滚轮是否正常。接着对照官方初始化数据，而不是反复试猜测的配置包。

第二轮已经通过：四个方向输出`49/09/44/04`，配置后Tall仍输出`00/80`，无专门配置回复或停止码。结果见 [第二轮分析](captures/第二轮串口采集分析.md)。后续采集都使用已测的`unlock→config-zero`初始化。

## 7. 补采组合旋转与同次上电重连

这一步补充首次有线采集尚未覆盖的组合输入和重开串口行为。无需再次重复所有单独控件。

1. 退出Console，拔插设备，运行 `python3 tools/tourbox_probe.py capture`，按第6节发送`unlock`、等待回复、发送`config-zero`并等待2秒。
2. 输入 `mark tall_knob_configured`。按住Tall，旋钮顺时针慢转数步并停止，再逆时针慢转数步并停止，最后释放Tall。
3. 输入 `mark top_scroll_configured`。按住Top，滚轮分别向上、向下慢转数步，最后释放Top。每个方向间等待约1秒。
4. 输入`quit`，保持USB连接及Console退出，立即重新运行capture，创建新日志。
5. 新会话先不操作、输入`unlock`，等待回复2秒。输入`mark knob_before_reconfig`并只顺时针慢转，观察前次配置是否保留。
6. 输入`config-zero`，等待2秒，再输入`mark knob_after_reconfig`并顺时针慢转，最后输入`quit`。

两份新日志用于检查组合时是否沿用原始按钮/步进码，以及同次上电重开串口后是否仍需重新配置。开关DTR或关闭串口可能改变设备状态，不能仅凭未拔USB就认定协议状态保留。若重开后解锁无回复，结束该日志，不追加未知命令，保留结果供分析。

此项现已完成：两种组合按原始按钮/旋转码分开上报；重开后配置保留，重发配置也可用。分析见 [第三轮报告](captures/第三轮组合与重连采集分析.md)。电脑端基础协议采集到此完成，后续转为ESP32 USB Host/BLE实机验证；此前步骤保留供复测。

## 8. 当前固件验证与复测范围（2026-11-20）

当前验证环境为Mac与官方TourBox Console，固件1.0.2。用户已确认配对、按键、滚轮上下、旋钮顺逆时针及重启Console自动连接；开发板复位后的自动加密重连和旋转配置恢复也有 [串口日志](docs/logs/reconnect-cycle-device.log)证据。Windows、完整配置/组合键、最终手动添加页面、USB热插拔和长期压力仍待专项验证。

运行桥接固件时，Lite通过带供电OTG/Y线连接ESP32原生USB Host，当前开发板通过电脑连接UART供电并输出日志。Mac此时看到的是ESP32调试串口，不是Lite的直接串口：

| 场景 | 工具与端口 |
| --- | --- |
| Lite直连Mac，复现本说明采集 | `tools/tourbox_probe.py capture --port 实际Lite串口`，历史为`/dev/cu.usbmodemTourBox1` |
| ESP32桥接固件诊断 | `tools/serial_capture.py --port 实际ESP32_UART --duration 600 --output tmp/会话.log`，当前UART为`/dev/cu.usbmodem5D4D0503581` |
| 只查看ESP32蓝牙广播 | TourBox BLE Probe加`--scan-only`；操作见README |

先打开ESP32串口再执行BLE测试，打开控制线可能使开发板复位。ESP32数据由Console发起初始化，桥接固件只将匹配的94字节配置参数归零；不要同时运行直连Lite采集脚本或额外抢发一套解锁。

本项目当前日志走UART，并未实现Flash持久日志及离线导出。它们如需新增，应另行设计和验证，不属于已完成的采集或固件功能。直接USB原始记录存于`captures/`，ESP32联调与Console筛选证据存于`docs/logs/`，两者按版本和测试阶段区分。所有复测资料与当前知识均在本项目内。
