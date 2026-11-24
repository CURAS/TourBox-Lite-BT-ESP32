#!/usr/bin/env python3
"""BLE smoke test; requires pip install bleak. --unlock writes the known handshake."""
import argparse
import asyncio
import sys

from bleak import BleakClient, BleakScanner

SERVICE = "0000fff0-0000-1000-8000-00805f9b34fb"
NOTIFY = "0000fff1-0000-1000-8000-00805f9b34fb"
WRITE = "0000fff2-0000-1000-8000-00805f9b34fb"
BATTERY = "00002a19-0000-1000-8000-00805f9b34fb"


async def probe(args):
    device = await BleakScanner.find_device_by_filter(
        lambda device, adv: adv.local_name == args.name and SERVICE in adv.service_uuids,
        timeout=args.timeout,
    )
    if device is None:
        raise RuntimeError(f"No {args.name!r} advertisement with {SERVICE} found")
    print(f"FOUND {device.name} {device.address}", flush=True)
    for cycle in range(args.cycles):
        async with BleakClient(device, timeout=args.timeout) as client:
            services = client.services
            assert services.get_service(SERVICE), "Missing TourBox service"
            tx = services.get_characteristic(NOTIFY)
            rx = services.get_characteristic(WRITE)
            assert tx and "notify" in tx.properties, "Missing TX Notify"
            assert rx and "write" in rx.properties and "write-without-response" in rx.properties, "Missing RX Write modes"
            for service in services:
                print(f"SERVICE {service.uuid}", flush=True)
                for characteristic in service.characteristics:
                    print(f"  CHAR {characteristic.uuid} {characteristic.properties}", flush=True)
            if services.get_characteristic(BATTERY):
                level = await client.read_gatt_char(BATTERY)
                assert level == b"\x64", f"Unexpected battery level: {level.hex()}"
                print("BATTERY 100%", flush=True)
            await client.start_notify(NOTIFY, lambda _, data: print(f"NOTIFY {data.hex(' ')}", flush=True))
            print(f"SUBSCRIBED cycle={cycle + 1} MTU={client.mtu_size}", flush=True)
            if args.unlock:
                await client.write_gatt_char(WRITE, bytes.fromhex("5500078894001afe"), response=True)
                print("UNLOCK SENT", flush=True)
            await asyncio.sleep(args.listen)
            await client.stop_notify(NOTIFY)
        print(f"DISCONNECTED cycle={cycle + 1}", flush=True)
        if cycle + 1 < args.cycles:
            await asyncio.sleep(1)
    print("PASS: advertisement, GATT properties, battery, subscription, reconnect", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", default="TBE Lite")
    parser.add_argument("--timeout", type=float, default=15)
    parser.add_argument("--listen", type=float, default=2)
    parser.add_argument("--cycles", type=int, default=2)
    parser.add_argument("--unlock", action="store_true")
    if sys.platform == "darwin":
        from CoreBluetooth import CBManager
        from Foundation import NSBundle
        bundle = NSBundle.mainBundle()
        print(f"macOS bundle={bundle.bundleIdentifier()} authorization={CBManager.authorization()} (3=allowed)", flush=True)
    asyncio.run(probe(parser.parse_args()))


if __name__ == "__main__":
    main()
