#!/usr/bin/env python3
"""Read USB descriptors or record a manually initiated TourBox serial session.

Python standard library only. Descriptor mode additionally uses an existing
libusb shared library; it does not open/claim/reset the USB device.
Capture mode changes tty line settings and sends the known unlock command
only after the operator types 'unlock'. The optional 'config-zero' command
manually sends tuxbox's 94-byte zero-parameter configuration for comparison.
"""

import argparse
import array
import ctypes as C
import ctypes.util
import datetime as dt
import fcntl
import json
import os
from pathlib import Path
import platform
import select
import subprocess
import sys
import termios
import time
import tty

UNLOCK = bytes.fromhex("5500078894001afe")
# Protocol template from AndyCappDev/tuxbox v3.8.1, haptic.py.
# Commit 2f4c5adce36dee6ebc7d080ebfbd82d88d6c3f1f; MIT, see third_party license.
CONFIG_ZERO = bytes.fromhex(
    "b5005d04"
    "00050006000700080009000b000c000d"
    "000e000f0026002700280029003b003c003d003e"
    "003f004000410042004300440045004600470048"
    "0049004a004b004c004d004e004f005000510052"
    "0053005400a800a900aa00ab00fe"
)
ROOT = Path(__file__).resolve().parents[1]
U8, U16 = C.c_uint8, C.c_uint16
PBYTE = C.POINTER(U8)


class DeviceDescriptor(C.Structure):
    _fields_ = [("bLength", U8), ("bDescriptorType", U8), ("bcdUSB", U16),
                ("bDeviceClass", U8), ("bDeviceSubClass", U8),
                ("bDeviceProtocol", U8), ("bMaxPacketSize0", U8),
                ("idVendor", U16), ("idProduct", U16), ("bcdDevice", U16),
                ("iManufacturer", U8), ("iProduct", U8),
                ("iSerialNumber", U8), ("bNumConfigurations", U8)]


class EndpointDescriptor(C.Structure):
    _fields_ = [("bLength", U8), ("bDescriptorType", U8),
                ("bEndpointAddress", U8), ("bmAttributes", U8),
                ("wMaxPacketSize", U16), ("bInterval", U8),
                ("bRefresh", U8), ("bSynchAddress", U8),
                ("extra", PBYTE), ("extra_length", C.c_int)]


class InterfaceDescriptor(C.Structure):
    _fields_ = [(name, U8) for name in (
        "bLength", "bDescriptorType", "bInterfaceNumber", "bAlternateSetting",
        "bNumEndpoints", "bInterfaceClass", "bInterfaceSubClass",
        "bInterfaceProtocol", "iInterface")]
    _fields_ += [("endpoint", C.POINTER(EndpointDescriptor)),
                 ("extra", PBYTE), ("extra_length", C.c_int)]


class Interface(C.Structure):
    _fields_ = [("altsetting", C.POINTER(InterfaceDescriptor)),
                ("num_altsetting", C.c_int)]


class ConfigDescriptor(C.Structure):
    _fields_ = [("bLength", U8), ("bDescriptorType", U8), ("wTotalLength", U16),
                ("bNumInterfaces", U8), ("bConfigurationValue", U8),
                ("iConfiguration", U8), ("bmAttributes", U8), ("MaxPower", U8),
                ("interface", C.POINTER(Interface)),
                ("extra", PBYTE), ("extra_length", C.c_int)]


def scalars(obj):
    return {name: int(getattr(obj, name)) for name, typ in obj._fields_
            if typ in (U8, U16)}


def extras(obj):
    raw = C.string_at(obj.extra, obj.extra_length) if obj.extra_length > 0 else b""
    descriptors = []
    offset = 0
    while offset < len(raw):
        size = raw[offset]
        if size < 2 or offset + size > len(raw):
            descriptors.append({"malformed_tail_hex": raw[offset:].hex(" ")})
            break
        data = raw[offset:offset + size]
        item = {"type": data[1], "hex": data.hex(" ")}
        if data[1] == 0x24 and size >= 3:
            item["cdc_subtype"] = data[2]
            if data[2] == 0x00 and size >= 5:
                item["cdc_version"] = int.from_bytes(data[3:5], "little")
            elif data[2] == 0x01 and size >= 5:
                item.update(capabilities=data[3], data_interface=data[4])
            elif data[2] == 0x02 and size >= 4:
                item["acm_capabilities"] = data[3]
            elif data[2] == 0x06 and size >= 5:
                item.update(master_interface=data[3], slave_interfaces=list(data[4:]))
        descriptors.append(item)
        offset += size
    return {"extra_hex": raw.hex(" "), "extra_descriptors": descriptors}


