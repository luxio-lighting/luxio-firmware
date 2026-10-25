"""Opt-in, read-only BLE soak alongside the HTTP/Matter hardware tests."""
import argparse
import asyncio
import json
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / '.sdk/python'))
import luxio_pb2 as pb
from bleak import BleakClient, BleakScanner

parser = argparse.ArgumentParser()
parser.add_argument('--address', required=True)
parser.add_argument('--device-id', required=True)
parser.add_argument('--led-count', type=int, default=1024)
parser.add_argument('--seconds', type=int, default=120)
parser.add_argument('--interval', type=float, default=.2, help='Delay after each complete reply; zero tests immediate next requests')
parser.add_argument('--output', default='.sdk/ble-memory-results.json')
args = parser.parse_args()
assert 1 <= args.led_count <= 1024 and args.seconds >= 6 and args.interval >= 0
SERVICE = 'e97b1000-3c27-4a20-9df4-7d025bc7e451'
RX = SERVICE.replace('1000', '1001')
TX = SERVICE.replace('1000', '1002')

async def main():
    device = await BleakScanner.find_device_by_filter(
        lambda d, a: d.address == args.address and SERVICE in a.service_uuids, timeout=15)
    assert device, 'Selected Luxio BLE device not found'
    results = []
    next_id = 90000
    uptime = 0
    script_name = None
    try:
        for connection in range(3):
            buffer = bytearray()
            responses = asyncio.Queue()
            def notification(_, data):
                buffer.extend(data)
                while len(buffer) >= 8:
                    assert buffer[:4] == b'LXP1', 'Invalid BLE framing'
                    size = struct.unpack('>I', buffer[4:8])[0]
                    assert 0 < size <= 32768
                    if len(buffer) < size + 8:
                        break
                    responses.put_nowait(pb.Response.FromString(bytes(buffer[8:size+8])))
                    del buffer[:size+8]
            async with BleakClient(device, timeout=45) as client:
                await client.start_notify(TX, notification)
                deadline = time.monotonic() + args.seconds / 3
                while time.monotonic() < deadline:
                    next_id += 1
                    request = pb.Request(id=next_id, operation=pb.GET_STATE).SerializeToString()
                    frame = b'LXP1' + struct.pack('>I', len(request)) + request
                    for offset in range(0, len(frame), 7):
                        await client.write_gatt_char(RX, frame[offset:offset+7], response=True)
                    response = await asyncio.wait_for(responses.get(), 15)
                    assert response.id == next_id and not response.error, response.error
                    assert response.state.device_id == args.device_id, 'Wrong device'
                    assert response.state.config.led_count == args.led_count
                    assert not response.state.matter_disabled and response.network_status.psram_size == 0
                    assert response.state.mode == pb.ANIMATION and not response.animation_error, response.animation_error
                    if script_name is None:
                        script_name = response.state.script_name
                    assert response.state.script_name == script_name, 'Lighting changed during test'
                    assert response.network_status.connected
                    assert response.network_status.uptime_ms >= uptime, 'Device restarted'
                    uptime = response.network_status.uptime_ms
                    results.append({'connection': connection + 1, 'uptime': uptime,
                                    'free': response.network_status.internal_free,
                                    'largest': response.network_status.internal_largest_block})
                    await asyncio.sleep(args.interval)
                await client.stop_notify(TX)
            print(f'PASS: encrypted BLE connection {connection + 1}; {len(results)} total reads', flush=True)
    finally:
        Path(args.output).write_text(json.dumps(results, indent=2))
    print(f'PASS: three encrypted BLE sessions, {args.led_count} LEDs, no PSRAM; '
          f'minimum sampled free {min(r["free"] for r in results)}, '
          f'largest block {min(r["largest"] for r in results)}', flush=True)

asyncio.run(main())
