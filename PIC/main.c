/* Daikin Air Conditioning remote.
 * Listens for commands in format like https://www.wirelessthings.net/wireless-temperature-sensor#lot

 * aAC0CT24F320 - off
 *            | 1 quiet, 0 normal
 *           |  0 no swing, 1 swing up/down, 2 swing side/side, 3 swing both
 *         F|   Fan 1-5, A auto, N night
 *      T||     temperature 00 -> 29
 *     |        A auto, D dehumidify, C cool, V ventillate, H heat
 *    |         1 on, 0 off
 *   |          C chambre, V salle a vivre, T request temperature
 * aA           start

 * aAV1CT24F320 switch on cooling 24deg Fan 3 swing side/side normal
 * aACT-------- request temperature

 * responds with aACACK------ or aAVACK------ or aACDwxyzwxyz where wxyz is analog measured voltage across thermistor in hex

 * Real thanks and acknowledgments to microchip for http://www.microchip.com/pagehandler/en-us/family/mplabx/
 * Built using http://www.microchip.com/mplabx-ide-linux-installer version 3.10 and http://www.microchip.com/mplabxc8linux version 1.34
 * Programmed using microchip PICKit 2

 */

/****************************************************************
pins used:
 * AN4 (pin 7) input. Analog from thermistor. 10kohms to power.
 *
 * RC0 (pin 11) PWM output to salle a vivre - aAV. Needs external pull-down resistor, say 1k, 2nF => 2usec
 * RC1 (pin 12) PWM output to chambre - aVC

 *******************************************************************/

#define USE_OR_MASKS
#define __18F26J50
#include "p18cxxx.h"
#include "xc.h"
#include "plib.h"
#include  <stdlib.h>
//#include "pwm.h"
#define TRUE 1
#define ADC_PORTS 0b1001111111101101 //1 is digial, 0 is analog. MSB enables band gap ref for ADC
#define _XTAL_FREQ 8000000

#define MY_CODE_1 'A' // Air conditioning
#define MY_CODE_2_1 'V' // salle a Vivre
#define MY_CODE_2_2 'C' // Chambre

//************Configuration in program*****************************

#pragma config DSBOREN = ON, DSWDTEN = OFF
#pragma config T1DIG = OFF, LPT1OSC = ON
#pragma config OSC=INTOSC, FCMEN=ON, WDTEN=OFF, IESO=OFF, XINST=OFF, CPUDIV=OSC1

//Function Prototypes
void user_sleep(void);
void init(void);
void LPDelay(unsigned char);
unsigned int get_temp(void);
void init_usart(void);
void putc_usart(unsigned int, unsigned char);
void puts_usart(char *);
void send_preamble(void);
void send_1(void);
void send_2(void);
void send_3(void);
void send_bit(char);
void stop_preamble(void);
void start(void);
void stop(void);
void stop_end(void);
void send_char(unsigned char);
void update_checksum(void);

char block1[8] = {0xd7, 0x00, 0x00, 0xc5, 0x00, 0x27, 0xda, 0x11};
char block2[8] = {0x64, 0x10, 0x00, 0x42, 0x00, 0x27, 0xda, 0x11};
char block3[19] = {0xf4, 0x00, 0x80, 0xc1, 0x00, 0x20, 0x60, 0x06,
    0x00, 0x00, 0xb0, 0x00, 0x22, 0x49, 0x00, 0x00, 0x27, 0xda, 0x11};

char in_buf[120];
unsigned char in_buf_ptr = 0x00;

struct FLAGBITS {
    // unsigned lock : 1;
    unsigned send_temp : 1;
    unsigned good_cmd : 1; // 1 if all bits of the air-con command are valid, 0 if any invalid
    unsigned output_RC0 : 1; // 1 if using RC0, 0 if using RC1
};

typedef union {
    struct FLAGBITS Bits;
    unsigned char data;
} FLAGS;

volatile FLAGS FLAGbits;

