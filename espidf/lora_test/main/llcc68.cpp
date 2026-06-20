#include "llcc68.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "LLCC68";

LLCC68::LLCC68()
    : radioSpi_(nullptr)
{
}

void LLCC68::waitWhileBusy() const
{
    while (gpio_get_level(RADIO_PIN_NUM_BUSY) == 1) {
        esp_rom_delay_us(10);
    }
}

esp_err_t LLCC68::spiWrite(const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = data;

    return spi_device_transmit(radioSpi_, &t);
}

esp_err_t LLCC68::spiTransfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    if (!tx || !rx || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = tx;
    t.rx_buffer = rx;

    return spi_device_transmit(radioSpi_, &t);
}

esp_err_t LLCC68::writeCmd(uint8_t opcode, const uint8_t *buf, size_t len)
{
    uint8_t tmp[64] = {};

    if (len + 1 > sizeof(tmp)) {
        return ESP_ERR_INVALID_SIZE;
    }

    waitWhileBusy();

    tmp[0] = opcode;

    if (len > 0 && buf != nullptr) {
        memcpy(&tmp[1], buf, len);
    }

    esp_err_t err = spiWrite(tmp, len + 1);

    waitWhileBusy();

    return err;
}

esp_err_t LLCC68::readCmd(uint8_t opcode, uint8_t *out, size_t len)
{
    if (out == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx[64] = {};
    uint8_t rx[64] = {};

    /*
     * 大多数 SX126x/LLCC68 read command 格式：
     * opcode + dummy + N bytes
     *
     * GetStatus 比较特殊：
     * opcode 后直接返回 status。
     */
    size_t transferLen = 0;

    if (opcode == OPCODE_GET_STATUS) {
        transferLen = 1 + len;

        if (transferLen > sizeof(tx)) {
            return ESP_ERR_INVALID_SIZE;
        }

        tx[0] = opcode;

        waitWhileBusy();

        esp_err_t err = spiTransfer(tx, rx, transferLen);

        if (err == ESP_OK) {
            memcpy(out, &rx[1], len);
        }

        waitWhileBusy();

        return err;
    }

    transferLen = len + 2;

    if (transferLen > sizeof(tx)) {
        return ESP_ERR_INVALID_SIZE;
    }

    tx[0] = opcode;
    tx[1] = 0x00;

    waitWhileBusy();

    esp_err_t err = spiTransfer(tx, rx, transferLen);

    if (err == ESP_OK) {
        memcpy(out, &rx[2], len);
    }

    waitWhileBusy();

    return err;
}

esp_err_t LLCC68::writeRegister(uint16_t addr, const uint8_t *data, size_t len)
{
    uint8_t tmp[64] = {};

    if (len + 3 > sizeof(tmp)) {
        return ESP_ERR_INVALID_SIZE;
    }

    tmp[0] = OPCODE_WRITE_REGISTER;
    tmp[1] = static_cast<uint8_t>(addr >> 8);
    tmp[2] = static_cast<uint8_t>(addr);

    if (len > 0 && data != nullptr) {
        memcpy(&tmp[3], data, len);
    }

    waitWhileBusy();

    esp_err_t err = spiWrite(tmp, len + 3);

    waitWhileBusy();

    return err;
}

esp_err_t LLCC68::setSyncWord(uint16_t syncWord)
{
    uint8_t buf[2] = {
        static_cast<uint8_t>(syncWord >> 8),
        static_cast<uint8_t>(syncWord)
    };

    return writeRegister(REG_LORA_SYNC_WORD_MSB, buf, 2);
}

esp_err_t LLCC68::calibrateImage433()
{
    /*
     * 433MHz 附近建议使用 0x6B, 0x6F。
     */
    uint8_t buf[2] = {0x6B, 0x6F};
    return writeCmd(OPCODE_CALIBRATE_IMAGE, buf, 2);
}

esp_err_t LLCC68::setDio2AsRfSwitch()
{
    uint8_t enable = 0x01;
    return writeCmd(OPCODE_SET_DIO2_AS_RF_SWITCH_CTRL, &enable, 1);
}

esp_err_t LLCC68::reset()
{
    gpio_set_level(RADIO_PIN_NUM_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));

    gpio_set_level(RADIO_PIN_NUM_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(20));

    waitWhileBusy();

    return ESP_OK;
}

esp_err_t LLCC68::setStandby()
{
    /*
     * 0x00 = STDBY_RC
     */
    uint8_t mode = 0x00;
    return writeCmd(OPCODE_SET_STANDBY, &mode, 1);
}

esp_err_t LLCC68::setPacketTypeLoRa()
{
    /*
     * 0x01 = LoRa packet type
     */
    uint8_t type = 0x01;
    return writeCmd(OPCODE_SET_PACKET_TYPE, &type, 1);
}

