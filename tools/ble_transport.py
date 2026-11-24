"""Serialized writes over the device's Nordic UART BLE link."""
import asyncio
from contextlib import suppress

RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'


class BleTransport:
    def __init__(self, client):
        self.client = client
        self.lock = asyncio.Lock()

    @property
    def is_connected(self):
        return bool(self.client.is_connected)

    async def write(self, payload):
        characteristic = self.client.services.get_characteristic(RX)
        if characteristic is None:
            raise ValueError('未找到设备蓝牙接收通道')
        size = max(20, min(244, characteristic.max_write_without_response_size))
        wire = payload.rstrip(b'\r\n') + b'\n'
        for offset in range(0, len(wire), size):
            await self.client.write_gatt_char(RX, wire[offset:offset+size], response=False)

    async def send_payload(self, payload):
        async with self.lock:
            await asyncio.wait_for(self.write(payload), timeout=5)

    async def close(self):
        with suppress(Exception):
            pass
