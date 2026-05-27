#!/usr/bin/python3
'''
 * Video Device Test Run without Tilt, over USB
 * 
 * Run steps:
 * 1. ltrun UnitTestLTDeviceVideo
 * 2. python3 stream.py /dev/ttyACM0 capture
 * ...
 * 3. click on the streaming window
 * 4. type "q" to quit
 *
 * Copyright 2024, Roku, Inc.  All Rights Reserved.
'''
import serial
import sys
import io
import msgpack
import cv2
import numpy
from datetime import datetime
import time
from PIL import Image

# Parsing functions: Process the response MessagePack for each command:

imgdata = b''
imgcnt = 0
def parse_stream_data(pack: msgpack.Unpacker):
    global imgdata
    global imgcnt
    length = pack.__next__()

    if length < 0 :
        print('capture fail')
        return True, 1
    
    # last block of the current image
    if length == 0 :
        imgcnt += 1
        print("image", time.time(), imgcnt, len(imgdata))
        if len(imgdata) != 0:
            frame = numpy.asarray(bytearray(imgdata), dtype="uint8")
            frame = cv2.imdecode(frame, cv2.IMREAD_COLOR)
            smallframe = cv2.resize(frame, (640, 360))
            cv2.imshow('frame', smallframe)
            key = cv2.waitKey(1)  # key is int

            if key == ord('s'):
                imfile = 'image' + str(datetime.now())
                cv2.imwrite(imfile + '.jpg', frame)
                print("saved", imfile)

            elif key == ord('q'):
                print("quit")
                # cap.release()
                cv2.destroyAllWindows()
                return True, 0

        imgdata = b''
        return False, 0

    offset = pack.__next__()
    data = pack.__next__()
    assert(length == len(data))
    imgdata += data
    # print(offset + length)
    return False, 0

def process_response(ser: serial.Serial, requestID: int, timeout: int, parse_func):
    pack = msgpack.Unpacker()
    while True:
        try:
            incoming = ser.read()
            if incoming:
                pack.feed(incoming)
            else:
                timeout -= 1
                if timeout <= 0:
                    print("Operation timed out - no response from device", file=sys.stderr)
                    return 0
        except SystemExit:
            return 1
        except:
            pass
        for payload in pack:
            if isinstance(payload, msgpack.ExtType):
                data = msgpack.Unpacker()
                data.feed(payload[1])
                returnID = data.__next__()
                if not returnID == requestID:
                    print(f"Unexpected response ID: ({returnID} != {requestID})", file=sys.stderr)
                    return 5
                done, err = parse_func(data)
                if not done :
                    continue
                else:
                    return err
            else:
                try:
                    c = chr(payload)
                    print(c, end = "")
                except:
                    pass

def send_command(ser: serial.Serial, cmdId: int, args: list):
    payload = io.BytesIO()
    payload.write(msgpack.packb("stream")) # the LTMailbox for these commands
    payload.write(msgpack.packb(cmdId))
    payload.write(msgpack.packb(args))
    ser.write(msgpack.packb(msgpack.ExtType(65, payload.getvalue())))

def usage():
    print("Example: ./stream.py /dev/ttyACM0 capture")

# Command ID map: Pair the command string from the command-line argument with
#                 a request ID:
command_map = {
    "capture"     : [0, parse_stream_data],
}

if len(sys.argv) < 3:
    print("Unrecognized command", sys.argv)
    usage()
    exit(1)

ser = serial.Serial(port=sys.argv[1], baudrate=115200, timeout=1)
args = sys.argv[2:]
cmd = args[0]
if cmd not in command_map :
    exit(2)

cmd = command_map[cmd]
send_command(ser, cmd[0], args)
err = process_response(ser, cmd[0], 10, cmd[1])
print('exit code', err)
exit(err)