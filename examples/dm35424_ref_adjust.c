/**
    @file

    @brief
        Example program which demonstrates the reference voltage
        adjustment function blocks.

    @verbatim

            This example program demonstrates using the reference adjustment
            value to adjust the value reported by the ADC or sent by the DAC.
            The example allows for setting an adjustment value, and then
            writing that value to the appropriate memory location.

            The reference voltage source should be attached to the differential
            inputs of ADC0 or ADC1 (depending on which reference adjustment you
            are using).  The voltage reading from the ADC will be displayed on
            the screen, allowing you to view the effect reference value changes has
            on the measured voltage.

            For purposes of running the example, and for convenience, the
            onboard DACs will be set to supply a value of 2 volts.  You can then
            loopback the DAC outputs into the ADC inputs, as described below.

            Note that the supplied DAC outputs should not be considered a
            reference voltage source for the purposes of calibrating the
            board.

            WARNING: This example will allow you to change the preset
            calibration values on the board.  Do not do so unless you
            understand the risks of permanently changing that value.

            Setup: Attach a reference voltage source to the ADC Channel 0-
            and Channel 0+ pins.  If using the onboard DACs, connect DAC
            Channel 0 to ADC Channel 0+ and DAC Channel 1 to ADC Channel 0-.

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

    $Id: dm35424_ref_adjust.c 141531 2024-03-06 21:05:25Z lfrankenfield $
*/

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <error.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <sys/time.h>


#include "dm35424_gbc_library.h"
#include "dm35424_adc_library.h"
#include "dm35424_dac_library.h"
#include "dm35424_dma_library.h"
#include "dm35424_ioctl.h"
#include "dm35424_examples.h"
#include "dm35424_util_library.h"
#include "dm35424_board_access.h"
#include "dm35424_types.h"
#include "dm35424.h"
#include "dm35424_ref_adjust_library.h"

/**
 * Sampling rate to use (Hz)
 */
#define DEFAULT_RATE	80000

/**
 * Maximum REF adjust to allow
 */
#define MAX_REF_ADJUST	0xFF

/**
 * Name of the program as invoked on the command line
 */
static char *program_name;

/**
 * Boolean indicating whether or not to exit the program.
 */
volatile int exit_program = 0;

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
	fprintf(stderr, "\t\tShow this help screen and exit.\n\n");
	fprintf(stderr, "\t--minor NUM\n");
	fprintf(stderr, "\t\tSpecify the minor number (>= 0) of the board to open.  When not specified,\n");
	fprintf(stderr, "\t\tthe device file with minor 0 is opened.\n");

	fprintf(stderr, "\n");
	exit(EXIT_FAILURE);
}

/**
*******************************************************************************
@brief
    Detect a keypress by selecting on STDIO with 0 wait time.

 @retval
    None.
 *******************************************************************************
*/
int keyboard_hit()
{

	struct timeval tv = { 0L, 0L };
	fd_set fds;

	FD_ZERO(&fds);
	FD_SET(0, &fds);

	return select(1, &fds, NULL, NULL, &tv);

}

/**
*******************************************************************************
@brief
    Get the key that was pressed.

 @retval
    None.
 *******************************************************************************
*/
int getch()
{
	int data;
	unsigned char c;

	if ((data = read(0, &c, sizeof(c))) < 0) {
		return data;
	} else {
		return c;
	}
}

/**
*******************************************************************************
@brief
    Signal handler for SIGINT Control-C keyboard interrupt.

@param
    signal_number

    Signal number passed in from the kernel.

@warning
    One must be extremely careful about what functions are called from a signal
    handler.
 *******************************************************************************
*/

