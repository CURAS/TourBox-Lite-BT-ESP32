# TourBox Lite 有线版转蓝牙版（ESP32-S3）系统设计与验证

| 文档属性 | 当前基线 |
| --- | --- |
| 项目 | TourBox Lite Wired-to-BLE Bridge |
| 文档/固件版本 | **1.0.2，2026-11-20** |
| 硬件 | ESP32-S3-DevKitC-1 N16R8；实测ESP32-S3 rev0.2、16MB Flash、8MB Octal PSRAM |
| 框架 | ESP-IDF v6.1.1、FreeRTOS、USB Host CDC-ACM、NimBLE |
| 已验证上位机 | 当前Mac上的官方TourBox Console；Info版本5.12.1，运行日志版本5.13.0 |
| 平台范围 | Windows是兼容目标，尚未实测；其他Mac、Console版本和TourBox批次尚未验证 |
| 协议依据 | 用户直接USB采集、当前固件联调日志、已安装Console原生BLE库、tuxbox v3.8.1参考常量 |
| 操作入口 | [README.md](README.md)；详细证据见 [docs/validation.md](docs/validation.md) |

本文已将原v1.0方案更新为实际实现。明确区分已实现行为、日志验证、用户确认和待验证目标，供后续新对话直接续接。当前源码、已烧录开发板与`dist/`制品均为1.0.2。

## 1. 项目目标与已达成状态

通过开发板原生USB Host连接有线TourBox Lite，使用自定义BLE GATT服务连接官方Console，保持原设备和电脑之间的输入/控制字节流。无需拆解TourBox，也无需修改Console程序或安装常驻主机辅助程序。

用户已确认配对成功，按键、滚轮上下及旋钮顺逆时针都有响应，并再次确认重启Console后自动连接。保持Console运行时，UART复位开发板也已在日志中验证自动加密重连和初始化恢复。USB热插拔、故障恢复及长期压力测试属于已实现但尚未完成实机验证的部分。

协议原则是优先透传，当前唯一内置转换为当前实物已核实的94字节旋转配置。没有伪造型号、认证或扩展握手应答；不能把“模拟官方蓝牙设备”理解为任意构造响应或声称已复刻全部官方蓝牙硬件行为。

## 2. 硬件、供电与资源

### 2.1 接线与引脚

```text
电脑 ── UART口 ── ESP32-S3（烧录、UART0日志、开发板供电）
                         │
                  原生USB口（Host）
                         │
             带外部5V供电的OTG Y线
                         │
                   TourBox Lite
```

原生USB Host模式不会自动为下游TourBox输出5V VBUS。当前用户通过电脑连接UART供电；TourBox侧须有带数据功能的OTG连接及稳定外部5V供电。推荐5V/≥1A供电，但本次未测量电流、VBUS压降或电源纹波，不能将原方案中的功耗预算当作实测结果。

| 功能 | 引脚/配置 | 实现与证据 |
| --- | --- | --- |
| 原生USB D-/D+ | GPIO19/GPIO20 | USB Host，连接TourBox |
| UART0 TX/RX | GPIO43/GPIO44 | 板载UART桥，115200日志与烧录 |
| RGB WS2812 | **GPIO48** | 当前板实测；初用GPIO38不亮，修正后用户确认红色慢闪 |
| BOOT | GPIO0，上拉 | 短按统计，长按3秒清除绑定 |
| 外部避让 | GPIO35/36/37 | N16R8内部存储相关引脚，不用于本项目外设 |
| CPU/内存 | 240MHz、16MB QIO Flash、8MB Octal PSRAM/80MHz | 启动日志验证 |
| 控制台 | UART0；USB主/次控制台关闭 | 原生USB资源用于Host |

不同DevKitC板型的RGB可能接GPIO38，允许Kconfig切换，当前基线保持GPIO48。红色慢闪表示USB等待，蓝色2Hz呼吸表示USB就绪但BLE未连接，绿色常亮表示链路建立；绿色本身不证明全部握手或输入已经可用。

