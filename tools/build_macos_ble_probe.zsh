#!/bin/zsh
set -e
mkdir -p tmp/TourBoxBLEProbe.app/Contents/MacOS tmp/swift-module-cache
cp tools/macos_ble_probe.plist tmp/TourBoxBLEProbe.app/Contents/Info.plist
xcrun swiftc -module-cache-path "$PWD/tmp/swift-module-cache" \
    tools/macos_ble_probe.swift -o tmp/TourBoxBLEProbe.app/Contents/MacOS/TourBoxBLEProbe
codesign --force --sign - tmp/TourBoxBLEProbe.app
