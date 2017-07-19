#include "stm32f4xx_hal.h"
#include <stdio.h>

#define DS18B20_SCRATCHPAD_SIZE 0x09

typedef struct {
	GPIO_TypeDef* PORT;
	uint16_t      PIN;
} DS18B20_Pins_t;

typedef struct {
	int32_t           temperature;
	int8_t            tempInt;
	int8_t            tempFract;
	uint8_t           scratchpad[DS18B20_SCRATCHPAD_SIZE];
	DS18B20_Pins_t    pins;
	TIM_HandleTypeDef *TIMx;
} DS18B20_Device_t;

void HAL_DS18B20_Init(DS18B20_Device_t *device, TIM_HandleTypeDef *TIMx) {
	device->TIMx        = TIMx;
	device->temperature = 0;
	device->tempInt     = 0;
	device->tempFract   = 0;

	HAL_TIM_Base_Init(device->TIMx);
	HAL_TIM_Base_Start(device->TIMx);
}

/* WARNING! Timer must be set to counting µseconds! */
static void HAL_DS18B20_Delay(DS18B20_Device_t *device, uint16_t time) {
	device->TIMx->Instance->CNT = 0;

	while((uint16_t)(device->TIMx->Instance->CNT) <= time);
}

uint8_t HAL_DS18B20_ResetPulse(DS18B20_Device_t *device) {
	uint8_t presence = 0;

	HAL_GPIO_WritePin(device->pins.PORT, device->pins.PIN, GPIO_PIN_RESET);
	HAL_DS18B20_Delay(device, 480);
	HAL_GPIO_WritePin(device->pins.PORT, device->pins.PIN, GPIO_PIN_SET);
	HAL_DS18B20_Delay(device, 70);

	if(HAL_GPIO_ReadPin(device->pins.PORT, device->pins.PIN) == GPIO_PIN_RESET) {
		presence++;
	}

	HAL_DS18B20_Delay(device, 410);

	if(HAL_GPIO_ReadPin(device->pins.PORT, device->pins.PIN) == GPIO_PIN_SET) {
		presence++;
	}

	if(presence == 2) {
		return 1;
	}

	return 0;
}

void HAL_DS18B20_WriteBit(DS18B20_Device_t *device, uint8_t bit) {
	if(bit) {
		HAL_GPIO_WritePin(device->pins.PORT, device->pins.PIN, GPIO_PIN_RESET);
		HAL_DS18B20_Delay(device, 10);
		HAL_GPIO_WritePin(device->pins.PORT, device->pins.PIN, GPIO_PIN_SET);
		HAL_DS18B20_Delay(device, 65);
	} else {
		HAL_GPIO_WritePin(device->pins.PORT, device->pins.PIN, GPIO_PIN_RESET);
		HAL_DS18B20_Delay(device, 65);
		HAL_GPIO_WritePin(device->pins.PORT, device->pins.PIN, GPIO_PIN_SET);
		HAL_DS18B20_Delay(device, 10);
	}
}

uint8_t HAL_DS18B20_ReadBit(DS18B20_Device_t *device) {
	uint8_t bit = 0;

	HAL_GPIO_WritePin(device->pins.PORT, device->pins.PIN, GPIO_PIN_RESET);
	HAL_DS18B20_Delay(device, 5);
	HAL_GPIO_WritePin(device->pins.PORT, device->pins.PIN, GPIO_PIN_SET);
	HAL_DS18B20_Delay(device, 5);

	if(HAL_GPIO_ReadPin(device->pins.PORT, device->pins.PIN) == GPIO_PIN_SET) {
		bit = 1;
	}

	HAL_DS18B20_Delay(device, 55);

	return bit;
}

void HAL_DS18B20_WriteByte(DS18B20_Device_t *device, uint8_t value) {
	uint8_t i = 8;

	while(i--) {
		HAL_DS18B20_WriteBit(device, value & 0x01);
		value >>= 1;
	}
}

uint8_t HAL_DS18B20_ReadByte(DS18B20_Device_t *device) {
	uint8_t i     = 8;
	uint8_t value = 0;

	while(i--) {
		value  >>= 1;
		value   |= (HAL_DS18B20_ReadBit(device) << 7);
	}

	return value;
}

void HAL_DS18B20_ReadTemp(DS18B20_Device_t *device) {
	uint8_t i         = 0;
	uint8_t msb       = 0;
	uint8_t lsb       = 0;
	uint8_t minus     = 0;
	uint16_t presence = 0;

	presence = HAL_DS18B20_ResetPulse(device);

	if(presence == 1) {
		HAL_DS18B20_WriteByte(device, 0xCC); // Skip ROM
		HAL_DS18B20_WriteByte(device, 0x44); // Convert T

		for(i = 0; i < 100; i++) { // odczekanie 750ms na odczyt i konwersje
			HAL_DS18B20_Delay(device, 7500); // temperatury
		}
	}

	presence = HAL_DS18B20_ResetPulse(device);

	if(presence == 1) {
		HAL_DS18B20_WriteByte(device, 0xCC);	// Skip ROM
		HAL_DS18B20_WriteByte(device, 0xBE);	// Read Scratchpad

		for(i = 0; i < DS18B20_SCRATCHPAD_SIZE; i++) {
			device->scratchpad[i] = HAL_DS18B20_ReadByte(device);
		}

		lsb = device->scratchpad[0]; // czytamy LSB i MSB przechowujace temperature
		msb = device->scratchpad[1];

		if(msb & 0x80) { // dla liczb ujemnych negacja i +1
			msb   =~ msb;
			lsb   =~ lsb+1;
			minus = 1;
		} else {
			minus = 0; // in case of changing from + to -
		}

		device->tempInt = (uint8_t)((uint8_t)(msb & 0x7) << 4) | ((uint8_t)(lsb & 0xf0) >> 4); // Get integral part of temperature

		if(minus) {
			device->tempInt =- device->tempInt;
		}

		device->tempFract = ((lsb & 0x0F) * 625) / 1000;
	}

	presence = HAL_DS18B20_ResetPulse(device);
}