### 2.2 Flash分区与发布制品

| 分区/镜像 | 地址 | 大小/用途 |
| --- | --- | --- |
| bootloader | 0x0 | 引导镜像 |
| partition table | 0x8000 | 分区表 |
| NVS | 0x9000 | 64KiB，绑定密钥及桥接配置 |
| PHY | 0x19000 | 4KiB |
| factory app | 0x20000 | 3MiB，1.0.2应用622,384字节 |

`dist/`保存引导、分区表、应用、`sdkconfig`、`flash_args`和 [SHA-256清单](dist/manifest.json)。首次烧录需三份镜像；正常烧录保留NVS。Flash镜像头使用构建生成的DIO烧录参数，应用启动日志报告QIO；使用`dist/flash_args`中的真实参数，不自行推测覆盖。

## 3. USB与实物协议

### 3.1 当前设备档案及证据来源

本机TourBox Lite的真实VID/PID为 **CAFE:4001**，已经由用户直接USB采集及ESP32枚举确认，不能仅因该VID常见于其他固件就将其判为错误设备。

| 项目 | 实测值 |
| --- | --- |
| bcdDevice | 0200 |
| CDC控制/数据接口 | 0 / 1 |
| Bulk OUT | 02，最大包64字节 |
| Bulk IN | 82，最大包64字节 |
| Interrupt IN | 81，最大包8字节 |
| CDC线路 | 115200、8N1、DTR=1、RTS=1 |

DTR/RTS置位组合已验证工作；尚未独立测试每条控制线的必要性。固件优先匹配CAFE:4001，保留0483:5741、0483:5740、C251:2005、2E3C:5740和标准CDC通用匹配；这些其他ID没有在本次硬件上逐一验证。

直接USB采集依据已复制到本项目，所有入口均为本地相对路径：

- [设备数据采集说明](tourbox_lite_data_collection.md)及复测工具`tools/tourbox_probe.py`。
- [采集索引](captures/README.md)、[原始实测档案](captures/tourbox-lite-observed-profile.json)、三轮分析及四份`serial-session-20261117-*.jsonl`。
- [归档清单](captures/archive-manifest.json)，记录原始文件及导入工具的SHA-256。

原始描述符、日志与profile保持内容不变，分析报告仅修订本地文档链接。其scope/pending等字段描述2026-11-17直接USB采集当时状态。本地 [采集摘要](docs/lite-observed-profile.json)通过相对路径指向这些原始证据；2026-11-20新增BLE验证以本文和`docs/validation.md`为准。

### 3.2 解锁与扩展握手

Console发出的解锁帧：

```text
55 00 07 88 94 00 1a fe
```

当前设备实测26字节回复样本：

```text
07 e0 5c d1 c1 b2 ca 5e 9e a1 5f 26 33 6c f7 0b
00 00 00 02 00 03 03 01 00 00
```

此样本用于识别和比对抓包，字段含义尚未全部解析，绝不能作为通用模板伪造。Console的16字节D5扩展握手及实物12字节应答同样完整透传，见 [集成日志](docs/logs/integration-v102-device.log)。不依据包长判断并丢弃回复，不修改未知认证字节。

正常模式由Console发起解锁，`CONFIG_TB_USB_AUTO_UNLOCK`默认关闭；不重复发送额外解锁。仅解锁能启用按键，**不能保证滚轮和旋钮已启用**。

### 3.3 94字节旋转配置适配

直接USB采集确认以下零参数配置启用四个旋转方向，未观察到独立配置ACK：

```text
b5 00 5d 04 00 05 00 06 00 07 00 08 00 09 00 0b
00 0c 00 0d 00 0e 00 0f 00 26 00 27 00 28 00 29
00 3b 00 3c 00 3d 00 3e 00 3f 00 40 00 41 00 42
00 43 00 44 00 45 00 46 00 47 00 48 00 49 00 4a
00 4b 00 4c 00 4d 00 4e 00 4f 00 50 00 51 00 52
00 53 00 54 00 a8 00 a9 00 aa 00 ab 00 fe
```

