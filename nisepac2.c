//  PASOPIA PAC2 emulator for Pico2
//
//  PAC2 connector
//  GP0-7: CDB0-7
//  GP8: CDAD0
//  GP9: CDAD1
//  GP10: CSELP2
//  GP11: CDWR
//  GP12: CDRD


#define FLASH_INTERVAL 300      // 5sec

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/sync.h"
//#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"
#include "hardware/uart.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"

#include "tusb.h"
#include "bsp/board.h"

// ROM configuration

#define ROMBASE 0x10080000u

uint8_t *rompac  =(uint8_t *)(ROMBASE-0x30000);
uint8_t *kanjirom=(uint8_t *)(ROMBASE-0x20000);
uint8_t *rampacs =(uint8_t *)(ROMBASE);

// RAM configuration (224KiB)

//uint8_t rampac0[0x10000];   // For Utilities (slot 5)
uint8_t rampac1[0x10000];   // Slot 4
uint8_t rampac2[0x10000];   // Slot 3
uint8_t kanjipac[0x20000];  // Slot 2

#define MAXPACPAGE 56       // = 3.5MiB/64KiB

volatile uint8_t pacmode=0;

uint32_t rampac1ptr=0;
uint32_t rampac2ptr=0;
uint32_t kanjipacptr=0;

volatile uint8_t rampac1page=0;
volatile uint8_t rampac2page=1;

// 32K RAMPAC Header
static const uint8_t rampac32_header[16] = {
	0xaa, 0x1f, 0x04, 0x00, 0x04, 0x80, 0x00, 0x01, 0x04, 0x04, 0x01, 0x03, 0x08, 0x00, 0x00, 0x00
};

// 64K RAMPAC Header
static const uint8_t rampac64_header[16] = {
	0xaa, 0x1f, 0x08, 0x00, 0x04, 0x80, 0x00, 0x02, 0x04, 0x04, 0x01, 0x03, 0x10, 0x00, 0x00, 0x00
};

// USB

hid_keyboard_report_t prev_report = { 0, 0, {0} }; // previous report to check key released
extern void hid_app_task(void);

uint32_t usbcheck_count=0;
uint32_t kbhit=0;            // 4:Key pressed (timer stop)/3&2:Key depressed (timer running)/1:no key irq triggerd
uint8_t hid_dev_addr=255;
uint8_t hid_instance=255;
uint8_t hid_led;

extern volatile uint8_t gamepad_info; 
uint8_t gamepad_select;

struct repeating_timer timer;
volatile uint32_t vscount=0;
volatile uint32_t vsflag=0;
volatile uint32_t rampac1count=0;
volatile uint32_t rampac1save=0;
volatile uint32_t rampac2count=0;
volatile uint32_t rampac2save=0;

//

volatile uint32_t flashcommand=0;

uint8_t __attribute__  ((aligned(sizeof(unsigned char *)*4096))) flash_buffer[4096];

//
//  Timer

bool __not_in_flash_func(timer_handler)(struct repeating_timer *t) {

    vscount++;
    vsflag=1;

    return true;
}

int32_t rampac_compare(uint32_t rampacno,uint32_t pageno) {

    uint8_t pacdata,flashdata;
    int32_t match;

    match=0;

    for(int i=0;i<0x10000;i++) {

        if(rampacno==1) {
            pacdata=rampac1[i];
        } else if(rampacno==2) {
            pacdata=rampac2[i];
        }       

        flashdata=rampacs[pageno*0x10000+i];

        if(pacdata!=flashdata) {
            match=1;
        }

    }

    return match;

}

void rampac_write(uint32_t rampacno,uint32_t pageno) {

    uint8_t pacdata;

        for(int i=0;i<0x10000;i+=4096) {
            uint32_t ints = save_and_disable_interrupts();   
//            multicore_lockout_start_blocking();     // pause another core
            flash_range_erase(pageno*0x10000 + i + 0x80000, 4096);  
//            multicore_lockout_end_blocking();
            restore_interrupts(ints);
        }


        for(int i=0;i<0x10000;i+=4096) {
            if(rampacno==1) {
                memcpy(flash_buffer,rampac1+i,4096);
            } else if (rampacno==2) {
                memcpy(flash_buffer,rampac2+i,4096);               
            }

            uint32_t ints = save_and_disable_interrupts();
//            multicore_lockout_start_blocking();     // pause another core
            flash_range_program(pageno*0x10000 + i + 0x80000, (const uint8_t *)flash_buffer, 4096);
//            multicore_lockout_end_blocking();
            restore_interrupts(ints);

        }

    return;

}

