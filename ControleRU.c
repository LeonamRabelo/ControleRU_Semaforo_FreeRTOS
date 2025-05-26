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

#define MAX_PESSOAS 25              //Quantidade de pessoas no restaurante
#define BOTAO_A 5                   //Pino do botao A
#define BOTAO_B 6                   //Pino do botao B
#define BOTAO_JOYSTICK 22           //Pino do botao joystick
#define LED_R 13                    //Pino do led vermelho
#define LED_G 11                    //Pino do led verde
#define LED_B 12                    //Pino do led azul
#define BUZZER 21                   //Pino do buzzer
#define WS2812 7                    //Pino da matriz de LEDs
#define I2C_PORT i2c1               //Porta I2C
#define I2C_SDA 14                  //Pino SDA, dados
#define I2C_SCL 15                  //Pino SCL, clock
#define IS_RGBW false
#define ENDERECO_DISPLAY 0x3C       //Endereco I2C do display

SemaphoreHandle_t xSemContagem;      //Semaforo de contagem para controlar o acesso ao contador
SemaphoreHandle_t xSemReset;         //Semaforo binary para controlar o acesso ao reset
SemaphoreHandle_t xMutexDisplay;     //Mutex para controlar o acesso ao display

volatile uint8_t contador = 0;      //Contador de pessoas no restaurante, variavel global
uint buzzer_slice;                  //Slice do buzzer
ssd1306_t ssd;                      //Estrutura do display