当前Console通过BLE下发同一布局，但参数为08，出现“按键正常、旋转无事件”。`main/lite_profile.c`提供内置替换规则，`CONFIG_TB_LITE_ZERO_CONFIG=y`默认启用：

1. 仅USB身份为CAFE:4001时激活。
2. 匹配完整94字节布局，仅忽略从0计数的偶数位置4至92的参数值。
3. 保留头部、控件ID及FE结尾，参数归零；其他布局和其他设备透传。
4. 合并Console实测的20+20+20+20+14字节片段，间隔约30ms；规则前缀超时至少250ms。
5. 不额外生成ACK、旋转停止码或输入事件；型号切换时清理旧会话并更新规则。

规则上限由64提升为128字节。一般前缀超时20ms，Lite适配激活时至少250ms；只有匹配前缀进入等待，上行输入不因此延迟。真实夹具分别为 [Console非零帧](tests/fixtures/console-b5-nonzero.hex) 和 [实物零参数帧](tests/fixtures/lite-config-zero.hex)。

### 3.4 输入码与组合顺序

| 输入 | 按下/步进 | 释放 |
| --- | --- | --- |
| Tall | 00 | 80 |
| Top | 02 | 82 |
| Short | 03 | 83 |
| C1 | 22 | A2 |
| C2 | 23 | A3 |
| Tour | 2A | AA |
| 滚轮点击 | 0A | 8A |
| 旋钮点击 | 37 | B7 |
| 滚轮上/下 | 49 / 09 | 未观察到停止码 |
| 旋钮顺/逆时针 | 44 / 04 | 未观察到停止码 |

本地 [组合采集报告](captures/第三轮组合与重连采集分析.md)显示Tall+旋钮、Top+滚轮的独立按键与步进字节按原顺序出现，无需组合专有码。例如Tall按住顺/逆转为00、若干44、若干04、80。固件保持原始顺序；完整多键组合及全部Console映射仍待实机覆盖。

## 4. BLE发现、配对与自动重连

### 4.1 GATT结构

UUID可在NimBLE中压缩为等价16位形式，完整形式以`0000xxxx-0000-1000-8000-00805f9b34fb`表示。

| 服务/特征 | 属性与实现 |
| --- | --- |
| GAP 1800 | 设备名TBE Lite，Appearance默认0 |
| GATT 1801 | Service Changed；按服务表指纹变化标记缓存失效 |
| DIS 180A | 默认开启；制造商TourBox Tech、型号TourBox Lite、固件ESP32 bridge 1.0.2；未实现PnP ID |
| BAS 180F / 2A19 | Read + Notify，固定100%，没有真实电池测量 |
| 主服务 FFF0 | Primary Service |
| TX FFF1 | Notify，自动CCCD；默认要求加密订阅 |
| RX FFF2 | Write + Write Without Response；默认要求加密写入 |

连接时协商MTU247，Notify最多244字节；订阅后请求间隔6~12×1.25ms、latency0、监督超时400×10ms。实际观测间隔为15ms。这是链路间隔，不是端到端输入延迟测量。

### 4.2 Console兼容广播与稳定身份

参考tuxbox提供了FFF0/1/2等常量，但不能据此推定官方Console接受任意TourBox名称。本机Console原生库位于`/Applications/TourBox Console.app/Contents/Resources/osxlibs/libBLEWrapper.dylib`，实测其默认名称前缀为TourBox Elite，并兼容TBE前缀。最初的TourBox Lite名称被过滤，最终名称为 **TBE Lite**。

Console使用`CBPeripheral.name`筛选。完整短名与厂商数据同时放入主广播，避免只在扫描响应放名的缓存问题：