esp_err_t LLCC68::setRfFreq(uint32_t freqHz)
{
    /*
     * RF frequency word = freqHz * 2^25 / 32MHz
     */
    uint32_t frf = static_cast<uint32_t>(
        (static_cast<uint64_t>(freqHz) << 25) / 32000000ULL
    );

    uint8_t buf[4] = {
        static_cast<uint8_t>(frf >> 24),
        static_cast<uint8_t>(frf >> 16),
        static_cast<uint8_t>(frf >> 8),
        static_cast<uint8_t>(frf)
    };

    return writeCmd(OPCODE_SET_RF_FREQUENCY, buf, 4);
}

esp_err_t LLCC68::setPaConfig()
{
    /*
     * 当前保持你原来的 PA 配置。
     * 常见 LLCC68/SX126x 模块参数可能需要根据模块硬件调整。
     */
    uint8_t buf[4] = {0x04, 0x07, 0x00, 0x01};
    return writeCmd(OPCODE_SET_PA_CONFIG, buf, 4);
}

esp_err_t LLCC68::setTxParams(int8_t powerDbm)
{
    /*
     * byte0: power
     * byte1: ramp time
     * 0x04 常见为 200us ramp。
     */
    uint8_t buf[2] = {
        static_cast<uint8_t>(powerDbm),
        0x04
    };

    return writeCmd(OPCODE_SET_TX_PARAMS, buf, 2);
}

esp_err_t LLCC68::setBufferBaseAddress()
{
    /*
     * TX base = 0x00
     * RX base = 0x00
     */
    uint8_t buf[2] = {0x00, 0x00};
    return writeCmd(OPCODE_SET_BUFFER_BASE_ADDR, buf, 2);
}

esp_err_t LLCC68::setModulationParams()
{
    /*
     * 默认参数：
     * SF9, BW125, CR4/5, LDRO off
     */
    uint8_t buf[4] = {
        0x09,
        0x04,
        0x01,
        0x00
    };

    return writeCmd(OPCODE_SET_MODULATION_PARAMS, buf, 4);
}

esp_err_t LLCC68::setPacketParams(uint8_t payloadLen, uint16_t preambleLen)
{
    /*
     * LoRa packet params:
     * byte0-1: preamble length
     * byte2: header type, 0x00 explicit
     * byte3: payload length
     * byte4: CRC, 0x01 on
     * byte5: invert IQ, 0x00 standard
     */
    uint8_t buf[6] = {
        static_cast<uint8_t>(preambleLen >> 8),
        static_cast<uint8_t>(preambleLen),
        0x00,
        payloadLen,
        0x01,
        0x00
    };

    return writeCmd(OPCODE_SET_PACKET_PARAMS, buf, 6);
}

esp_err_t LLCC68::setDioIrq()
{
    /*
     * SetDioIrqParams:
     *
     * irqMask  : 2 bytes
     * dio1Mask : 2 bytes
     * dio2Mask : 2 bytes
     * dio3Mask : 2 bytes
     *
     * 把 TX_DONE / RX_DONE / TIMEOUT / CRC_ERR / HEADER_ERR 映射到 DIO1。
     */
    uint16_t irqMask =
        IRQ_TX_DONE |
        IRQ_RX_DONE |
        IRQ_TIMEOUT |
        IRQ_CRC_ERR |
        IRQ_HEADER_ERR;

    uint16_t dio1Mask = irqMask;
    uint16_t dio2Mask = 0x0000;
    uint16_t dio3Mask = 0x0000;

    uint8_t buf[8] = {
        static_cast<uint8_t>((irqMask >> 8) & 0xFF),
        static_cast<uint8_t>(irqMask & 0xFF),

        static_cast<uint8_t>((dio1Mask >> 8) & 0xFF),
        static_cast<uint8_t>(dio1Mask & 0xFF),

        static_cast<uint8_t>((dio2Mask >> 8) & 0xFF),
        static_cast<uint8_t>(dio2Mask & 0xFF),

        static_cast<uint8_t>((dio3Mask >> 8) & 0xFF),
        static_cast<uint8_t>(dio3Mask & 0xFF)
    };

    return writeCmd(OPCODE_SET_DIO_IRQ_PARAMS, buf, sizeof(buf));
}