//Função para modularizar a inicialização dos perifericos
void init_perifericos(){
    stdio_init_all();

    //Inicializa o display via I2C
    i2c_init(I2C_PORT, 400000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    ssd1306_init(&ssd, 128, 64, false, ENDERECO_DISPLAY, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_send_data(&ssd);

    //Inicializa os leds RGB
    gpio_init(LED_R); gpio_set_dir(LED_R, GPIO_OUT);
    gpio_init(LED_G); gpio_set_dir(LED_G, GPIO_OUT);
    gpio_init(LED_B); gpio_set_dir(LED_B, GPIO_OUT);

    //Inicializa o buzzer via PWM
    gpio_set_function(BUZZER, GPIO_FUNC_PWM);
    buzzer_slice = pwm_gpio_to_slice_num(BUZZER);
    pwm_set_clkdiv(buzzer_slice, 125.0f);
    pwm_set_wrap(buzzer_slice, 999);
    pwm_set_gpio_level(BUZZER, 300);
    pwm_set_enabled(buzzer_slice, false);

    //Inicializa a matriz de LEDs via PIO
    PIO pio = pio0;
    int sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, WS2812, 800000, IS_RGBW);

    //Inicializa os botoes
    gpio_init(BOTAO_A); gpio_set_dir(BOTAO_A, GPIO_IN); gpio_pull_up(BOTAO_A);
    gpio_init(BOTAO_B); gpio_set_dir(BOTAO_B, GPIO_IN); gpio_pull_up(BOTAO_B);
    gpio_init(BOTAO_JOYSTICK); gpio_set_dir(BOTAO_JOYSTICK, GPIO_IN); gpio_pull_up(BOTAO_JOYSTICK);
}

//Funcao para atualizar o display
void atualizar_display(const char* msg){
    if(xSemaphoreTake(xMutexDisplay, pdMS_TO_TICKS(200))){    //Tenta pegar o mutex
        ssd1306_fill(&ssd, false);  //Limpa o display
        //Borda
        ssd1306_rect(&ssd, 0, 0, 128, 64, true, false);
        ssd1306_rect(&ssd, 1, 1, 128 - 2, 64 - 2, true, false);
        ssd1306_rect(&ssd, 2, 2, 128 - 4, 64 - 4, true, false);
        ssd1306_rect(&ssd, 3, 3, 128 - 6, 64 - 6, true, false);

        char buffer[32];            //Buffer para armazenar a string
        sprintf(buffer, "Clientes: %d", contador);  //Formata a string
        ssd1306_draw_string(&ssd, msg, 10, 10);     //Desenha a string
        ssd1306_draw_string(&ssd, buffer, 10, 30);  //Desenha a string
        ssd1306_send_data(&ssd);                    //Envia os dados para o display
        xSemaphoreGive(xMutexDisplay);               //Libera o mutex
    }
}

//Funcao para tocar o buzzer n vezes de duracao em ms
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

//Funcao de interrupcao do botao, no caso, do joystick
void gpio_irq_handler(uint gpio, uint32_t events){
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;  
    if (gpio == BOTAO_JOYSTICK && debounce_botao(BOTAO_JOYSTICK)){
        xSemaphoreGiveFromISR(xSemReset, &xHigherPriorityTaskWoken);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

//Tarefa para o botao de entrada de pessoas
void vTaskEntrada(void *param){
    bool ultimo_estado = true;  //Variavel para armazenar o ultimo estado do botao
    while(1){
        bool estado_atual = gpio_get(BOTAO_A);  //Le o estado do botao
        if(!estado_atual && ultimo_estado){ //Pressionado
            if(contador < MAX_PESSOAS && xSemaphoreTake(xSemContagem, 0) == pdTRUE){ //Verifica se o local nao esta cheio
                contador++;                                             //Incrementa o contador
                gpio_put(LED_G, 1);                                     //Liga o led verde
                beep(1, 400);                                           //Toca o buzzer com um beep de 400ms (mais longo)
                gpio_put(LED_G, 0);                                     //Desliga o led verde
                atualizar_display("Entrada feita!");                    //Atualiza o display com a mensagem
            }else{                                                      //Se o local estiver cheio
                atualizar_display("Local cheio!");                      //Atualiza o display com a mensagem
                beep(1, 100);                                           //Toca o buzzer com um beep de 100ms (mais curto)
            }
            vTaskDelay(pdMS_TO_TICKS(300));  //Debounce do botao
        }
        ultimo_estado = estado_atual;                   //Armazena o ultimo estado
        vTaskDelay(pdMS_TO_TICKS(20));                  //Delay
    }
}

//Tarefa para o botao de saida de pessoas
void vTaskSaida(void *param){
    bool ultimo_estado = true;      //Variavel para armazenar o ultimo estado do botao
    while(1){
        bool estado_atual = gpio_get(BOTAO_B);  //Le o estado do botao
        if (!estado_atual && ultimo_estado){ // Pressionado
            if(contador > 0){               //Verifica se o local nao esta vazio
                contador--;                 //Decrementa o contador
                xSemaphoreGive(xSemContagem);        //Libera espaço no semaforo de contagem
                for(int i = 0; i < 3; i++){         //Toca o buzzer 3 vezes de 100ms
                    gpio_put(LED_R, 1);             //Liga o led vermelho
                    beep(1, 100);                   //Toca o buzzer
                    gpio_put(LED_R, 0);             //Desliga o led vermelho
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                atualizar_display("Saida feita!");  //Atualiza o display com a mensagem
            }else{                                  //Se o local estiver vazio
                atualizar_display("Local vazio!");  //Atualiza o display com a mensagem
                beep(1, 100);
            }
            vTaskDelay(pdMS_TO_TICKS(300));  //Debounce
        }
        ultimo_estado = estado_atual;       //Armazena o ultimo estado
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

//Tarefa para o botao de reset do sistema (Joystick)
void vTaskReset(void *param){
    while(1){
        if(xSemaphoreTake(xSemReset, portMAX_DELAY) == pdTRUE){  //Verifica se o botao foi pressionado
            while(contador > 0){    //Enquanto o contador for maior que 0
                xSemaphoreGive(xSemContagem);    //Libera o semaforo de contagem
                contador--;                     //Decrementa o contador
            }
        gpio_put(LED_B, 1); //Liga o led azul
        beep(2, 300);       //Toca o buzzer 2 vezes de 300ms
        atualizar_display("Reset feito!");  //Atualiza o display com a mensagem
        gpio_put(LED_B, 0);     //Desliga o led azul
        }
    }
}

//Tarefa para controlar o led RGB
void vTaskRGB(void *param){
    while(1){
        //Inicia os leds vermelho, verde e azul desligados
        gpio_put(LED_R, 0);
        gpio_put(LED_G, 0);
        gpio_put(LED_B, 0);
        if(contador == 0) gpio_put(LED_B, 1);   //Se o contador for 0, liga o led azul
        else if(contador < MAX_PESSOAS - 1) gpio_put(LED_G, 1); //Se o contador for menor que MAX_PESSOAS - 1, liga o led verde
        else if(contador == MAX_PESSOAS - 1){                   //Se o contador for igual a MAX_PESSOAS - 1 -> 24 pessoas
            gpio_put(LED_R, 1);     //Liga o led vermelho
            gpio_put(LED_G, 1);     //Liga o led verde
        }
        else gpio_put(LED_R, 1);        //Se o contador for maior que MAX_PESSOAS - 1, liga o led vermelho

        vTaskDelay(pdMS_TO_TICKS(200));     //Delay
    }
}

//Tarefa para controlar a matriz de LEDs
void vTaskMatriz(void *param){
    while(1){
        mostrar_ocupacao_matriz(contador);  //Chama a funcao para mostrar a ocupacao da matriz
        vTaskDelay(pdMS_TO_TICKS(300));     //Delay
    }
}

int main(){
    init_perifericos(); //Inicia os perifericos

    ssd1306_fill(&ssd, false);
    //Borda
    ssd1306_rect(&ssd, 0, 0, 128, 64, true, false);
    ssd1306_rect(&ssd, 1, 1, 128 - 2, 64 - 2, true, false);
    ssd1306_rect(&ssd, 2, 2, 128 - 4, 64 - 4, true, false);
    ssd1306_rect(&ssd, 3, 3, 128 - 6, 64 - 6, true, false);
    ssd1306_draw_string(&ssd, "Bem-vindo", 30, 10);
    ssd1306_draw_string(&ssd, "ao", 60, 25);
    ssd1306_draw_string(&ssd, "RU CONTROLLER!", 12, 40);
    ssd1306_send_data(&ssd);

    xSemContagem = xSemaphoreCreateCounting(MAX_PESSOAS, MAX_PESSOAS);    //Cria o semaforo de contagem
    xSemReset = xSemaphoreCreateBinary();                                 //Cria o semaforo binario para reset
    xMutexDisplay = xSemaphoreCreateMutex();                              //Cria o mutex para o display

    gpio_set_irq_enabled_with_callback(BOTAO_JOYSTICK, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);    //Habilita a interrupcao do botao do joystick

    //Cria as tarefas
    xTaskCreate(vTaskEntrada, "Entrada", 256, NULL, 1, NULL);
    xTaskCreate(vTaskSaida, "Saida", 256, NULL, 1, NULL);
    xTaskCreate(vTaskReset, "Reset", 256, NULL, 2, NULL);
    xTaskCreate(vTaskRGB, "RGB", 256, NULL, 1, NULL);
    xTaskCreate(vTaskMatriz, "Matriz", 256, NULL, 1, NULL);

    vTaskStartScheduler();  //Inicia o scheduler
    while (true) {}
}