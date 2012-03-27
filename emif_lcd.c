/*
 * emif_lcd.c
 *
 *  Created on: Mar 15, 2012
 *      Author: staticd
 */

#include <emif_lcd.h>

// EMIF address
#define IOPORT 0xA1111111

//test
#define OUTPUT 0xA0000000
int *output = (int *) OUTPUT;

// pointer to get data out
int *ioport = (int *)IOPORT;

// temp storage
int input, output1;

// holds FFT array after downsizing
float bandage[16];

//int toprow[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
//int botrow[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// toprow ==> "Signal Detected"
int toprow[16] = {83, 105, 103, 110, 97, 108, 32, 68, 101, 116, 101, 99, 116, 101, 100, 33};
// botrow ==> "Distance:XX.X ft"
int botrow[16] = {68, 105, 115, 116, 97, 110, 99, 101, 58, 88, 88, 46, 88, 32, 102, 116};

// start on top row
short rowselect = 1;

// start on left of LCD
short colselect = 0;

// initialization for LCD
#define LCD_CTRL_INIT 0x38
#define LCD_CTRL_OFF 0x08
#define LCD_CTRL_ON 0x0C
#define LCD_AUTOINC 0x06
#define LCD_ON 0x0C
#define LCD_FIRST_LINE 0x80

// address of second line
#define LCD_SECOND_LINE 0xC0

// to fill arrays with characters
void set_LCD_characters()
{
	// temp index variable
	int n = 0;

	for (n = 0; n < 16; n++) {

		// first threshold
		if(bandage[n] > 40000)
		{
			// block character
			toprow[n] = 0xFF;
			botrow[n] = 0xFF;
		}
		// second threshold
		else if(bandage[n] > 20000)
		{
			// blank space
			toprow[n] = 0x20;
			botrow[n] = 0xFF;
		}
		// below second threshold
		else
		{
			toprow[n] = 0x20;
			botrow[n] = 0x20;
		}
	}
}

void send_LCD_characters()
{
	int m = 0;
	// start address
	LCD_PUT_CMD(LCD_FIRST_LINE);

	// display top row
	for ( m = 0; m < 16; m++) LCD_PUT_CHAR(toprow[m]);

	// second line
	LCD_PUT_CMD(LCD_SECOND_LINE);

	// display bottom row
	for (m = 0; m < 16; m++) LCD_PUT_CHAR(botrow[m]);
}

void init_LCD()
{
	// put command
	LCD_PUT_CMD(LCD_CTRL_INIT);

	// off display
	LCD_PUT_CMD(LCD_CTRL_OFF);

	// turn on
	LCD_PUT_CMD(LCD_CTRL_ON);

	// clear display
	LCD_PUT_CMD(0x01);

	// set address mode
	LCD_PUT_CMD(LCD_AUTOINC);

	// set it
	LCD_PUT_CMD(LCD_CTRL_ON);
}

void LCD_PUT_CMD(int data)
{

	// RS=0, RW=0
	*ioport = (data & 0x000000FF);
	delay();

	// bring enable line high
	*ioport = (data | 0x20000000);
	delay();

	// bring enable line low
	*ioport = (data & 0x000000FF);
	delay();
}

void LCD_PUT_CHAR(int data)
{
	// RS=1, RW=0
	*ioport = ((data & 0x000000FF)| 0x80000000);

	// enable high
	*ioport = ((data & 0x000000FF)| 0xA0000000);

	// enable Low
	*ioport = ((data & 0x000000FF)| 0x80000000);
	delay();
}

// create 1 ms delay
void delay() {

	int q = 0;
	int junk = 2;
	for (q = 0; q < 8000; q++) junk = junk*junk;
}

void lcd_test () {

	*output = 0xFFFFFFFF;
}
