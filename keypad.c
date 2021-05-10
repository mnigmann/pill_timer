/**
 * F7:  R0
 * F6:  R1
 * F5:  R2
 * F4:  R3
 * F3:  C0
 * F2:  C1
 * F1:  C2
 * F0:  C3
 * 
 * PORTL = {D7, D6, D5, D4,,,RS, E}
 * PORTB = {buzzer,,,backlight,,,,}
 * 
 * Press * to stop alarm
 */

#define BUZZER_PERIOD       1.5
#define BUZZER_ON           0.75
#define BUZZER_TOTAL        20

#define BACKLIGHT_ON        120
#define BACKLIGHT_DIM       31
#define BACKLIGHT_BRIGHT    255

//#define TEST

#define TIME(h,m,s) (3600UL*h+60*m+s)



#include <avr/io.h>
#include <avr/interrupt.h>
#include <string.h>

volatile uint8_t    flags = 0b00000000;             // {,,,,enableSets,buzzer,backlight,sendFromTx}

         char       txbuf[32];
volatile uint8_t    txbuflen;
volatile uint8_t    txidx;
volatile char       lcdbuf[16];
volatile uint8_t    lcdbuflen;

uint8_t             delay;

const char          symlut[16]  =  "123A456B789C*0#D";
volatile uint16_t   mask = 0b0000000000000000;
volatile uint16_t   lastmask = 0;

// Delay counters
volatile uint32_t   set1;
volatile uint32_t   set1_to_afterBreakfast;
volatile uint32_t   afterBreakfast;
volatile uint32_t   beforeDinner;
volatile uint32_t   currentTime = 0;                // COMPTIME is an argument to the compiler
volatile uint8_t    timer_states = 0b00000000;      // {,gab_second_try, set1_to_afterBreakfast, beforeDinner, afterBreakfast_b,afterBreakfast_a, set1_b,set1_a}

volatile uint16_t   backlight_timer;
volatile uint16_t   buzzer_timer;

         uint8_t    time_in_buf[4];
volatile uint8_t    time_in_buflen;


void printnum_noblock(uint16_t num, uint8_t bits, uint8_t offset) {
    for (uint8_t x=0; x<bits; x++) {
        txbuf[x+offset] = ((num & (1<<(bits-1)))?'1':'0');
        num = num << 1;
    }
    txbuf[offset + bits] = '\r';
    txbuf[offset + bits + 1] = '\n';
    UDR0 = txbuf[0];
    txbuflen = offset + bits + 2;
    txidx = 1;
}

void printdec(uint32_t num, uint8_t digits, uint8_t offset) {
    for (uint8_t x=0; x<digits; x++) {
        txbuf[offset+digits-1-x] = '0' + (num % 10);
        num = num / 10;
    }
    UDR0 = txbuf[0];
    txidx = 1;
}

void send_nibble(uint8_t byte) {
    uint8_t temp = TCCR0B;
    TCCR0B = 0b00000010;    // divide by 8
    OCR0A  = 3;             // 2us
    
    PORTL  = byte | 0b00000001;
    TCNT0  = 0;
    while (!(TIFR0 & 0b00000010));
    TIFR0 |= 0b00000010;
    
    PORTL  = byte;
    OCR0A  = 79;            // 80us
    TCNT0  = 0;
    while (!(TIFR0 & 0b00000010));
    TIFR0 |= 0b00000010;
    
    TCCR0B = temp;
}


void send(uint8_t byte, uint8_t rs) {
    send_nibble((byte & 0xf0) | (rs?0x02:0x00));
    byte = byte << 4;
    send_nibble((byte & 0xf0) | (rs?0x02:0x00));
}

void send_from_txbuf() {
    uint8_t n = 0;
    for (n=0; n<lcdbuflen; n++) {
        send(lcdbuf[n], 1);
    }
}

void send_from_string(char* text, uint8_t len) {
    uint8_t n=0;
    for (n=0; n<len; n++) {
        send(text[n], 1);
    }
}