esp_err_t LLCC68::applyConfig(const Config &config)
{
    esp_err_t err;

    err = setStandby();
    if (err != ESP_OK) return err;

    err = setPacketTypeLoRa();
    if (err != ESP_OK) return err;

    err = setRfFreq(config.freqHz);
    if (err != ESP_OK) return err;

    /*
     * 目前根据你的项目默认 433MHz 校准。
     * 如果后续支持 470/868/915MHz，建议按频段选择 calibrateImage 参数。
     */
    err = calibrateImage433();
    if (err != ESP_OK) return err;

    err = setPaConfig();
    if (err != ESP_OK) return err;

    err = setTxParams(config.powerDbm);
    if (err != ESP_OK) return err;

    err = setBufferBaseAddress();
    if (err != ESP_OK) return err;

    /*
     * LoRa modulation params:
     * byte0: SF
     * byte1: BW
     * byte2: CR
     * byte3: LDRO
     *
     * LDRO 简单处理：
     * SF11/SF12 在 BW125 下开启。
     */
    uint8_t ldro = 0x00;

    if (config.bandwidth == 0x04 && config.spreadingFactor >= 11) {
        ldro = 0x01;
    }

    uint8_t mod[4] = {
        config.spreadingFactor,
        config.bandwidth,
        config.codingRate,
        ldro
    };

    err = writeCmd(OPCODE_SET_MODULATION_PARAMS, mod, sizeof(mod));
    if (err != ESP_OK) return err;

    /*
     * 默认先设置一个 packet params。
     * TX 前会根据实际 payloadLen 再 setPacketParams(len)。
     * RX 前建议 setPacketParams(255, preambleLength)。
     */
    err = setPacketParams(0, config.preambleLength);
    if (err != ESP_OK) return err;

    err = setSyncWord(config.syncWord);
    if (err != ESP_OK) return err;

    err = setDioIrq();
    if (err != ESP_OK) return err;

    err = clearIrq(0xFFFF);
    if (err != ESP_OK) return err;

    return ESP_OK;
}

