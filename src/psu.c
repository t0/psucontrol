/*
 * PSU Interface Implementation
 */

#include "psu.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <math.h>

LOG_MODULE_REGISTER(psu, LOG_LEVEL_INF);

#define PSU_EEPROM_NODE DT_NODELABEL(psu_eeprom)
#define PSU_NODE DT_NODELABEL(psu)
#define PSON_L_NODE DT_NODELABEL(pson_l)

static const struct device *eeprom_dev = DEVICE_DT_GET(PSU_EEPROM_NODE);

static const struct device *psu_i2c_dev = DEVICE_DT_GET(DT_BUS(PSU_NODE));
static const uint8_t psu_addr = DT_REG_ADDR(PSU_NODE);

static const struct gpio_dt_spec pson_l = GPIO_DT_SPEC_GET(PSON_L_NODE, gpios);

void psu_test(void)
{
	/* Initialize PSON_L GPIO */
	if (gpio_is_ready_dt(&pson_l)) {
		gpio_pin_configure_dt(&pson_l, GPIO_OUTPUT_INACTIVE);
		LOG_INF("PSON_L GPIO (PA15) initialized - PSU disabled");
	} else {
		LOG_ERR("PSON_L GPIO not ready");
	}

	uint8_t data[32];
	int ret;

	if (device_is_ready(eeprom_dev)) {
		ret = eeprom_read(eeprom_dev, 0, data, sizeof(data));
		if (ret == 0) {
			LOG_INF("PSU EEPROM (0x51) accessible");
			LOG_HEXDUMP_INF(data, sizeof(data), "EEPROM data:");
		} else {
			LOG_ERR("Failed to read PSU EEPROM: %d", ret);
		}
	} else {
		LOG_ERR("PSU EEPROM device not ready");
	}

	uint8_t value;
	char mfr_id[32], mfr_model[32], mfr_rev[32], mfr_serial[32];

	if (device_is_ready(psu_i2c_dev)) {
		/* Try to read PMBus CAPABILITY register (0x19) using I2C */
		uint8_t cmd = 0x19;
		int ret = i2c_write_read(psu_i2c_dev, psu_addr, &cmd, 1, &value, 1);
		if (ret == 0) {
			LOG_INF("PMBus PSU (0x59) accessible, CAPABILITY=0x%02x", value);
		} else {
			LOG_ERR("Failed to read PMBus PSU: %d", ret);
		}

		/* Read manufacturer information */
		if (psu_get_mfr_id(mfr_id, sizeof(mfr_id)) == 0) {
			LOG_INF("MFR_ID: %s", mfr_id);
		}
		if (psu_get_mfr_model(mfr_model, sizeof(mfr_model)) == 0) {
			LOG_INF("MFR_MODEL: %s", mfr_model);
		}
		if (psu_get_mfr_revision(mfr_rev, sizeof(mfr_rev)) == 0) {
			LOG_INF("MFR_REVISION: %s", mfr_rev);
		}
		if (psu_get_mfr_serial(mfr_serial, sizeof(mfr_serial)) == 0) {
			LOG_INF("MFR_SERIAL: %s", mfr_serial);
		}
	} else {
		LOG_ERR("PSU I2C bus not ready");
	}
}

int psu_read_byte(uint8_t reg, uint8_t *value)
{
	if (!device_is_ready(psu_i2c_dev))
		return -ENODEV;

	return i2c_write_read(psu_i2c_dev, psu_addr, &reg, 1, value, 1);
}

int psu_read_word(uint8_t reg, uint16_t *value)
{
	uint8_t data[2];
	int ret;

	if (!device_is_ready(psu_i2c_dev)) {
		return -ENODEV;
	}

	ret = i2c_write_read(psu_i2c_dev, psu_addr, &reg, 1, data, 2);
	if (ret == 0) {
		/* PMBus uses little-endian byte order */
		*value = data[0] | (data[1] << 8);
	}
	return ret;
}

int psu_write_byte(uint8_t reg, uint8_t value)
{
	uint8_t data[2] = {reg, value};

	if (!device_is_ready(psu_i2c_dev)) {
		return -ENODEV;
	}
	return i2c_write(psu_i2c_dev, data, 2, psu_addr);
}

/* PMBus block read - first byte is length */
static int psu_block_read(uint8_t reg, uint8_t *data, size_t max_len)
{
	uint8_t len;
	int ret;

	if (!device_is_ready(psu_i2c_dev)) {
		return -ENODEV;
	}

	/* Read length byte first */
	ret = i2c_write_read(psu_i2c_dev, psu_addr, &reg, 1, &len, 1);
	if (ret != 0) {
		return ret;
	}

	if (len == 0 || len > max_len) {
		return -EINVAL;
	}

	/* Read the actual data */
	ret = i2c_write_read(psu_i2c_dev, psu_addr, &reg, 1, data, len + 1);
	if (ret == 0) {
		/* First byte is length, shift data down */
		memmove(data, data + 1, len);
		data[len] = '\0'; /* Null terminate */
	}
	return ret;
}

