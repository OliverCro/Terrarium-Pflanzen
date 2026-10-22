//*******************************************************************
/*!
 \file   HwI2Cmaster.cpp
 \author Thomas Breuer, Michael Tjukawkin
 \date   29.01.2024
 \brief  Code for a weatherstation
 */

//*******************************************************************
/*
 Usage:	Code to collect data from the BME680 sensor and to display it on an OLED-Display
 */

//*******************************************************************
#include <stdio.h>
#include "EmbSysLib.h"

//-------------------------------------------------------------------
#include "ReportHandler.h"
#include "configHw.h"

using namespace EmbSysLib::Ctrl;

#include "Hardware/Peripheral/Display/DisplayGraphic_SSD1306i2c.cpp"
#include "Resource/Font/Font_8x12.h"

DisplayGraphic_SSD1306i2c disp( i2cBus,
                                false,
                                fontFont_8x12,
                                1 );

ScreenGraphic screen( disp );


#define CTRL_MEAS ((BYTE)0x74)
#define CTRL_HUM ((BYTE)0x72)
#define OVERSAMPLE_X1 0b001
#define BIT_MASK 0b11111100
#define FORCED_MODE 0b01
#define RESET ((BYTE)0xE0)

double var1, var2, var3, var4;
double hum_comp, press_comp, temp_comp;
uint16_t par_h1, par_h2, par_t1, par_p1;
int8_t par_h3, par_h4, par_h5, par_h7, par_t3, par_p3, par_p6, par_p7;
uint8_t par_h6, par_p10;
int16_t par_t2, par_p2, par_p4, par_p5, par_p8, par_p9;
int32_t temp_adc, hum_adc, press_adc, t_fine, tmp;





double temp(I2Cmaster::Device &i2cDevice) {

	temp_adc = i2cDevice.read((BYTE) 0x22) << 12  	//int32_t
			| i2cDevice.read((BYTE) 0x23) << 4
			| i2cDevice.read((BYTE) 0x24) >> 4;

	par_t1 = i2cDevice.read((BYTE) 0xEA) << 8		//uint16
           | i2cDevice.read((BYTE) 0xE9);

	par_t2 = i2cDevice.read((BYTE) 0x8B) << 8		//int16_t
	       | i2cDevice.read((BYTE) 0x8A);

	par_t3 = i2cDevice.read((BYTE) 0x8C);			//int8_t


	var1 = (((double)temp_adc / 16384.0) - ((double)par_t1 / 1024.0)) * (double)par_t2;
	var2 = ((((double)temp_adc / 131072.0) - ((double)par_t1 / 8192.0)) *
		    (((double)temp_adc / 131072.0) - ((double)par_t1 / 8192.0))) *
		     ((double)par_t3 * 16.0);
	t_fine = var1 + var2;
	temp_comp = t_fine / 5120.0;
	return temp_comp;
}

double hum(I2Cmaster::Device &i2cDevice) {

	hum_adc = i2cDevice.read((BYTE) 0x25) << 8		//int32_t
			| i2cDevice.read((BYTE) 0x26);

	par_h1 = i2cDevice.read((BYTE) 0xE3) << 4		//uint16_t
		   | ((i2cDevice.read((BYTE) 0xE2) & 0b00001111));

	par_h2 = i2cDevice.read((BYTE) 0xE1) << 4		//uint16_t
		   | (i2cDevice.read((BYTE) 0xE2) >> 4);

	par_h3 = i2cDevice.read((BYTE) 0xE4);			//int8_t

	par_h4 = i2cDevice.read((BYTE) 0xE5);			//int8_t

	par_h5 = i2cDevice.read((BYTE) 0xE6);			//int8_t

	par_h6 = i2cDevice.read((BYTE) 0xE7);			//uint8_t

	par_h7 = i2cDevice.read((BYTE) 0xE8);			//int8_t




	var1 = hum_adc - (((double)par_h1 * 16.0) + (((double)par_h3 / 2.0) * temp_comp));
	var2 = var1 * (((double)par_h2 / 262144.0) * (1.0 + (((double)par_h4 / 16384.0) *
		   temp_comp) + (((double)par_h5 / 1048576.0) * temp_comp * temp_comp)));
	var3 = (double)par_h6 / 16384.0;
	var4 = (double)par_h7 / 2097152.0;
	hum_comp = var2 + ((var3 + (var4 * temp_comp)) * var2 * var2);

	return hum_comp;
}


