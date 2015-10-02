# python code running all the time on Raspberry Pi. Records sensors to /home/pi/temp.csv
# and listens for commands for a/c in /run/shm/

# needs access to /dev/ttyAMA0 so:
# 1. Either run as root (not recommended), or add the user to the group 'dialout' - try 'sudo usermod -a -G dialout pi'
# 2. Remove the references to /dev/ttyAMA0 in /boot/cmdline.txt => delete 'console=ttyAMA0,115200 kgdboc=ttyAMA0,115200'
# 3. Remove the reference to ttyAMA0 in /etc/inittab => comment out 'T0:23:respawn:/sbin/getty -L ttyAMA0 115200 vt100'

# Connect XRF serial RF module:
# Ground: XRF pin 10 to RasPi P1-06
# +3.3V:  XRF pin  1 to RasPi P1-01
# data to XRF:   XRF pin 3 to RasPi P1-08
# data from XRF: XRF pin 2 to RasPi P1-010

# aAC0cT24F320 - off
#            | 1 quiet, 0 normal
#           |  0 no swing, 1 swing up/down, 2 swing side/side, 3 swing both
#         F|   Fan 1-5, A auto, N night
#      T||     temperature 00 -> 29
#     |        A auto, D dehumidify, C cool, V ventillate, H heat
#    |         1 on, 0 off
#   |          C bedroom, V lounge, T request temperature
# aA           start

# aAV1cT24F320 switch on cooling 24deg Fan 3 swing side/side normal
# aACT-------- request temperature

import time
import serial
import datetime
import sys
import os
from math import log

baud = 9600
port = '/dev/ttyAMA0'
print time.strftime("%Y%m%d %H:%M:%S")
print "Opening port "+ port + " at ", baud, " baud."

ser = serial.Serial(port, baud)
# Clear out anything in the read buffer
ser.flushInput()

llapMsg = ''
new_data = False

tempTE = float(0.0)
battTE = float(0.0)

tempTF = float(0.0)
battTF = float(0.0)

tempAC = float(0.0)

time_NOW = time.time()
time_last_AC = time_NOW - 10
recd_AC_ACK = False
recd_AV_ACK = False
retrans_AC = 0
retrans_AV = 0

## touch these files and this prog will send the command then delete the file
ask_AC_HEAT = '/run/shm/ask_ac_heat'
ask_AC_COOL = '/run/shm/ask_ac_cool'
ask_AC_OFF = '/run/shm/ask_ac_off'
ask_AV_HEAT = '/run/shm/ask_av_heat'
ask_AV_COOL = '/run/shm/ask_av_cool'
ask_AV_OFF = '/run/shm/ask_av_off'

code_AC_HEAT_ON = 'aAC1HT17FA00'
code_AC_COOL_ON = 'aAC1CT22FA00'
code_AC_OFF = 'aAC0CT24F320'
code_AV_HEAT_ON = 'aAV1HT17FA00'
code_AV_COOL_ON = 'aAV1CT22FA00'
code_AV_OFF = 'aAV0CT24F320'

##Thermistor to temperature constants.
beta = 3380             # beta value for the thermistor
Rzero = 273.15
Rtemp = 25.0 + Rzero   # reference temperature (25C)
Rresi = 10000           # reference resistance at reference temperature - adjust to calibrate was 10300
Rnom = 10000     #nominal resistance of thermistor at Rtemp

