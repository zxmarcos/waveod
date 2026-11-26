/*
 * Internal interfaces between the QME-200 board files.
 */
#pragma once

#include <stdint.h>

/* system.c: core set-up done before main (watchdogs, FPU) and the memory system (MPU, caches) */
void system_early_init(void);
void system_memory_init(void);
uint32_t system_measure_core_hz(void);
void system_reset_to_bootloader(void);

/* clock.c: core at 600 MHz like the original (FUN_0001BCA4); returns 1 when it had to change the clocks */
int clock_init(void);

/* tempmon.c: the temperature monitor powered up and measuring by itself (plat_cpu_temp reads it) */
void tempmon_init(void);

/* time.c */
void time_init(uint32_t core_hz);
extern volatile uint32_t ms_count;

/* safety.c: the crash record of the previous run (first thing at start-up), the watchdog tick (SysTick) */
void safety_boot(void);
void safety_tick(const uint32_t *frame, uint32_t now);

/* audio.c: zeros to the outputs from now on (a crash) */
void audio_silence(void);

/* pins.c */
void pins_init(void);
void gpio_write(uint32_t gpio, int pin, int value);
void gpio_init(uint32_t gpio, int pin, int output, int value);

/* shift_reg.c: buttons in, LEDs / analog switches out */
void shift_reg_xfer(const uint8_t out[4], uint8_t in[4]);
extern uint8_t shift_reg_out[4];

/* lcd.c */
void lcd_init(void);

/* controls.c */
void controls_init(void);

/* i2c_soft.c: bit-banged I2C master; transfers return 1 when every byte was acknowledged */
struct i2c_bus {
    uint32_t scl_gpio, sda_gpio;
    uint8_t scl, sda;
    uint8_t half_us;                /* half a clock period */
};
void i2c_bus_clear(const struct i2c_bus *b);
int i2c_write(const struct i2c_bus *b, uint8_t addr7, const uint8_t *data, unsigned n);
/* write `cmd` (register address), repeated START, read `n` bytes */
int i2c_read(const struct i2c_bus *b, uint8_t addr7, const uint8_t *cmd, unsigned ncmd, uint8_t *data,
             unsigned n);

/* touch_gt911.c */
void touch_init(void);

/* fmt.c: diagnostics text without printf */
struct fmt {
    char *buf;
    unsigned size, len;
};
void fmt_init(struct fmt *f, char *buf, unsigned size);
void fmt_str(struct fmt *f, const char *s);
void fmt_dec(struct fmt *f, uint32_t v);
void fmt_hex(struct fmt *f, uint32_t v, int digits);

/* flexspi.c: NOR flash commands; addresses are flash offsets, 0 = done */
void flexspi_init(void);
int flexspi_ready(void);
int flexspi_erase_4k(uint32_t addr);
int flexspi_program_page(uint32_t addr, const uint8_t *data, uint32_t len);   /* within one 256-byte page */
void flexspi_diag(struct fmt *f);           /* controller set-up, flash id / status, last error */
struct flash_status;
void flexspi_status(struct flash_status *out);      /* platform/flash.h: id + status registers at start-up */
int flexspi_lock(uint8_t protect);                  /* platform/flash.h flash_lock */
int flexspi_lock_state(void);
void flexspi_read_status(int sr[3]);                /* status registers 1-3 read now, -1 = failed */
