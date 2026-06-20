#pragma once

#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "config.h"

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
     * LLCC68 opcodes
     */
    static constexpr uint8_t OPCODE_SET_STANDBY           = 0x80;
    static constexpr uint8_t OPCODE_SET_PACKET_TYPE       = 0x8A;
    static constexpr uint8_t OPCODE_SET_RF_FREQUENCY      = 0x86;
    static constexpr uint8_t OPCODE_SET_PA_CONFIG         = 0x95;
    static constexpr uint8_t OPCODE_SET_TX_PARAMS         = 0x8E;
    static constexpr uint8_t OPCODE_SET_BUFFER_BASE_ADDR  = 0x8F;
    static constexpr uint8_t OPCODE_WRITE_BUFFER          = 0x0E;
    static constexpr uint8_t OPCODE_READ_BUFFER           = 0x1E;
    static constexpr uint8_t OPCODE_SET_MODULATION_PARAMS = 0x8B;
    static constexpr uint8_t OPCODE_SET_PACKET_PARAMS     = 0x8C;
    static constexpr uint8_t OPCODE_SET_DIO_IRQ_PARAMS    = 0x08;
    static constexpr uint8_t OPCODE_CLR_IRQ_STATUS        = 0x02;
    static constexpr uint8_t OPCODE_GET_IRQ_STATUS        = 0x12;
    static constexpr uint8_t OPCODE_GET_RX_BUFFER_STATUS  = 0x13;
    static constexpr uint8_t OPCODE_GET_PACKET_STATUS     = 0x14;
    static constexpr uint8_t OPCODE_SET_TX                = 0x83;
    static constexpr uint8_t OPCODE_SET_RX                = 0x82;
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
    static constexpr uint16_t IRQ_TX_DONE         = 0x0001;
    static constexpr uint16_t IRQ_RX_DONE         = 0x0002;
    static constexpr uint16_t IRQ_PREAMBLE_DETECTED = 0x0004;
    static constexpr uint16_t IRQ_SYNC_WORD_VALID = 0x0008;
    static constexpr uint16_t IRQ_HEADER_VALID    = 0x0010;
    static constexpr uint16_t IRQ_HEADER_ERR      = 0x0020;
    static constexpr uint16_t IRQ_CRC_ERR         = 0x0040;
    static constexpr uint16_t IRQ_CAD_DONE        = 0x0080;
    static constexpr uint16_t IRQ_CAD_DETECTED    = 0x0100;
    static constexpr uint16_t IRQ_TIMEOUT         = 0x0200;

public:
    LLCC68();
    ~LLCC68() = default;

    esp_err_t init();

    esp_err_t applyConfig(const Config &config);

    esp_err_t setPacketParams(uint8_t payloadLen, uint16_t preambleLen = 12);

    esp_err_t writeBuffer(const uint8_t *data, uint8_t len);

    /*
     * 新增：读取接收 Buffer
     *
     * data: 输出数据缓存
     * len: 输出实际 payload 长度
     * maxLen: data 最大容量
     */
    esp_err_t readBuffer(uint8_t *data, uint8_t *len, uint8_t maxLen = 255);

    esp_err_t clearIrq(uint16_t irq);

    esp_err_t setTx(uint32_t timeoutRtc);

    /*
     * 新增：进入接收模式
     *
     * timeoutRtc:
     *   0x000000 表示无超时，持续接收
     *   其他值为芯片 RTC tick 超时时间
     */
    esp_err_t setRx(uint32_t timeoutRtc);

    esp_err_t getIrqStatus(uint16_t *irq);

    /*
     * 新增：获取 RX buffer 状态
     */
    esp_err_t getRxBufferStatus(
        uint8_t *payloadLen,
        uint8_t *rxStartBufferPointer
    );

    /*
     * 新增：获取最近一个包的 RSSI / SNR
     */
    esp_err_t getPacketStatus(int *rssi, float *snr);

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