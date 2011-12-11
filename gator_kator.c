/******************************************************************************
 * Program: gator_kator.c
 * Author: Natt Day
 * Description:
 * Notes:
 * +	Any file that is part of the main library file that I intend on
 * changing was formally added to the project.  So far they include:
 * 	. C6713.cmd
 * 	. C6713dskinit.h
 * 	. C6713dskinit.c
 * + Pulse_length * Fs = 1.5 * 8e3 = 12000 samples per pulse
 * + Pause_length * Fs = 2.4 * 8e3 = 19200 samples per pause
 * + Use -ppo option to get preprocessor files and look for errors
 * 	. Just type "preprocessor" into build options search and check -ppo box
 * TODO:
 * + Need to low pass filter before band pass filtering
 * + Figure out how long processes are taking using time.h
 * + Add anti-aliasing filter prior to signal_on detection
 *****************************************************************************/
#include <stdio.h>
#include <math.h>
#include <time.h>

#include "dsk6713_aic23.h"	// codec support
#include "bp_bellow.h"	// bandpass filter coefficients

#define DSK6713_AIC23_INPUT_MIC 0x0015
#define DSK6713_AIC23_INPUT_LINE 0x0011

/******************************************************************************
 * Here, I set Fs to be 8 kHz.  This is because most of the energy in the nine
 * samples I have is between 0 and 4 kHz.  Now, one of the samples has some
 * frequency componenets out to 12 kHz so that is something to consider down
 * the road.  For now, I set Fs to get as much data into one buffer as possible
 * and that helps make sure the application is very fast.
 *****************************************************************************/
Uint32 fs = DSK6713_AIC23_FREQ_8KHZ;   //set sampling rate
Uint16 inputsource = DSK6713_AIC23_INPUT_MIC;	//select input

// debug

/******************************************************************************
 *  DEBUG	DO
 *  -----   --
 *  -1		==> debug off
 *  0 		==> run interrupt debug
 *  1 		==> run main() debug
 *  2 		==> run playback() in interrupts
 *****************************************************************************/
#define DEBUG 0
short sine_table[8]={0,707,1000,707,0,-707,-1000,-707};//sine values
short loop = 0;
short gain = 1;
short output_signal;
short temp = 0;
int zero_count = 0;
volatile short zero_count_flag = 0;
// debug

// filter_signal
float filter_signal(short);	// function prototype
float x[B];	// initialize samples
// filter_signal

// block_dc
short block_dc(short);	// function prototype
#define dc_coeff 10	// coefficient for the DC blocking filter
// block_dc

// detect_envelope
short detect_envelope(short);	// function prototype
#define env_coeff 4000	// 32768  envelope filter parameter
int envelope = 0;	// current sample of the signal envelope (32-bit)
// detect_envelope

// buffer declaration
#define column_len 512
#define row_len 10

struct complex {
	float real;
	float imag;
};

struct buffer {
	struct complex data[row_len][column_len];
};

/******************************************************************************
 * For now, I've taken the #pragma section out as a test.  It seems the program
 * works well without it.  Keep an eye for future testing.
 *****************************************************************************/
//#pragma DATA_SECTION(buffer, ".EXTRAM")
struct buffer data_buffer;

#define threshold 400

short signal_on = 0;
// buffer declaration

// interrupts
short column_index = 0;
short row_index = 0;
int samples_collected;
short sample_data;	// variable that stores the current sample
short output;	// output variable for processed sample
// interrupts

// playback
short playback();	// function prototype
short row = 0;
short col = 0;
// playback

// main
volatile short program_control = 0;	// always start in collect samples mode
#define MAX_INT 2147483647
// main

///////////////////////////////////////////////////////////////////////////////
// function prototypes for C6713init.h and functions defined in csl6713.lib

/*******************************************************************************
 * I have had to manually prototype the functions in C6713init.h because every
 * time I include C6713init.h, there are variables that are not externed.  Thus,
 * I manually add them here to make sure they are called correctly.  Luckily,
 * when c6713dskinit.c compiles, it is referring to the correct header file so
 * there aren't any worries there.
 ******************************************************************************/

short input_left_sample();
void output_sample(int);
void comm_intr();

// found in dsk6713_led.h online, defined in csl6713.lib
void DSK6713_LED_init();
void DSK6713_LED_on(Uint32);
void DSK6713_LED_off(Uint32);
void DSK6713_LED_toggle(Uint32);
// found in dsk6713_led.h online, defined in csl6713.lib

// found in dsk6713_dip.h online, defined in csl6713.lib
void DSK6713_DIP_init();
Uint32 DSK6713_DIP_get(Uint32);
// found in dsk6713_dip.h online, defined in csl6713.lib

