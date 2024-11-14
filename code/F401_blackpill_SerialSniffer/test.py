#!/usr/bin/env python3

import sys
import time
from serial import Serial
import random

port1 = '/dev/ttyUSB0'
port2 = '/dev/ttyUSB1'

class bcolors:
    FAIL = '\033[91m'    #red
    OKGREEN = '\033[92m' #green
    WARNING = '\033[93m' #yellow
    OKBLUE = '\033[94m'  #dblue
    HEADER = '\033[95m'  #purple
    OKCYAN = '\033[96m'  #cyan
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'
#print(f'{bcolors.FAIL}{bcolors.ENDC}')
#print(f'{bcolors.OKGREEN}{bcolors.ENDC}')
#print(f'{bcolors.WARNING}{bcolors.ENDC}')
#print(f'{bcolors.OKBLUE}{bcolors.ENDC}')
#print(f'{bcolors.HEADER}{bcolors.ENDC}')
#print(f'{bcolors.OKCYAN}{bcolors.ENDC}')

def dump_data(data, width):
    idx = len(data)
    for w in range(width):
        print (f'{bcolors.OKCYAN}  {w:02} ', end = '')
    print(f'{bcolors.ENDC}')
    for i in range(0, len(data), width):
        #print(f'idx: {idx}')
        if idx < width:
            stop = idx
        else:
            stop = width
        for j in range(0,stop):
            print(f'{bcolors.WARNING}0x{data[i+j]:02X} ', end = '')
        idx -= width
        if (idx >= width):
            print(f'{bcolors.ENDC} - {(i+width):>2} ({(i+width):02X})\r')
        else:
            print(f'{bcolors.ENDC}')
        #print(f'{bcolors.ENDC}\r')
#------------------------------------------
def write_ser(channel, data):
    if (channel == None):
        return
    channel.write(data)
#----------------------------------
def read_ser(ser, size):
    data = ser.read(size)
    return data
#----------------------------------

def main(ser1 = None, ser2 = None):

    if (ser1 != None):
        print(f'Serial {port1} open.')
    else:
        print(f'Serial {port1} problem!')
        exit()
    if (ser2 != None):
        print(f'Serial {port2} open.')
    else:
        print(f'Serial {port2} problem!')
        ser1.close()
        exit()
    #-----------------------------------------------
    print(f'Writing Port 1')
    start = 0x123456
    message = bytearray.fromhex('AA')
    message += start.to_bytes(3,'big')
    write_ser(ser1, message)
    retval = read_ser(ser2, 4)
    dump_data(message, 16)
    #dump_data(retval, 16)
    #time.sleep(0.5)
    #-----------------------------------------------
    print(f'Writing Port 2')
    start = 0x654321
    message = bytearray.fromhex('55')
    message += start.to_bytes(3,'big')
    write_ser(ser2, message)
    retval = read_ser(ser1, 4)
    dump_data(message, 16)
    #dump_data(retval, 16)

    #-----------------------------------------------
    print(f'Writing Port 1')
    message = bytes(random.sample(range(0, 255), 7))
    dump_data(message, 16)
    write_ser(ser1, message)
    retval = read_ser(ser1, 7)
    #-----------------------------------------------
    print(f'Writing Port 2')
    message = bytes(random.sample(range(0, 255), 9))
    dump_data(message, 16)
    write_ser(ser2, message)
    retval = read_ser(ser1, 9)

    #-----------------------------------------------
    print(f'Writing Port 1')
    message = bytes(random.sample(range(0, 255), 55))
    dump_data(message, 16)
    write_ser(ser1, message)
    retval = read_ser(ser1, 55)
    #-----------------------------------------------
    print(f'Writing Port 2')
    message = bytes(random.sample(range(0, 255), 32))
    dump_data(message, 16)
    write_ser(ser2, message)
    retval = read_ser(ser1, 32)

    #-----------------------------------------------
    print(f'Writing Port 1')
    message = bytes(random.sample(range(0, 255), 250))
    dump_data(message, 16)
    write_ser(ser1, message)
    retval = read_ser(ser1, 250)
    #-----------------------------------------------
    print(f'Writing Port 2')
    message = bytes(random.sample(range(0, 255), 130))
    dump_data(message, 16)
    write_ser(ser2, message)
    retval = read_ser(ser1, 130)



    ser1.close()
    ser2.close()


if __name__ == '__main__':
    #if len(sys.argv) < 2:
    #    print('Please supply Reset (high = 1 or Low = 0) as argument!')
    #    exit()
    #val = int(sys.argv[1], 0)

    ser1 = None
    ser2 = None
    try:
        ser1 = Serial(port1, 38400, timeout = 1, writeTimeout = 1)
    except IOError:
        print('Port not found!')
    try:
        ser2 = Serial(port2, 38400, timeout = 1, writeTimeout = 1)
    except IOError:
        print('Port not found!')

    ser1.flush()
    ser2.flush()

    main(ser1, ser2)