double press(I2Cmaster::Device &i2cDevice) {

	press_adc = i2cDevice.read((BYTE) 0x1F) << 12	//int32_t
				| i2cDevice.read((BYTE) 0x20) << 4
				| i2cDevice.read((BYTE) 0x21) >> 4;

	par_p1 = i2cDevice.read((BYTE) 0x8F) << 8		//uint16_t
			| i2cDevice.read((BYTE) 0x8E);

	par_p2 = i2cDevice.read((BYTE) 0x91) << 8		//int16_t
			| i2cDevice.read((BYTE) 0x90);

	par_p3 = i2cDevice.read((BYTE) 0x92);			//int8_t


	par_p4 = i2cDevice.read((BYTE) 0x95) << 8		//int16_t
			| i2cDevice.read((BYTE) 0x94);

	par_p5 = i2cDevice.read((BYTE) 0x97) << 8		//int16_t
			| i2cDevice.read((BYTE) 0x96);

	par_p6 = i2cDevice.read((BYTE) 0x99);			//int8_t

	par_p7 = i2cDevice.read((BYTE) 0x98);			//int8_t

	par_p8 = i2cDevice.read((BYTE) 0x9D) << 8		//int16_t
			| i2cDevice.read((BYTE) 0x9C);

	par_p9 = i2cDevice.read((BYTE) 0x9F) << 8		//int16_t
			| i2cDevice.read((BYTE) 0x9E);

	par_p10 = i2cDevice.read((BYTE) 0xA0);			//uint8_t

	var1 = ((double)t_fine / 2.0) - 64000.0;
	var2 = var1 * var1 * ((double)par_p6 / 131072.0);
	var2 = var2 + (var1 * (double)par_p5 * 2.0);
	var2 = (var2 / 4.0) + ((double)par_p4 * 65536.0);
	var1 = ((((double)par_p3 * var1 * var1) / 16384.0) +
		   	 ((double)par_p2 * var1)) / 524288.0;
	var1 = (1.0 + (var1 / 32768.0)) * (double)par_p1;
	press_comp = 1048576.0 - (double)press_adc;
	press_comp = ((press_comp - (var2 / 4096.0)) * 6250.0) / var1;
	var1 = ((double)par_p9 * press_comp * press_comp) / 2147483648.0;
	var2 = press_comp * ((double)par_p8 / 32768.0);
	var3 = (press_comp / 256.0) * (press_comp / 256.0) *
		   (press_comp / 256.0) * (par_p10 / 131072.0);
	press_comp = press_comp + (var1 + var2 + var3 +
				((double)par_p7 * 128.0)) / 16.0;

	return press_comp;
}

//*******************************************************************
int main(void) {

	I2Cmaster::Device i2cDevice(i2cBus,
			0x76 << 1 /* I2Sslave device address*/);


	screen.drawRectangle(0,0,screen.getWidth(), screen.getHeight(), 1, 1 );


	i2cDevice.write( RESET, 0b10110110 );  // reset

	i2cDevice.write( CTRL_HUM, (OVERSAMPLE_X1 << 0) );    // osrs_h

	i2cDevice.write( CTRL_MEAS,
			(OVERSAMPLE_X1 << 5) | (OVERSAMPLE_X1 << 2) | (FORCED_MODE) ); // osrs_t osrs_p mode:forced





	while (1) {


		double temp1 = temp( i2cDevice );
		double press1 = press( i2cDevice );
		double hum1 = hum( i2cDevice );


		tmp = i2cDevice.read( CTRL_MEAS );
			  i2cDevice.write( CTRL_MEAS,
			  (tmp & BIT_MASK) | FORCED_MODE );  //force


		screen.drawText(12, 1,"Wetterstation");
		screen.drawText(2, 16, "Temp: %3d.%1d %cC", (int) temp1, (int) abs(((temp1 - (int) temp1 ) * 10)), 9 );
		screen.drawText(2, 32, "rel.F.:%4u %%", (int) hum1);
		screen.drawText(2, 47, "Druck: %4u hPa", (int) press1 / 100);
		screen.refresh();
	//	System::delayMilliSec(1000);
	}

}