void interrupt ISR(void) {
    int j;
    //Check if we received something in the USART
    if (RC1IE && RC1IF) {
        in_buf[in_buf_ptr] = Read1USART();
        if (in_buf_ptr < 100) {
            in_buf_ptr++;
        } else {
            //shift the buffer along if is getting full - save the last 12 characters received just to be safe.
            for (j = 0; j < 12; j++) {
                in_buf[j] = in_buf[j + in_buf_ptr - 11];
            }
            in_buf_ptr = 12;
        }

        if (RCSTA1bits.OERR) { //clear any overrun
            RCSTA1bits.CREN = 0;
            RCSTA1bits.CREN = 1;
        }
    }
    CLRWDT(); //clear the watchdog timer
}

void main(void) {
    unsigned int temperature;
    unsigned int ac_temp = 0;
    char send_str[12] = "aPO---------";
    char ack_str[12] = "aPOACK------";
    unsigned char i, j, msg_start;
    send_str[1] = MY_CODE_1;
    send_str[2] = MY_CODE_2_1;
    ack_str[1] = MY_CODE_1;
    ack_str[2] = MY_CODE_2_1;

    init(); //including charge of deep sleep cap
    init_usart();

    PEIE = 1;
    ei();

    while (1) {
        FLAGbits.Bits.good_cmd = 0;
        FLAGbits.Bits.send_temp = 0;
        msg_start = 0;
        if (in_buf_ptr > 11) { //we have received enough bytes for a message
            di();
            for (i = 0; i < in_buf_ptr - 11; i++) {
                if (in_buf[i] == 'a' && in_buf[i + 1] == MY_CODE_1 && (in_buf[i + 2] == MY_CODE_2_1 || in_buf[i + 2] == MY_CODE_2_2)) {
                    msg_start = i; // save for later
                    //we got enough bytes for a full message for us - first choose if RC0 or RC1
                    if (in_buf[i + 2] == MY_CODE_2_1) {
                        ack_str[2] = MY_CODE_2_1;
                        FLAGbits.Bits.output_RC0 = 1;
                    } else {
                        ack_str[2] = MY_CODE_2_2;
                        FLAGbits.Bits.output_RC0 = 0;
                    }

                    //now decide what command we got
                    FLAGbits.Bits.good_cmd = 1; // if any part is invalid, this will be changed
                    switch (in_buf[i + 3]) {
                        case '1': //switch on
                            block3[13] = block3[13] | 0x01;
                            break;
                        case '0': //switch off
                            block3[13] = block3[13] & 0xFE;
                            break;
                        case 'T': //send temperature reading
                            FLAGbits.Bits.send_temp = 1;
                            FLAGbits.Bits.good_cmd = 0; //not an air-con command
                            break;
                        default:
                            FLAGbits.Bits.good_cmd = 0; //invalid if it wasn't '0' or '1'
                            break;
                    }

                    //note mode done at end as it needs to overwrite sometimes
                    if (((in_buf[i + 6] & 0xFC) == 0x30) && ((in_buf[i + 7] & 0xF0) == 0x30) && ((in_buf[i + 7] & 0x0F) < 10)) {

                        ac_temp = 10 * (in_buf[i + 6] & 0x0F) + (in_buf[i + 7] & 0x0F);
                        //get temp from buf 6 & 7; test it is between 0 & 31; update
                        block3[12] = ac_temp << 1;
                    } else {
                        FLAGbits.Bits.good_cmd = 0; // invalid temperature
                    }

                    switch (in_buf[i + 9]) {
                            // fan options
                        case '1': //fan1
                            block3[10] = (block3[10] & 0x0F) | 0x30;
                            break;
                        case '2': //fan2
                            block3[10] = (block3[10] & 0x0F) | 0x40;
                            break;
                        case '3': //fan3
                            block3[10] = (block3[10] & 0x0F) + 0x50;
                            break;
                        case '4': //fan4
                            block3[10] = (block3[10] & 0x0F) + 0x60;
                            break;
                        case '5': //fan5
                            block3[10] = (block3[10] & 0x0F) + 0x70;
                            break;
                        case 'A': //fan auto
                            block3[10] = (block3[10] & 0x0F) + 0xA0;
                            break;
                        case 'N': //fan night
                            block3[10] = (block3[10] & 0x0F) + 0xB0;
                            break;
                        default:
                            // do nothing
                            FLAGbits.Bits.good_cmd = 0; // invalid fan command
                            break;
                    }
                    switch (in_buf[i + 10]) {
                            // swing options
                        case '0': //no swing
                            block3[10] = (block3[10] & 0xF0);
                            block3[9] = (block3[9] & 0xF0);
                            break;
                        case '1': //swing up & down
                            block3[10] = (block3[10] | 0x0F);
                            block3[9] = (block3[9] & 0xF0);
                            break;
                        case '2': //swing side to side
                            block3[10] = (block3[10] & 0xF0);
                            block3[9] = (block3[9] | 0x0F);
                            break;
                        case '3': //both swing up & side
                            block3[10] = (block3[10] | 0x0F);
                            block3[9] = (block3[9] | 0x0F);
                            break;
                        default:
                            FLAGbits.Bits.good_cmd = 0; // invalid swing command
                            break;
                    }
                    switch (in_buf[i + 11]) {
                        case '0': //quiet mode off
                            block3[5] = (block3[5] & 0xDF);
                            break;
                        case '1': //quiet mode on
                            block3[5] = (block3[5] | 0x20);
                            break;
                        default:
                            FLAGbits.Bits.good_cmd = 0; // invalid quiet mode
                            break;
                    }

                    switch (in_buf[i + 4]) {
                        case 'A': //automatic mode
                            block3[13] = block3[13] & 0x0F;
                            break;
                        case 'D': //dehumidify mode
                            block3[5] = (block3[5] & 0xDF); // quiet mode off
                            block3[10] = (block3[10] & 0x0F) + 0xA0; //auto fan
                            // and lots of bits
                            break;
                        case 'C': //cool mode
                            block3[13] = (block3[13] & 0x0F) | 0x30;
                            break;
                        case 'V': //ventilation mode
                            block3[13] = (block3[13] & 0x0F) | 0x60;
                            // and temp to 25
                            break;
                        case 'H': //heat mode
                            block3[13] = (block3[13] & 0x0F) | 0x40;
                            break;
                        default:
                            FLAGbits.Bits.good_cmd = 0; // invalid mode
                            break;
                    }
                    //discard used characters
                    for (j = 0; j < in_buf_ptr - (msg_start + 12); j++) {
                        in_buf[j] = in_buf[j + msg_start + 12];
                    }
                    in_buf_ptr = in_buf_ptr - (msg_start + 12);

                    break; // Only process one match. If there is another in the buffer it will be processed next time.
                } //end of if aXY statement
            } // end of for loop
            ei();
        }//end of (in_buf_ptr > 11)

        //test to see if we got a valid air-con command
        if (FLAGbits.Bits.good_cmd) {
            FLAGbits.Bits.good_cmd = 0; // in fact don't need to do it here as it is done at the start of the while loop
            //send ack
            di();
            for (j = 0; j < 12; j++) {
                while (Busy1USART());
                Write1USART(ack_str[j]);
            }
            ei();

            update_checksum();

            OpenTimer2(TIMER_INT_OFF & T2_PS_1_1 & T2_POST_1_1);
            SetOutputPWM1(SINGLE_OUT, PWM_MODE_1);
            SetDCPWM1(75); // first guess was 20. 75 gives about 9us on time.
            OpenPWM1(55); // 38KHz = 26.3microsec = 52+1 instruction cycles. 55 gives 28us
            di();
            send_preamble();
            send_1();
            send_2();
            send_3();
            ei();
            ClosePWM1();
            TRISCbits.TRISC0 = 1;
            TRISCbits.TRISC1 = 1;
        }

        Delay1KTCYx(30); //15ms delay to let another packet arrive

        if (FLAGbits.Bits.send_temp) {
            // send the temperature
            FLAGbits.Bits.send_temp = 0; // in fact don't need to do it here as it is done at the start of the while loop
            di();
            temperature = get_temp();
            while (Busy1USART());
            Write1USART('a');
            while (Busy1USART());
            Write1USART('A');
            while (Busy1USART());
            Write1USART('C');
            while (Busy1USART());
            Write1USART('D');
            putc_usart(temperature, 4);
            putc_usart(temperature, 4);
            ei();
        }

        Delay1KTCYx(30); //15ms delay to let another packet arrive
        CLRWDT(); //clear the watchdog timer
    } //end of while(1) loop
}

