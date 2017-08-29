#include "pinout.h"
#include "sys.h"
#include "delay.h"
#include "ds18x20.h"

ds18x20Dev devs[DS18X20_MAX_DEV];
uint8_t devCount = 0;

bit ds18x20IsOnBus(void)
{
	bit result;
	
	onewire = 1;
	delay_5us(5);
	onewire = 0;
	delay_5us(96);		// delay 480 us	
	onewire = 1;
	delay_5us(14);		// delay 70 us
	result = !onewire;
	delay_5us(82);		// delay 410 us

	return result;
}

void ds18x20SendBit(bit value)
{
	onewire = 0;
	delay_5us(1);			// delay 6 us
	if(!value)
		delay_5us(11);	// delay 54 us
	onewire = 1;
	if(value)
		delay_5us(11);	// delay 54 us

	return;
}

bit ds18x20GetBit(void)
{
	bit result;
	onewire = 0;
	delay_5us(1);		// delay 6 us
	onewire = 1;
	delay_5us(2);		// delay 9 us
	result = onewire;
	delay_5us(11);	// delay 55 us

	return result;
}

void ds18x20SendByte(uint8_t value)
{
	uint8_t i;
	
	for(i=0; i<8; i++) {
		ds18x20SendBit(value & 0x01);
		value >>= 1;
	}

	return;
}

uint8_t ds18x20GetByte(void)
{
	uint8_t i, result = 0;
	for(i=0; i<8; i++) {
		result >>= 1;
		if (ds18x20GetBit())
			result |= 0x80;
	}	

	return result;
}

void ds18x20Select(ds18x20Dev *dev)
{
	uint8_t i;

	ds18x20SendByte(DS18X20_CMD_MATCH_ROM);

	for (i = 0; i < 8; i++)
		ds18x20SendByte(dev->id[i]);

	return;
}

void ds18x20GetAllTemps()
{
	uint8_t i, j;
	uint8_t crc;

	uint8_t arr[DS18X20_SCRATCH_LEN];

	for (i = 0; i < devCount; i++) {
		if (ds18x20IsOnBus()) {
			ds18x20Select(&devs[i]);
			ds18x20SendByte(DS18X20_CMD_READ_SCRATCH);

			// Control scratchpad checksum
			crc = 0;
			for (j = 0; j < DS18X20_SCRATCH_LEN; j++) {
				arr[j] = ds18x20GetByte();
				crc = _crc_ibutton_update(crc, arr[j]);
			}

			if (crc == 0) {
				// Save first 2 bytes (temperature) of scratchpad
				for (j = 0; j < DS18X20_SCRATCH_TEMP_LEN; j++)
					devs[i].sp[j] = arr[j];
			}
		}
	}

	return;
}

void ds18x20ConvertTemp(void)
{
	ds18x20SendByte(DS18X20_CMD_SKIP_ROM);
	ds18x20SendByte(DS18X20_CMD_CONVERT);

#ifdef DS18X20_PARASITE_POWER
	// Set active 1 on port for at least 750ms as parasitic power
	onewire = 1;
#endif

	return;
}

uint8_t ds18x20SearchRom(uint8_t *bitPattern, uint8_t lastDeviation)
{
	uint8_t currBit;
	uint8_t newDeviation = 0;
	uint8_t bitMask = 0x01;
	uint8_t bitA;
	uint8_t bitB;

	// Send SEARCH ROM command on the bus
	ds18x20SendByte(DS18X20_CMD_SEARCH_ROM);

	// Walk through all 64 bits
	for (currBit = 0; currBit < DS18X20_ID_LEN * 8; currBit++)
	{
		// Read bit from bus twice.
		bitA = ds18x20GetBit();
		bitB = ds18x20GetBit();

		if (bitA && bitB) {								// Both bits 1 = ERROR
			return 0xFF;
		} else if (!(bitA || bitB)) {					// Both bits 0
			if (currBit == lastDeviation) {				// Select 1 if device has been selected
				*bitPattern |= bitMask;
			} else if (currBit > lastDeviation) {		// Select 0 if no, and remember device
				(*bitPattern) &= ~bitMask;
				newDeviation = currBit;
			} else if (!(*bitPattern & bitMask)) {		 // Otherwise just remember device
				newDeviation = currBit;
			}
		} else { // Bits differ
			if (bitA)
				*bitPattern |= bitMask;
			else
				*bitPattern &= ~bitMask;
		}

		// Send the selected bit to the bus.
		ds18x20SendBit(*bitPattern & bitMask);

		// Adjust bitMask and bitPattern pointer.
		bitMask <<= 1;
		if (!bitMask)
		{
			bitMask = 0x01;
			bitPattern++;
		}
	}

	return newDeviation;
}

void ds18x20SearchDevices(void)
{
	uint8_t i, j;
	uint8_t *newID;
	uint8_t *currentID;
	uint8_t lastDeviation;
	uint8_t count = 0;

	// Reset addresses
	for (i = 0; i < DS18X20_MAX_DEV; i++)
		for (j = 0; j < DS18X20_ID_LEN; j++)
			devs[i].id[j] = 0x00;

	// Search all sensors
	newID = &devs[0].id;
	lastDeviation = 0;
	currentID = newID;

	do {
		for (j = 0; j < DS18X20_ID_LEN; j++)
			newID[j] = currentID[j];

		if (!ds18x20IsOnBus()) {
			devCount = 0;

			return;
		}

		lastDeviation = ds18x20SearchRom(newID, lastDeviation);

		currentID = newID;
		count++;
		newID=&devs[count].id;

	} while (lastDeviation != 0);

	devCount = count;

	return;
}

uint8_t ds18x20Process(void)
{
	ds18x20GetAllTemps();

	// Convert temperature
	if (ds18x20IsOnBus())
		ds18x20ConvertTemp();

	return devCount;
}

int16_t ds18x20GetTemp(uint8_t num)
{
	int16_t ret = devs[num].sp[0];
	ret |= (devs[num].sp[1] << 8);
	ret *= 5;

	if (devs[num].id[0] == 0x28) // DS18B20 has 8X better resolution
		ret /= 8;

	// Return value is in 0.1�C units
	return ret;
}

uint8_t ds18x20GetDevCount(void)
{
	return devCount;
}

uint8_t _crc_ibutton_update(uint8_t crc, uint8_t value)
{
    uint8_t i;

    crc = crc ^ value;
    for (i = 0; i < 8; i++)
    {
        if (crc & 0x01)
            crc = (crc >> 1) ^ 0x8C;
        else
            crc >>= 1;
    }

    return crc;
}
