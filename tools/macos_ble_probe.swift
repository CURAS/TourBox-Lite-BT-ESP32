import Cocoa
import CoreBluetooth

// A named app bundle gives macOS a clear Bluetooth privacy permission target.
final class Probe: NSObject, NSApplicationDelegate, CBCentralManagerDelegate, CBPeripheralDelegate {
    private var manager: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var tx: CBCharacteristic?
    private var rx: CBCharacteristic?
    private var battery: CBCharacteristic?
    private var pendingServices = Set<CBUUID>()
    private var cycle = 0
    private var disconnectExpected = false
    private var finished = false
    private var scanOnly = false
    private var logFile: FileHandle?
    private var window: NSWindow!
    private var label: NSTextField!
    private let serviceID = CBUUID(string: "FFF0")

    func applicationDidFinishLaunching(_ notification: Notification) {
        let args = CommandLine.arguments
        scanOnly = args.contains("--scan-only")
        if let i = args.firstIndex(of: "--output"), i + 1 < args.count {
            let path = args[i + 1]
            FileManager.default.createFile(atPath: path, contents: nil)
            logFile = FileHandle(forWritingAtPath: path)
        }
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 460, height: 150),
                          styleMask: [.titled, .closable], backing: .buffered, defer: false)
        window.title = "TourBox BLE Probe"
        label = NSTextField(wrappingLabelWithString: "正在请求蓝牙权限，请在 macOS 弹窗中允许 TourBox BLE Probe。")
        label.frame = NSRect(x: 24, y: 28, width: 412, height: 94)
        window.contentView?.addSubview(label)
        window.center()
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        log("START: TourBox BLE Probe")
        manager = CBCentralManager(delegate: self, queue: .main,
                                   options: [CBCentralManagerOptionShowPowerAlertKey: true])
        log("INITIAL BLUETOOTH state=\(manager.state.rawValue) authorization=\(CBCentralManager.authorization.rawValue)")
        DispatchQueue.main.asyncAfter(deadline: .now() + 2) { [weak self] in
            guard let self = self, !self.finished else { return }
            self.log("POLL BLUETOOTH state=\(self.manager.state.rawValue) authorization=\(CBCentralManager.authorization.rawValue)")
            if self.manager.state == .poweredOn && !self.manager.isScanning && self.peripheral == nil {
                self.centralManagerDidUpdateState(self.manager)
            }
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 180) { [weak self] in
            self?.finish(false, "Timed out waiting for Bluetooth permission or device")
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    private func log(_ message: String) {
        logFile?.write(Data((message + "\n").utf8))
        print(message)
    }

    private func finish(_ success: Bool, _ message: String) {
        guard !finished else { return }
        finished = true
        log((success ? "PASS: " : "FAIL: ") + message)
        label.stringValue = success
            ? (scanOnly ? "验证通过：已收到设备广播，详细名称和厂商数据已写入日志。" : "验证通过：广播、GATT 属性、100% 电量、通知订阅及两次连接正常。")
            : "验证失败：\(message)"
        manager?.stopScan()
        if let p = peripheral, p.state != .disconnected { manager?.cancelPeripheralConnection(p) }
        DispatchQueue.main.asyncAfter(deadline: .now() + 2) { NSApp.terminate(nil) }
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        log("BLUETOOTH state=\(central.state.rawValue) authorization=\(CBCentralManager.authorization.rawValue)")
        switch central.state {
        case .poweredOn:
            label.stringValue = "已获得蓝牙权限，正在扫描 TourBox Lite 并验证服务。"
            central.scanForPeripherals(withServices: [serviceID], options: nil)
        case .unauthorized:
            // A permission sheet can be pending; do not exit before the user answers.
            label.stringValue = "请允许 TourBox BLE Probe 使用蓝牙。若已拒绝，请在系统设置 → 隐私与安全性 → 蓝牙中开启。"
            if CBCentralManager.authorization == .denied || CBCentralManager.authorization == .restricted {
                finish(false, "Bluetooth permission denied for TourBox BLE Probe")
            }
        case .poweredOff:
            label.stringValue = "请打开电脑蓝牙，程序会自动继续。"
        case .unsupported:
            finish(false, "Bluetooth unsupported")
        default:
            break
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover device: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? device.name ?? ""
        guard name.contains("TourBox") || name.hasPrefix("TBE") else { return }
        guard peripheral == nil else { return }
        let uuids = advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID] ?? []
        guard uuids.contains(serviceID) else { return }
        log("FOUND \(name) id=\(device.identifier) services=\(uuids) RSSI=\(RSSI)")
        if let manufacturer = advertisementData[CBAdvertisementDataManufacturerDataKey] as? Data {
            log("MANUFACTURER " + manufacturer.map { String(format: "%02x", $0) }.joined(separator: " "))
        } else { log("MANUFACTURER absent") }
        central.stopScan()
        if scanOnly { finish(true, "passive advertisement inspection complete"); return }
        peripheral = device
        device.delegate = self
        central.connect(device)
    }

    func centralManager(_ central: CBCentralManager, didConnect device: CBPeripheral) {
        cycle += 1
        tx = nil; rx = nil; battery = nil
        pendingServices.removeAll()
        disconnectExpected = false
        log("CONNECTED cycle=\(cycle) max_write=\(device.maximumWriteValueLength(for: .withoutResponse))")
        device.discoverServices(nil)
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect device: CBPeripheral, error: Error?) {
        finish(false, "Connection failed: \(String(describing: error))")
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral device: CBPeripheral, error: Error?) {
        guard !finished else { return }
        log("DISCONNECTED cycle=\(cycle) error=\(String(describing: error))")
        guard disconnectExpected else { finish(false, "Unexpected disconnect"); return }
        if cycle < 2 {
            DispatchQueue.main.asyncAfter(deadline: .now() + 1) { central.connect(device) }
        } else { finish(true, "advertisement, GATT properties, battery, subscription, two connection cycles") }
    }

    func peripheral(_ device: CBPeripheral, didDiscoverServices error: Error?) {
        guard error == nil, let services = device.services, services.contains(where: { $0.uuid == serviceID }) else {
            finish(false, "TourBox service discovery failed: \(String(describing: error))"); return
        }
        pendingServices = Set(services.map { $0.uuid })
        for service in services {
            log("SERVICE \(service.uuid)")
            device.discoverCharacteristics(nil, for: service)
        }
    }

    func peripheral(_ device: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        guard error == nil else { finish(false, "Characteristic discovery: \(String(describing: error))"); return }
        for characteristic in service.characteristics ?? [] {
            log("CHAR \(characteristic.uuid) properties=\(characteristic.properties.rawValue)")
            switch characteristic.uuid {
            case CBUUID(string: "FFF1"): tx = characteristic
            case CBUUID(string: "FFF2"): rx = characteristic
            case CBUUID(string: "2A19"): battery = characteristic
            default: break
            }
        }
        pendingServices.remove(service.uuid)
        guard pendingServices.isEmpty else { return }
        guard let tx = tx, tx.properties.contains(.notify),
              let rx = rx, rx.properties.contains(.write), rx.properties.contains(.writeWithoutResponse) else {
            finish(false, "Incorrect TourBox characteristic properties"); return
        }
        if let battery = battery { device.readValue(for: battery) }
        else { finish(false, "Missing battery characteristic"); return }
    }

    func peripheral(_ device: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard error == nil else { finish(false, "Read/notify error: \(String(describing: error))"); return }
        let data = characteristic.value ?? Data()
        if characteristic.uuid == CBUUID(string: "2A19") {
            guard data == Data([100]), let tx = tx else { finish(false, "Battery is not 100 percent"); return }
            log("BATTERY 100%")
            device.setNotifyValue(true, for: tx)
        } else {
            log("NOTIFY " + data.map { String(format: "%02x", $0) }.joined(separator: " "))
        }
    }

    func peripheral(_ device: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        guard error == nil else { finish(false, "CCCD subscribe: \(String(describing: error))"); return }
        log("SUBSCRIPTION cycle=\(cycle) enabled=\(characteristic.isNotifying)")
        if characteristic.isNotifying {
            DispatchQueue.main.asyncAfter(deadline: .now() + 2) { device.setNotifyValue(false, for: characteristic) }
        } else {
            disconnectExpected = true
            manager.cancelPeripheralConnection(device)
        }
    }
}

let app = NSApplication.shared
let probe = Probe()
app.setActivationPolicy(.regular)
app.delegate = probe
app.run()