void init_rampac(uint8_t pac) {

    if(pac==1) {
        memset(rampac1, 0, sizeof(rampac1));
        memcpy(rampac1, rampac64_header, sizeof(rampac64_header));
//            memcpy(rampac1, rampac32_header, sizeof(rampac32_header));
        memset(rampac1 + 0x20, 0xff, 0x200);
        memset(rampac1 + 0x300, 0xfe, 0x004);
        memset(rampac1 + 0x304, 0xff, 0x0fc);
    } else {
        memset(rampac2, 0, sizeof(rampac2));
        memcpy(rampac2, rampac64_header, sizeof(rampac64_header));
//            memcpy(rampac2, rampac32_header, sizeof(rampac32_header));
        memset(rampac2 + 0x20, 0xff, 0x200);
        memset(rampac2 + 0x300, 0xfe, 0x004);
        memset(rampac2 + 0x304, 0xff, 0x0fc);
    }

    return;

}

uint8_t  rampac_load(uint8_t pac,uint8_t page) {

    if(page>MAXPACPAGE) {
        return 1;
    }

    // Cannot mount same rampac on both slots

    if((pac==1)&&(rampac2page==page)) {
        return 1;
    }

    if((pac==2)&&(rampac1page==page)) {
        return 1;
    }   

    // NEW PAC ?

    if(pac==1) {
        memcpy(rampac1,rampacs+page*0x10000,0x10000);
        if(rampac1[0]!=0xaa) {
            init_rampac(1);
        }
    } else {
        memcpy(rampac2,rampacs+page*0x10000,0x10000);
        if(rampac2[0]!=0xaa) {
            init_rampac(2);
        }
    }

    return 0;

}

static inline uint8_t io_read( uint16_t address)
{

    uint8_t b;

    switch(pacmode) {

        case 1:

            if(address==2) {    //  joystick 1
                return gamepad_info;
            }

            return 0xff;


        case 2:

//            return kanjirom[kanjipacptr&0x1ffff];
            return kanjipac[kanjipacptr&0x1ffff];

        case 3:

            return rampac2[rampac2ptr&0xffff];

        case 4:

            return rampac1[rampac1ptr&0xffff];

        case 5:  // NisePAC2 control

            switch(address) {

                case 0:

                    return(rampac1page);

                case 1:

                    return(rampac2page);

                case 2: // Not saved ?

                    b=0;

                    if(rampac1count>rampac1save) b|=1;
                    if(rampac2count>rampac2save) b|=2;

                    return b;

                default:

                    return 0xff;

            }


        default:

            return 0xff;

    }

    return 0xff;
}

static inline void io_write(uint16_t address, uint8_t data)
{

    uint8_t b;

    switch(address&3) {

        case 0:  // PAC (Low)

            switch(pacmode) {

                case 2:
                    kanjipacptr&=0x1ff00;
                    kanjipacptr|=data;
                    break;

                case 3:
                    rampac2ptr&=0xff00;
                    rampac2ptr|=data;
                    break;
                    
                case 4:
                    rampac1ptr&=0xff00;
                    rampac1ptr|=data;
                    break;

                case 5:  // NisePAC2 control (Change slot 4)

                    flashcommand=0x100+data;

                    break;

                default:

            }

            return;

        case 1:  // PAC (Middle)

            switch(pacmode) {

                case 2:
                    kanjipacptr&=0x100ff;
                    kanjipacptr|=data<<8;  
                    break;

                case 3:
                    rampac2ptr&=0x00ff;
                    rampac2ptr|=data<<8;
                    break;
                    
                case 4:
                    rampac1ptr&=0x00ff;
                    rampac1ptr|=data<<8;
                    break;

                case 5:  // NisePAC2 control (Change slot 3)


                    flashcommand=0x200+data;

                    break;


                default:

            }

            return;
        
        case 2:  // PAC (High or DATA write) 

            switch(pacmode) {

                case 2:
                    kanjipacptr&=0xffff;
                    kanjipacptr|=data<<16;  
                    kanjipacptr&=0x1ffff;
                    break;

                case 3:

                    if(rampac2[rampac2ptr&0xffff]!=data) {
                        rampac2[rampac2ptr&0xffff]=data;
                        rampac2count=vscount;
                    }
                    break;
                    
                case 4:
                    if(rampac1[rampac1ptr&0xffff]!=data) {
                        rampac1[rampac1ptr&0xffff]=data;
                        rampac1count=vscount;
                    }
                    break;

                default:

            }

            return;
        
        case 3:  // SLOT

            if((data&0x80)==0) {
                pacmode=data;
            }

            return;

    }

    return;

}