int psu_eeprom_read(uint32_t offset, uint8_t *data, size_t len)
{
	if (!device_is_ready(eeprom_dev)) {
		return -ENODEV;
	}
	return eeprom_read(eeprom_dev, offset, data, len);
}

/* Convert PMBus LINEAR11 format to float */
static float linear11_to_float(uint16_t value)
{
	int16_t exponent = (value >> 11) & 0x1F;  /* Extract 5-bit exponent */
	int16_t mantissa = value & 0x7FF;         /* Extract 11-bit mantissa */

	/* Sign extend exponent from 5 bits to 16 bits */
	if (exponent & 0x10) {
		exponent |= 0xFFE0;  /* Sign extend from bit 4 */
	}

	/* Sign extend mantissa from 11 bits to 16 bits */
	if (mantissa & 0x400) {
		mantissa |= 0xF800;  /* Sign extend from bit 10 */
	}

	return (float)mantissa * powf(2.0f, (float)exponent);
}

/* PMBus Telemetry Functions */
int psu_get_voltage_in(float *volts)
{
	uint16_t raw;
	int ret = psu_read_word(0x88, &raw); /* READ_VIN */
	if (ret == 0) {
		*volts = linear11_to_float(raw);
		LOG_DBG("VIN raw=0x%04x decoded=%.2f", raw, (double)*volts);
	}
	return ret;
}

int psu_get_voltage_out(float *volts)
{
	uint16_t raw;
	uint8_t vout_mode;
	int ret;

	/* Read VOUT_MODE to get exponent */
	ret = psu_read_byte(0x20, &vout_mode);
	if (ret != 0) {
		return ret;
	}

	ret = psu_read_word(0x8B, &raw); /* READ_VOUT */
	if (ret == 0) {
		/* VOUT uses LINEAR16 format with mode-specific exponent */
		int8_t exponent = (int8_t)(vout_mode & 0x1F);
		/* Sign extend from 5 bits */
		if (exponent & 0x10) {
			exponent |= 0xE0;
		}
		*volts = (float)(int16_t)raw * powf(2.0f, (float)exponent);
		LOG_DBG("VOUT mode=0x%02x raw=0x%04x exp=%d decoded=%.2f",
			vout_mode, raw, exponent, (double)*volts);
	}
	return ret;
}

int psu_get_current_out(float *amps)
{
	uint16_t raw;
	int ret = psu_read_word(0x8C, &raw); /* READ_IOUT */
	if (ret == 0) {
		*amps = linear11_to_float(raw);
	}
	return ret;
}

int psu_get_temperature(float *celsius)
{
	uint16_t raw;
	int ret = psu_read_word(0x8D, &raw); /* READ_TEMPERATURE_1 */
	if (ret == 0) {
		*celsius = linear11_to_float(raw);
	}
	return ret;
}

int psu_get_fan_speed(int *rpm)
{
	uint16_t raw;
	int ret = psu_read_word(0x90, &raw); /* READ_FAN_SPEED_1 */
	if (ret == 0) {
		*rpm = (int)linear11_to_float(raw);
	}
	return ret;
}

int psu_set_output(bool enable)
{
	int ret = 0;

	/* Control PSON_L pin (active low) */
	if (gpio_is_ready_dt(&pson_l)) {
		if (enable) {
			gpio_pin_set_dt(&pson_l, 1); /* Active = LOW, so set=1 enables */
		} else {
			gpio_pin_set_dt(&pson_l, 0); /* Inactive = HIGH, so set=0 disables */
		}
		LOG_INF("PSON_L set to %s", enable ? "ACTIVE (PSU ON)" : "INACTIVE (PSU OFF)");
	}

	/* Also send PMBus OPERATION command */
	uint8_t value = enable ? 0x80 : 0x00;
	ret = psu_write_byte(0x01, value);

	return ret;
}

int psu_get_output_status(bool *enabled)
{
	uint8_t status;
	int ret = psu_read_byte(0x78, &status); /* STATUS_BYTE */
	if (ret == 0) {
		/* Bit 6 = OFF, Bit 7 = VOUT_OV_FAULT */
		*enabled = !(status & 0x40);
	}
	return ret;
}

/* Manufacturer Information Functions */
int psu_get_mfr_id(char *buf, size_t buflen)
{
	return psu_block_read(0x99, (uint8_t *)buf, buflen - 1);
}

int psu_get_mfr_model(char *buf, size_t buflen)
{
	return psu_block_read(0x9A, (uint8_t *)buf, buflen - 1);
}

int psu_get_mfr_revision(char *buf, size_t buflen)
{
	return psu_block_read(0x9B, (uint8_t *)buf, buflen - 1);
}

int psu_get_mfr_serial(char *buf, size_t buflen)
{
	return psu_block_read(0x9E, (uint8_t *)buf, buflen - 1);
}
