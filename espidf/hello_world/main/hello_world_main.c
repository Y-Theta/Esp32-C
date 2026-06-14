#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "LLCC68";

// VSPI pins
#define PIN_NUM_MISO 19
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS    5

#define PIN_NUM_BUSY  4
#define PIN_NUM_RST  27
#define PIN_NUM_DIO1 26

#define RADIO_SPI_HOST SPI2_HOST

// LLCC68 opcodes
#define OPCODE_SET_STANDBY           0x80
#define OPCODE_SET_PACKET_TYPE       0x8A
#define OPCODE_SET_RF_FREQUENCY      0x86
#define OPCODE_SET_PA_CONFIG         0x95
#define OPCODE_SET_TX_PARAMS         0x8E
#define OPCODE_SET_BUFFER_BASE_ADDR  0x8F
#define OPCODE_WRITE_BUFFER          0x0E
#define OPCODE_SET_MODULATION_PARAMS 0x8B
#define OPCODE_SET_PACKET_PARAMS     0x8C
#define OPCODE_SET_DIO_IRQ_PARAMS    0x08
#define OPCODE_CLR_IRQ_STATUS        0x02
#define OPCODE_GET_IRQ_STATUS        0x12
#define OPCODE_SET_TX                0x83
#define OPCODE_GET_STATUS            0xC0
#define OPCODE_CALIBRATE_IMAGE 0x98
#define OPCODE_SET_DIO2_AS_RF_SWITCH_CTRL 0x9D
#define OPCODE_WRITE_REGISTER 0x0D

#define REG_LORA_SYNC_WORD_MSB 0x0740
#define REG_LORA_SYNC_WORD_LSB 0x0741

#define IRQ_TX_DONE                  0x0001
#define IRQ_TIMEOUT                  0x0200

static spi_device_handle_t radio_spi;

static void llcc68_wait_while_busy(void) {
    while (gpio_get_level(PIN_NUM_BUSY) == 1) {
        esp_rom_delay_us(10);
    }
}

static esp_err_t llcc68_spi_write(const uint8_t *data, size_t len) {
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    return spi_device_transmit(radio_spi, &t);
}

static esp_err_t llcc68_spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len) {
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    return spi_device_transmit(radio_spi, &t);
}

static esp_err_t llcc68_write_cmd(uint8_t opcode, const uint8_t *buf, size_t len) {
    uint8_t tmp[32];
    if (len + 1 > sizeof(tmp)) {
        return ESP_ERR_INVALID_SIZE;
    }

    llcc68_wait_while_busy();
    tmp[0] = opcode;
    if (len > 0 && buf != NULL) {
        memcpy(&tmp[1], buf, len);
    }
    esp_err_t err = llcc68_spi_write(tmp, len + 1);
    llcc68_wait_while_busy();
    return err;
}

static void llcc68_write_register(uint16_t addr, const uint8_t *data, size_t len) {
    uint8_t tmp[32];

    if (len + 3 > sizeof(tmp)) {
        return;
    }

    tmp[0] = OPCODE_WRITE_REGISTER;
    tmp[1] = (uint8_t)(addr >> 8);
    tmp[2] = (uint8_t)(addr);

    if (len > 0 && data != NULL) {
        memcpy(&tmp[3], data, len);
    }

    llcc68_wait_while_busy();
    llcc68_spi_write(tmp, len + 3);
    llcc68_wait_while_busy();
}

static void llcc68_set_sync_word(uint16_t sync_word) {
    uint8_t buf[2] = {
        (uint8_t)(sync_word >> 8),
        (uint8_t)(sync_word)
    };

    llcc68_write_register(REG_LORA_SYNC_WORD_MSB, buf, 2);
}

static void llcc68_calibrate_image_433(void) {
    uint8_t buf[2] = {0x6B, 0x6F};
    llcc68_write_cmd(OPCODE_CALIBRATE_IMAGE, buf, 2);
}

static void llcc68_set_dio2_as_rf_switch(void) {
    uint8_t enable = 0x01;
    llcc68_write_cmd(OPCODE_SET_DIO2_AS_RF_SWITCH_CTRL, &enable, 1);
}

static esp_err_t llcc68_read_cmd(uint8_t opcode, uint8_t *out, size_t len) {
    uint8_t tx[32] = {0};
    uint8_t rx[32] = {0};

    if (len + 2 > sizeof(tx)) {
        return ESP_ERR_INVALID_SIZE;
    }

    llcc68_wait_while_busy();
    tx[0] = opcode;
    tx[1] = 0x00; // dummy byte
    esp_err_t err = llcc68_spi_transfer(tx, rx, len + 2);
    if (err == ESP_OK) {
        memcpy(out, &rx[2], len);
    }
    llcc68_wait_while_busy();
    return err;
}

static void llcc68_reset(void) {
    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_NUM_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    llcc68_wait_while_busy();
}

static void llcc68_set_standby(void) {
    uint8_t mode = 0x00; // STDBY_RC
    llcc68_write_cmd(OPCODE_SET_STANDBY, &mode, 1);
}

static void llcc68_set_packet_type_lora(void) {
    uint8_t type = 0x01; // LoRa
    llcc68_write_cmd(OPCODE_SET_PACKET_TYPE, &type, 1);
}