//sends 0b00000 followed by stop_preamble

void send_preamble(void) {
    int i;
    for (i = 0; i < 5; i++) {
        send_bit(0);
    }
    stop_preamble();
}

//sends start then block1 followed by stop

void send_1(void) {
    int i;
    start();
    for (i = 8; i > 0; i--) {
        send_char(block1[i - 1]);
    }
    stop();
}

//sends start then block2 followed by stop

void send_2(void) {
    int i;
    start();
    for (i = 8; i > 0; i--) {
        send_char(block2[i - 1]);
    }
    stop();
}

//sends start then block3 followed by stop_end

void send_3(void) {
    int i;
    start();
    for (i = 19; i > 0; i--) {
        send_char(block3[i - 1]);
    }
    stop_end();
}

void send_bit(char n) {
    //if FLAG...output_RC0 else TRISCbits.TRISC1
    if (FLAGbits.Bits.output_RC0) {
        TRISCbits.TRISC0 = 0; //activate output
    } else {
        TRISCbits.TRISC1 = 0;
    }
    Delay10TCYx(80); // delay 550 microsecs
    if (FLAGbits.Bits.output_RC0) {
        TRISCbits.TRISC0 = 1; //switch off output
    } else {
        TRISCbits.TRISC1 = 1;
    }
    if (n == 0) {
        Delay10TCYx(52); // delay 300 microsecs
    } else {
        Delay10TCYx(185); // delay 1200 microsecs
    }
}