esp_err_t LLCC68::writeBuffer(const uint8_t *data, uint8_t len)
{
    uint8_t tmp[260] = {};

    if (data == nullptr && len > 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (len + 2 > sizeof(tmp)) {
        return ESP_ERR_INVALID_SIZE;
    }

    tmp[0] = OPCODE_WRITE_BUFFER;
    tmp[1] = 0x00;

    if (len > 0) {
        memcpy(&tmp[2], data, len);
    }

    waitWhileBusy();

    esp_err_t err = spiWrite(tmp, len + 2);

    waitWhileBusy();

    return err;
}

esp_err_t LLCC68::readBuffer(
    uint8_t *data,
    uint8_t *len,
    uint8_t maxLen
)
{
    if (!data || !len || maxLen == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t payloadLen = 0;
    uint8_t startPtr = 0;

    esp_err_t err = getRxBufferStatus(&payloadLen, &startPtr);
    if (err != ESP_OK) {
        return err;
    }

    if (payloadLen == 0) {
        *len = 0;
        return ESP_OK;
    }

    if (payloadLen > maxLen) {
        payloadLen = maxLen;
    }

    /*
     * ReadBuffer:
     * opcode + offset + dummy + payload
     */
    const size_t transferLen = 3 + payloadLen;

    uint8_t tx[260] = {};
    uint8_t rx[260] = {};

    if (transferLen > sizeof(tx)) {
        return ESP_ERR_INVALID_SIZE;
    }

    tx[0] = OPCODE_READ_BUFFER;
    tx[1] = startPtr;
    tx[2] = 0x00;

    waitWhileBusy();

    err = spiTransfer(tx, rx, transferLen);

    if (err == ESP_OK) {
        memcpy(data, &rx[3], payloadLen);
        *len = payloadLen;
    }

    waitWhileBusy();

    return err;
}

esp_err_t LLCC68::clearIrq(uint16_t irq)
{
    uint8_t buf[2] = {
        static_cast<uint8_t>(irq >> 8),
        static_cast<uint8_t>(irq)
    };

    return writeCmd(OPCODE_CLR_IRQ_STATUS, buf, 2);
}

esp_err_t LLCC68::setTx(uint32_t timeoutRtc)
{
    /*
     * SetTx 参数 3 字节 timeout。
     */
    uint8_t buf[3] = {
        static_cast<uint8_t>((timeoutRtc >> 16) & 0xFF),
        static_cast<uint8_t>((timeoutRtc >> 8) & 0xFF),
        static_cast<uint8_t>(timeoutRtc & 0xFF)
    };

    return writeCmd(OPCODE_SET_TX, buf, 3);
}

esp_err_t LLCC68::setRx(uint32_t timeoutRtc)
{
    /*
     * SetRx 参数 3 字节 timeout。
     * 0x000000 通常表示无超时连续接收。
     */
    uint8_t buf[3] = {
        static_cast<uint8_t>((timeoutRtc >> 16) & 0xFF),
        static_cast<uint8_t>((timeoutRtc >> 8) & 0xFF),
        static_cast<uint8_t>(timeoutRtc & 0xFF)
    };

    return writeCmd(OPCODE_SET_RX, buf, 3);
}

esp_err_t LLCC68::getIrqStatus(uint16_t *irq)
{
    if (irq == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buf[2] = {};

    esp_err_t err = readCmd(
        OPCODE_GET_IRQ_STATUS,
        buf,
        sizeof(buf)
    );

    if (err == ESP_OK) {
        *irq = static_cast<uint16_t>(
            (static_cast<uint16_t>(buf[0]) << 8) |
            static_cast<uint16_t>(buf[1])
        );
    }

    return err;
}

esp_err_t LLCC68::getRxBufferStatus(
    uint8_t *payloadLen,
    uint8_t *rxStartBufferPointer
)
{
    if (!payloadLen || !rxStartBufferPointer) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * 返回：
     * status, payloadLengthRx, rxStartBufferPointer
     *
     * readCmd() 会去掉 opcode/dummy 后把 3 个字节放到 out。
     */
    uint8_t out[3] = {};

    esp_err_t err = readCmd(
        OPCODE_GET_RX_BUFFER_STATUS,
        out,
        sizeof(out)
    );

    if (err != ESP_OK) {
        return err;
    }

    *payloadLen = out[1];
    *rxStartBufferPointer = out[2];

    return ESP_OK;
}

esp_err_t LLCC68::getPacketStatus(int *rssi, float *snr)
{
    if (!rssi || !snr) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * LoRa GetPacketStatus 返回：
     * status, rssiPkt, snrPkt, signalRssiPkt
     */
    uint8_t out[4] = {};

    esp_err_t err = readCmd(
        OPCODE_GET_PACKET_STATUS,
        out,
        sizeof(out)
    );

    if (err != ESP_OK) {
        return err;
    }

    /*
     * rssiPkt = -rssiPktRaw / 2 dBm
     * snrPkt = snrRaw / 4 dB，有符号。
     */
    *rssi = -static_cast<int>(out[1]) / 2;

    int8_t rawSnr = static_cast<int8_t>(out[2]);
    *snr = static_cast<float>(rawSnr) / 4.0f;

    return ESP_OK;
}

esp_err_t LLCC68::printStatus()
{
    uint8_t status = 0;

    esp_err_t err = readCmd(
        OPCODE_GET_STATUS,
        &status,
        1
    );

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "radio status=0x%02X", status);
    }

    return err;
}

bool LLCC68::isDio1High() const
{
    return gpio_get_level(RADIO_PIN_NUM_DIO1) == 1;
}

esp_err_t LLCC68::init()
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask =
        (1ULL << RADIO_PIN_NUM_BUSY) |
        (1ULL << RADIO_PIN_NUM_DIO1);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return err;
    }

    gpio_config_t out_conf = {};
    out_conf.pin_bit_mask = (1ULL << RADIO_PIN_NUM_RST);
    out_conf.mode = GPIO_MODE_OUTPUT;
    out_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    out_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    out_conf.intr_type = GPIO_INTR_DISABLE;

    err = gpio_config(&out_conf);
    if (err != ESP_OK) {
        return err;
    }

    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = RADIO_PIN_NUM_MOSI;
    buscfg.miso_io_num = RADIO_PIN_NUM_MISO;
    buscfg.sclk_io_num = RADIO_PIN_NUM_CLK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 260;

    err = spi_bus_initialize(
        RADIO_SPI_HOST,
        &buscfg,
        SPI_DMA_CH_AUTO
    );

    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = 8*1000* 1000;
    devcfg.mode = 0;
    devcfg.spics_io_num = RADIO_PIN_NUM_CS;
    devcfg.queue_size = 1;

    err = spi_bus_add_device(
        RADIO_SPI_HOST,
        &devcfg,
        &radioSpi_
    );

    if (err != ESP_OK) {
        return err;
    }

    err = reset();
    if (err != ESP_OK) return err;

    err = setStandby();
    if (err != ESP_OK) return err;

    err = setPacketTypeLoRa();
    if (err != ESP_OK) return err;

    err = setDio2AsRfSwitch();
    if (err != ESP_OK) return err;

    err = setRfFreq(433000000);
    if (err != ESP_OK) return err;

    err = calibrateImage433();
    if (err != ESP_OK) return err;

    err = setPaConfig();
    if (err != ESP_OK) return err;

    err = setTxParams(14);
    if (err != ESP_OK) return err;

    err = setBufferBaseAddress();
    if (err != ESP_OK) return err;

    err = setModulationParams();
    if (err != ESP_OK) return err;

    err = setPacketParams(0, 12);
    if (err != ESP_OK) return err;

    err = setSyncWord(0x1424);
    if (err != ESP_OK) return err;

    err = setDioIrq();
    if (err != ESP_OK) return err;

    err = clearIrq(0xFFFF);
    if (err != ESP_OK) return err;

    return printStatus();
}