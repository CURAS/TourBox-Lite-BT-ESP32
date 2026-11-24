# TourBox Lite USB → BLE 固件

用于 ESP32-S3-DevKitC-1 N16R8，使用 ESP-IDF **6.1.1**。开发板的原生 USB 口连接 TourBox，UART 口用于烧录和日志。当前实现与设计依据见 [tourbox_lite_esp32_s3.md](tourbox_lite_esp32_s3.md)。

已实现 USB CDC-ACM 双向桥接、BLE GATT Server、MTU 分包、加密配对持久化、USB/BLE 断线恢复、RGB 状态灯、BOOT 按键及协议适配规则。固件 **1.0.2** 广播名称为 `TBE Lite`；用户已确认官方 TourBox Console 配对成功，按键、滚轮上下及旋钮顺逆时针都有响应。已核实的设备配置适配见下文。

## 当前基线与后续接续

本节更新于 **2026-11-24**，供后续新对话恢复上下文。当前源码版本为1.0.2；2026-11-20已烧录固件及本机保留的`dist/`也为1.0.2。该次联调用户最后确认：**重启TourBox Console后自动连接成功**。保持Console开启时，UART复位开发板也已实测自动重连，约9.6秒连接、10.1秒恢复加密和订阅、12秒完成旋转配置；这些是历史观测时间，不是跨平台时限保证。2026-11-24完整性检查、全新目录重编和主机测试通过，随后按用户要求清理生成物；本轮未烧录或重新验证实机，摘要见[验证记录](docs/validation.md)。

| 项目 | 当前事实 |
| --- | --- |
| 工作目录 | `/Users/ramda/Documents/test/TourBox-Lite-BT-ESP32-NEW`；2026-11-24清理时未初始化Git，后续状态请实际检查 |
| 实测开发板 | ESP32-S3 rev0.2，16MB Flash / 8MB Octal PSRAM；RGB GPIO48 |
| 历史实测UART | `/dev/cu.usbmodem5D4D0503581`；使用前重新查询，打开串口可能导致开发板复位 |
| 本机SDK | `/Users/ramda/.espressif/v6.1.1/esp-idf`；Python为`/Users/ramda/.espressif/tools/python/v6.1.1/venv/bin/python` |
| 实测上位机 | 当前Mac，`/Applications/TourBox Console.app`；Info版本5.12.1，运行日志版本5.13.0；Windows尚未验证 |
| BLE身份 | 芯片公共BT MAC `f8:5b:1b:97:c8:de`派生固定静态随机身份`f8:5b:1b:97:c8:84` |
| 已配对Mac | 公共身份`d0:11:e5:6e:d1:b3`；由加密绑定解析并存入NVS，未硬编码到固件 |
| 当前配置与制品 | `sdkconfig`及`dist/sdkconfig`开启Hex日志；Kconfig默认关闭；[制品与校验值](dist/manifest.json) |

后续先读本README、设计文档第7节及 [验证记录](docs/validation.md)，再定位对应模块。不要仅凭开发早期“找不到设备”的反馈回退已验证的广播、配对与旋转配置。下一步优先复核最终手动添加页面、USB热插拔和长时间高频输入；完整组合键、其他Mac/Console版本、Windows及其他硬件批次仍待验证。

设备采集资料已完整归档在本项目：[设备数据采集说明](tourbox_lite_data_collection.md)、[采集索引](captures/README.md)、`captures/`中的原始描述符、四份串口日志、三份分析报告和实测档案。复测脚本为`tools/tourbox_probe.py`，相应许可证位于`third_party/tuxbox-LICENSE.txt`。复制完整性见 [归档清单](captures/archive-manifest.json)，后续无需访问原项目目录。

原始描述符、JSONL日志及profile保持内容不变，分析报告仅修订本地文档链接。它们描述2026-11-17直接USB采集时的状态；其中“BLE未测试”等历史字段不能覆盖2026-11-20的固件验证。本项目 [采集摘要](docs/lite-observed-profile.json)已通过相对路径关联本地原始数据，当前固件结论以本文及`docs/validation.md`为准。续接不依赖忽略目录`tmp/`中的中间文件，关键联调日志已归档到`docs/logs/`。