void start(void) {
    if (FLAGbits.Bits.output_RC0) {
        TRISCbits.TRISC0 = 0; //activate output
    } else {
        TRISCbits.TRISC1 = 0;
    }
    Delay100TCYx(69); // delay 3640 microsecs
    Delay10TCYx(5);
    if (FLAGbits.Bits.output_RC0) {
        TRISCbits.TRISC0 = 1; //switch off output
    } else {
        TRISCbits.TRISC1 = 1;
    }
    Delay100TCYx(32); // delay 1600 microsecs
}

void stop(void) {
    if (FLAGbits.Bits.output_RC0) {
        TRISCbits.TRISC0 = 0; //activate output
    } else {
        TRISCbits.TRISC1 = 0;
    }
    Delay10TCYx(85); // delay 550 microsecs
    if (FLAGbits.Bits.output_RC0) {
        TRISCbits.TRISC0 = 1; //switch off output
    } else {
        TRISCbits.TRISC1 = 1;
    }
    Delay1KTCYx(69); // delay 34900 microsecs as 34500 + 400
    Delay10TCYx(80);
}

void stop_preamble(void) {
    if (FLAGbits.Bits.output_RC0) {
        TRISCbits.TRISC0 = 0; //activate output
    } else {
        TRISCbits.TRISC1 = 0;
    }
    Delay10TCYx(85); // delay 550 microsecs
    if (FLAGbits.Bits.output_RC0) {
        TRISCbits.TRISC0 = 1; //switch off output
    } else {
        TRISCbits.TRISC1 = 1;
    }
    Delay1KTCYx(50); // delay 25300 microsecs as 25000 + 300
    Delay10TCYx(39);
}

void stop_end(void) {
    if (FLAGbits.Bits.output_RC0) {
        TRISCbits.TRISC0 = 0; //activate output
    } else {
        TRISCbits.TRISC1 = 0;
    }
    Delay10TCYx(85); // delay 550 microsecs
    if (FLAGbits.Bits.output_RC0) {
        TRISCbits.TRISC0 = 1; //switch off output
    } else {
        TRISCbits.TRISC1 = 1;
    }
}

//sends one character least significant bit first

void send_char(char c) {
    int i;
    for (i = 0; i < 8; i++) {
        if (c & 0x01) {
            send_bit(1);
        } else {
            send_bit(0);
        }
        c = c >> 1;
    }
}