| AD内容 | 占用 |
| --- | --- |
| Flags：通用发现、仅BLE | 3字节 |
| 完整名称TBE Lite | 10字节 |
| 厂商数据 | 18字节，含AD头 |
| 主广播总长 | **31字节** |
| 扫描响应 | 完整服务UUID FFF0 |

厂商有效负载为16字节：

```text
54 42 45 20 | 设备地址6字节（显示序） | 目标PC地址6字节（显示序）
 T  B  E 空格
```

此兼容格式不额外前置Espressif公司ID，否则地址字段偏移。设备身份来自芯片公共BT地址确定性派生：NimBLE小端数组`[5] |= 0xc0`、`[0] ^= 0x5a`。当前公共地址f8:5b:1b:97:c8:de，最终静态随机身份 **f8:5b:1b:97:c8:84**。该身份跨复位保持，不能为绕过缓存而每次随机更换。

### 4.3 加密、NVS及缓存处理

Just Works、LE Secure Connections及绑定开启，NVS保存密钥；FFF1订阅和FFF2写入默认要求加密。新配对可以主动请求安全过程；已保存配对主机时由macOS恢复加密，避免重复SMP在约30秒后报`Encryption status=13`。

加密成功或身份解析后，每500ms检查绑定，最多6次。仅已加密、已绑定且具有公共身份的连接用于保存主机地址。NVS命名空间`tb_bridge`：

| 键/数据 | 用途 |
| --- | --- |
| peer_pc，6字节blob | 真实配对主机公共地址，显示字节序 |
| gatt_schema，u32 | UUID、服务布局及权限指纹 |
| NimBLE持久化数据 | 配对密钥及相关绑定状态，由BLE存储管理 |

本机解析的Mac身份 **d0:11:e5:6e:d1:b3** 与系统蓝牙控制器地址一致。固件没有硬编码这个Mac地址。公共身份不可用时保持通用发现，不能编造主机目标。

实测每次订阅都调用Service Changed会卡住macOS特征发现；当前仅指纹变化时调用`ble_svc_gatt_changed`标记已绑定客户端缓存失效。BOOT长按只清BLE绑定和peer_pc，不擦整个NVS。

### 4.4 两种发现模式与重连状态机

本机Console原生筛选规则：

- 手动配对模式（pairMode=1）：目标为零生成SCAN BRDCST事件，选中设备后允许连接。
- 自动模式（pairMode=0）：目标须等于本机公共蓝牙地址，设备地址还须在Console白名单。

因此只发送零目标会需要手动重连，只发送主机目标又可能从手动添加列表隐藏。最终`CONFIG_TB_BLE_HOST_TARGET_ADVERTISING=y`采用：

```text
未配对 → 持续通用发现（目标全零）
已配对、未连接 → 真实主机目标5秒 → 通用发现2秒 → 循环
连接成功 → 停止轮换 → 恢复加密/订阅 → Console解锁/握手/旋转配置
断线 → 清理连接会话 → 优先主机目标广播
清绑定 → 删除密钥与peer_pc → 持续通用发现
```

“主机目标”仅指厂商数据字段；链路仍使用可连接、可扫描的普通广播（BLE_GAP_CONN_MODE_UND），并非BLE协议层的Directed Advertising。

本机Console配置`~/Library/Application Support/TourBox Console/customConfig`含`validAddrs=f85b1b97c884#`。原生离线筛选通过后，实际Console一度仍未自动连接；正常退出再启动Console后恢复。用户最后也确认Console重启自动连接。离线筛选值不能替代真实连接证据，Console扫描模式/会话状态是排查重点。

曾在受限环境下用旧IOBluetooth接口读取本机MAC得到null，正常权限环境读到d011e56ed1b3；不能仅凭受限环境的空结果认定系统没有MAC。当前没有修改Console偏好设置或二进制。最终手动添加页面尚未得到用户复核，已分别验证轮换广播和原生筛选。

## 5. 软件架构、队列与恢复