ISR(TIMER3_COMPA_vect) {
    if (DDRF == 0b00001000) {
        DDRF  = 0b00000001;
        // Do actions based on keyboard
        uint16_t mask_diff = ~lastmask & mask;
        lastmask = mask;
        
        txbuflen = 0;
        
        if (flags & 0b00001000) {
            if (mask_diff & 0x8000) {
                // Start set1 and the set1_to_afterBreakfast timer
                timer_states |= 0b00100001;
                //flags |= 0b00000001;
                set1 = 3600*0.5;
                set1_to_afterBreakfast = 3600*1.5;
                send(0x80, 0);
                send_from_string("carba + mido", 12);
                flags |= 0b00000110;
                strcpy(txbuf, "start 1 -----\r\n");
                txbuflen = 15;
                printdec(currentTime, 5, 8);
            } /*else if (mask_diff & 0x2000) {
                // start set 3 (before dinner)
                timer_states |= 0b00010000;
                beforeDinner = 3600*0.5;
                send(0x80, 0);
                send_from_string("omir           ", 15);
                flags |= 0b00000110;
            } else if (mask_diff & 0x4000) {
                // start set 2 (after breakfast)
                timer_states |= 0b00000100;
                afterBreakfast = 3600*6;
                send(0x80, 0);
                send_from_string("gab + AM reg   ", 15);
                flags |= 0b00000110;
            }*/ else if (mask_diff & 0x0008) {
                flags &= 0b11111011;
                PORTB |= 0b10000000;
            }
        } else {
            uint8_t n = 16;
            char l;
            do {
                n--;
                l = symlut[n];
                if ((mask_diff & 1) && (l >= '0') && (l <= '9')) {
                    send(l, 1);
                    time_in_buf[time_in_buflen] = l;
                    time_in_buflen++;
                    if (time_in_buflen == 2) send(':', 1);
                    UDR0 = l;
                    break;
                }
                mask_diff = mask_diff >> 1;
            } while (n > 0);
            if (time_in_buflen == 4) {
                flags |= 0b00001000;
                currentTime = 36000UL*time_in_buf[0] + 3600UL*time_in_buf[1] + 600*time_in_buf[2] + 60*time_in_buf[3];
                send(0x80 | 0x40, 0);
                send_from_string("Next in 00:00:00", 16);
            }
        }
        
        if (mask_diff) {
            OCR2A = BACKLIGHT_BRIGHT;
            flags |= 0b00000010;
        }
        
        
        /*uint8_t n = 16;
        txbuflen = 0;
        do {
            n--;
            if (mask_diff & 1) {
                txbuf[txbuflen] = symlut[n];
                txbuflen++;
            }
            mask_diff = mask_diff >> 1;
        } while (n > 0);
        if (txbuflen > 0) UDR0 = txbuf[0];
        txidx = 1;*/
        mask = 0;
        

        
        if (flags & 0b00001000) {
            // A temporary solution:
            // Whenever the column counter for
            // the keypad overflows, write the
            // time. This will be better than
            // doing it it the "second" counter.
            
            uint32_t temp_time = set1;
            if ( (beforeDinner > 0) && ((beforeDinner < temp_time) || (temp_time == 0)) ) temp_time = beforeDinner;
            if ( (afterBreakfast > 0) && ((afterBreakfast < temp_time) || (temp_time == 0)) ) temp_time = afterBreakfast;
            if ( (set1_to_afterBreakfast > 0) && ((set1_to_afterBreakfast < temp_time) || (temp_time == 0)) ) temp_time = set1_to_afterBreakfast;
            
            uint8_t hours = temp_time/3600;
            uint8_t seconds = temp_time%60;
            uint8_t minutes = (temp_time%3600)/60;
            /*uint8_t thours = currentTime/3600;
            uint8_t tseconds = currentTime%60;
            uint8_t tminutes = (currentTime%3600)/60;*/
            lcdbuf[0] = hours/10 + '0';
            lcdbuf[1] = hours%10 + '0';
            lcdbuf[2] = ':';
            lcdbuf[3] = minutes/10 + '0';
            lcdbuf[4] = minutes%10 + '0';
            lcdbuf[5] = ':';
            lcdbuf[6] = seconds/10 + '0';
            lcdbuf[7] = seconds%10 + '0';
            
            /*lcdbuf[8] = thours/10 + '0';
            lcdbuf[9] = thours%10 + '0';
            lcdbuf[10] = ':';
            lcdbuf[11] = tminutes/10 + '0';
            lcdbuf[12] = tminutes%10 + '0';
            lcdbuf[13] = ':';
            lcdbuf[14] = tseconds/10 + '0';
            lcdbuf[15] = tseconds%10 + '0';*/
            lcdbuflen = 8;
            send(0x80 | 0x48, 0);
            send_from_txbuf();
        }
        
    } else {
        DDRF  = DDRF << 1;
    }
    PORTF = ~DDRF;
    
    // TODO: move to other ISR
    if (flags & 0b00000010) {
        if (backlight_timer == 100*BACKLIGHT_ON) {
            OCR2A = BACKLIGHT_DIM;
            backlight_timer = 0;
            flags &= 0b11111101;
        }
        backlight_timer ++;
    }
    
    // TODO: move to other ISR
    if (flags & 0b00000100) {
        if (buzzer_timer % (uint16_t)(100*BUZZER_PERIOD) < (uint16_t)(100*BUZZER_ON))
            PORTB &= 0b01111111;
        else
            PORTB |= 0b10000000;
        
        if (buzzer_timer == 100*BUZZER_TOTAL) {
            flags &= 0b11111011;
            buzzer_timer = 0;
            PORTB |= 0b10000000;
        }
        buzzer_timer ++;
    }
}