void update_checksum(void) {
    int i;
    unsigned char c = 0x00;
    for (i = 1; i < 19; i++) {
        c += (block3[i]);
    }
    block3[0] = c;
}

void init(void) {
    int i;
    TRISAbits.TRISA3 = 1; //input for XRF sleep/awake - 0 => asleep, 1=> awake
    TRISAbits.TRISA2 = 1; //input for XRF transmit pin - 0 => finished, 1=> transmitting
    TRISAbits.TRISA5 = 1; //input for analog temperature
    LATB = 0xFE; //set sensor off (RB0 low) and XRF off (RB1 high)
    TRISB = 0x00; //set all B pins as outputs (except 4 & 5 - used for I2C)

    ANCON0 = ADC_PORTS; // configures the analog input ports
    ANCON1 = ADC_PORTS >> 8; // switches on the band gap reference
    WDTCONbits.REGSLP = 1; //allows the on chip regulator to sleep - takes longer to wake

    //start charging the Deep Sleep capacitor
    TRISAbits.TRISA0 = 0;
    PORTAbits.RA0 = 1;
    LPDelay(3); //wait 3 ms - should be enough for 1 microfarad
    for (i = 0; i < 120; i++) {
        in_buf[i] = 0x00;
    }
    in_buf_ptr = 0x00;

    PPSUnLock();
    RPOR11 = 14;
    RPOR12 = 14;
    PPSLock();
}

void init_usart() {
    unsigned char config = 0, spbrg = 0, baudconfig = 0;
    Close1USART(); //turn off usart if was previously on
    //-----configure USART -----
    config = USART_TX_INT_OFF | USART_RX_INT_ON | USART_ASYNCH_MODE | USART_EIGHT_BIT |
            USART_CONT_RX | USART_BRGH_LOW;
    spbrg = 12; //At 8Mhz of oscillator frequency & baud rate of 9600.
    Open1USART(config, spbrg);
    baudconfig = BAUD_8_BIT_RATE | BAUD_AUTO_OFF;
    baud1USART(baudconfig);
    LATC = 0x00;
    TRISC = 0x83; //RC7(RX1) input, RC6(TX1) output. RC0 and RC1 input for now.
}

void putc_usart(unsigned int data, unsigned char num_chars) {
    //    writes num_chars hex ascii characters to serial
    unsigned char x, i = 0;

    for (i = num_chars; i > 0; i--) {
        // Transmit a byte
        x = 0x0F & (data >> 4 * (i - 1));
        if (x > 9) {
            x += 0x37;
        } else {
            x += 0x30;
        }
        while (Busy1USART());
        Write1USART(x);
    }
}

void puts_usart(char * data) {
    //    same as builtin puts1USART() except is doesn't send the end null
    while (*data) {
        // Transmit a byte
        while (Busy1USART());
        putc1USART(*data);
        *data++;
    }
}

unsigned int get_temp(void) {
    unsigned int ADCResult = 0;
    unsigned char config1 = 0x00, config2 = 0x00, i = 0;
    //open ADC
    CloseADC();
    config1 = ADC_FOSC_8 | ADC_RIGHT_JUST | ADC_4_TAD;
    config2 = ADC_CH4 | ADC_INT_OFF | ADC_REF_VDD_VSS;
    OpenADC(config1, config2, ADC_PORTS);
    //sample and convert
    for (i = 0; i < 4; i++) {
        ConvertADC();
        while (BusyADC());
        ADCResult += (unsigned int) ReadADC();
    }

    ADCResult /= 4;
    CloseADC(); //turn off ADC
    //ADCResult = 0x129A;
    return ADCResult;
}

void LPDelay(unsigned char loop) {
    unsigned char i;
    OSCCONbits.IRCF = 0x1; //clocked at 125kHz to reduce power 32microsec per cycle
    //unsigned int loop = 0x03FF;
    for (i = 0; i < loop; i++) _delay3(10); //30 cycles = 1ms
    OSCCONbits.IRCF = 0x7; //clocked at 8MHz
}
