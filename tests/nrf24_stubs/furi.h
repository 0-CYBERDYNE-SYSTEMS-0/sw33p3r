#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct { int pin; } GpioPin;
typedef struct { int bus; } FuriHalSpiBusHandle;
enum { GpioModeOutputPushPull, GpioModeAnalog, GpioPullNo, GpioSpeedVeryHigh, GpioSpeedLow };
extern const GpioPin gpio_ext_pc3;
extern const GpioPin gpio_ext_pb2;
extern const FuriHalSpiBusHandle furi_hal_spi_bus_handle_external;
void furi_delay_ms(uint32_t ms);
void furi_hal_gpio_write(const GpioPin* pin, bool value);
void furi_hal_gpio_init(const GpioPin* pin, int mode, int pull, int speed);
void furi_hal_spi_bus_handle_init(const FuriHalSpiBusHandle* handle);
void furi_hal_spi_bus_handle_deinit(const FuriHalSpiBusHandle* handle);
void furi_hal_spi_acquire(const FuriHalSpiBusHandle* handle);
void furi_hal_spi_release(const FuriHalSpiBusHandle* handle);
bool furi_hal_spi_bus_trx(const FuriHalSpiBusHandle* handle, const uint8_t* tx, uint8_t* rx, size_t len, uint32_t timeout);
bool furi_hal_power_is_otg_enabled(void);
bool furi_hal_power_enable_otg(void);
void furi_hal_power_disable_otg(void);