// function prototypes for C6713init.h and functions defined in csl6713.lib
///////////////////////////////////////////////////////////////////////////////

// fft

// # of points for FFT, 512 => 10 Hz resolution for Nyquist = 4 kHz
void fft (struct buffer *, int , int );	// function prototype
#define PI 3.14159
// fft

interrupt void c_int11() {

	printf("hello\n");
	// collect samples

	if (program_control == 0) {

		sample_data = input_left_sample();

		if (sample_data > threshold) signal_on = 1;

		if (signal_on) {

			if (column_index < column_len) {

				data_buffer.data[row_index][column_index].real = filter_signal(sample_data);
				column_index++;
			}

			if ( (row_index < row_len) && (column_index >= column_len) ) {

				row_index++;	// increment row index by 1
				column_index = 0;	// reset column index back to 0
				//printf("collecting frame number: %d\n", row_index);
			}
		}
	}

	if (row_index >= row_len) program_control = 1;	// move to next state
	// collect samples

	// playback
	if (program_control == 1) {

		output = playback();
		output_sample(output);

		if ( (output == 0) && (row < row_len) && (zero_count_flag == 0) ) {

			zero_count++;

			/*****************************************************************
			 * row >= (row_len - 1) because playback() increments row_len will
			 * at the end of its process.
			 *****************************************************************/
			if (row >= (row_len - 1) ) {

				printf ("Counted %d zeros in collected samples.\n", zero_count);
				zero_count_flag = 1;
			}
		}
	}
	// playback
	printf("hello agian\n");
	return;
}

/***************************************************
 * Function: fft() is based on speaker_recognition.c
 * Written by Vasanthan Rangan and Sowmya Narayanan
 ***************************************************/

// args ==> sampled data, n = number of points, m = total number of stages !SET m in main()!!!!!!!!!!!!!!!!!!!!!!!!!!!
void fft(struct buffer *input_data, int n, int m) {

	/*******************************************
	 * n1 ==> difference between upper and lower
	 * i, j, k, l ==> counters
	 * row_index ==> used to index every frame
	 *******************************************/
	int n1,n2,i,j,k,l,row_index;

	/******************************************************
	 * xt,yt ==> temporary real and imaginary, respectively
	 * c ==> cosine
	 * s ==> sine
	 * e, a ==> computing the arguments to cosine and sine
	 *****************************************************/
	float xt,yt,c,s,e,a;

	// for each frame
	for ( row_index = 0; row_index < row_len; row_index++) {

		// Loop through all the stages
		n2 = n;

		for ( k=0; k<m; k++) {

			n1 = n2;
			n2 = n2/2;
			e = PI/n1;

			// Compute Twiddle Factors
			for ( j= 0; j<n2; j++) {

				a = j*e;
				c = (float) cos(a);
				s = (float) sin(a);

				// Do the Butterflies for all `column_len' samples
				for (i=j; i<n; i+= n1) {

					l = i+n2;
					xt = input_data->data[row_index][i].real - input_data->data[row_index][l].real;
					input_data->data[row_index][i].real = input_data->data[row_index][i].real+input_data->data[row_index][l].real;
					yt = input_data->data[row_index][i].imag - input_data->data[row_index][l].imag;
					input_data->data[row_index][i].imag = input_data->data[row_index][i].imag+input_data->data[row_index][l].imag;
					input_data->data[row_index][l].real = c*xt + s*yt;
					input_data->data[row_index][l].imag = c*yt - s*yt;
				}
			}
		}

		// Bit Reversal
		j = 0;

		for ( i=0; i<n-1; i++) {

			if (i<j) {

				xt = input_data->data[row_index][j].real;
				input_data->data[row_index][j].real = input_data->data[row_index][i].real;
				input_data->data[row_index][i].real = xt;
				yt = input_data->data[row_index][j].imag;
				input_data->data[row_index][j].imag = input_data->data[row_index][i].imag;
				input_data->data[row_index][i].imag = yt;
			}
		}
	}

	return;
}

/********************************************************
 * Function: playback() is based on speaker_recognition.c
 * Written by Vasanthan Rangan and Sowmya Narayanan
 ********************************************************/
short playback() {

	col++;	// skips a sample but very simple implementation
	if (col > column_len) {

		col = 0;
		row++;	// skips a frame if row < row_len
	}
	if (row > row_len) {

		row = 0;
	}

	// debug
	if (DEBUG == 0) {

		output_signal = (short)(filter_signal(data_buffer.data[row][col].real)*gain); // make sure that the gain here is appropriate

		if (output_signal > temp) {

			temp = output_signal;
		}

		return output_signal;
	} // debug
	else {

		return (short)data_buffer.data[row][col].real;
	}
}

