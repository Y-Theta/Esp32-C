#pragma once

#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

class LLCC68 {
public:
    struct Config {
        uint32_t freqHz = 433000000;
        int8_t powerDbm = 14;

        uint8_t spreadingFactor = 9;   // SF7~SF12
        uint8_t bandwidth = 0x04;      // 0x04 = 125kHz
        uint8_t codingRate = 0x01;     // 4/5
        uint16_t preambleLength = 12;
        uint16_t syncWord = 0x1424;
    };
    /*
     * 引脚定义
     */
    static constexpr gpio_num_t PIN_NUM_MISO = GPIO_NUM_19;
    static constexpr gpio_num_t PIN_NUM_MOSI = GPIO_NUM_23;
    static constexpr gpio_num_t PIN_NUM_CLK  = GPIO_NUM_18;
    static constexpr gpio_num_t PIN_NUM_CS   = GPIO_NUM_5;

    static constexpr gpio_num_t PIN_NUM_BUSY = GPIO_NUM_4;
    static constexpr gpio_num_t PIN_NUM_RST  = GPIO_NUM_27;
    static constexpr gpio_num_t PIN_NUM_DIO1 = GPIO_NUM_26;

    static constexpr spi_host_device_t RADIO_SPI_HOST = SPI2_HOST;

    /*
     * LLCC68 opcodes
     */
    static constexpr uint8_t OPCODE_SET_STANDBY           = 0x80;
    static constexpr uint8_t OPCODE_SET_PACKET_TYPE       = 0x8A;
    static constexpr uint8_t OPCODE_SET_RF_FREQUENCY      = 0x86;
    static constexpr uint8_t OPCODE_SET_PA_CONFIG         = 0x95;
    static constexpr uint8_t OPCODE_SET_TX_PARAMS         = 0x8E;
    static constexpr uint8_t OPCODE_SET_BUFFER_BASE_ADDR  = 0x8F;
    static constexpr uint8_t OPCODE_WRITE_BUFFER          = 0x0E;
    static constexpr uint8_t OPCODE_SET_MODULATION_PARAMS = 0x8B;
    static constexpr uint8_t OPCODE_SET_PACKET_PARAMS     = 0x8C;
    static constexpr uint8_t OPCODE_SET_DIO_IRQ_PARAMS    = 0x08;
    static constexpr uint8_t OPCODE_CLR_IRQ_STATUS        = 0x02;
    static constexpr uint8_t OPCODE_GET_IRQ_STATUS        = 0x12;
    static constexpr uint8_t OPCODE_SET_TX                = 0x83;
    static constexpr uint8_t OPCODE_GET_STATUS            = 0xC0;
    static constexpr uint8_t OPCODE_CALIBRATE_IMAGE       = 0x98;
    static constexpr uint8_t OPCODE_SET_DIO2_AS_RF_SWITCH_CTRL = 0x9D;
    static constexpr uint8_t OPCODE_WRITE_REGISTER        = 0x0D;

    /*
     * Registers
     */
    static constexpr uint16_t REG_LORA_SYNC_WORD_MSB = 0x0740;
    static constexpr uint16_t REG_LORA_SYNC_WORD_LSB = 0x0741;

    /*
     * IRQ flags
     */
    static constexpr uint16_t IRQ_TX_DONE = 0x0001;
    static constexpr uint16_t IRQ_TIMEOUT = 0x0200;

public:
    LLCC68();
    ~LLCC68() = default;

    esp_err_t init();

    esp_err_t applyConfig(const Config &config);    
    esp_err_t setPacketParams(uint8_t payloadLen, uint16_t preambleLen = 12);
    esp_err_t writeBuffer(const uint8_t *data, uint8_t len);
    esp_err_t clearIrq(uint16_t irq);
    esp_err_t setTx(uint32_t timeoutRtc);
    esp_err_t getIrqStatus(uint16_t *irq);
    bool isDio1High() const;

private:
    spi_device_handle_t radioSpi_;

private:
    void waitWhileBusy() const;

    esp_err_t spiWrite(const uint8_t *data, size_t len);
    esp_err_t spiTransfer(const uint8_t *tx, uint8_t *rx, size_t len);

    esp_err_t writeCmd(uint8_t opcode, const uint8_t *buf, size_t len);
    esp_err_t readCmd(uint8_t opcode, uint8_t *out, size_t len);

    esp_err_t writeRegister(uint16_t addr, const uint8_t *data, size_t len);
    esp_err_t setSyncWord(uint16_t syncWord);

    esp_err_t calibrateImage433();
    esp_err_t setDio2AsRfSwitch();

    esp_err_t reset();
    esp_err_t setStandby();
    esp_err_t setPacketTypeLoRa();
    esp_err_t setRfFreq(uint32_t freqHz);
    esp_err_t setPaConfig();
    esp_err_t setTxParams(int8_t powerDbm);
    esp_err_t setBufferBaseAddress();
    esp_err_t setModulationParams();
    esp_err_t setDioIrq();
    esp_err_t printStatus();
};