2026-11-20联调曾获得开发板操作及Console/TourBox BLE Probe的macOS蓝牙授权，这是历史记录，不代表新会话已获得烧录、复位、清绑定或连接设备的许可。清理后探针app已删除，需从保留的源码重新构建；新进程或重签名可能重新请求系统授权。只读扫描用`--scan-only`，避免测试工具占用Console的连接。

### 清理后的Git基线与新对话

2026-11-24已清理构建目录、托管组件、整个临时目录和Python字节码缓存。源码、测试、依赖锁、默认配置、原始采集及关键历史验证资料保留；本机配置、原发布制品和编辑器设置本地保留但由[忽略规则](.gitignore)排除。清理后首次固件构建可能需下载锁定组件。建立Git基线、恢复开发工具以及新对话建议入口见[开发接续说明](docs/development.md)。

## 接线与第一次使用

1. 电脑连接开发板 **UART** 口，用于供电、烧录和日志。
2. 将带 **外部 5V 供电** 的 OTG Y 线接到开发板 **原生 USB** 口，再接 TourBox Lite。有线 TourBox 必须获得 5V VBUS；原生 USB Host 模式不会使开发板自动向下游输出 5V。
3. 给 OTG 线接入 5V / ≥1A 电源，等待日志出现 `TourBox USB ready ... DTR=1 RTS=1`。
4. 在官方 TourBox Console 添加蓝牙设备 `TBE Lite`，允许 macOS 配对弹窗。Console 发出的解锁和扩展握手由固件原样转发，随后自动适配已核实 Lite 的旋转配置。
5. 配对后绑定密钥和真实主机地址保存在 NVS。Console 保持开启即可尝试自动连接；已配对广播以5秒主机目标、2秒通用发现轮换，保留手动添加入口。若 Console 停留在旧的配对扫描状态，可正常退出再启动。

独立 BLE 联调时先订阅 `FFF1`，向 `FFF2` 写入解锁指令 `55 00 07 88 94 00 1A FE`。解锁即可启用按键；旋转还需发送 [94字节配置](tests/fixtures/lite-config-zero.hex)。默认要求加密访问，主机须支持配对。

只插 UART 时，固件可正常广播，LED 红色慢闪，状态显示 `USB=waiting`。这时没有 TourBox 输入，向 RX 写入命令会失败。

## 构建与烧录

