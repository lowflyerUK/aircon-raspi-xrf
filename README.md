# aircon-raspi-xrf
Raspberry Pi control of Daikin air conditioner using infra-red and Ciseco xrf serial modules

# History
I needed to control my a/c remotely over the internet. I was inspired by [mat_fr](http://www.instructables.com/id/Reverse-engineering-of-an-Air-Conditioning-control/) to reverse engineer the IR protocol. Thanks!!!!

I already had a Raspberry Pi with an [XRF module](https://www.wirelessthings.net/xrf-wireless-rf-radio-uart-serial-data-module-xbee-shaped) and I didn't want to use the send part of lirc so I decided to use another XRF module with a [PIC](http://www.microchip.com/).

# Reverse engineering the Daikin IR protocol
My Daikin IR remote is an ARC452A3 and the indoor units are FTXS series - installed around 2010. Obviously different units may well use different codes.

Using exactly the same approach as [mat_fr](http://www.instructables.com/id/Reverse-engineering-of-an-Air-Conditioning-control/), I recorded the result of a lot of different commands. I iterated my understanding of the protocol, eventually arriving at dec_2.py. Yes, horrible code, but it tests that the received lirc code has the same timings as a code generated from scratch. It runs with `python -i 'lirc input filename' -c 'command string - for example aXY1cT20F100'` and outputs a whole lot of (possibly) useful diagnostics.
You will need to install python bitstream from https://pypi.python.org/pypi/bitstring/3.1.3

# Building the remote IR transmitter
I have made a few projects with [XRF modules](https://www.wirelessthings.net/xrf-wireless-rf-radio-uart-serial-data-module-xbee-shaped) and [PICs](http://www.microchip.com/) so this choice was easy. I had a spare PIC18F26J50 although this project should work with any PIC18 with serial and PPM modules. It is easy to interface to the XRF if the PIC is also running at 3.3V.

The circuit schematic is in the PIC folder. It has independent IR LEDs for the air conditioning units in 2 rooms. I used an ethernet cable to extend to the second room. 3 pairs in parallel for the LED and one pair for the thermistor.

The C source is in the PIC folder. It needs the microchip mplabx IDE to compile and build.

All the bit timings were taken from the lirc captures as described above. The PWM timings were adjusted to give the same frequency and duty cycle as the Daikin transmitter using a fast photodiode and oscilloscope. Roughly 36KHz with 33% duty cycle. It wasn't very critical.

# The Raspberry Pi

My RasPi was already set up with an XRF module. I actually used a [slice of pi](https://www.wirelessthings.net/slice-of-pi-add-on-for-raspberry-pi) but a [slice of radio](https://www.wirelessthings.net/slice-of-radio-wireless-rf-transciever-for-the-raspberry-pi) would work just as well - particularly if you change the chip antenna for a whip as described [here](http://openmicros.org/index.php/articles/84-xrf-basics/289).

The python code (in the folder raspi) also listens for sensor readings from other sensors. If it recognises a command for the air conditioning in /run/shm/ it sends the correct code to the PIC and expects to receive an acknowledgement. If no acknowledgement is received, it tries again 4 times before giving up. It removes the command from /run/shm/. Every 5 minutes, it asks the remote to send the current thermistor voltage and converts this to degrees centigrade. You will have to adjust the thermistor constants to get the correct results.
