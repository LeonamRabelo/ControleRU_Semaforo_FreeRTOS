#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/pio.h"
#include "ws2812.pio.h"
#include "lib/ssd1306.h"
#include "lib/matriz_leds.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdio.h>

#define MAX_PESSOAS 25
#define BOTAO_A 5
#define BOTAO_B 6
#define BOTAO_JOYSTICK 22
#define LED_R 13
#define LED_G 11
#define LED_B 12
#define BUZZER 21
#define WS2812 7
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define IS_RGBW false
#define ENDERECO_DISPLAY 0x3C

SemaphoreHandle_t semEntrada;
SemaphoreHandle_t semSaida;
SemaphoreHandle_t semReset;
SemaphoreHandle_t mutexDisplay;

volatile uint8_t contador = 0;
uint buzzer_slice;
ssd1306_t ssd;

void init_perifericos(){
    stdio_init_all();
    i2c_init(I2C_PORT, 400000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    ssd1306_init(&ssd, 128, 64, false, ENDERECO_DISPLAY, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_send_data(&ssd);

    gpio_init(LED_R); gpio_set_dir(LED_R, GPIO_OUT);
    gpio_init(LED_G); gpio_set_dir(LED_G, GPIO_OUT);
    gpio_init(LED_B); gpio_set_dir(LED_B, GPIO_OUT);

    gpio_set_function(BUZZER, GPIO_FUNC_PWM);
    buzzer_slice = pwm_gpio_to_slice_num(BUZZER);
    pwm_set_clkdiv(buzzer_slice, 125.0f);
    pwm_set_wrap(buzzer_slice, 999);
    pwm_set_gpio_level(BUZZER, 300);
    pwm_set_enabled(buzzer_slice, false);

    PIO pio = pio0;
    int sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, WS2812, 800000, IS_RGBW);

    gpio_init(BOTAO_A); gpio_set_dir(BOTAO_A, GPIO_IN); gpio_pull_up(BOTAO_A);
    gpio_init(BOTAO_B); gpio_set_dir(BOTAO_B, GPIO_IN); gpio_pull_up(BOTAO_B);
    gpio_init(BOTAO_JOYSTICK); gpio_set_dir(BOTAO_JOYSTICK, GPIO_IN); gpio_pull_up(BOTAO_JOYSTICK);
}

void atualizar_display(const char* msg){
    if(xSemaphoreTake(mutexDisplay, pdMS_TO_TICKS(200))){
        ssd1306_fill(&ssd, false);
        char buffer[32];
        sprintf(buffer, "Usuarios: %d", contador);
        ssd1306_draw_string(&ssd, msg, 10, 10);
        ssd1306_draw_string(&ssd, buffer, 10, 30);
        ssd1306_send_data(&ssd);
        xSemaphoreGive(mutexDisplay);
    }
}

void beep(uint8_t vezes, uint16_t dur_ms){
    for(int i = 0; i < vezes; i++){
        pwm_set_enabled(buzzer_slice, true);
        vTaskDelay(pdMS_TO_TICKS(dur_ms));
        pwm_set_enabled(buzzer_slice, false);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

//Debounce do botão (evita leituras falsas)
bool debounce_botao(uint gpio){
    static uint32_t ultimo_tempo = 0;
    uint32_t tempo_atual = to_ms_since_boot(get_absolute_time());
    //Verifica se o botão foi pressionado e se passaram 200ms
    if (gpio_get(gpio) == 0 && (tempo_atual - ultimo_tempo) > 200){ //200ms de debounce
        ultimo_tempo = tempo_atual;
        return true;
    }
    return false;
}

void gpio_irq_handler(uint gpio, uint32_t events){
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if(gpio == BOTAO_A && debounce_botao(BOTAO_A)){
        xSemaphoreGiveFromISR(semEntrada, &xHigherPriorityTaskWoken);
    }else if(gpio == BOTAO_B && debounce_botao(BOTAO_B)){
        xSemaphoreGiveFromISR(semSaida, &xHigherPriorityTaskWoken);
    }else if(gpio == BOTAO_JOYSTICK && debounce_botao(BOTAO_JOYSTICK)){
        xSemaphoreGiveFromISR(semReset, &xHigherPriorityTaskWoken);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void vTaskEntrada(void *param){
    while(1){
        if(xSemaphoreTake(semEntrada, portMAX_DELAY) == pdTRUE){
            if(contador < MAX_PESSOAS){
                contador++;
                gpio_put(LED_G, 1);
                beep(1, 200);
                gpio_put(LED_G, 0);
                atualizar_display("Entrada realizada");
            } else {
                atualizar_display("Capacidade cheia!");
                beep(1, 100);
            }
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }
}

void vTaskSaida(void *param){
    while(1){
        if(xSemaphoreTake(semSaida, portMAX_DELAY) == pdTRUE){
            if(contador > 0){
                contador--;
                for(int i = 0; i < 3; i++){
                    gpio_put(LED_R, 1);
                    beep(1, 100);
                    gpio_put(LED_R, 0);
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                atualizar_display("Saida registrada");
            } else {
                atualizar_display("Ninguem no local!");
            }
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }
}

void vTaskReset(void *param){
    while(1){
        if(xSemaphoreTake(semReset, portMAX_DELAY) == pdTRUE){
            contador = 0;
            gpio_put(LED_B, 1);
            pwm_set_enabled(buzzer_slice, true);
            atualizar_display("Reset realizado");
            vTaskDelay(pdMS_TO_TICKS(2000));
            pwm_set_enabled(buzzer_slice, false);
            gpio_put(LED_B, 0);
        }
    }
}

void vTaskRGB(void *param){
    while(1){
        gpio_put(LED_R, 0); gpio_put(LED_G, 0); gpio_put(LED_B, 0);
        if(contador == 0) gpio_put(LED_B, 1);
        else if(contador < MAX_PESSOAS - 1) gpio_put(LED_G, 1);
        else if(contador == MAX_PESSOAS - 1) { gpio_put(LED_R, 1); gpio_put(LED_G, 1); }
        else gpio_put(LED_R, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void vTaskMatriz(void *param){
    while(1){
        mostrar_ocupacao_matriz(contador);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

int main(){
    init_perifericos();

    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "Bem-vindo!", 10, 10);
    ssd1306_draw_string(&ssd, "Controle RU", 10, 30);
    ssd1306_send_data(&ssd);

    semEntrada = xSemaphoreCreateCounting(25, 0);
    semSaida = xSemaphoreCreateCounting(25, 0);
    semReset = xSemaphoreCreateBinary();
    mutexDisplay = xSemaphoreCreateMutex();

    gpio_set_irq_enabled_with_callback(BOTAO_A, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(BOTAO_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(BOTAO_JOYSTICK, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    xTaskCreate(vTaskEntrada, "Entrada", 256, NULL, 1, NULL);
    xTaskCreate(vTaskSaida, "Saida", 256, NULL, 1, NULL);
    xTaskCreate(vTaskReset, "Reset", 256, NULL, 2, NULL);
    xTaskCreate(vTaskRGB, "RGB", 256, NULL, 1, NULL);
    xTaskCreate(vTaskMatriz, "Matriz", 256, NULL, 1, NULL);

    vTaskStartScheduler();
    while (true) {}
}