在已激活 ESP-IDF 6.1.1 的终端运行：

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem5D4D0503581 flash
idf.py -p /dev/cu.usbmodem5D4D0503581 monitor
```

串口名称随电脑变化。macOS 用 `ls /dev/cu.*` 查询；Windows 通常是 `COMx`；Linux 通常是 `/dev/ttyACM*` 或 `/dev/ttyUSB*`。监视器按 `Ctrl+]` 退出。

本机可使用 `zsh tools/idf.sh build` 和 `zsh tools/idf.sh -p PORT flash`；该包装脚本指向本机现有 SDK 安装。其他电脑使用标准 `idf.py` 即可。

`dependencies.lock` 锁定 CDC-ACM 2.4.1、USB Host 1.6.1、LED Strip 3.1.1。ESP-IDF 6.x 的 USB Host 是独立托管组件，由 CDC 驱动依赖自动引入。

分区为 NVS `0x9000` / 64KiB、PHY `0x19000` / 4KiB、应用 `0x20000` / 3MiB。烧录包含引导程序、分区表和应用三个文件。只写应用文件不能完成首次烧录。默认烧录不会擦除 NVS 绑定。

## 配置

通过 `idf.py menuconfig` → **TourBox USB to BLE bridge** 修改：

| 配置 | 默认值 / 行为 |
| --- | --- |
| 首选 VID/PID | `CAFE:4001`，来自当前实物采集 |
| 通用 CDC 匹配 | 开启；未知设备必须有标准 CDC-ACM 接口 |
| CDC 接口号 | `0`，选择通信接口而非数据接口 |
| 串口 | `115200 8N1`，始终置位 DTR/RTS |
| USB 自动解锁 | 关闭；可用于独立 USB 联调 |
| Lite旋转配置适配 | 开启；仅对`CAFE:4001`的已核实94字节B5布局生效 |
| BLE 名称 | `TBE Lite`；Console兼容广播启用时上限8个UTF-8字节，关闭时26字节 |
| Console广播兼容 | 开启；主广播包含完整名称和16字节厂商标识 |
| 配对主机广播 | 开启；配对后轮换主机目标与通用发现，未配对时持续通用发现 |
| BLE地址 | 芯片MAC派生的固定静态随机地址，重启后保持一致 |
| Service / TX / RX UUID | `FFF0` / `FFF1` / `FFF2` 的 Bluetooth-base UUID |
| 加密访问要求 | 开启；Just Works / Secure Connections，绑定密钥保存在NVS |
| DIS / Battery Service | 开启；电量固定为 100%，无真实电池测量 |
| LED | 默认 GPIO48（当前开发板实测）；GPIO38 板型可在菜单切换，也可禁用 |
| Hex 日志 / 自定义协议规则 | Kconfig默认关闭；当前烧录/发布配置开启Hex日志，自定义规则表为空；Lite内置适配独立开启 |

芯片默认 240MHz、16MB QIO Flash、8MB Octal PSRAM / 80MHz。日志只走 UART0 GPIO43/44，主、次 USB 控制台均禁用。GPIO35/36/37 不用于外部功能。

## 已核实的协议

来源为 [AndyCappDev/tuxbox v3.8.1](https://github.com/AndyCappDev/tuxbox/tree/v3.8.1)，只提取协议事实，未复制驱动实现。

| 项目 | 值 / 依据 |
| --- | --- |
| BLE 扫描名称条件 | `TourBox` 前缀；源码没有确认 Lite 专属广播身份 |
| 主服务 | `0000fff0-0000-1000-8000-00805f9b34fb` |
| 上行 Notify | `0000fff1-0000-1000-8000-00805f9b34fb`，自动生成 CCCD |
| 下行 Write | `0000fff2-0000-1000-8000-00805f9b34fb`，同时支持 Write / Write Without Response |
| USB/BLE 解锁 | `55 00 07 88 94 00 1A FE` |
| USB 串口 | `115200` |
| 源码 VID/PID | `0483:5741`、`0483:5740` |
| 文档补充批次 | `C251:2005`、`2E3C:5740`，也纳入匹配 |

UUID 在 ESP-IDF 6.1.1 中可能压缩为等价的16位形式。主广播为Flags（3字节）+完整名称`TBE Lite`（10字节）+厂商数据（18字节），恰好31字节；扫描响应携带服务UUID。厂商数据内容为`TBE ` + 6字节设备BLE地址 + 6字节目标主机地址，均为显示字节序。零目标用于手动配对；加密绑定后保存的真实公共主机身份用于自动连接。两种模式使用相同的设备身份，已配对时以5秒/2秒轮换。该格式和筛选行为已核实于本机Console原生库。

Console使用`CBPeripheral.name`筛选设备，因此完整名称与厂商数据同时放入主广播。开发期间旧名称可能被macOS按设备身份缓存；当前使用由出厂蓝牙MAC确定性派生的静态随机地址（高位设为11，最低字节异或0x5a），既创建独立身份又保持重启后地址稳定。当前开发板地址为`f8:5b:1b:97:c8:84`。

GATT提供GAP、含Service Changed的GATT、可选DIS/BAS，以及TourBox服务。服务表指纹保存在NVS，只有UUID、权限或服务布局改变时才标记已绑定客户端的缓存失效，避免在macOS发现特征时反复发送Service Changed。

参考BLE驱动主要面向Elite；官方Console的发现条件另以本机应用的BLE库核实并补齐。固件不根据包长猜测并丢弃握手响应，所有未知上行字节保持原样。

当前实物的CDC控制接口0、数据接口1，VID/PID为`CAFE:4001`，详见 [采集档案](docs/lite-observed-profile.json)。解锁后按键已工作，但Console下发的94字节B5配置使用`08`参数，导致旋转没有事件。`main/lite_profile.c`仅匹配采集中已核实的完整帧布局，保留头部、所有控件ID和`FE`结尾，把偶数位置4至92的参数转换为采集验证过的`00`。其他型号、其他布局及解锁/扩展握手保持透传；不生成虚假应答或旋转停止码。

实测输入码：滚轮上`49`、下`09`、旋钮顺时针`44`、逆时针`04`。按键与旋转组合的原始字节顺序保持不变；四个方向和按键均已得到用户确认。

## 已解决的问题与保留约束

| 现象 | 原因与最终处理 |
| --- | --- |
| RGB完全不亮 | 当前板接GPIO48，最初GPIO38配置错误；修正后红色慢闪得到用户确认 |
| 扫描工具能发现，Console找不到 | `TourBox Lite`名称及缺少厂商数据不符合本机Console筛选；改为主广播完整名`TBE Lite`和`TBE `厂商数据 |
| macOS特征发现停滞 | 每次订阅发送Service Changed打断发现；改为NVS服务表指纹变化时才标记缓存失效 |
| 按键有响应，滚轮/旋钮没有 | 解锁不足以启用旋转，Console的B5参数`08`在当前Lite上无事件；按已核实94字节布局转换为`00` |
| 配对后需要手动连接 | 零主机目标只满足配对发现；自动模式还检查本机地址及Console设备白名单 |
| 改主机目标后手动列表隐藏 | 非零目标不生成手动配对发现事件；最终轮换5秒真实主机目标与2秒零目标 |
| 原生筛选允许，Console仍未连接 | 本次重新启动Console后恢复自动连接；结合固件日志确认，不能仅以离线筛选返回值认定真实连接已完成 |
| 重连后约30秒出现加密超时13 | 已绑定连接重复发起SMP；已保存主机时由macOS恢复加密，不重复主动请求 |

继续修改时保留这些约束：不伪造26字节解锁或12字节扩展握手应答；不凭响应长度猜型号并丢包；不添加旋转停止码；仅对`CAFE:4001`及完整已核实B5布局归零参数，其他字节与输入顺序保持透传；USB发送超时后不盲目重发；不随意更改稳定BLE身份或擦除NVS来掩盖连接问题。本次没有修改Console程序、偏好设置，也没有安装常驻主机辅助程序。

## 运行与恢复

USB Host 守护任务运行在 Core 1 / 优先级20，CDC 驱动19，USB 应用18，BLE 通知发送16；NimBLE 运行在 Core 0，UI 优先级5。USB 应用与桥接发送任务受5秒任务看门狗监控。

两个2048字节环形缓冲区受短临界区保护，支持 USB 上行和规则伪造应答的多个生产者。USB和GATT回调不等待发送。Hex日志经独立队列输出，调试日志拥塞不会阻塞协议回调。

通知按 `MTU - 3` 分片，默认20字节，MTU247时最多244字节。内存不足时间隔2ms重试3次；若仍失败或上行溢出，终止BLE连接，让主机建立新会话，避免遗漏释放事件后继续使用旧输入状态。BLE通知在NimBLE主机上下文提交，断线会话编号用于拒绝旧包。

下行缓冲区满时整段拒绝写入，避免转发半条命令；Write Without Response也会触发断线恢复。USB发送超时后不盲目重发，因为设备可能已收到部分字节。连续3次超时或总线错误会关闭CDC句柄并重置Host根端口以重新枚举；该操作不会切断OTG线提供的外部VBUS。

USB拔出时断开BLE，使Console重新连接后重新握手。BLE未连接/未订阅时丢弃上行输入；断线和取消订阅清理历史事件。订阅前收到的下行初始化命令可以转发，但当时的应答没有订阅者，测试时应先订阅再解锁。

| LED / 按键 | 行为 |
| --- | --- |
| 红色1Hz闪烁 | 等待USB设备 |
| 蓝色2Hz呼吸 | USB就绪，等待BLE连接 |
| 绿色常亮 | USB就绪，BLE已连接 |
| 青色短闪 | 数据活动 |
| 紫色快闪3次 | 成功清除绑定 |
| BOOT短按 | UART打印状态和统计 |
| BOOT长按3秒 | 断开连接，清除BLE绑定及配对主机地址，恢复通用广播；不擦除其他NVS数据 |

每30秒自动打印状态、内存和收发计数。当前开发板的RGB灯已实测使用GPIO48；最初使用GPIO38时灯不亮，改为GPIO48后恢复正常红色慢闪。DevKitC-1 v1.1的GPIO38板型可通过菜单切换。LED引脚选错不会影响USB或BLE通信。

## 添加实测协议规则

编辑 `main/protocol_rules.c`，用静态字节数组定义 `tb_rule_t` 表，并把 `tb_protocol_rules`、`tb_protocol_rule_count` 指向该表。启用 `CONFIG_TB_PROTO_RULES`：

- `TB_RULE_REPLY`：匹配下行查询，消费原查询，向BLE回复已核实的字节。
- `TB_RULE_REPLACE`：在原方向替换已核实的完整响应/命令。
- `TB_RULE_DROP`：消费明确不需要转发的命令。

匹配支持任意USB/GATT分包与粘包。每条规则最长128字节；匹配前缀互相重叠的规则会被拒绝。未匹配内容原样透传，未完成前缀一般20ms后释放。当前Lite配置分片实测间隔约30ms，启用其适配时超时至少250ms。仅匹配前缀进入等待，上行按键/旋转事件不因此延迟。不要仅按接收包长度识别型号响应或电量查询。

自定义规则表默认为空；已核实Lite的内置规则由`CONFIG_TB_LITE_ZERO_CONFIG`单独控制，在USB型号切换时清理旧会话并更新规则。测试中的实际B5夹具来自用户采集，其他人工测试字节不代表TourBox协议。

## 验证

2026-11-20已完成并归档的验证：

| 项目 | 结论与证据 |
| --- | --- |
| 构建与烧录 | 1.0.2构建通过，三份镜像烧录校验通过；应用622,384字节；[烧录日志](docs/logs/flash-v102.log) |
| 原生核心测试 | ASan/UBSan通过，含5万次FIFO操作、95个B5分片位置、30ms实际间隔、型号切换及事件顺序；[测试输出](docs/logs/core-v102-tests.log) |
| BLE基础功能 | GATT属性、电量100%、两轮订阅/取消/重连、MTU247与15ms连接间隔通过；[历史基础验证](docs/logs/macos-ble.log) |
| 当前Lite输入 | 四方向共201次旋转、旋钮/滚轮点击14个事件；USB_RX=BLE_TX=305，BLE_RX=USB_TX=134，错误计数为0；[实测统计](docs/logs/integration-analysis.json) |
| Console重启自动连接 | 用户再次确认自动连接成功，并已确认连接后的按键和旋转正常；[连接日志](docs/logs/reconnect-fresh-device.log) |
| 开发板复位自动重连 | Console保持运行，约10秒自动恢复加密/订阅，完整94字节配置再次下发，无超时13；[复位与轮换日志](docs/logs/reconnect-cycle-device.log) |
| 最终手动添加页面 | 已验证两种广播及原生筛选；最终页面显示尚未得到用户确认 |

基础验证日志来自早期1.0.0；当前旋转、加密与自动重连结论来自1.0.2日志，不能混用版本来宣称所有功能均完成验证。USB热插拔、故障注入恢复、BOOT清绑定、十分钟高频旋转及完整配置指令仍需实机覆盖。

原生测试不需要ESP-IDF，包含ASan/UBSan：

```sh
cmake -S tests -B tmp/host-tests
cmake --build tmp/host-tests
ctest --test-dir tmp/host-tests --output-on-failure
```

可选配置编译检查：

```sh
idf.py -B tmp/build-features -D SDKCONFIG=tmp/sdkconfig.features \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;tests/sdkconfig.features' build
```

串口抓日志：

```sh
python tools/serial_capture.py --port PORT --reset --duration 35 --output tmp/boot.log
```

BLE测试需要 `bleak`；串口脚本需要 `pyserial`。可在独立虚拟环境安装。BLE测试检查广播、GATT属性、电量、通知订阅和两次连接；接入TourBox后加 `--unlock --listen 30` 检查实机回包：

```sh
python tools/ble_probe.py
python tools/ble_probe.py --unlock --listen 30
```

macOS需给运行脚本的终端/应用蓝牙权限。最终硬件验证结果见 [docs/validation.md](docs/validation.md)。

如果Python进程无法正常弹出蓝牙授权，可使用有明确应用标识的macOS测试程序：

```sh
zsh tools/build_macos_ble_probe.zsh
open -W -n tmp/TourBoxBLEProbe.app --args --output "$PWD/tmp/ble-probe.log"
```

在弹窗中允许 **TourBox BLE Probe**，程序会验证广播、GATT属性、电量和两次订阅/重连，完成后自动退出。该程序需要Xcode或Command Line Tools。串口监视器若需要打开，应先打开再运行BLE测试，以免串口控制线导致开发板复位。
