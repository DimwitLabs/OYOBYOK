// Board power, backlight, charger status and battery gauge.
//
//   GPIO42  power keep-alive latch, active-low. Held low the device stays on; driven high it powers off.
//   GPIO17/35/39/47  display control lines, held low for the whole run (as stock does).
//   GPIO2   backlight, LEDC PWM.
//   GPIO12  charger CHRG (low = charging), GPIO13 charger DONE (low = full). Open-drain, pulled up.
//   GPIO1   battery voltage, ADC1 channel 0, through a divider.
#pragma once

#define OYOBYOK_PWR_LATCH_GPIO   42
#define OYOBYOK_PWR_AUX_GPIO     39
#define OYOBYOK_LCD_EN_GPIO      47
#define OYOBYOK_LCD_A0_GPIO      35
#define OYOBYOK_LCD_RESET_GPIO   17
#define OYOBYOK_BL_GPIO          2
#define OYOBYOK_CHG_STAT1_GPIO   12
#define OYOBYOK_CHG_STAT2_GPIO   13
#define OYOBYOK_BATT_ADC_GPIO    1

// Assert the power latch and the display lines. Must be the first thing app_main does.
void oyobyok_power_latch_on(void);

// Release the latch. On battery the rail collapses; on USB the device restarts instead.
__attribute__((noreturn)) void oyobyok_power_off(void);

// Reboot into the ROM serial downloader so the device can be flashed without the buttons.
void oyobyok_enter_download_mode(void);

// Backlight 0..100 %. cycle() steps through the preset levels and returns the new one.
void oyobyok_backlight_set(int pct);
int  oyobyok_backlight_cycle(void);

typedef enum {
    OYOBYOK_CHG_UNKNOWN = 0,
    OYOBYOK_CHG_DISCHARGE,
    OYOBYOK_CHG_CHARGING,
    OYOBYOK_CHG_FULL,
} oyobyok_charge_state_t;

void oyobyok_power_charge_init(void);
void oyobyok_power_charge_read(int* stat1, int* stat2);
oyobyok_charge_state_t oyobyok_power_charge_state(void);

// Cell voltage in mV (averaged, calibrated) or -1; percentage from a Li-ion curve or -1.
int oyobyok_power_battery_mv(void);
int oyobyok_power_battery_pct_from_mv(int mv);