/*************************************************
 * Function: filter_signal is based on FIR4types.c
 * Written by Rulph Chassaing
 *************************************************/
float filter_signal(short sample) {

	float yn = 0.0;
	int i;
	x[0] = (float)detect_envelope(block_dc(sample));	// x[0] = sample --> block dc --> detect envelope

	for (i = 0; i < B; i++) yn += (h[i] * x[i]); // y(n) = sum[ filter_coeff * x(n-i) ]

	for (i = B -1; i > 0; i--) x[i] = x[i - 1];	// shift each input so we feed the transfer function

	return yn;
}

/**************************************************
 * Function: block_dc is based on block_dc.c
 * Written by Vasanthan Rangan and Sowmya Narayanan
 **************************************************/
short block_dc(short sample) {

	int dc = 0;	// current DC estimate (32-bit: SS30-bit)
	short word1,word2;

	if (dc < 0) {

		word1 = -((-dc) >> 15);	// equal to +1 for all values less than 0
		word2 = -((-dc) & 0x00007fff);	// equal to actual sample value
	}
	else {

		/***************************************************************
		 * word1 = high-order 15-bit word of dc ==> this is zero for all
		 * positive integers
		 ***************************************************************/
		word1 = dc >> 15;
		/******************************************************************
		 * word2 = low-order 15-bit word of dc ==> this is the dc value +dc
		 ******************************************************************/
		word2 = dc & 0x00007fff;
	}

	/***********************************************
	 * It turns out that this function just returns:
	 * 	if sample < 0 --> return sample - 1
	 * 	if sample > 0 --> return sample
	 *
	 * We need a much better dc blocker than this!
	 ***********************************************/
	dc = word1 * (32768 - dc_coeff) +
			((word2 * (32768 - dc_coeff)) >> 15) +
				sample * dc_coeff;	// dc = dc*(1-coeff) + sample*coeff

	return sample - (dc >> 15);	// return sample - dc
}

/*********************************************************
 * Function: detect_envelope is based on detect_envelope.c
 * Written by Vasanthan Rangan and Sowmya Narayanan
 *********************************************************/
short detect_envelope(short sample)
{
	/*************************************
	 * If sample = 32767, envelope = 4065
	 * if sample = -32768, envelope = -431
	 *************************************/
	short word1,word2;

	if (sample < 0) sample = -sample;	// rectify the signal

	word1 = envelope >> 15;	// high-order word
	word2 = envelope & 0x00007fff;	// low-order word

	// envelope = envelope*(1-coeff) + sample*coeff
	envelope = (word1 * (32768 - env_coeff)) +
					((word2 * (32768 - env_coeff)) >> 15) +
						sample * env_coeff;

	return envelope >> 15;
}

void main() {

	comm_intr();	// initialize interrupts from c6713DSKinit.asm (codec, McBSP, and DSK)
	DSK6713_LED_init();	// initialize LEDs from dsk6713bsl.lib
	DSK6713_DIP_init();	// initialize DIP switches from dsk6713bsl.lib

	int i,j;

	// initialize filter coefficient array
	for (i = 0; i < B; i++) {
		x[i] = 0;
	}

	// initialize data_buffer
	for (i = 0; i < row_len; i++) {

		for (j = 0; j < column_len; j++) {

			data_buffer.data[i][j].real = 0.0;
			data_buffer.data[i][j].imag = 0.0;
		}
	}

	/************************
	* step 1: collect samples
	*************************/

	DSK6713_LED_on(0);	// turn LED 0 on to signify beginning of interrupt service routine (ISR)

	while (program_control == 0);	// collect samples

	/************************************
	 * step 2: playback processed samples
	 ************************************/

	DSK6713_LED_on(1);	// turn LED 1 on to signify sample collection is complete and now in playback mode

	while (program_control == 1);	// playback processed samples

	if (row_index >= row_len) {

		samples_collected = column_len * row_len;	// no overlap
		printf("Collected %d frames at %d samples per frame and for a total of %d samples (using int samples_collected).\n",row_index,column_len,samples_collected);
	}

	/***********************************
	 * step 3: perform spectral analysis
	 ***********************************/

	// debug
	while (DEBUG == 1) {

		if (DEBUG == 1) {

			output_sample(sine_table[loop]*gain);	// output for on-time sec
			if (loop < 7) ++loop;	// check for end of table
			else loop = 0;	// reinit loop index
		}
	}
	// debug

	// show that we are done here!
	for (i = 0; i < 4; i++) {

		DSK6713_LED_toggle(i);
		DSK6713_LED_off(i);
	}
}