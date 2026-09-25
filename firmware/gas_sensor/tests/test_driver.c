#include "sgp30.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static uint8_t written[8], response[9];
static size_t written_size;
static unsigned slept;
static int io_error;
static int wr(void *ctx,const uint8_t *buf,size_t n) {
    (void)ctx; memcpy(written,buf,n); written_size=n; return io_error;
}
static int rd(void *ctx,uint8_t *buf,size_t n) {
    (void)ctx; memcpy(buf,response,n); return io_error;
}
static void delay(unsigned ms) { slept=ms; }
static void set_word(size_t i,uint16_t value) {
    response[3*i]=(uint8_t)(value>>8); response[3*i+1]=(uint8_t)value;
    response[3*i+2]=sgp30_crc(response+3*i,2);
}
int main(void) {
    sgp30_t s={NULL,wr,rd,delay};
    const uint8_t vector[]={0xbe,0xef};
    assert(sgp30_crc(vector,2)==0x92);
    assert(sgp30_init(&s)==0 && written_size==2 && written[0]==0x20 && written[1]==0x03 && slept>=10);
    set_word(0,415); set_word(1,17);
    uint16_t eco2=0,tvoc=0;
    assert(sgp30_measure(&s,&eco2,&tvoc)==0 && eco2==415 && tvoc==17 && slept>=12);
    response[5]^=1; eco2=123; tvoc=456;
    assert(sgp30_measure(&s,&eco2,&tvoc)==SGP_CRC && eco2==123 && tvoc==456);
    io_error=1;
    assert(sgp30_measure(&s,&eco2,&tvoc)==SGP_IO && eco2==123);
    io_error=0;
    assert(sgp30_set_baseline(&s,0x1234,0xabcd)==0);
    assert(written_size==8 && written[1]==0x1e && written[2]==0xab && written[3]==0xcd);
    assert(written[5]==0x12 && written[6]==0x34);
    assert(written[4]==sgp30_crc(written+2,2) && written[7]==sgp30_crc(written+5,2));
    assert(sgp30_set_humidity(&s,15.5f)==0 && written[2]==0x0f && written[3]==0x80);
    assert(sgp30_set_humidity(&s,NAN)==SGP_ARGUMENT);
    assert(sgp30_set_humidity(&s,-1)==SGP_ARGUMENT);
    assert(sgp30_set_humidity(&s,256)==SGP_ARGUMENT);
    assert(sgp30_set_humidity(&s,0)==0 && written[2]==0 && written[3]==0);
    set_word(0,0xd400); assert(sgp30_selftest(&s)==0 && slept>=220);
    set_word(0,0); assert(sgp30_selftest(&s)==SGP_SELFTEST);
    assert(sgp30_measure(&s,NULL,&tvoc)==SGP_ARGUMENT);
    puts("SGP30 driver protocol tests passed");
}