static void sigint_handler(int signal_number)
{
	exit_program = 0xff;
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
	struct DM35424_Function_Block my_adc;
	struct DM35424_Function_Block my_dac;
	struct DM35424_Function_Block my_ref;

	unsigned long int minor = 0;
	int result;
	int my_value;
	char last_action[100];

	unsigned int rate = DEFAULT_RATE;
	unsigned int actual_rate;

	int help_option_given = 0;
	int status;
	struct sigaction signal_action;

	uint8_t ref_value = 0;
	float volts = 0;

	int16_t max_value, min_value;
	struct termios old_tio, new_tio;
	unsigned char keypress = '0';

	char *invalid_char_p;
	struct option options[] = {
		{"help", 0, 0, HELP_OPTION},
		{"minor", 1, 0, MINOR_OPTION},
		{0, 0, 0, 0}
	};

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

	signal_action.sa_handler = sigint_handler;
	sigfillset(&(signal_action.sa_mask));
	signal_action.sa_flags = 0;

	if (sigaction(SIGINT, &signal_action, NULL) < 0) {
		error(EXIT_FAILURE, errno, "ERROR: sigaction() FAILED");
	}

	printf("\n\n");
	printf("************************************************************************\n");
	printf("*                              WARNING                                 *\n");
	printf("************************************************************************\n");
	printf("*  This example program demonstrates how to change the calibration     *\n");
	printf("*  values of the board.  Specifically, writing to volatile memory will *\n");
	printf("*  temporarily change it, and writing to non-volatile will permanently *\n");
	printf("*  change it.  Do not proceed unless you understand the risks of       *\n");
	printf("*  changing the calibration values.                                    *\n");
	printf("************************************************************************\n");
	printf("\n\nPress Enter to continue, or Ctrl-C to abort.\n\n");

	keypress = getchar();

	if (exit_program) {
		return 0;
	}

	printf("\n\nApply reference voltage to ADC differential inputs (or attach \n");
	printf("loopbacks) if adjusting ADC reference, or attach voltage measuring\n");
	printf("device to DAC output, if adjusting DAC reference.  Press Enter to\n");
	printf("continue.\n");

	keypress = getchar();

	tcgetattr(STDIN_FILENO, &old_tio);

	new_tio = old_tio;

	new_tio.c_lflag &= ~ICANON;
	new_tio.c_lflag &= ~ECHO;
	new_tio.c_lflag &= ~ISIG;

	tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

	printf("Opening board.....");
	result = DM35424_Board_Open(minor, &board);

	DM35424_Check_Result(result, "Could not open board");
	printf("success.\nResetting board.....");
	result = DM35424_Gbc_Board_Reset(board);

	DM35424_Check_Result(result, "Could not reset board");
	printf("success.\n");

	printf("success.\nOpening DAC......");


	result = DM35424_Dac_Open(board, DAC_0, &my_dac);

	DM35424_Check_Result(result, "Could not open DAC");

	printf("Found DAC%u, with %d DMA channels (%d buffers each)\n",
	       DAC_0, my_dac.num_dma_channels,
	       my_dac.num_dma_buffers);

	result = DM35424_Dac_Reset(board,
					&my_dac);

	DM35424_Check_Result(result, "Error resetting DAC.");



	result = DM35424_Dac_Volts_To_Conv(3.5f, &max_value);

	DM35424_Check_Result(result,
		     "Error converting value to conversion counts.");

	result = DM35424_Dac_Set_Last_Conversion(board,
						&my_dac,
						CHANNEL_0,
						0,
						max_value);

	DM35424_Check_Result(result, "Error setting last conversion for DAC0 Chan 0.");

	result = DM35424_Dac_Volts_To_Conv(1.5f, &min_value);

	DM35424_Check_Result(result,
		     "Error converting value to conversion counts.");


	result = DM35424_Dac_Set_Last_Conversion(board,
						&my_dac,
						CHANNEL_1,
						0,
						min_value);

	DM35424_Check_Result(result, "Error setting last conversion for DAC0 Chan 1.");

	printf("Opening Reference Adjustment....\n");

	result = DM35424_Ref_Adjust_Open(board,
					REF_0,
					&my_ref);

	DM35424_Check_Result(result, "Error opening reference adjustment.");

	printf("Opening ADC......\n");

	result = DM35424_Adc_Open(board, ADC_0, &my_adc);

	DM35424_Check_Result(result, "Could not open ADC");

	printf("Found ADC%d, with %d DMA channels (%d buffers each)\n",
	       ADC_0, my_adc.num_dma_channels, my_adc.num_dma_buffers);

	result = DM35424_Adc_Uninitialize(board, &my_adc);

	DM35424_Check_Result(result, "Error setting ADC to Uninitialized.");

	result = DM35424_Adc_Channel_Setup(board,
						&my_adc,
						CHANNEL_0,
						DM35424_ADC_RNG_BIPOLAR_2_5V,
						DM35424_ADC_INPUT_DIFFERENTIAL);

	DM35424_Check_Result(result, "Error setting up channel.");


	result = DM35424_Adc_Ad_Config_Set_Mode(board,
						&my_adc,
						DM35424_ADC_MODE_CONFIG_HIGH_SPEED);

	DM35424_Check_Result(result, "Error setting AD config.");

	result = DM35424_Adc_Set_Start_Trigger(board,
					       &my_adc,
					       DM35424_CLK_SRC_IMMEDIATE);
	DM35424_Check_Result(result, "Error setting start trigger.");

	result = DM35424_Adc_Set_Stop_Trigger(board,
					      &my_adc,
					      DM35424_CLK_SRC_NEVER);
	DM35424_Check_Result(result, "Error setting stop trigger.");

	result = DM35424_Adc_Set_Clock_Src(board,
					   &my_adc,
					   DM35424_CLK_SRC_IMMEDIATE);

	DM35424_Check_Result(result, "Error setting ADC clock");

	result = DM35424_Adc_Set_Sample_Rate(board,
					     &my_adc, rate, &actual_rate);

	DM35424_Check_Result(result, "Failed to set sample rate for ADC.");
	fprintf(stdout,
		"ADC:%d Rate requested: %d  Actual Rate Achieved: %d\n",
		ADC_0, rate, actual_rate);

	result = DM35424_Adc_Set_Pre_Trigger_Samples(board, &my_adc, 0);

	DM35424_Check_Result(result, "Error setting pre-capture samples.");

	result = DM35424_Adc_Set_Post_Stop_Samples(board, &my_adc, 0);

	DM35424_Check_Result(result, "Error setting post-capture samples.");

	result = DM35424_Adc_Initialize(board, &my_adc);

	DM35424_Check_Result(result, "Failed or timed out initializing ADC.");

	printf("Starting ADC......\n");

	result = DM35424_Adc_Start(board, &my_adc);

	printf("\n\n==========================================================\n");
	printf("Press i to increase the ref adjust value.\n");
	printf("Press d to decrease the ref adjust value.\n");
	printf("Press v to write the ref adjust value to ADC volatile memory.\n");
	printf("Press n to write the ref adjust value to ADC non-volatile memory.\n");
	printf("Press c to write the ref adjust value to DAC volatile memory.\n");
	printf("Press m to write the ref adjust value to DAC non-volatile memory.\n");
	printf("Press q to quit.\n\n\n");
	printf("Ref Value        ADC%d Chan 0        Last Action\n", ADC_0);
	printf("---------        ------------       --------------------------------------\n");

	DM35424_Check_Result(result, "Error starting ADC");

	strcpy(last_action, "Waiting for input.");

	while (!exit_program && keypress != 'q') {

		while (!keyboard_hit()){
			result =
				DM35424_Adc_Channel_Get_Last_Sample(board,
								&my_adc,
								CHANNEL_0,
								&my_value);

			DM35424_Check_Result(result,
					"Error getting ADC value.");

			result =
				DM35424_Adc_Sample_To_Volts(DM35424_ADC_RNG_BIPOLAR_2_5V,
									my_value,
							&volts);

			DM35424_Check_Result(result,
					"Error converting ADC sample to volts.");

			printf("   %2u             %+2.8f       %s                                       \r",
						ref_value, volts, last_action);


			DM35424_Micro_Sleep(10000);
		}

		keypress = getch();

		if (keypress == 'i') {

			if (ref_value < MAX_REF_ADJUST) {
				ref_value ++;
				strcpy(last_action, "Reference value increased.");
			}

		}

		if (keypress == 'd') {
			if (ref_value > 0) {
				ref_value --;
				strcpy(last_action, "Reference value decreased.");
			}

		}

		if (keypress == 'v') {
			result = DM35424_Ref_Adjust_Write_Adc_To_Volatile(
					board,
					&my_ref,
					ref_value);

			DM35424_Check_Result(result, "Error writing reference value to ADC volatile memory.");
			strcpy(last_action, "Reference value written to ADC volatile memory.");
		}

		if (keypress == 'n') {
			result = DM35424_Ref_Adjust_Write_Adc_To_NonVolatile(
					board,
					&my_ref,
					ref_value);

			DM35424_Check_Result(result, "Error writing reference value to ADC non-volatile memory.");
			strcpy(last_action, "Reference value written to ADC non-volatile memory.");
		}

		if (keypress == 'c') {
			result = DM35424_Ref_Adjust_Write_Dac_To_Volatile(
					board,
					&my_ref,
					ref_value);

			DM35424_Check_Result(result, "Error writing reference value to DAC volatile memory.");
			strcpy(last_action, "Reference value written to DAC volatile memory.");
		}

		if (keypress == 'm') {
			result = DM35424_Ref_Adjust_Write_Dac_To_NonVolatile(
					board,
					&my_ref,
					ref_value);

			DM35424_Check_Result(result, "Error writing reference value to DAC non-volatile memory.");
			strcpy(last_action, "Reference value written to DAC non-volatile memory.");
		}

	}

	tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);


	printf("\n\nStopping Adc %d............", ADC_0);

	result = DM35424_Adc_Reset(board, &my_adc);

	DM35424_Check_Result(result, "Error starting ADC");

	printf("success.\n");

	result = DM35424_Gbc_Board_Reset(board);
	printf("Closing Board\n");
	result = DM35424_Board_Close(board);

	DM35424_Check_Result(result, "Error closing board.");
	printf("Example program successfully completed.\n");
	return 0;

}
