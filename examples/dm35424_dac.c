/**
    @file

    @brief
        Example program which demonstrates the use of the DAC.

    @verbatim

        This example program sends data to the DAC for instant conversion.
        To see the output data, connect an oscilloscope to the DACx Channel 0
        through Channel 3 pins, or appropriate DACx pin if you change the DAC
        number.

        The user can control what value goes out the DAC by using keys to
        increase or decrease the desired voltage, up to 5 V and down to -5 V.
        Follow the on-screen instructions for adjusting the voltage.

	Press 'q' to quit the program.

    @endverbatim

    @verbatim
    --------------------------------------------------------------------------
    This file and its contents are copyright (C) RTD Embedded Technologies,
    Inc.  All Rights Reserved.

    This software is licensed as described in the RTD End-User Software License
    Agreement.  For a copy of this agreement, refer to the file LICENSE.TXT
    (which should be included with this software) or contact RTD Embedded
    Technologies, Inc.
    --------------------------------------------------------------------------
    @endverbatim

    $Id: dm35424_dac.c 141531 2024-03-06 21:05:25Z lfrankenfield $
*/

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <error.h>
#include <unistd.h>
#include <limits.h>
#include <getopt.h>
#include <termios.h>
#include <time.h>
#include <sys/time.h>

#include "dm35424_gbc_library.h"
#include "dm35424_dac_library.h"
#include "dm35424_ioctl.h"
#include "dm35424_examples.h"
#include "dm35424.h"
#include "dm35424_util_library.h"

/**
 * DAC to use, if user does not choose one.
 */
#define DEFAULT_DAC_NUM		0

/**
 * Name of the program as invoked on the command line
 */
static char *program_name;

/**
*******************************************************************************
@brief
    Print information on stderr about how the program is to be used.  After
    doing so, the program is exited.
 *******************************************************************************
*/

static void usage(void)
{
	fprintf(stderr, "\n");
	fprintf(stderr, "NAME\n\n\t%s\n\n", program_name);
	fprintf(stderr, "USAGE\n\n\t%s [OPTIONS]\n\n", program_name);

	fprintf(stderr, "OPTIONS\n\n");
	fprintf(stderr, "\t--help\n");
	fprintf(stderr, "\t\tShow this help screen and exit.\n");
	fprintf(stderr, "\t--minor NUM\n");
	fprintf(stderr, "\t\tSpecify the minor number (>= 0) of the board to open.  When not specified,\n");
	fprintf(stderr, "\t\tthe device file with minor 0 is opened.\n");
	fprintf(stderr, "\t--dac NUM\n");
	fprintf(stderr, "\t\tUse the specified DAC.  The default is %d.\n", DEFAULT_DAC_NUM);
	fprintf(stderr, "\n");
	exit(EXIT_FAILURE);
}

/**
*******************************************************************************
@brief
    The main program.

@param
    argument_count

    Number of args passed on the command line, including the executable name

@param
    arguments

    Pointer to array of character strings, which are the args themselves.

@retval
    0

    Success

@retval
    Non-Zero

    Failure.

 *******************************************************************************
*/

