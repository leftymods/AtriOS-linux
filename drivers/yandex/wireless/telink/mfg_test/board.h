#pragma once

#define UART_TX_PIN UART0_TX_PB2
#define UART_RX_PIN UART0_RX_PB3
#define UART_BAUDRATE 115200

#define UART_PIN_CFG() uart_set_pin(UART_TX_PIN, UART_RX_PIN);
