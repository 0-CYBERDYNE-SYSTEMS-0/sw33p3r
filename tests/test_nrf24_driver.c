#include <stdio.h>
#include <string.h>
#include <furi.h>
#include "../nrf24_survey.h"

const GpioPin gpio_ext_pc3 = {7};
const GpioPin gpio_ext_pb2 = {6};
const FuriHalSpiBusHandle furi_hal_spi_bus_handle_external = {1};
static bool initialized, acquired, powered, attached = true, ce;
static uint8_t registers[32];
static int failures, init_count, deinit_count;
#define CHECK(cond, msg) do { if(!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while(0)

void furi_delay_ms(uint32_t ms) { (void)ms; }
void furi_hal_gpio_write(const GpioPin* pin, bool value) {
    if(pin == &gpio_ext_pb2) ce = value;
}
void furi_hal_gpio_init(const GpioPin* pin, int mode, int pull, int speed) {
    (void)pin; (void)mode; (void)pull; (void)speed;
}
void furi_hal_spi_bus_handle_init(const FuriHalSpiBusHandle* handle) {
    (void)handle; initialized = true; init_count++;
}
void furi_hal_spi_bus_handle_deinit(const FuriHalSpiBusHandle* handle) {
    (void)handle; initialized = false; deinit_count++;
}
void furi_hal_spi_acquire(const FuriHalSpiBusHandle* handle) { (void)handle; acquired = true; }
void furi_hal_spi_release(const FuriHalSpiBusHandle* handle) { (void)handle; acquired = false; }
bool furi_hal_spi_bus_trx(const FuriHalSpiBusHandle* handle, const uint8_t* tx,
                        uint8_t* rx, size_t len, uint32_t timeout) {
    (void)handle; (void)timeout;
    CHECK(acquired, "transfer owns SPI bus");
    memset(rx, 0, len);
    /* An uninitialized CS pin cannot select the attached chip. */
    if(!initialized || !attached) return true;
    rx[0] = 0x0E;
    if(len == 2) {
        uint8_t reg = tx[0] & 0x1F;
        if((tx[0] & 0xE0) == 0x20) registers[reg] = tx[1];
        else rx[1] = reg == 9 ? (uint8_t)ce : registers[reg];
    }
    return true;
}
bool furi_hal_power_is_otg_enabled(void) { return powered; }
bool furi_hal_power_enable_otg(void) { powered = true; return true; }
void furi_hal_power_disable_otg(void) { powered = false; }

int main(void) {
    RoomSweepNrf24State state;
    room_sweep_nrf24_init(&state);
    CHECK(nrf24_survey_begin(&state), "attached radio detected from cold GPIO state");
    CHECK(initialized && init_count == 1, "external handle initialized once");
    CHECK(nrf24_survey_probe(&state), "repeated probe succeeds");
    CHECK(init_count == 1, "repeated probe does not reinitialize handle");
    nrf24_survey_sample_step(&state, 1);
    CHECK(state.hits[0] == 1, "RX energy sample reaches state");
    nrf24_survey_end();
    CHECK(!initialized && !powered && !ce, "end releases pins and owned power");
    CHECK(deinit_count == 1, "end deinitializes external handle");
    nrf24_survey_end();
    CHECK(deinit_count == 1, "repeated end is harmless");

    attached = false;
    CHECK(!nrf24_survey_begin(&state), "missing radio rejected");
    CHECK(state.phase == RoomSweepNrf24Error, "missing radio reports error");
    CHECK(!initialized && !powered, "failed begin releases pins and power");
    attached = true;
    powered = true;
    CHECK(nrf24_survey_begin(&state), "retry detects reattached radio");
    nrf24_survey_end();
    CHECK(powered, "preexisting OTG power preserved");
    printf("nRF24 driver: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