ISR(TIMER3_COMPB_vect) {
    uint8_t inp = ~PINF;
    uint16_t col = (inp & 0b00001111) << 12;
    //mask |= col;
    //printnum_noblock(inp, 8, 0);
    uint8_t _r = 4;
    inp = inp >> 4;
    while (_r > 0) {
        _r--;
        if (inp & (1<<_r)) {
            mask |= col;
        }
        col = col >> 4;
   }
}

ISR(USART0_TX_vect) {
    if (txidx < txbuflen) {
        UDR0 = txbuf[txidx];
        txidx++;
    }
}

// Runs every second (or every 1/1024 second in test mode)
ISR(TIMER1_COMPA_vect) {
    if (timer_states & 0b00000011) {
        set1 --;
        if (set1 == 0) {
            OCR2A = BACKLIGHT_BRIGHT;
            flags |= 0b00000110;
            send(0x80, 0);
            switch (timer_states & 0b00000011) {
                case 0b01:
                    // 30m:     breakfast
                    send_from_string("breakfast      ", 15);
                    timer_states = (timer_states & 0b11111100) | 0b10;
                    set1 = 3600*4;
                    break;
                case 0b10:
                    // 4h30m:   carba + mido + eset
                    send_from_string("carba,mido,eset", 15);
                    timer_states = (timer_states & 0b11111100) | 0b11;
                    // Next dose of carba+omir must be after 17:30
                    set1 = TIME(17, 30, 0) - currentTime;
                    if (set1 < TIME(4, 30, 0)) set1 = TIME(4, 30, 0);
                    break;
                case 0b11:
                    // 9h: carba (spaces to clear display)
                    send_from_string("carba,omir     ", 16);
                    timer_states &= 0b11111100;
                    break;
            }
        }
        if (timer_states & 0b00100000) {
            set1_to_afterBreakfast --;
            if (currentTime == TIME(10, 30, 0)) {
                strcpy(txbuf, "autostart\r\n");
                txbuflen = 11;
                txidx=1;
                UDR0 = txbuf[0];
            }
            if (set1_to_afterBreakfast == 0 || currentTime == TIME(10, 30, 0)) {
                // Set 2 starts either 1.5 hours after set 1, or at 10:30, whichever comes first
                timer_states |= 0b00000100;
                timer_states &= 0b11011111;
                afterBreakfast = TIME(16, 0, 0) - currentTime;
                if (afterBreakfast < TIME(6, 0, 0)) afterBreakfast = TIME(6, 0, 0);
                if (afterBreakfast > TIME(7, 0, 0)) afterBreakfast = TIME(7, 0, 0);
                strcpy(txbuf, "to next -----\r\n");
                txbuflen = 15;
                printdec(afterBreakfast, 5, 8);
                send(0x80, 0);
                send_from_string("gab + AM reg   ", 15);
                flags |= 0b00000110;
            }
        }
    }
    /*if (timer_states & 0b00010000) {
        beforeDinner --;
        if (beforeDinner == 0) {
            OCR2A = BACKLIGHT_BRIGHT;
            flags |= 0b00000110;
            send(0x80, 0);
            send_from_string("dinner         ", 15);
            timer_states &= 0b11101111;
        }
    }*/
    if (timer_states & 0b00001100) {
        if (afterBreakfast > 0) afterBreakfast --;
        if (afterBreakfast == 0) {
            send(0x80, 0);
            switch (timer_states & 0b00001100) {
                case 0b0100:
                    // 6h:      gab
                    /*if (!(timer_states & 0b01000000)) {
                        // Check if it is time to give gab
                        if (currentTime < TIME(16, 0, 0)) {
                            // not time
                            afterBreakfast = TIME(16, 0, 0) - currentTime;
                            afterBreakfast = (afterBreakfast > 3600?3600:afterBreakfast);
                            timer_states |= 0b01000000;
                            break;
                        }
                    }*/
                    strcpy(txbuf, "gab -----\r\n");
                    txbuflen = 11;
                    printdec(currentTime, 5, 4);
                    OCR2A = BACKLIGHT_BRIGHT;
                    flags |= 0b00000110;
                    send_from_string("gab            ", 15);
                    timer_states = (timer_states & 0b10110011) | 0b1000;
                    afterBreakfast = 3600*6;
                    break;
                case 0b1000:
                    // 12h:     gab + PM reg
                    strcpy(txbuf, "gab+p -----\r\n");
                    txbuflen = 13;
                    printdec(currentTime, 5, 6);
                    OCR2A = BACKLIGHT_BRIGHT;
                    flags |= 0b00000110;
                    send_from_string("gab + PM reg   ", 15);
                    timer_states = (timer_states & 0b11110011) | 0b1100;
                    afterBreakfast = 3600*0.5;
                    break;
                case 0b1100:
                    // 12h30m:  clon
                    strcpy(txbuf, "clon -----\r\n");
                    txbuflen = 12;
                    printdec(currentTime, 5, 5);
                    OCR2A = BACKLIGHT_BRIGHT;
                    flags |= 0b00000110;
                    send_from_string("clon           ", 15);
                    timer_states &= 0b11110011;
                    break;
            }
        }
    }
    currentTime = (currentTime+1)%(3600UL*24);
}