### 5.1 模块与数据通路

```text
USB RX回调 → bridge_core上行队列 → bridge_tx_task → NimBLE主机事件 → FFF1 Notify
FFF2写回调 → bridge_core下行规则/队列 → usb_cdc_task → CDC USB TX
```

| 文件 | 职责 |
| --- | --- |
| main/app_main.c | NVS、看门狗与模块初始化 |
| main/usb_cdc_host.c | CDC枚举、线路、热插拔、USB发送及根端口恢复 |
| main/ble_gatt_server.c | 服务、广播轮换、配对、身份持久化及主机上下文Notify |
| main/tourbox_advertisement.c | 厂商负载格式与字节序 |
| main/bridge_core.c | 无RTOS依赖的有界FIFO、规则、MTU与会话编号 |
| main/proto_bridge.c | 临界区保护、任务与桥接统计、异步Hex日志 |
| main/lite_profile.c | 实测CAFE:4001的94字节配置模板和掩码 |
| main/protocol_rules.c | 默认空的自定义规则表 |
| main/ui_indicator.c | RGB与BOOT行为 |

### 5.2 任务配置

| 任务 | 核心 | 优先级 | 栈 |
| --- | --- | --- | --- |
| NimBLE主机 | Core0 | SDK默认 | 4096B |
| USB库守护 | Core1 | 20 | 4096B |
| CDC驱动 | Core1 | 19 | 4096B |
| USB应用 | Core1 | 18 | 4096B |
| BLE桥接发送 | Core1 | 16 | 4096B |
| UI | Core0 | 5 | 3072B |
| Hex trace，开启时 | Core0 | 4 | 4096B |

双向缓冲实际为两个2048字节自定义环形FIFO，配合短临界区与任务通知，**不是原设计示例中的FreeRTOS StreamBuffer**。规则应答可构成额外生产者，因此必须串行保护访问。USB/GATT回调不等待发送，不直接执行阻塞BLE传输；Hex经独立队列延期打印。

### 5.3 会话隔离与错误恢复

Notify按MTU-3分片，保持输入原顺序。未连接/未订阅时丢弃上行；USB断开、BLE断开、取消订阅或型号切换清理旧数据，会话编号拒绝迟到旧包。

BLE发送在NimBLE主机事件上下文执行。内存不足退避2ms重试3次；持续失败或上行溢出则断BLE建立新会话，避免漏掉释放事件后继续卡键。请求等待超过1秒触发错误处理，不能复用仍有待处理事件的请求存储。

下行满时整段拒绝，Write Without Response也触发断链恢复。USB发送超时可能已发送部分数据，不盲目重发；超时断BLE，连续3次超时或总线错误关闭CDC并重置Host根端口重新枚举。根端口重置不等于切断外部VBUS。

USB拔出会使BLE断开，重连后由Console重新初始化；此分支已实现，热插拔及连续超时故障注入尚待实测。USB应用与桥接发送任务受5秒TWDT监控。每30秒记录队列、堆、内部堆、mbuf及字节/错误计数。

### 5.4 自定义规则边界

规则支持跨包、粘包、掩码，最大128字节，互相重叠的匹配前缀会被拒绝。`TB_RULE_REPLACE/REPLY/DROP`均为可扩展能力；当前自定义表为空，内置Lite规则独立控制。新增回复或丢弃规则必须基于准确采集帧，不能根据猜测型号或包长添加。

## 6. 开发、诊断与操作知识

依赖锁定USB CDC-ACM 2.4.1、USB Host 1.6.1、LED Strip 3.1.1，见`dependencies.lock`。ESP-IDF 6.x USB Host由托管组件引入。当前`sdkconfig`及`dist/sdkconfig`开启Hex联调日志；Kconfig默认关闭Hex，自定义规则仍关闭，Lite配置适配与加密/主机广播默认开启。

