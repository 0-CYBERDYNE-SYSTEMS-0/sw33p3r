#include "nrf24_survey.h"

#include <furi.h>
#include <furi_hal_gpio.h>
#include <furi_hal_resources.h>
#include <furi_hal_spi.h>
#include <furi_hal_power.h>

/* nRF24L01+ register / command bits — RX/RPD survey only. */
#define NRF24_R_REGISTER 0x00
#define NRF24_W_REGISTER 0x20
#define NRF24_NOP 0xFF
#define NRF24_REG_CONFIG 0x00
#define NRF24_REG_EN_AA 0x01
#define NRF24_REG_EN_RXADDR 0x02
#define NRF24_REG_SETUP_AW 0x03
#define NRF24_REG_SETUP_RETR 0x04
#define NRF24_REG_RF_CH 0x05
#define NRF24_REG_RF_SETUP 0x06
#define NRF24_REG_STATUS 0x07
#define NRF24_REG_RPD 0x09
#define NRF24_REG_FEATURE 0x1D

#define NRF24_CONFIG_EN_CRC (1U << 3)
#define NRF24_CONFIG_CRCO (1U << 2)
#define NRF24_CONFIG_PWR_UP (1U << 1)
#define NRF24_CONFIG_PRIM_RX (1U << 0)

#define NRF24_SPI_TIMEOUT 1000U

static bool s_bus_held;
static bool s_otg_on;
static bool s_ce_ready;

static void ce_low(void) {
    furi_hal_gpio_write(&gpio_ext_pc3, false);
}

static void ce_high(void) {
    furi_hal_gpio_write(&gpio_ext_pc3, true);
}

