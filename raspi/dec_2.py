import bitstring
from bitstring import BitArray, BitStream
import argparse

P_MARK = int(550)
P_SHORT = int(300)
P_LONG = int(1200)
MARGIN = int(100)
P_INTRO = int(3600)
P_INTRO2 = int(1620)
P_SEPARATOR1 = int(25350)
P_SEPARATOR2 = int(34900)

parser = argparse.ArgumentParser(description='Test script to convert lirc raw files from Daikin a/c')
parser.add_argument('-i','--input', help='Input file name',required=True)
parser.add_argument('-c','--command',help='command string aXY1CT20F100', required=True)
args = parser.parse_args()

if (len(args.command) != 12):
   print('command is wrong length')
   my_command = 'aXY1CT20F100'
else:
   my_command =args.command

in_preamble = BitArray('0b00000')
in_device1 = BitArray('0xd70000c50027da11')
in_device2 = BitArray('0x641000420027da11')
in_params = BitArray('0xf40080c1002060060000b0002249000027da11')
in_list = [in_preamble,in_device1,in_device2,in_params]

#in_list[3].reverse()
in_first = in_params.copy()

print(in_list[0].bin)
print(in_list[1].hex)

print(in_list[2].hex)
print(in_list[3].hex)
#print(in_list[3].bin)

in_list[0].clear()
in_list[1].clear()
in_list[2].clear()
in_list[3].clear()

with open(args.input, 'r') as raw_file:
  A_list1 = []
  section_no = 0
  for line in raw_file:
    part = [int(x) for x in line.split()]
    A_list1 = A_list1 + part

  for i in range(1, len(A_list1) - 2, 2):
    v1 = A_list1[i]
    v2 = A_list1[i+1]
    if abs(v1 - P_INTRO) < MARGIN and abs(v2 - P_INTRO2) < MARGIN :
      section_no +=1
      if section_no > 3:
        section_no = 3

    if abs(v1 - P_MARK) < MARGIN and abs(v2 - P_SHORT) < MARGIN :
      in_list[section_no].append('0b0') 

    if abs(v1 - P_MARK) < MARGIN and abs(v2 - P_LONG) < MARGIN :
      in_list[section_no].append('0b1')

in_list[1].reverse()
in_list[2].reverse()
in_list[3].reverse()

print(in_list[0].bin)
print(in_list[1].hex)

print(in_list[2].hex)
print(in_list[3].hex)
#print(in_list[3].bin)

#print(in_first.bin)

in_temp = in_first.copy()
in_temp ^= in_list[3]
#print(in_temp.bin)
print('\n')

print(my_command)

#example command aXY1CT20F100
if (my_command[3] == '1'):
  # on
  in_first.overwrite('0b1', 111)

if (my_command[3] == '0'):
  # off
  in_first.overwrite('0b0', 111)


#note: temperature 0 for dehum - overwritten later
temp = int(my_command[6:8])
#print(str(temp))
temp_bits = BitArray(uint=temp, length=5)
#print(temp_bits.bin)
in_first.overwrite(temp_bits, 98)

#temperature 17
#in_first.overwrite('0b10001', 98)
#temperature 23
#in_first.overwrite('0b10111', 98)
#temperature 25
#in_first.overwrite('0b11001', 98)

if (my_command[9] == '1'):
  #fan1
  in_first.overwrite('0b0011', 80)
if (my_command[9] == '2'):
  #fan2
  in_first.overwrite('0b0100', 80)
if (my_command[9] == '3'):
  #fan3
  in_first.overwrite('0b0101', 80)
if (my_command[9] == '4'):
  #fan4
  in_first.overwrite('0b0110', 80)
if (my_command[9] == '5'):
  #fan5
  in_first.overwrite('0b0111', 80)
if (my_command[9] == 'A'):
  #fan auto
  in_first.overwrite('0b1010', 80)
if (my_command[9] == 'N'):
  #fan night
  in_first.overwrite('0b1011', 80)

if (my_command[10] == '0'):
  #swing up
#in_first.overwrite('0b1111', 84)
#swing up off
  in_first.overwrite('0b0000', 84)
  in_first.overwrite('0b0000', 76)
if (my_command[10] == '1'):
  #swing up & down
  in_first.overwrite('0b1111', 84)
  in_first.overwrite('0b0000', 76)
if (my_command[10] == '2'):
  #swing side to side
  in_first.overwrite('0b1111', 76)
  in_first.overwrite('0b0000', 84)
if (my_command[10] == '3'):
  #swing side to side and up & down
  in_first.overwrite('0b1111', 76)
  in_first.overwrite('0b1111', 84)

if (my_command[11] == '0'):
  #quiet off
  in_first.overwrite('0b0', 42)
if (my_command[11] == '1'):
  #quiet
  in_first.overwrite('0b1', 42)

if (my_command[4] == 'A'):
  #auto mode
  in_first.overwrite('0b00000', 103)
if (my_command[4] == 'D'):
  #auto dehum also needs #quiet off and auto_fan
  in_first.overwrite('0b0', 42)
  in_first.overwrite('0b1010', 80)
  in_first.overwrite('0b1100000000101', 96)
if (my_command[4] == 'C'):
  #auto cool
  in_first.overwrite('0b00011', 103)
if (my_command[4] == 'V'):
  #auto vent also forces temp to 25
  in_first.overwrite('0b00110', 103)
  in_first.overwrite('0b11001', 98)
if (my_command[4] == 'H'):
  #auto heat
  in_first.overwrite('0b00100', 103)

checksum = in_list[3][0:8].uint
print(str(checksum))

#now calculate the new checksum
check_here = int(0)
i = 8
while i < (in_first.len):
#  print(in_list[3][i:i+8].bin)

  check_here += in_first[i:i+8].uint
  i += 8

print(str(check_here%256))
csum_array = BitArray(uint=check_here%256, length = 8)
#print(csum_array.bin)
in_first.overwrite(csum_array,0)
#print(in_first.bin)

print(in_first.hex)
in_temp ^= in_first
print((in_first ^ in_list[3]).hex)

