#!/usr/bin/env python3
"""Bleak central demo for the ESLAB HW4 STM32 accelerometer service."""

import argparse
import asyncio
import struct
import time

from bleak import BleakClient, BleakScanner


DEVICE_NAME = "ESLAB_HW4"
SERVICE_UUID = "12345678-1234-5678-1234-56789abcdef0"
XYZ_UUID = "12345678-1234-5678-1234-56789abcdef1"
FREQUENCY_UUID = "12345678-1234-5678-1234-56789abcdef2"
MOTION_UUID = "12345678-1234-5678-1234-56789abcdef3"


def parse_xyz(data: bytearray) -> tuple[int, int, int]:
    if len(data) != 6:
        raise ValueError(f"Expected 6 XYZ bytes, received {len(data)}")
    return struct.unpack("<hhh", data)


async def set_frequency(client: BleakClient, hz: int) -> None:
    if not 1 <= hz <= 100:
        raise ValueError("Frequency must be between 1 and 100 Hz")
    await client.write_gatt_char(FREQUENCY_UUID, struct.pack("<H", hz), response=True)
    print(f"Sampling frequency set to {hz} Hz")


async def run_demo(args: argparse.Namespace) -> None:
    print(f"Scanning for {DEVICE_NAME}...")
    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=args.scan_timeout)
    if device is None:
        raise RuntimeError(
            f"Cannot find {DEVICE_NAME}. Check STM32 power, BLE advertising, and distance."
        )

    print(f"Found: {device.address}")
    start = time.monotonic()

    def xyz_callback(_sender: int, data: bytearray) -> None:
        try:
            x, y, z = parse_xyz(data)
            elapsed = time.monotonic() - start
            print(f"[{elapsed:7.2f}s] XYZ = ({x:5d}, {y:5d}, {z:5d}) mg")
        except ValueError as exc:
            print(f"Invalid XYZ notification: {exc}; raw={bytes(data).hex()}")

    def motion_callback(_sender: int, data: bytearray) -> None:
        print(f"*** Significant motion: {bytes(data).hex()} ***")

    async with BleakClient(device, timeout=20.0) as client:
        print(f"Connected: {client.is_connected}")
        service = client.services.get_service(SERVICE_UUID)
        if service is None:
            raise RuntimeError(f"Required GATT service not found: {SERVICE_UUID}")
        for uuid in (XYZ_UUID, FREQUENCY_UUID, MOTION_UUID):
            characteristic = client.services.get_characteristic(uuid)
            if characteristic is None:
                raise RuntimeError(f"Required characteristic not found: {uuid}")
            print(f"Characteristic {uuid}: {characteristic.properties}")

        await client.start_notify(XYZ_UUID, xyz_callback)
        await client.start_notify(MOTION_UUID, motion_callback)
        print("Subscribed to XYZ and Motion notifications.")

        if args.sequence:
            for hz, duration in ((5, args.fast_seconds), (2, args.slow_seconds)):
                await set_frequency(client, hz)
                print(f"\n=== DEMO: {hz} Hz for {duration:g} seconds ===")
                await asyncio.sleep(duration)
            print("\n=== DEMO: Significant Motion ===")
            print(f"Move the STM32 board for {args.motion_seconds:g} seconds.")
            await asyncio.sleep(args.motion_seconds)
        elif args.frequency is None:
            await set_frequency(client, 5)
            print(f"\n=== DEMO: 5 Hz for {args.fast_seconds:g} seconds ===")
            await asyncio.sleep(args.fast_seconds)
            await set_frequency(client, 2)
            print("\n=== DEMO: 2 Hz continuous read ===")
            print("Press Ctrl+C to stop; move the board and watch XYZ values change.")
            while True:
                await asyncio.sleep(1)
        else:
            await set_frequency(client, args.frequency)
            print("Press Ctrl+C to stop the fixed-frequency read.")
            while True:
                await asyncio.sleep(1)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--frequency",
        type=int,
        help="stream at a fixed rate instead of the default 5 Hz-to-2 Hz demo",
    )
    parser.add_argument("--scan-timeout", type=float, default=20.0)
    parser.add_argument(
        "--sequence",
        action="store_true",
        help="run the timed 5 Hz / 2 Hz / significant-motion demo",
    )
    parser.add_argument("--fast-seconds", type=float, default=15.0)
    parser.add_argument("--slow-seconds", type=float, default=15.0)
    parser.add_argument("--motion-seconds", type=float, default=30.0)
    return parser.parse_args()


if __name__ == "__main__":
    try:
        asyncio.run(run_demo(parse_args()))
    except KeyboardInterrupt:
        print("\nStopped.")