static void ce_init(void) {
    if(s_ce_ready) return;
    furi_hal_gpio_init(&gpio_ext_pc3, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
    ce_low();
    s_ce_ready = true;
}

static bool spi_trx(uint8_t* tx, uint8_t* rx, size_t len) {
    if(!tx || !rx || len == 0) return false;
    furi_hal_spi_acquire(&furi_hal_spi_bus_handle_external);
    bool ok = furi_hal_spi_bus_trx(
        &furi_hal_spi_bus_handle_external, tx, rx, len, NRF24_SPI_TIMEOUT);
    furi_hal_spi_release(&furi_hal_spi_bus_handle_external);
    return ok;
}

static bool write_reg(uint8_t reg, uint8_t value) {
    uint8_t tx[2] = {(uint8_t)(NRF24_W_REGISTER | (reg & 0x1FU)), value};
    uint8_t rx[2] = {0};
    return spi_trx(tx, rx, 2);
}

static bool read_reg(uint8_t reg, uint8_t* value) {
    if(!value) return false;
    uint8_t tx[2] = {(uint8_t)(NRF24_R_REGISTER | (reg & 0x1FU)), NRF24_NOP};
    uint8_t rx[2] = {0};
    if(!spi_trx(tx, rx, 2)) return false;
    *value = rx[1];
    return true;
}

static bool read_status(uint8_t* status) {
    if(!status) return false;
    uint8_t tx = NRF24_NOP;
    uint8_t rx = 0xFF;
    furi_hal_spi_acquire(&furi_hal_spi_bus_handle_external);
    bool ok = furi_hal_spi_bus_trx(
        &furi_hal_spi_bus_handle_external, &tx, &rx, 1, NRF24_SPI_TIMEOUT);
    furi_hal_spi_release(&furi_hal_spi_bus_handle_external);
    if(!ok) return false;
    *status = rx;
    return true;
}

static void ensure_power(void) {
    if(!furi_hal_power_is_otg_enabled()) {
        furi_hal_power_enable_otg();
        s_otg_on = true;
        furi_delay_ms(40);
    }
}

static bool configure_rx(void) {
    ce_low();
    /* RX, 2-byte CRC, power up — no continuous TX path. */
    if(!write_reg(NRF24_REG_CONFIG, (uint8_t)(NRF24_CONFIG_EN_CRC | NRF24_CONFIG_CRCO |
                                               NRF24_CONFIG_PWR_UP | NRF24_CONFIG_PRIM_RX)))
        return false;
    if(!write_reg(NRF24_REG_EN_AA, 0x00)) return false;
    if(!write_reg(NRF24_REG_EN_RXADDR, 0x01)) return false;
    if(!write_reg(NRF24_REG_SETUP_AW, 0x03)) return false;
    if(!write_reg(NRF24_REG_SETUP_RETR, 0x00)) return false;
    if(!write_reg(NRF24_REG_RF_SETUP, 0x0F)) return false; /* 2 Mbps, 0 dBm */
    furi_delay_ms(5);
    return true;
}

bool nrf24_survey_probe(RoomSweepNrf24State* state) {
    if(!state) return false;
    ensure_power();
    ce_init();
    ce_low();

    uint8_t status = 0xFF;
    if(!read_status(&status)) {
        room_sweep_nrf24_set_present(state, false);
        return false;
    }
    /* Unconnected SPI often floats 0x00/0xFF. A live nRF24 STATUS is rarely both. */
    bool looks_alive = (status != 0x00U && status != 0xFFU);
    if(!looks_alive) {
        /* Try a CONFIG write/read handshake. */
        if(!write_reg(NRF24_REG_CONFIG, (uint8_t)(NRF24_CONFIG_EN_CRC | NRF24_CONFIG_PWR_UP |
                                                   NRF24_CONFIG_PRIM_RX))) {
            room_sweep_nrf24_set_present(state, false);
            return false;
        }
        furi_delay_ms(2);
        uint8_t cfg = 0;
        if(!read_reg(NRF24_REG_CONFIG, &cfg) || (cfg & NRF24_CONFIG_PWR_UP) == 0) {
            room_sweep_nrf24_set_present(state, false);
            return false;
        }
        looks_alive = true;
    }
    room_sweep_nrf24_set_present(state, looks_alive);
    return looks_alive;
}

bool nrf24_survey_begin(RoomSweepNrf24State* state) {
    if(!state) return false;
    ensure_power();
    ce_init();
    if(!nrf24_survey_probe(state) || !state->module_present) {
        state->phase = RoomSweepNrf24Error;
        return false;
    }
    if(!configure_rx()) {
        room_sweep_nrf24_set_present(state, false);
        state->phase = RoomSweepNrf24Error;
        return false;
    }
    room_sweep_nrf24_start(state);
    s_bus_held = true;
    return true;
}

void nrf24_survey_end(void) {
    ce_low();
    /* Power down. */
    write_reg(NRF24_REG_CONFIG, (uint8_t)(NRF24_CONFIG_EN_CRC | NRF24_CONFIG_CRCO));
    s_bus_held = false;
    if(s_otg_on) {
        /* Leave OTG on if external radio stack may need it; only drop if we enabled. */
        furi_hal_power_disable_otg();
        s_otg_on = false;
    }
}

bool nrf24_survey_sample_step(RoomSweepNrf24State* state, uint8_t dwell_ms) {
    if(!state || state->phase != RoomSweepNrf24Scanning) return false;
    if(!state->module_present) {
        state->phase = RoomSweepNrf24Error;
        return true;
    }

    uint8_t ch = state->channel;
    if(!write_reg(NRF24_REG_RF_CH, ch)) {
        state->phase = RoomSweepNrf24Error;
        return true;
    }
    ce_high();
    furi_delay_ms(dwell_ms == 0 ? 1 : dwell_ms);
    uint8_t rpd = 0;
    bool ok = read_reg(NRF24_REG_RPD, &rpd);
    ce_low();
    if(!ok) {
        state->phase = RoomSweepNrf24Error;
        return true;
    }
    room_sweep_nrf24_note_sample(state, ch, (rpd & 0x01U) != 0);
    return room_sweep_nrf24_step_channel(state);
}