| 本机资源 | 路径 |
| --- | --- |
| 项目 | /Users/ramda/Documents/test/TourBox-Lite-BT-ESP32-NEW |
| UART | /dev/cu.usbmodem5D4D0503581 |
| ESP-IDF | /Users/ramda/.espressif/v6.1.1/esp-idf |
| SDK Python | /Users/ramda/.espressif/tools/python/v6.1.1/venv/bin/python |
| Console | /Applications/TourBox Console.app；Bundle ID com.tourbox.ui.launch |
| BLE探针 | tools/macos_ble_probe.swift；生成tmp/TourBoxBLEProbe.app；ID dev.tourbox.bridge.bleprobe |

常用命令：

```sh
zsh tools/idf.sh build
zsh tools/idf.sh -p /dev/cu.usbmodem5D4D0503581 flash
cmake -S tests -B tmp/host-tests
cmake --build tmp/host-tests
ctest --test-dir tmp/host-tests --output-on-failure
```

UART采集脚本为`tools/serial_capture.py`，支持`--duration`、`--reset`及`--output`。交互验证用600秒窗口，短窗口可能在用户操作前结束。打开串口的控制线可能使板复位，先开UART再做BLE操作；停止采集使用Ctrl+C保存日志并释放端口，烧录前确认没有监视器占用。

macOS权限按应用/进程分别授予，Console授权不代表Python或探针已获授权。2026-11-20用户曾允许Console与TourBox BLE Probe，这是历史记录；命名app探针能够显示明确权限目标。2026-11-24清理已删除探针app，需从源码重新构建；重新构建/签名可能触发新弹窗。获得本次扫描许可后，只查看广播：

```sh
zsh tools/build_macos_ble_probe.zsh
open -W -n tmp/TourBoxBLEProbe.app --args --scan-only --output "$PWD/tmp/tourbox-scan.log"
```

完整探针会连接并验证服务、电量、订阅与两次连接，可能占用Console链路；扫描版不主动连接。Console运行日志可能包含与蓝牙无关的认证信息，只按BLE事件窄范围提取，不整份发布。

2026-11-20的开发板读取与烧录授权仅为历史联调记录；以后必须依据本次会话的用户请求与实际工具权限操作，不能自动继承烧录、复位、清绑定或连接许可。2026-11-24清理时未初始化Git，后续应实际检查状态。原始证据以本地`captures/`归档为准，实现以当前源码及本机保留的`dist/`为准；发布制品不随默认Git克隆提供，不依赖原项目或临时目录的中间文件。清理后的重建、Git准备及新会话入口见[开发接续说明](docs/development.md)。

## 7. 验证结果、故障归因与后续接续

### 7.1 已验证结果

| 项目 | 结果/范围 | 证据 |
| --- | --- | --- |
| 硬件/启动 | ESP32-S3 rev0.2、16MB/8MB、RGB48修正 | [RGB日志](docs/logs/rgb-gpio48.log) |
| 1.0.2构建/烧录 | 成功，三镜像校验通过，应用622,384字节 | [烧录](docs/logs/flash-v102.log)、[manifest](dist/manifest.json) |
| 核心测试 | ASan/UBSan；5万次FIFO操作、MTU、溢出、会话、95种B5切分、30ms片间隔、未知布局及组合顺序 | [测试输出](docs/logs/core-v102-tests.log) |
| BLE基础测试 | 服务与属性、电量100%、两次订阅/重连、MTU247及15ms间隔；属于早期1.0.0验证 | [探针日志](docs/logs/macos-ble.log) |
| 四方向旋转 | 上56、下36、顺时针26、逆时针83，共201步 | [输入统计](docs/logs/integration-analysis.json) |
| 点击事件 | 旋钮37/B7四对、滚轮0A/8A三对，共14个事件；其他按键得到用户反馈 | [集成日志](docs/logs/integration-v102-device.log) |
| 收发完整性 | USB_RX=BLE_TX=305、BLE_RX=USB_TX=134，含握手，drops/rejects/notify_errors均0，B5规则1次 | [集成日志](docs/logs/integration-v102-device.log) |
| Console重新启动 | 启动约6.15秒连接/加密，8.31秒配置；超过120秒无加密超时13；用户确认连接及输入正常 | [自动连接](docs/logs/reconnect-fresh-device.log) |
| 板复位、Console保持开启 | 9.60秒连接、10.05秒加密、10.06秒订阅、12秒配置，无手动操作 | [最终复位](docs/logs/reconnect-cycle-device.log) |
| 最后用户反馈 | 重启Console后自动连接成功 | [最后设备日志](docs/logs/reconnect-ui-device.log) |
| 手动发现 | 两种广播和原生筛选已验证；最终添加页面显示未确认 | [筛选日志](docs/logs/console-directed-filter.log) |

