import serial, time
ser = serial.Serial('/dev/ttyUSB0', 9600, timeout=1)
end = b'\xff\xff\xff'
ser.write(b't0.txt="ztl"' + end)
time.sleep(0.1)
ser.write(b'n0.val=456' + end)
time.sleep(0.1)
print(ser.read(64))
ser.close()