while 1 :
        time_NOW = time.time()
        recd_AC_ACK = False
        recd_AV_ACK = False
	if ser.inWaiting() :
                time.sleep(0.05)
                new_data = False
                llapMsg = llapMsg + ser.read(ser.inWaiting())
                print time.strftime("%Y%m%d %H:%M:%S")
		print 'Content = |' + llapMsg + '|'
        
        if len(llapMsg) > 11 :
                found_a = llapMsg.find('aTE')
                if (found_a != -1 and len(llapMsg) > found_a + 11) :
                    newMsg = llapMsg[found_a:found_a + 12]
                    if newMsg.startswith('aTETMPA'):
                          try :
                              tempTE = float(newMsg[7:])
                              print 'got ' + newMsg + ' from TE temp: ' + str(round(tempTE,2))
                              new_data = True
                          except exceptions.ValueError:
                              print 'Got ValueError in tempTE'  

                    if newMsg.startswith('aTEBATT'):
                          try :
                              newMsg = newMsg.replace('-','')
                              battTE = float(newMsg[7:])
                              print 'got ' + newMsg + ' from TE battery: ' + str(round(battTE,2))
                              new_data = True
                          except exceptions.ValueError:
                              print 'Got ValueError in battTE'  


                    llapMsg = llapMsg[found_a + 12:]


                found_a = llapMsg.find('aTF')
                if (found_a != -1 and len(llapMsg) > found_a + 11) :
                    newMsg = llapMsg[found_a:found_a + 12]
                    if newMsg.startswith('aTFTMPA'):
                          try :
                              tempTF = float(newMsg[7:])
                              print 'got ' + newMsg + ' from TF temp: ' + str(round(tempTF,2))
                              new_data = True
                      ##        temperature = tempTF  
                          except exceptions.ValueError:
                              print 'Got ValueError in tempTF'

                    if newMsg.startswith('aTFBATT'):
                          try :
                              newMsg = newMsg.replace('-','')
                              battTF = float(newMsg[7:])
                              print 'got ' + newMsg + ' from TF battery: ' + str(round(battTF,2))
                              new_data = True
                           ##   voltage = battTF
                          except exceptions.ValueError:
                              print 'Got ValueError in battTF'  

                    llapMsg = llapMsg[found_a + 12:]

                found_a = llapMsg.find('aA')
                if (found_a != -1 and len(llapMsg) > found_a + 11) :
                    newMsg = llapMsg[found_a:found_a + 12]
                    if newMsg.startswith('aACD'):
                          try :
                              Rtherm = float(Rresi / (1024.0/int(newMsg[4:8],16) -1) )
                              tempAC = Rtemp * beta / (beta + Rtemp * (log(Rtherm/Rnom))) - Rzero
                              print 'got ' + newMsg + ' from ACD : ' + str(round(tempAC,2)) + ' deg '
                              new_data = True
                          except :
                              print 'Got ValueError in data from ACD' + str(sys.exc_info()[0] )

                    if newMsg.startswith('aACACK'):
                          print 'got ' + newMsg + ' from AC'
                          recd_AC_ACK = True

                    if newMsg.startswith('aAVACK'):
                          print 'got ' + newMsg + ' from AV'
                          recd_AV_ACK = True

                    llapMsg = llapMsg[found_a + 12:]

        if new_data :
            with open('/home/pi/temp.csv' , 'w') as csv_file:
                 s = str(round(tempTE,2)) + str(',') + str(round(battTE,2)) + str(',') + str(round(tempTF,2)) + str(',') + str(round(battTF,2)) + str(',') + str(round(tempAC,2)) + str('\r\n')
                 csv_file.write(s)
            new_data = False

        if (time_NOW > time_last_AC + 300 ):
            ser.write('aACT--------')
            time_last_AC = time_NOW
            print 'sent request for aCT'

        if os.path.isfile(ask_AC_HEAT) :
            if not recd_AC_ACK :
                if retrans_AC < 6 :
                    ser.write(code_AC_HEAT_ON)
                    print 'Asking for AC HEAT'
                    retrans_AC += 1
                else :
                    os.remove(ask_AC_HEAT)
                    retrans_AC = 0
                    print 'No AC HEAT acknowledge'
            else :
                os.remove(ask_AC_HEAT)
                retrans_AC = 0
                print 'Got AC HEAT acknowledge'

        if os.path.isfile(ask_AC_COOL) :
            if not recd_AC_ACK :
                if retrans_AC < 6 :
                    ser.write(code_AC_COOL_ON)
                    print 'Asking for AC COOL'
                    retrans_AC += 1
                else :
                    os.remove(ask_AC_COOL)
                    retrans_AC = 0
                    print 'No AC COOL acknowledge'
            else :
                os.remove(ask_AC_COOL)
                retrans_AC = 0
                print 'Got AC COOL acknowledge'

        if os.path.isfile(ask_AC_OFF) :
            if not recd_AC_ACK :
                if retrans_AC < 6 :
                    ser.write(code_AC_OFF)
                    print 'Asking for AC OFF'
                    retrans_AC += 1
                else :
                    os.remove(ask_AC_OFF)
                    retrans_AC = 0
                    print 'No AC OFF acknowledge'
            else :
                os.remove(ask_AC_OFF)
                retrans_AC = 0
                print 'Got AC OFF acknowledge'

        if os.path.isfile(ask_AV_HEAT) :
            if not recd_AV_ACK :
                if retrans_AV < 6 :
                    ser.write(code_AV_HEAT_ON)
                    print 'Asking for AV HEAT'
                    retrans_AV += 1
                else :
                    os.remove(ask_AV_HEAT)
                    retrans_AV = 0
                    print 'No AV HEAT acknowledge'
            else :
                os.remove(ask_AV_HEAT)
                retrans_AV = 0
                print 'Got AV HEAT acknowledge'

        if os.path.isfile(ask_AV_COOL) :
            if not recd_AV_ACK :
                if retrans_AV < 6 :
                    ser.write(code_AV_COOL_ON)
                    print 'Asking for AV COOL'
                    retrans_AV += 1
                else :
                    os.remove(ask_AV_COOL)
                    retrans_AV = 0
                    print 'No AV COOL acknowledge'
            else :
                os.remove(ask_AV_COOL)
                retrans_AV = 0
                print 'Got AV COOL acknowledge'

        if os.path.isfile(ask_AV_OFF) :
            if not recd_AV_ACK :
                if retrans_AV < 6 :
                    ser.write(code_AV_OFF)
                    print 'Asking for AV OFF'
                    retrans_AV += 1
                else :
                    os.remove(ask_AV_OFF)
                    retrans_AV = 0
                    print 'No AV OFF acknowledge'
            else :
                os.remove(ask_AV_OFF)
                retrans_AV = 0
                print 'Got AV OFF acknowledge'

        if len(llapMsg) == 0 :
                time.sleep(0.1)
        else :
                time.sleep(0.02)
