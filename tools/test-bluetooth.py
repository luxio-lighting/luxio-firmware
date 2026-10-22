"""Physical Luxio BLE test. Generate luxio_pb2 into .sdk/python first; see README."""
import argparse
import asyncio
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / '.sdk/python'))
import luxio_pb2 as pb
from bleak import BleakClient, BleakScanner

SERVICE = 'e97b1000-3c27-4a20-9df4-7d025bc7e451'
RX = 'e97b1001-3c27-4a20-9df4-7d025bc7e451'
TX = 'e97b1002-3c27-4a20-9df4-7d025bc7e451'
parser = argparse.ArgumentParser()
parser.add_argument('--device-id', required=True, help='Expected fingerprint; abort before mutations if it differs')
args = parser.parse_args()

async def main():
    device = await BleakScanner.find_device_by_filter(lambda d, a: SERVICE in a.service_uuids, timeout=15)
    if device is None:
        raise RuntimeError('Luxio Bluetooth service not found')
    buffer = bytearray()
    responses = asyncio.Queue()
    def notification(_, data):
        buffer.extend(data)
        while len(buffer) >= 8:
            if buffer[:4] != b'LXP1':
                raise RuntimeError('Invalid response framing')
            size = struct.unpack('>I', buffer[4:8])[0]
            if size > 32768:
                raise RuntimeError('Oversized response')
            if len(buffer) < 8 + size:
                break
            responses.put_nowait(pb.Response.FromString(bytes(buffer[8:8+size])))
            del buffer[:8+size]
    async with BleakClient(device) as client:
        await client.start_notify(TX, notification)
        next_id = 0
        async def send(operation, **fields):
            nonlocal next_id
            next_id += 1
            message = pb.Request(id=next_id, operation=operation, **fields).SerializeToString()
            packet = b'LXP1' + struct.pack('>I', len(message)) + message
            # Deliberately fragment the header and payload, even when the MTU is larger.
            for offset in range(0, len(packet), 7):
                await client.write_gatt_char(RX, packet[offset:offset+7], response=True)
            result = await asyncio.wait_for(responses.get(), timeout=15)
            assert result.id == next_id, 'Response ID mismatch'
            assert not result.error, result.error
            return result
        initial = (await send(pb.GET_STATE)).state
        assert initial.device_id == args.device_id, 'Discovered a different device; no changes made'
        print('PASS: Bluetooth identity and fragmented GET_STATE', flush=True)
        script_id = None
        try:
            await send(pb.SET_BRIGHTNESS, brightness=16)
            response = await send(pb.SET_COLOR, color=pb.Color(red=64, green=0, blue=128))
            assert response.state.color.red == 64 and response.state.color.blue == 128
            source = '-- ' + 'fragmentation-test ' * 64 + '\nfunction loop() luxio_set_pixel_color(0,128,0,128) end'
            created = await send(pb.CREATE_SCRIPT, script=pb.Script(name='BLE integration test', source=source))
            script_id = created.script.id
            read = await send(pb.GET_SCRIPT, script_id=script_id)
            assert read.script.source == source
            await send(pb.RUN_SCRIPT, script_id=script_id)
            dimmed = await send(pb.SET_BRIGHTNESS, brightness=8)
            assert dimmed.state.active_script_id == script_id
            stopped = await send(pb.SET_COLOR, color=pb.Color(cool_white=128))
            assert not stopped.state.active_script_id
            await send(pb.DELETE_SCRIPT, script_id=script_id)
            script_id = None
            print('PASS: BLE color, large script upload/download, Lua run, brightness arbitration and delete', flush=True)
        finally:
            if script_id:
                await send(pb.DELETE_SCRIPT, script_id=script_id)
            await send(pb.SET_COLOR, color=initial.color)
            await send(pb.SET_BRIGHTNESS, brightness=initial.brightness)
            if initial.active_script_id:
                await send(pb.RUN_SCRIPT, script_id=initial.active_script_id)
        await client.stop_notify(TX)

asyncio.run(main())