def output_path(prefix):
    folder = ROOT / "captures"
    folder.mkdir(exist_ok=True)
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    return folder / f"{prefix}-{stamp}"


def descriptors(args):
    library = ctypes.util.find_library("usb-1.0")
    if not library:
        raise RuntimeError("未找到 libusb；本机检查时已有 /opt/homebrew/lib/libusb-1.0.dylib。")
    lib = C.CDLL(library)
    prototypes = {
        "libusb_init": ([C.POINTER(C.c_void_p)], C.c_int),
        "libusb_exit": ([C.c_void_p], None),
        "libusb_get_device_list": ([C.c_void_p, C.POINTER(C.POINTER(C.c_void_p))], C.c_ssize_t),
        "libusb_free_device_list": ([C.POINTER(C.c_void_p), C.c_int], None),
        "libusb_get_device_descriptor": ([C.c_void_p, C.POINTER(DeviceDescriptor)], C.c_int),
        "libusb_get_config_descriptor": ([C.c_void_p, U8, C.POINTER(C.POINTER(ConfigDescriptor))], C.c_int),
        "libusb_free_config_descriptor": ([C.POINTER(ConfigDescriptor)], None),
        "libusb_get_device_speed": ([C.c_void_p], C.c_int),
    }
    for name, (argtypes, restype) in prototypes.items():
        function = getattr(lib, name)
        function.argtypes, function.restype = argtypes, restype
    context, devices = C.c_void_p(), C.POINTER(C.c_void_p)()
    code = lib.libusb_init(C.byref(context))
    if code < 0:
        raise RuntimeError(f"libusb_init failed: {code}")
    report = {"collected_at": dt.datetime.now().astimezone().isoformat(),
              "platform": platform.platform(), "library": library,
              "method": "libusb cached device/config descriptors; no device open or claim",
              "devices": []}
    try:
        count = lib.libusb_get_device_list(context, C.byref(devices))
        if count < 0:
            raise RuntimeError(f"libusb_get_device_list failed: {count}")
        for index in range(count):
            device = devices[index]
            desc = DeviceDescriptor()
            if lib.libusb_get_device_descriptor(device, C.byref(desc)) < 0:
                continue
            if (desc.idVendor, desc.idProduct) != (args.vid, args.pid):
                continue
            info = {"device_descriptor": scalars(desc),
                    "usb_speed_code": lib.libusb_get_device_speed(device),
                    "configurations": []}
            for config_index in range(desc.bNumConfigurations):
                config = C.POINTER(ConfigDescriptor)()
                code = lib.libusb_get_config_descriptor(device, config_index, C.byref(config))
                if code < 0:
                    raise RuntimeError(f"Cannot read configuration {config_index}: {code}")
                try:
                    value = config.contents
                    row = scalars(value)
                    row.update(extras(value))
                    row["interfaces"] = []
                    for iface_index in range(value.bNumInterfaces):
                        iface = value.interface[iface_index]
                        for alt_index in range(iface.num_altsetting):
                            setting = iface.altsetting[alt_index]
                            alt = scalars(setting)
                            alt.update(extras(setting))
                            alt["endpoints"] = []
                            for ep_index in range(setting.bNumEndpoints):
                                endpoint = setting.endpoint[ep_index]
                                ep = scalars(endpoint)
                                ep.update(extras(endpoint))
                                ep["direction"] = "IN" if endpoint.bEndpointAddress & 0x80 else "OUT"
                                ep["transfer_type"] = ["CONTROL", "ISOCHRONOUS", "BULK", "INTERRUPT"][endpoint.bmAttributes & 3]
                                alt["endpoints"].append(ep)
                            row["interfaces"].append(alt)
                    info["configurations"].append(row)
                finally:
                    lib.libusb_free_config_descriptor(config)
            report["devices"].append(info)
    finally:
        if devices:
            lib.libusb_free_device_list(devices, 1)
        lib.libusb_exit(context)
    if not report["devices"]:
        raise RuntimeError(f"没有找到 USB {args.vid:04X}:{args.pid:04X}，请检查设备连接。")
    path = output_path("usb-descriptors").with_suffix(".json")
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    print(f"\n已保存：{path}")


