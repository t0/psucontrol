/*
 * PSU Interface
 */

#ifndef PSU_H
#define PSU_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Test PSU communication */
void psu_test(void);

/* Low-level register access */
int psu_read_byte(uint8_t reg, uint8_t *value);
int psu_read_word(uint8_t reg, uint16_t *value);
int psu_write_byte(uint8_t reg, uint8_t value);
int psu_eeprom_read(uint32_t offset, uint8_t *data, size_t len);

/* PMBus telemetry functions */
int psu_get_voltage_in(float *volts);      /* READ_VIN */
int psu_get_voltage_out(float *volts);     /* READ_VOUT */
int psu_get_current_out(float *amps);      /* READ_IOUT */
int psu_get_temperature(float *celsius);   /* READ_TEMPERATURE_1 */
int psu_get_fan_speed(int *rpm);           /* READ_FAN_SPEED_1 */

/* PSU control */
int psu_set_output(bool enable);                /* OPERATION register */
int psu_get_output_status(bool *enabled);       /* STATUS_BYTE */

/* Manufacturer information (PMBus block reads) */
int psu_get_mfr_id(char *buf, size_t buflen);       /* 0x99 */
int psu_get_mfr_model(char *buf, size_t buflen);    /* 0x9A */
int psu_get_mfr_revision(char *buf, size_t buflen); /* 0x9B */
int psu_get_mfr_serial(char *buf, size_t buflen);   /* 0x9E */

#endif /* PSU_H */