// freqReg = freqHz * 2^25 / 32MHz
static void llcc68_set_rf_freq(uint32_t freq_hz) {
    uint32_t frf = (uint32_t)(((uint64_t)freq_hz << 25) / 32000000ULL);
    uint8_t buf[4] = {
        (uint8_t)(frf >> 24),
        (uint8_t)(frf >> 16),
        (uint8_t)(frf >> 8),
        (uint8_t)(frf)
    };
    llcc68_write_cmd(OPCODE_SET_RF_FREQUENCY, buf, 4);
}

static void llcc68_set_pa_config(void) {
    // 常见 LLCC68 发射配置，后续可按模块功率能力再细调
    uint8_t buf[4] = {0x04, 0x07, 0x00, 0x01};
    llcc68_write_cmd(OPCODE_SET_PA_CONFIG, buf, 4);
}

static void llcc68_set_tx_params(int8_t power_dbm) {
    uint8_t buf[2] = {(uint8_t)power_dbm, 0x04}; // 200us ramp
    llcc68_write_cmd(OPCODE_SET_TX_PARAMS, buf, 2);
}

static void llcc68_set_buffer_base_address(void) {
    uint8_t buf[2] = {0x00, 0x00};
    llcc68_write_cmd(OPCODE_SET_BUFFER_BASE_ADDR, buf, 2);
}

static void llcc68_write_buffer(const uint8_t *data, uint8_t len) {
    uint8_t tmp[260];
    tmp[0] = OPCODE_WRITE_BUFFER;
    tmp[1] = 0x00; // offset
    memcpy(&tmp[2], data, len);
    llcc68_wait_while_busy();
    llcc68_spi_write(tmp, len + 2);
    llcc68_wait_while_busy();
}

static void llcc68_set_modulation_params(void) {
    // SF9, BW125kHz, CR 4/5, LDRO off
    uint8_t buf[4] = {0x09, 0x04, 0x01, 0x00};
    llcc68_write_cmd(OPCODE_SET_MODULATION_PARAMS, buf, 4);
}

static void llcc68_set_packet_params(uint8_t payload_len) {
    // preamble=12, explicit header, payload len, CRC on, standard IQ
    uint8_t buf[6] = {
        0x00, 0x0C,
        0x00,
        payload_len,
        0x01,
        0x00
    };
    llcc68_write_cmd(OPCODE_SET_PACKET_PARAMS, buf, 6);
}

static void llcc68_set_dio_irq(void) {
    uint8_t buf[8] = {
        0x02, 0x01, // irq mask: TxDone | Timeout
        0x00, 0x01, // DIO1 mask: TxDone
        0x00, 0x00, // DIO2
        0x00, 0x00  // DIO3
    };
    llcc68_write_cmd(OPCODE_SET_DIO_IRQ_PARAMS, buf, 8);
}

static uint16_t llcc68_get_irq_status(void) {
    uint8_t buf[2] = {0};
    llcc68_read_cmd(OPCODE_GET_IRQ_STATUS, buf, 2);
    return ((uint16_t)buf[0] << 8) | buf[1];
}

static void llcc68_clear_irq(uint16_t irq) {
    uint8_t buf[2] = {(uint8_t)(irq >> 8), (uint8_t)irq};
    llcc68_write_cmd(OPCODE_CLR_IRQ_STATUS, buf, 2);
}

static void llcc68_set_tx(uint32_t timeout_rtc) {
    uint8_t buf[3] = {
        (uint8_t)(timeout_rtc >> 16),
        (uint8_t)(timeout_rtc >> 8),
        (uint8_t)(timeout_rtc)
    };
    llcc68_write_cmd(OPCODE_SET_TX, buf, 3);
}

static void llcc68_print_status(void) {
    uint8_t status = 0;
    llcc68_read_cmd(OPCODE_GET_STATUS, &status, 1);
    ESP_LOGI(TAG, "radio status=0x%02X", status);
}

static void radio_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_NUM_BUSY) | (1ULL << PIN_NUM_DIO1),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    gpio_config_t out_conf = {
        .pin_bit_mask = (1ULL << PIN_NUM_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&out_conf);

    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(RADIO_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 2000000,
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(RADIO_SPI_HOST, &devcfg, &radio_spi));

    llcc68_reset();
    llcc68_set_standby();
    llcc68_set_packet_type_lora();
    llcc68_set_dio2_as_rf_switch();
    llcc68_set_rf_freq(433000000);
    llcc68_calibrate_image_433();
    llcc68_set_pa_config();
    llcc68_set_tx_params(14);
    llcc68_set_buffer_base_address();
    llcc68_set_modulation_params();
    llcc68_set_sync_word(0x1424);
    llcc68_set_dio_irq();
    llcc68_print_status();
}

void app_main(void) {
    radio_init();

    uint32_t counter = 0;
    while (1) {
        char payload[64];
        int len = snprintf(payload, sizeof(payload), "LLCC68 433 TX #%lu", (unsigned long)counter++);

        llcc68_set_packet_params((uint8_t)len);
        llcc68_write_buffer((const uint8_t *)payload, (uint8_t)len);
        llcc68_clear_irq(0xFFFF);

        ESP_LOGI(TAG, "TX => %s", payload);
        llcc68_set_tx(0x000000); // no timeout

        while (gpio_get_level(PIN_NUM_DIO1) == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        uint16_t irq = llcc68_get_irq_status();
        ESP_LOGI(TAG, "IRQ = 0x%04X", irq);
        llcc68_clear_irq(irq);

        if (irq & IRQ_TX_DONE) {
            ESP_LOGI(TAG, "TX done");
        }
        if (irq & IRQ_TIMEOUT) {
            ESP_LOGW(TAG, "TX timeout");
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}