import serial
import time
import os
import msvcrt

count = 0
if os.path.exists("cameraPhoto.jpg"):
    os.remove("cameraPhoto.jpg")
serialPort = serial.Serial(
    port="COM10", baudrate=115200, bytesize=8, timeout=2, stopbits=serial.STOPBITS_ONE
)
serialString = ""  # Used to hold data coming over UART
count=1
while 1:
    if (msvcrt.kbhit()):
        ch = msvcrt.getch()
        serialPort.write(ch)

    # Wait until there is data waiting in the serial buffer
    if serialPort.in_waiting > 0:

        # Read data out of the buffer until a carraige return / new line is found
        serialString = serialPort.readline().decode('UTF-8')
        if serialString.strip() == "END":
            print(serialString)
            print("image capture complete")
            continue;
        if serialString.strip() == "START":
            print(serialString)
            if os.path.exists("cameraPhoto.jpg"):
                os.remove("cameraPhoto.jpg")
            continue;
        # Print the contents of the serial data
        try:
            bytearray.fromhex(serialString)
            image_file = open("cameraPhoto.jpg", 'ab')
            image_file.write(bytearray.fromhex(serialString))
            if (count%10 == 0):
                print("",end=".", flush=True)
            if count==0:
                print("\n")
 #           print(serialString)

            image_file.close()
            count = (count + 1) % 1000
        except:
            print(serialString)
            pass