void init_emulator(void) {

    // Erase Gamepad info

    gamepad_info=0xff;

    // Copy KANJI ROM to RAM

    memcpy(kanjipac,kanjirom,0x20000);

}

// Main thread (Core1)

void __not_in_flash_func(main_core1)(void) {
//void main_core1(void) {

    volatile uint32_t bus;

    uint32_t control,address,data;

//    uint32_t redraw_start,redraw_length;

//    multicore_lockout_victim_init();

    gpio_init_mask(0x1fff);
    gpio_set_dir_all_bits(0xffffe000);  

    while(1) {

        // Wait CSELP2 selected

        bus=gpio_get_all();

        control=bus&0x1c00;

        if(control==0x1000) { // Write

//      WAIT 40ns
            asm volatile("nop \n nop \n nop");
            asm volatile("nop \n nop \n nop");
            asm volatile("nop \n nop \n nop");
            asm volatile("nop \n nop \n nop");

            bus=gpio_get_all();

            address=(bus&0x300)>>8;
            data=bus&0xff;

            io_write(address,data);

            // Wait deactivate CDWR
            control=0;
            while(control==0) {
                bus=gpio_get_all();
                control=bus&0x800;
            }

            continue;

        } else if(control==0x800) { // Read

//      WAIT 40ns
            asm volatile("nop \n nop \n nop");
            asm volatile("nop \n nop \n nop");
            asm volatile("nop \n nop \n nop");
            asm volatile("nop \n nop \n nop");

            bus=gpio_get_all();


            address=(bus&0x300)>>8;

            // Set GP0-7 to OUTPUT

            gpio_set_dir_masked(0xff,0xff);

            data=io_read(address);

            // Set Data

            gpio_put_masked(0xff,data);

            // Wait deactivate CDRD
            control=0;
            while(control==0) {
                bus=gpio_get_all();
                control=bus&0x1000;
            }

            // Set GP0-7 to INPUT

            gpio_set_dir_masked(0xff,0x00);

        }
        
    }
}

int main() {

    uint32_t menuprint=0;
    uint32_t filelist=0;
    uint32_t subcpu_wait;
    uint32_t rampacno;
    uint32_t pacpage;

    static uint32_t hsync_wait,vsync_wait;

    vreg_set_voltage(VREG_VOLTAGE_1_20);  // for overclock to 300MHz
    set_sys_clock_khz(300000 ,true);

    tuh_init(BOARD_TUH_RHPORT);


    multicore_launch_core1(main_core1);
//    multicore_lockout_victim_init();

    init_emulator();

    rampac_load(1,rampac1page);
    rampac_load(2,rampac2page);

    // Sub thread (TinyUSB & Flash handling)

    // Set timer (1/60 sec) for USB scanning

    add_repeating_timer_us(16666,timer_handler,NULL  ,&timer);

    while(1) {
        
        while(vsflag==0) {

            // Change RAMPAC page
            if(flashcommand) {

                // SAME PAGE ?

                rampacno=(flashcommand&0xff00)>>8;
                pacpage=flashcommand&0xff;

                flashcommand=0;

                if(rampacno==1) {
                    if(pacpage==rampac1page) {
                        break;
                    }
                    if(pacpage==rampac2page) {
                        break;
                    }

                    if(rampac_compare(1,rampac1page)!=0) {
                        rampac_write(1,rampac1page);
                    }

                    if(rampac_load(1,pacpage)==0) {
                        rampac1page=pacpage;
                    }
                    rampac1count=vscount;

                } else if(rampacno==2) {
                    if(pacpage==rampac1page) {
                        break;
                    }
                    if(pacpage==rampac2page) {
                        break;
                    }

                    if(rampac_compare(2,rampac2page)!=0) {
                        rampac_write(2,rampac2page);
                    }

                    if(rampac_load(2,pacpage)==0) {
                        rampac2page=pacpage;
                    }
                    rampac2count=vscount;

                }


                flashcommand=0;

            }

        }

        vsflag=0;
        tuh_task();

        // Save flash task

        if(rampac1count>rampac1save) {
            if((vscount-rampac1count)>FLASH_INTERVAL) {
                if(rampac_compare(1,rampac1page)!=0) {
                    rampac_write(1,rampac1page);                    
                }
                rampac1save=vscount;
            }
        }

        if(rampac2count>rampac2save) {
            if((vscount-rampac2count)>FLASH_INTERVAL) {
                if(rampac_compare(2,rampac2page)!=0) {
                    rampac_write(2,rampac2page);                     
                }
                rampac2save=vscount;
            }
        }
    }
}