def log_record(file, started, kind, **fields):
    row = {"t_us": (time.monotonic_ns() - started) // 1000, "kind": kind, **fields}
    file.write(json.dumps(row, ensure_ascii=False) + "\n")
    file.flush()


def modem_bits(fd, request, bits=0):
    value = array.array("i", [bits])
    fcntl.ioctl(fd, request, value, True)
    return value[0]


def write_logged(fd, payload, file, started, command):
    offset = 0
    deadline = time.monotonic() + 1.0
    while offset < len(payload):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(f"发送 {command} 超时，不自动重发。")
        _, writable, _ = select.select([], [fd], [], remaining)
        if not writable:
            raise TimeoutError(f"发送 {command} 超时，不自动重发。")
        try:
            size = os.write(fd, payload[offset:])
        except BlockingIOError:
            continue
        if size == 0:
            raise RuntimeError("串口写入返回零，不自动重发。")
        log_record(file, started, "tx", command=command, length=size,
                   hex=payload[offset:offset + size].hex(" "))
        offset += size


def capture(args):
    if not sys.stdin.isatty():
        raise RuntimeError("请在交互终端运行 capture；它需要你输入 unlock 和操作标记。")
    if not os.path.exists(args.port):
        raise RuntimeError(f"串口不存在：{args.port}")
    result = subprocess.run(["/usr/sbin/lsof", "-nP", args.port,
                             args.port.replace("/dev/cu.", "/dev/tty.", 1)],
                            capture_output=True, text=True)
    if result.returncode == 0 and result.stdout.strip():
        raise RuntimeError("串口已被占用，请彻底退出官方 Console 和其他串口程序：\n" + result.stdout)
    if result.returncode not in (0, 1):
        raise RuntimeError("无法检查串口占用：" + result.stderr.strip())

    path = output_path("serial-session").with_suffix(".jsonl")
    fd, original, original_bits, exclusive = None, None, None, False
    started = time.monotonic_ns()
    sent_unlock = False
    sent_config = False
    unlock_time = None
    unlock_rx = bytearray()
    print(f"日志：{path}\n串口：{args.port}；115200、8N1，尝试设置 DTR/RTS=true。")
    print("打开串口本身会设置控制线。先不操作设备，输入 unlock 后才发送解锁指令。")
    print("命令：unlock；config-zero（手动测试94字节默认配置）；mark 操作名称；quit。")
    print("配置消息来自 tuxbox，是否启用 Lite 旋转上报需本轮验证。Ctrl+C 可保存并退出。")
    with path.open("x") as file:
        log_record(file, started, "metadata", port=args.port, baud=115200,
                   framing="8N1", platform=platform.platform(),
                   captured_at=dt.datetime.now().astimezone().isoformat(),
                   note="tty read chunks are not USB packet boundaries; config-zero is manual only",
                   config_source="tuxbox 2f4c5adce36dee6ebc7d080ebfbd82d88d6c3f1f haptic.py")
        try:
            fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            fcntl.ioctl(fd, termios.TIOCEXCL)
            exclusive = True
            original = termios.tcgetattr(fd)
            tty.setraw(fd, termios.TCSANOW)
            settings = termios.tcgetattr(fd)
            settings[2] &= ~(termios.CSIZE | termios.PARENB | termios.CSTOPB |
                             getattr(termios, "CRTSCTS", 0))
            settings[2] |= termios.CS8 | termios.CLOCAL | termios.CREAD
            settings[4] = settings[5] = termios.B115200
            settings[6][termios.VMIN] = 0
            settings[6][termios.VTIME] = 0
            termios.tcsetattr(fd, termios.TCSANOW, settings)
            # Keep existing incoming bytes: they may contain useful open-time replies.
            try:
                original_bits = modem_bits(fd, termios.TIOCMGET)
                modem_bits(fd, termios.TIOCMBIS, termios.TIOCM_DTR | termios.TIOCM_RTS)
                log_record(file, started, "control_lines", dtr_requested=True,
                           rts_requested=True, ioctl_ok=True)
            except OSError as error:
                log_record(file, started, "control_lines", ioctl_ok=False, error=str(error))
                print(f"控制线请求未成功：{error}；保留日志，不把它当作解锁失败结论。")
            while True:
                ready, _, _ = select.select([fd, sys.stdin], [], [], 0.2)
                if fd in ready:
                    try:
                        data = os.read(fd, 4096)
                    except BlockingIOError:
                        data = None
                    if data == b"":
                        log_record(file, started, "disconnect")
                        print("串口已断开，结束记录。")
                        break
                    if data:
                        log_record(file, started, "rx", length=len(data), hex=data.hex(" "))
                        if sent_unlock and not sent_config and len(unlock_rx) < 256:
                            unlock_rx.extend(data[:256 - len(unlock_rx)])
                        print(f"RX {len(data):3d} bytes: {data.hex(' ').upper()}")
                if sys.stdin in ready:
                    line = sys.stdin.readline()
                    if not line:
                        break
                    command = line.strip()
                    if command == "quit":
                        break
                    if command == "unlock":
                        if sent_unlock:
                            print("本会话已经发送解锁；如需重复测试，请退出并重新连接设备。")
                            continue
                        sent_unlock = True
                        unlock_time = time.monotonic()
                        write_logged(fd, UNLOCK, file, started, "unlock")
                        print("已发送 8 字节解锁。先保持不操作至少 2 秒，观察 RX。")
                    elif command == "config-zero":
                        if sent_config:
                            print("本会话已经发送配置；重复对照测试请新建会话。")
                            continue
                        # Guard for this measured Lite session, not a universal auth parser.
                        if not sent_unlock or len(unlock_rx) < 26 or unlock_rx[0] != 0x07:
                            print("请先 unlock 并等待本次 Lite 的26字节、07开头回复，再测试配置。")
                            continue
                        if time.monotonic() - unlock_time < 1.0:
                            print("解锁后请至少等待1秒，再输入 config-zero。")
                            continue
                        sent_config = True
                        log_record(file, started, "configuration_test", template="tuxbox-zero",
                                   length=len(CONFIG_ZERO), hex=CONFIG_ZERO.hex(" "))
                        write_logged(fd, CONFIG_ZERO, file, started, "config-zero")
                        print("已发送94字节配置。先不操作至少2秒记录回复，再逐项测试旋转。")
                    elif command.startswith("mark ") and command[5:].strip():
                        label = command[5:].strip()
                        log_record(file, started, "mark", label=label)
                        print(f"标记：{label}；现在可以进行该项操作。")
                    elif command:
                        print("可用命令：unlock、config-zero、mark 操作名称、quit。")
        except KeyboardInterrupt:
            log_record(file, started, "stopped", reason="Ctrl+C")
        except Exception as error:
            log_record(file, started, "error", error=str(error))
            raise
        finally:
            if fd is not None:
                if original_bits is not None:
                    try:
                        modem_bits(fd, termios.TIOCMSET, original_bits)
                    except OSError:
                        pass
                if original is not None:
                    try:
                        termios.tcsetattr(fd, termios.TCSANOW, original)
                    except (OSError, termios.error):
                        pass
                if exclusive:
                    try:
                        fcntl.ioctl(fd, termios.TIOCNXCL)
                    except OSError:
                        pass
                os.close(fd)
            log_record(file, started, "closed")
    print(f"记录结束，已保存：{path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    desc = commands.add_parser("descriptors", help="只读采集 USB 配置、接口与端点")
    desc.add_argument("--vid", type=lambda value: int(value, 0), default=0xCAFE)
    desc.add_argument("--pid", type=lambda value: int(value, 0), default=0x4001)
    serial = commands.add_parser("capture", help="交互记录串口，手动触发一次解锁")
    serial.add_argument("--port", default="/dev/cu.usbmodemTourBox1")
    args = parser.parse_args()
    try:
        (descriptors if args.command == "descriptors" else capture)(args)
    except Exception as error:
        print(f"错误：{error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
