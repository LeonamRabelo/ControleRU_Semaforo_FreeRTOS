#include "hardware/pio.h"
#include "ws2812.pio.h"
#include "matriz_leds.h"

//Função para ligar um LED
static inline void put_pixel(uint32_t pixel_grb){
    pio_sm_put_blocking(pio0, 0, pixel_grb << 8u);
}

//Função para converter cores RGB para um valor de 32 bits
static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b){
    return ((uint32_t)(r) << 8) | ((uint32_t)(g) << 16) | (uint32_t)(b);
}

#define TOTAL_PIXELS 25

//Função para setar os LEDs da matriz conforme ocupação
void mostrar_ocupacao_matriz(uint8_t ocupacao){
    uint32_t cor = urgb_u32(0, 150, 150);

    for(int i = 0; i < TOTAL_PIXELS; i++){
        if(i < ocupacao){
            put_pixel(cor);  // LED ligado
        }else{
            put_pixel(0);    // LED desligado
        }
    }
}