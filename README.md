# RU CONTROLLER 🧑‍🍳🎓

Sistema embarcado para controle de acesso ao Restaurante Universitário (RU) desenvolvido com a placa BitDogLab (RP2040) utilizando o FreeRTOS.

## 📋 Descrição

Este projeto simula o controle de entrada e saída de pessoas em um espaço físico com capacidade limitada (ex: RU), fornecendo feedback visual, sonoro e exibindo as informações em um display OLED.

### Funcionalidades

- ✅ Controle de capacidade máxima com **semáforo de contagem** (25 pessoas).
- 🔁 **Entrada e saída** por botões físicos (polling).
- 🔄 **Reset do sistema** por botão do joystick (interrupção).
- 💡 Feedback visual com **LED RGB** e **matriz de LEDs WS2812**.
- 🔊 Feedback sonoro com **buzzer**.
- 📟 Informações no **display OLED** via I2C.
- 🔒 Acesso ao display protegido com **mutex**.

---

## 🔧 Componentes Utilizados

- RP2040 (BitDogLab)
- Display OLED (SSD1306 - I2C)
- LED RGB (pinos GPIO)
- Buzzer (PWM)
- Matriz de LEDs WS2812 (PIO)
- Botões (GPIO) + joystick
- FreeRTOS

---

## 📐 Estrutura do Projeto

| Componente         | Descrição                                               |
|--------------------|----------------------------------------------------------|
| `vTaskEntrada`     | Verifica botão A, realiza entrada se houver vaga.       |
| `vTaskSaida`       | Verifica botão B, realiza saída se houver alguém.       |
| `vTaskReset`       | Reset do sistema via botão do joystick (ISR).           |
| `vTaskRGB`         | Sinalização do status de ocupação com LED RGB.          |
| `vTaskMatriz`      | Atualiza visual da matriz WS2812 com ocupação atual.    |
| `atualizar_display`| Mostra mensagens e contagem atual no OLED (mutex).      |
| `beep`             | Emite sinal sonoro conforme ação.                       |

---

## 📦 Dependências

- [FreeRTOS para RP2040](https://github.com/FreeRTOS/FreeRTOS)
- Biblioteca SSD1306 para OLED
- Biblioteca WS2812 com suporte ao PIO
- Pico SDK

---

## 🚀 Como Usar

1. Clone este repositório.
2. Compile com o CMake configurado para o Pico SDK.
3. Grave na sua placa RP2040.
4. Interaja com os botões:
   - **Botão A**: Entrada
   - **Botão B**: Saída
   - **Joystick (botão)**: Reset total

---

## 🎯 Requisitos do Projeto

-  Utilização de `xSemaphoreCreateCounting()` (controle de capacidade).
-  Utilização de `xSemaphoreCreateBinary()` (reset via ISR).
-  Utilização de `xSemaphoreCreateMutex()` (acesso ao display).
-  Mínimo de 3 tarefas.
-  Reset via interrupção.

---

## 💡 Observações

- Apenas o botão do joystick utiliza interrupção. Os botões de entrada/saída funcionam por polling com debounce.
- O sistema evita contagem incorreta ao controlar entrada/saída diretamente com o semáforo de contagem.
- Após o reset, todos os recursos visuais e sonoros são atualizados adequadamente.

---

## 📄 Autor

**Leonam S. Rabelo**