int main(int argument_count, char **arguments)
{

	struct DM35424_Board_Descriptor *board;
	struct DM35424_Function_Block my_dac;
	unsigned long int minor = 0;
	int result;

	unsigned int dac_num = DEFAULT_DAC_NUM;
	unsigned int channel = 0, channel_to_change = 0;

	int help_option_given = 0;
	struct termios old_tio, new_tio;

	int status;
	char *invalid_char_p;
	struct option options[] = {
		{"help", 0, 0, HELP_OPTION},
		{"minor", 1, 0, MINOR_OPTION},
		{"dac", 1, 0, DAC_NUM_OPTION},
		{0, 0, 0, 0}
	};

	float voltage = 0.0;
	int16_t conv_value = 0;

	int16_t register_value;
	int increment = 1;
	unsigned char keypress = '0';

	uint8_t marker;

	struct timeval start_pressing;
	struct timeval next_press;
	struct timeval temp_clock;
	float time_difference = 0.0;

	program_name = arguments[0];

	// Show usage, parse arguments
	while (1) {
		/*
		 * Parse the next command line option and any arguments it may require
		 */
		status = getopt_long(argument_count,
				     arguments, "", options, NULL);

		/*
		 * If getopt_long() returned -1, then all options have been processed
		 */
		if (status == -1) {
			break;
		}

		/*
		 * Figure out what getopt_long() found
		 */
		switch (status) {

		/*#################################################################
			  User entered '--help'
		################################################################# */
		case HELP_OPTION:
			help_option_given = 0xFF;
			break;

		/*#################################################################
			User entered '--minor'
		################################################################# */
		case MINOR_OPTION:
			/*
			 * Convert option argument string to unsigned long integer
			 */
			errno = 0;
			minor = strtoul(optarg, &invalid_char_p, 10);

			/*
			 * Catch unsigned long int overflow
			 */
			if ((minor == ULONG_MAX)
			    && (errno == ERANGE)) {
				error(0, 0,
				      "ERROR: Device minor number caused numeric overflow");
				usage();
			}

			/*
			 * Catch argument strings with valid decimal prefixes, for
			 * example "1q", and argument strings which cannot be converted,
			 * for example "abc1"
			 */
			if ((*invalid_char_p != '\0')
			    || (invalid_char_p == optarg)) {
				error(0, 0,
				      "ERROR: Non-decimal device minor number");
				usage();
			}

			break;

		/*#################################################################
		   User entered '--dac'
		 ################################################################# */
		case DAC_NUM_OPTION:
			/*
			 * Convert option argument string to unsigned long integer
			 */
			errno = 0;
			dac_num = strtoul(optarg, &invalid_char_p, 10);

			/*
			 * Catch unsigned long int overflow
			 */
			if ((dac_num == ULONG_MAX)
			    && (errno == ERANGE)) {
				error(0, 0,
				      "ERROR: DAC number caused numeric overflow");
				usage();
			}

			/*
			 * Catch argument strings with valid decimal prefixes, for
			 * example "1q", and argument strings which cannot be converted,
			 * for example "abc1"
			 */
			if ((*invalid_char_p != '\0')
			    || (invalid_char_p == optarg)) {
				error(0, 0, "ERROR: Non-decimal DAC number");
				usage();
			}

			break;

		/*#################################################################
		  User entered unsupported option
		 ################################################################# */
		case '?':
			usage();
			break;

		/*#################################################################
		   getopt_long() returned unexpected value
		################################################################# */
		default:
			error(EXIT_FAILURE,
			      0,
			      "ERROR: getopt_long() returned unexpected value %#x",
			      status);
			break;
		}
	}

	/*
	 * Recognize '--help' option before any others
	 */
	if (help_option_given) {
		usage();
	}

	printf("Opening board.....");
	result = DM35424_Board_Open(minor, &board);

	DM35424_Check_Result(result, "Could not open board");
	printf("success.\nResetting board.....");
	result = DM35424_Gbc_Board_Reset(board);

	DM35424_Check_Result(result, "Could not reset board");
	printf("success.\nOpening DAC......");

	result = DM35424_Dac_Open(board, dac_num, &my_dac);

	DM35424_Check_Result(result, "Could not open DAC");

	printf("Found DAC%d, with %d DMA channels (%d buffers each)\n",
	       dac_num, my_dac.num_dma_channels, my_dac.num_dma_buffers);

	result = DM35424_Dac_Reset(board, &my_dac);

	DM35424_Check_Result(result, "Error stopping DAC");

	tcgetattr(STDIN_FILENO, &old_tio);

	new_tio = old_tio;

	new_tio.c_lflag &= ~ICANON;
	new_tio.c_lflag &= ~ECHO;

	tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

	result = DM35424_Dac_Volts_To_Conv(voltage, &conv_value);

	DM35424_Check_Result(result, "Error converting voltage to conversion");

	printf
	    ("\n\nPress 'c' or TAB to cycle through the channels.  The active channel will have brackets around it.\n");
	printf("Press 'i' to increase the voltage, and 'd' to decrease it.\n");
	printf("Hold down the key to change the voltage more rapidly.\n");
	printf("Press '1' for 1.0 V, '2' for 2.0 V, etc.\n");
	printf
	    ("For negative numbers, hold down the Shift key ('Shift-1', 'Shift-2', etc)\n");
	printf
	    ("Press 'q' to quit.\n\n==================  Voltages  ====================\n");
	for (channel = 0; channel < my_dac.num_dma_channels; channel++) {
		printf(" Chan %d   ", channel);
	}
	printf("\n");
	gettimeofday(&start_pressing, NULL);

	channel_to_change = 0;

	for (channel = 0; channel < my_dac.num_dma_channels; channel++) {

		result = DM35424_Dac_Get_Last_Conversion(board,
							 &my_dac,
							 channel,
							 &marker,
							 &register_value);

		if (result != 0) {
			printf
			    ("ERROR getting last conversion.  Errno = %d.\n",
			     errno);
			keypress = 'q';
		}

		result = DM35424_Dac_Conv_To_Volts((int32_t) register_value,
						   &voltage);

		if (result != 0) {
			printf
			    ("ERROR converting conversion to voltage.  Errno = %d.\n",
			     errno);
			keypress = 'q';
		}
		if (channel == channel_to_change) {
			printf(" [%3.2f]   ", voltage);
		} else {
			printf("  %3.2f    ", voltage);
		}

	}

	printf("\r");



	while (keypress != 'q') {
		gettimeofday(&next_press, NULL);
		keypress = getchar();

		gettimeofday(&temp_clock, NULL);
		time_difference = DM35424_Get_Time_Diff(temp_clock, next_press);
		if (time_difference < 38000.0f) {
			/* They're holding down the key.  The longer it is held down,
			 * the larger the increment in the value to output the DAC,
			 * thus the faster the change in voltage.
			 */
			next_press = temp_clock;
			increment = (((int)
				      DM35424_Get_Time_Diff(next_press,
							    start_pressing)) +
				     1) / 50000;

		} else {
			start_pressing = temp_clock;
			next_press = temp_clock;
			increment = 1;

		}
		if (keypress == 'c' || keypress == '\t') {
			channel_to_change =
			    (channel_to_change + 1) % my_dac.num_dma_channels;
			result =
			    DM35424_Dac_Get_Last_Conversion(board, &my_dac,
							    channel_to_change,
							    &marker,
							    &conv_value);

			if (result != 0) {
				printf
				    ("ERROR getting last conversion.  Errno = %d.\n",
				     errno);
				keypress = 'q';
			}
		}
		if (keypress == 'i') {
			if (conv_value <=
			    0x7FFF - increment) {
				conv_value += increment;
			}

		}

		if (keypress == 'd') {

			if (conv_value >=
			    -32768 + increment) {
				conv_value -= increment;
			}
		}

		if (keypress == '0') {
			result = DM35424_Dac_Volts_To_Conv(0, &conv_value);

			DM35424_Check_Result(result,
				     "Error converting voltage to conversion");
		}

		if (keypress == '1') {
			result = DM35424_Dac_Volts_To_Conv(1, &conv_value);

			DM35424_Check_Result(result,
				     "Error converting voltage to conversion");
		}

		if (keypress == '2') {
			result = DM35424_Dac_Volts_To_Conv(2, &conv_value);

			DM35424_Check_Result(result,
				     "Error converting voltage to conversion");
		}

		if (keypress == '3') {
			result = DM35424_Dac_Volts_To_Conv(3, &conv_value);

			DM35424_Check_Result(result,
				     "Error converting voltage to conversion");
		}

		if (keypress == '4') {
			result = DM35424_Dac_Volts_To_Conv(4, &conv_value);

			DM35424_Check_Result(result,
				     "Error converting voltage to conversion");
		}

		if (keypress == '5') {

			/**
			 * The DAC cannot achieve +5.0 volts, but for purposes of
			 * the example, we allow them to select 5 as a value.
			 * However, we'll have to request the actual max
			 * value from the library function.
			 */
			result = DM35424_Dac_Volts_To_Conv(4.999847412f,
							   &conv_value);

			DM35424_Check_Result(result,
				     "Error converting voltage to conversion");
		}

		if (keypress == '!') {
			result = DM35424_Dac_Volts_To_Conv(-1, &conv_value);

			DM35424_Check_Result(result,
				     "Error converting voltage to conversion");
		}

		if (keypress == '@') {
			result = DM35424_Dac_Volts_To_Conv(-2, &conv_value);

			DM35424_Check_Result(result,
				     "Error converting voltage to conversion");
		}

		if (keypress == '#') {
			result = DM35424_Dac_Volts_To_Conv(-3, &conv_value);

			DM35424_Check_Result(result,
				     "Error converting voltage to conversion");
		}

		if (keypress == '$') {
			result = DM35424_Dac_Volts_To_Conv(-4, &conv_value);

			DM35424_Check_Result(result,
				     "Error converting voltage to conversion");
		}

		if (keypress == '%') {
			result = DM35424_Dac_Volts_To_Conv(-5, &conv_value);

			DM35424_Check_Result(result,
				     "Error converting voltage to conversion");
		}

		if (keypress != 'c') {
			result = DM35424_Dac_Set_Last_Conversion(board,
								 &my_dac,
								 channel_to_change,
								 0,
								 conv_value);
			DM35424_Check_Result(result, "Error setting last conversion");

			if (result != 0) {
				printf
				    ("ERROR setting last conversion.  Errno = %d.\n",
				     errno);
				keypress = 'q';
			}
		}

		for (channel = 0; channel < my_dac.num_dma_channels; channel++) {

			result = DM35424_Dac_Get_Last_Conversion(board,
								 &my_dac,
								 channel,
								 &marker,
								 &register_value);

			if (result != 0) {
				printf
				    ("ERROR getting last conversion.  Errno = %d.\n",
				     errno);
				keypress = 'q';
			}

			result = DM35424_Dac_Conv_To_Volts((int32_t) register_value,
							   &voltage);

			if (result != 0) {
				printf
				    ("ERROR converting conversion to voltage.  Errno = %d.\n",
				     errno);
				keypress = 'q';
			}
			if (channel == channel_to_change) {
				printf(" [%3.2f]   ", voltage);
			} else {
				printf("  %3.2f    ", voltage);
			}

		}

		printf("\r");

	}

	tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);

	result = DM35424_Gbc_Board_Reset(board);
	printf("\n\nClosing Board\n");
	result = DM35424_Board_Close(board);

	DM35424_Check_Result(result, "Error closing board.");

	printf("Example program successfully completed.\n");
	return 0;

}
