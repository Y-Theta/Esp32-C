#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"

/*
 * ============================================================
 * LLCC68 / LoRa SPI 引脚配置
 * ============================================================
 *
 * 根据你的开发板修改这里即可。
 *
 * 注意：
 * 1. LLCC68 SPI 使用 Mode 0，即 CPOL=0, CPHA=0
 * 2. LLCC68 SPI 最大时钟一般不超过 16MHz
 * 3. BUSY 引脚必须连接
 * 4. DIO1 建议连接，用于 TxDone / RxDone / Timeout 等中断
 */


/*
 * ESP32-C5 + LLCC68 推荐配置
 */

static constexpr spi_host_device_t RADIO_SPI_HOST = SPI2_HOST;

/*
 * SPI 引脚
 *
 * ESP32-C5 图中：
 * GPIO6  - FSPICLK
 * GPIO7  - FSPID，可作为 MOSI
 * GPIO10 - FSPICS0，可作为 CS
 *
 * MISO 可通过 GPIO Matrix 选择普通 GPIO。
 */
static constexpr gpio_num_t RADIO_PIN_NUM_MISO = GPIO_NUM_26;
static constexpr gpio_num_t RADIO_PIN_NUM_MOSI = GPIO_NUM_7;
static constexpr gpio_num_t RADIO_PIN_NUM_CLK  = GPIO_NUM_6;
static constexpr gpio_num_t RADIO_PIN_NUM_CS   = GPIO_NUM_10;

/*
 * LLCC68 控制引脚
 */
static constexpr gpio_num_t RADIO_PIN_NUM_BUSY = GPIO_NUM_3;
static constexpr gpio_num_t RADIO_PIN_NUM_RST  = GPIO_NUM_9;
static constexpr gpio_num_t RADIO_PIN_NUM_DIO1 = GPIO_NUM_8;
/*
 * SPI 参数
 */
static constexpr int RADIO_SPI_CLOCK_HZ = 8 * 1000 * 1000;   // 8MHz，LLCC68 最高建议不超过 16MHz
static constexpr int RADIO_SPI_MODE     = 0;                 // SPI Mode 0
static constexpr int RADIO_SPI_QUEUE_SIZE = 1;
static constexpr int RADIO_SPI_MAX_TRANSFER_SIZE = 256;

/*
 * 默认 LoRa 参数，可选
 * 如果你也想把无线参数统一放这里，可以保留这一段。
 */
static constexpr uint32_t RADIO_DEFAULT_FREQ_HZ = 433000000;
static constexpr int8_t RADIO_DEFAULT_POWER_DBM = 14;

static constexpr uint8_t RADIO_DEFAULT_SPREADING_FACTOR = 9;
static constexpr uint8_t RADIO_DEFAULT_BANDWIDTH        = 0x04;   // 125kHz
static constexpr uint8_t RADIO_DEFAULT_CODING_RATE      = 0x01;   // 4/5
static constexpr uint16_t RADIO_DEFAULT_PREAMBLE_LENGTH = 12;
static constexpr uint16_t RADIO_DEFAULT_SYNC_WORD       = 0x1424;