重连时间来自本机启动日志观测，不能沿用原设计“3秒内恢复”作为已达标结论。单个输入包或15ms连接间隔也不等于测得端到端延迟。更多原始验证及版本范围见 [docs/validation.md](docs/validation.md)。

### 7.2 已解决问题与排查顺序

| 现象 | 本次原因/处理 |
| --- | --- |
| RGB不亮 | 引脚38错误，当前板实际48；不影响USB/BLE逻辑 |
| 红色慢闪 | 正在等待USB，先查供电、OTG及枚举日志 |
| 蓝色呼吸、Console搜不到 | 查TBE Lite完整名、16字节厂商布局与macOS应用权限；标准扫描发现不等于Console筛选通过 |
| 服务发现停滞 | 不在每次订阅发Service Changed；只按NVS服务表变化更新 |
| 按键可用、旋转无事件 | 查94字节B5零参数适配，解锁不够；无需旋转停止码 |
| 配对后不自动连接 | 查peer_pc、真实Mac地址、Console白名单、主机目标广播及Console扫描模式 |
| 主机目标后手动列表消失 | 确认5秒目标/2秒通用轮换；不能只发主机目标 |
| 离线筛选通过但实际没连接 | 正常退出再启动Console，结合真实设备加密/订阅/握手日志确认 |
| 重连30秒后超时13 | 避免已绑定链路重复主动SMP |
| UART日志窗口没有用户操作 | 改长采集窗口，先开UART，确认采集进程与文件对应 |

### 7.3 待验证与修改约束

后续优先复核最终手动添加页面、USB热插拔及长期高频旋转。还需覆盖连续超时故障注入、BOOT实际清绑定、完整多键组合/全部Console配置、其他硬件批次、Windows和其他Mac版本。电流、压降、端到端延迟和十分钟压力均未形成测量结论。

续接时保留：

- 不伪造26字节解锁或12字节扩展握手应答，不猜字段、不按包长丢回复。
- 不添加旋转停止码；保持按键与步进事件顺序。
- B5参数适配限定CAFE:4001及完整已核实布局，其他设备/帧保持透传。
- 保持稳定BLE身份、NVS配对和真实公共主机地址；不硬编码任意Mac。
- USB超时不盲目重发；断线清队列和会话，避免旧事件泄漏。
- 不为“修复发现”无条件擦NVS、更换地址或修改Console。
- 保持本地captures原始采集内容不变，引用本地证据和docs/logs联调日志；重要知识不能只留在tmp或原目录。

新增对话可从README当前基线、本节以及对应模块开始，无需重新实现或重复已通过的基础功能测试。遇到新失败应先明确使用的是当前1.0.2固件、哪种扫描模式和哪个验证阶段，再按证据定位。

## 8. 后续扩展

电池供电与真实BAS测量、多主机选择、运行时配置及更多型号适配仍为扩展方向。当前BAS固定100%、自动广播目标来自单个最近保存的公共主机；虽然NimBLE最多保存4个绑定，尚未实现多主机目标轮换或切换UI。扩展不能以现有单Mac验证替代新增平台和硬件测试。