int main() {
    DDRB  |= 0b10010000;
    PORTB |= 0b10000000;
    DDRL   = 0b11110011;
    PORTL  = 0b00000000;

    DDRF   = 0b00000001;
    PORTF  = 0b11111111;
    TCCR3A = 0b00000000;
    TCCR3B = 0b00001100;
    TIMSK3 = 0b00000110;
    OCR3A  = 624;
    OCR3B  = 312;
    
    // misc. delay counter
    TCCR0A = 0b00000000;
    TCCR0B = 0b00000010;    // divide by 8
    
    // Second counter
    TCCR1A = 0b00000000;
#ifdef TEST
    TCCR1B = 0b00001001;    // ivide by 1 (00001001) for faster testing
#else
    TCCR1B = 0b00001101;    // divide by 1024 (00001101)
#endif
    TCCR1C = 0b00000000;
    TIMSK1 = 0b00000010;
    OCR1A  = 15624;         // Will trigger COMPA once a second
    
    UBRR0  = 16;
    UCSR0A = 0b00000010;
    UCSR0B = 0b01011000;
    UCSR0C = 0b00000110;
    
    TCCR2A = 0b10000011;
    TCCR2B = 0b00000010;
    OCR2A  = 31;
    
    sei();
    
    TCCR0B = 0b00000101;    // divide by 1024
    OCR0A  = 124;           // 8000us
    
    send_nibble(0x30);          // init
    TCNT0  = 0;
    while (!(TIFR0 & 0b00000010));
    TIFR0 |= 0b00000010;
    
    send_nibble(0x30);          // init
    OCR0A  = 255;           // 128us
    TCCR0B = 0b00000010;    // divide by 8
    TCNT0  = 0;
    while (!(TIFR0 & 0b00000010));
    TIFR0 |= 0b00000010;
    
    send_nibble(0x30);          // init
    TCNT0  = 0;
    while (!(TIFR0 & 0b00000010));
    TIFR0 |= 0b00000010;
    
    send_nibble(0x20);          // 4-bit mode
    send(0x28, 0);
    send(0x0c, 0);              // Enable the screen
    send(0x01, 0);              // clear the screen
    TCCR0B = 0b00000101;    // divide by 1024
    OCR0A  = 124;           // 8000us
    
    TCNT0  = 0;
    while (!(TIFR0 & 0b00000010));
    TIFR0 |= 0b00000010;
    send(0x06, 0);
    
    send(0x80 | 0x40, 0);
    send_from_string("Set time: 00:00", 15);
    send(0x80 | 0x4A, 0);
    
    
    while (1) {
        if (flags & 0b00000001) {
            send_from_txbuf();
            flags &= 0b11111110;
        }
    }
}
