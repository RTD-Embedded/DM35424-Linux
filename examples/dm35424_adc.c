/**
    @file

    @brief
        Example program which demonstrates the use of the ADC, setting
        and responding to interrupts.

    @verbatim

        This example program uses an ADC to collect data.  An interrupt
        is generated every time data is collected by the ADC.
        After acknowledging the interrupt, the program queries the value
        last taken by the ADC, and the sample counter, and prints them
        to the screen.

	You can put any differential signal you want on the ADC input pins.
	However, for convenience, this example sets up the DACs to provide
	a signal for the ADC to measure.  In order for that to work, you
	must loopback the DAC outputs to the ADC inputs.  Since there are
	twice as many ADC inputs as DAC outputs, each DAC output must go
	to 2 different ADC inputs. DAC Channel 0 would go to ADC Channel
	0+ and Channel 1-.  DAC Channel 1 would go to ADC Channel 0- and
	Channel 1+, etc.  In this way, all ADC even channels will have the
	same signal, and all odd channels will have the opposite.

	The program will demonstrate all input modes of the ADC: DIFFERENTIAL,
	SINGLE-ENDED POS, SINGLE-ENDED NEG, and DAC INTERNAL LOOPBACK.

	The program will also demonstrate the use of filters.  They will be
	applied to the odd-numbered channels after differential mode.

        The program will continue to run until CTRL-C is pressed.

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

    $Id: dm35424_adc.c 141531 2024-03-06 21:05:25Z lfrankenfield $
*/

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <error.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <getopt.h>
#include <signal.h>

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

/**
 * DAC rate to use.
 */
#define DAC_RATE	25

/**
 * Rate to run at, if the user does not provide one. (Hz)
 */
#define DEFAULT_RATE	300

/**
 * Number of samples to play out DAC pins
 */
#define BUFFER_SIZE_SAMPLES	100

/**
 * Number of bytes in DAC sample buffer
 */
#define BUFFER_SIZE_BYTES	(BUFFER_SIZE_SAMPLES * sizeof(int))

/**
 * Name of the program as invoked on the command line
 */
static char *program_name;

/**
 * Count of interrupts that have happened.
 */
volatile int interrupt_count = 0;

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
	fprintf(stderr, "\t\tShow this help screen and exit.\n");

	fprintf(stderr, "\t--minor NUM\n");
	fprintf(stderr, "\t\tSpecify the minor number (>= 0) of the board to open.  When not specified,\n");
	fprintf(stderr, "\t\tthe device file with minor 0 is opened.\n");

	fprintf(stderr, "\t--rate RATE\n");
	fprintf(stderr, "\t\tUse the specified rate (Hz).  The default is %d.\n", DEFAULT_RATE);
	fprintf(stderr, "\n");
	exit(EXIT_FAILURE);
}


/**
*******************************************************************************
@brief
    The interrupt subroutine that will execute when an interrupt occurs.  It will
    simply increment a count, which the main program will then trigger from.
 *******************************************************************************
*/

void ISR(struct dm35424_ioctl_interrupt_info_request int_info)
{

	if (int_info.error_occurred) {

		printf("ISR: Error received.\n");
		return;

	}

	if (int_info.valid_interrupt) {

		interrupt_count++;

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
	struct DM35424_Function_Block my_dac[DM35424_NUM_DAC_ON_BOARD];

	unsigned long int minor = 0;
	int result;
	int my_value;
	uint32_t sample_counts;
	int last_int_count = 0;
	uint16_t int_status;

	unsigned int rate = DEFAULT_RATE;
	unsigned int actual_rate;
	unsigned int channel = 0;
	unsigned int dac_num = 0;
	unsigned int index = 0;

	int help_option_given = 0;
	int status;
	struct sigaction signal_action;

	uint16_t interrupt_ena = 0;
	float volts = 0;

	int16_t max_value, min_value, offset;
	int32_t *buffer, *offset_buffer;

	char *invalid_char_p;
	struct option options[] = {
		{"help", 0, 0, HELP_OPTION},
		{"minor", 1, 0, MINOR_OPTION},
		{"rate", 1, 0, RATE_OPTION},
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
			  User entered rate
		################################################################# */
		case RATE_OPTION:
			/*
			 * Convert option argument string to unsigned long integer
			 */
			errno = 0;
			rate = strtoul(optarg, &invalid_char_p, 10);

			/*
			 * Catch unsigned long int overflow
			 */
			if ((rate == ULONG_MAX)
			    && (errno == ERANGE)) {
				error(0, 0,
				      "ERROR: Rate number caused numeric overflow");
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
				      "ERROR: Non-decimal rate value entered");
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

	printf("Opening board.....");
	result = DM35424_Board_Open(minor, &board);

	DM35424_Check_Result(result, "Could not open board");
	printf("success.\nResetting board.....");
	result = DM35424_Gbc_Board_Reset(board);

	DM35424_Check_Result(result, "Could not reset board");
	printf("success.\n");

	/*********************************************************************
	* DIFFERENTIAL MODE SETUP
	*********************************************************************/
	/**
	 * First, setup the DACS.  They will produce a sine wave that needs to
	 * be looped back to the ADC inputs.  This will cause the ADC to see
	 * what looks like a max-value sine wave.
	 */
	printf("success.\nOpening DACs......\n");

	for (dac_num = 0; dac_num < DM35424_NUM_DAC_ON_BOARD; dac_num++) {
		result = DM35424_Dac_Open(board, dac_num, &my_dac[dac_num]);

		DM35424_Check_Result(result, "Could not open DAC");

		printf("Found DAC%u, with %d DMA channels (%d buffers each)\n",
		       dac_num, my_dac[dac_num].num_dma_channels,
		       my_dac[dac_num].num_dma_buffers);

		result = DM35424_Dac_Set_Clock_Src(board,
						   &my_dac[dac_num],
						   DM35424_CLK_SRC_IMMEDIATE);

		DM35424_Check_Result(result, "Error setting DAC clock");

		result = DM35424_Dac_Set_Conversion_Rate(board,
							 &my_dac[dac_num],
							 DAC_RATE,
							 &actual_rate);

		fprintf(stdout,
			"Rate requested: %d  Actual Rate Achieved: %d\n",
			DAC_RATE, actual_rate);
		DM35424_Check_Result(result, "Error setting sample rate");

		buffer = (int *)malloc(BUFFER_SIZE_BYTES);
		offset_buffer = (int *)malloc(BUFFER_SIZE_BYTES);

		DM35424_Check_Result(buffer == NULL
			     || offset_buffer == NULL,
			     "Error allocating space for buffer.");

		result = DM35424_Dac_Volts_To_Conv(1.25f, &max_value);

		DM35424_Check_Result(result,
			     "Error converting value to conversion counts.");

		result = DM35424_Dac_Volts_To_Conv(-1.25f, &min_value);

		DM35424_Check_Result(result,
			     "Error converting value to conversion counts.");

		result = DM35424_Dac_Volts_To_Conv(2.5f, &offset);

		DM35424_Check_Result(result,
			     "Error converting value to conversion counts.");

		result = DM35424_Generate_Signal_Data(DM35424_SINE_WAVE,
						      buffer,
						      BUFFER_SIZE_SAMPLES,
						      max_value,
						      min_value, offset,
						      0x0000FFFF);

		DM35424_Check_Result(result,
			     "Error trying to generate data for the DAC.");

		for (index = 0; index < BUFFER_SIZE_SAMPLES; index++) {
			offset_buffer[index] =
			    buffer[(index +
				    (BUFFER_SIZE_SAMPLES / 2)) %
				   BUFFER_SIZE_SAMPLES];
		}

		for (channel = 0; channel < my_dac[dac_num].num_dma_channels;
		     channel++) {
			fprintf(stdout,
				"Initializing and configuring DMA Channel %d....",
				channel);
			result =
			    DM35424_Dma_Initialize(board, &my_dac[dac_num],
						   channel, 1,
						   BUFFER_SIZE_BYTES);

			DM35424_Check_Result(result, "Error initializing DMA");

			result = DM35424_Dma_Setup(board,
						   &my_dac[dac_num],
						   channel,
						   DM35424_DMA_SETUP_DIRECTION_WRITE,
						   IGNORE_USED);

			DM35424_Check_Result(result, "Error configuring DMA");

			fprintf(stdout, "success!\n");

			result = DM35424_Dma_Buffer_Setup(board,
							  &my_dac[dac_num],
							  channel,
							  BUFFER_0,
							  DM35424_DMA_BUFFER_CTRL_VALID |
							  DM35424_DMA_BUFFER_CTRL_LOOP);

			DM35424_Check_Result(result,
				     "Error setting up buffer control.");

			/**
			 * We load the even channels with the wave pattern, and the odd channels with
			 * the same pattern, but offset by half its length.  Doing this gives us an
			 * opposing pattern between the even and odd channels, which helps when using
			 * DAC for ADC input.
			 */
			if (channel % 2 == 0) {
				result = DM35424_Dma_Write(board,
							   &my_dac[dac_num],
							   channel,
							   BUFFER_0,
							   BUFFER_SIZE_BYTES,
							   buffer);

				DM35424_Check_Result(result,
					     "Writing to DMA buffer failed");
			} else {

				result = DM35424_Dma_Write(board,
							   &my_dac[dac_num],
							   channel,
							   BUFFER_0,
							   BUFFER_SIZE_BYTES,
							   offset_buffer);

				DM35424_Check_Result(result,
					     "Writing to DMA buffer failed");

			}

			fprintf(stdout, "Starting DMA Channel %d......",
				channel);
			result =
			    DM35424_Dma_Start(board, &my_dac[dac_num], channel);

			DM35424_Check_Result(result, "Error starting DMA");

			printf("success.\n");

		}

		free(buffer);
		free(offset_buffer);

		fprintf(stdout, "Starting DAC.\n");

		result = DM35424_Dac_Set_Start_Trigger(board,
						       &my_dac[dac_num],
						       DM35424_CLK_SRC_IMMEDIATE);

		DM35424_Check_Result(result, "Error setting start trigger for DAC.");

		result = DM35424_Dac_Set_Stop_Trigger(board,
						      &my_dac[dac_num],
						      DM35424_CLK_SRC_NEVER);

		DM35424_Check_Result(result, "Error setting stop trigger for DAC.");

		result = DM35424_Dac_Start(board, &my_dac[dac_num]);

		DM35424_Check_Result(result, "Error starting DAC");

	}

	printf("Opening ADC......\n");

	result = DM35424_Adc_Open(board, ADC_0, &my_adc);

	DM35424_Check_Result(result, "Could not open ADC");

	printf("Found ADC_0, with %d DMA channels (%d buffers each)\n",
	       my_adc.num_dma_channels, my_adc.num_dma_buffers);

	result = DM35424_Adc_Uninitialize(board, &my_adc);

	DM35424_Check_Result(result, "Error setting ADC to Uninitialized.");

	result = DM35424_Adc_Interrupt_Set_Config(board,
						  &my_adc,
						  DM35424_ADC_INT_ALL_MASK,
						  INTERRUPT_DISABLE);

	DM35424_Check_Result(result, "Error disabling interrupts.");

	result = DM35424_Adc_Interrupt_Clear_Status(board,
						    &my_adc,
						    DM35424_ADC_INT_ALL_MASK);

	DM35424_Check_Result(result, "Error clearing interrupt status.");

	for (channel = 0; channel < my_adc.num_dma_channels; channel++) {

		result = DM35424_Adc_Channel_Setup(board,
							&my_adc,
							channel,
							DM35424_ADC_RNG_BIPOLAR_2_5V,
							DM35424_ADC_INPUT_DIFFERENTIAL);

		DM35424_Check_Result(result, "Error setting up channel.");

		result = DM35424_Adc_Channel_Set_Filter(board,
							&my_adc,
							channel,
							DM35424_ADC_CHAN_FILTER_ORDER0);

		DM35424_Check_Result(result, "Error setting filter order to 0.");

	}

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
		"ADC:0; Rate requested: %d  Actual Rate Achieved: %d\n",
		rate, actual_rate);

	result = DM35424_Adc_Set_Pre_Trigger_Samples(board, &my_adc, 0);

	DM35424_Check_Result(result, "Error setting pre-capture samples.");

	result = DM35424_Adc_Set_Post_Stop_Samples(board, &my_adc, 0);

	DM35424_Check_Result(result, "Error setting post-capture samples.");

	result = DM35424_Adc_Initialize(board, &my_adc);

	DM35424_Check_Result(result, "Failed or timed out initializing ADC.");

	fprintf(stdout, "Installing user ISR ...\n");
	result = DM35424_General_InstallISR(board, ISR);
	DM35424_Check_Result(result, "DM35424_General_InstallISR()");

	result = DM35424_Adc_Interrupt_Set_Config(board,
						  &my_adc,
						  DM35424_ADC_INT_SAMPLE_TAKEN_MASK,
						  INTERRUPT_ENABLE);

	DM35424_Check_Result(result, "Error setting interrupt.");

	printf("Starting ADC......\nADC in DIFFERENTIAL mode, using DAC outputs via physical loopback.\n");

	result = DM35424_Adc_Start(board, &my_adc);

	DM35424_Check_Result(result, "Error starting ADC");

	result = DM35424_Adc_Interrupt_Get_Config(board,
						  &my_adc, &interrupt_ena);

	DM35424_Check_Result(result, "Error getting interrupt value");

	printf("\nPress Ctrl-C for next mode.\n\n");

	printf("Sample Count");
	for (channel = 0; channel < my_adc.num_dma_channels; channel++) {

		printf("\tVoltage %u.", channel);

	}
	printf("\n");
	printf("============");
	for (channel = 0; channel < my_adc.num_dma_channels; channel++) {

		printf("\t==========");

	}
	printf("\n");

	while (!exit_program) {
		if (last_int_count < interrupt_count) {

			result = DM35424_Adc_Interrupt_Get_Status(board,
								  &my_adc,
								  &int_status);
			DM35424_Check_Result(result, "Error getting interrupt status");

			result = DM35424_Adc_Get_Sample_Count(board,
							      &my_adc,
							      &sample_counts);

			DM35424_Check_Result(result, "Error getting sample count.");

			printf("%12d", sample_counts);

			for (channel = 0; channel < my_adc.num_dma_channels;
			     channel++) {

				result =
				    DM35424_Adc_Channel_Get_Last_Sample(board,
									&my_adc,
									channel,
									&my_value);

				DM35424_Check_Result(result,
					     "Error getting ADC value.");

				result =
				    DM35424_Adc_Sample_To_Volts(DM35424_ADC_RNG_BIPOLAR_2_5V,
				    	    			my_value,
								&volts);

				DM35424_Check_Result(result,
					     "Error converting ADC sample to volts.");

				printf("    %+2.5f    ", volts);

			}
			printf("\r");

			result = DM35424_Adc_Interrupt_Clear_Status(board,
								    &my_adc,
								    int_status);
			DM35424_Check_Result(result, "Error clearing interrupt status");

			last_int_count++;

		}

		DM35424_Micro_Sleep(100);
	}

	exit_program = 0;
	printf("\n\nStopping Adc 0............");

	result = DM35424_Adc_Reset(board, &my_adc);

	DM35424_Check_Result(result, "Error starting ADC");

	printf("success!\n");

	/*********************************************************************
	* SINGLE-ENDED POSITIVE MODE SETUP
	*********************************************************************/
	for (dac_num = 0; dac_num < DM35424_NUM_DAC_ON_BOARD; dac_num++) {
		result = DM35424_Dac_Reset(board, &my_dac[dac_num]);

		DM35424_Check_Result(result, "Could not reset DAC");

		printf("Resetting DAC%d.\n", dac_num);

		buffer = (int *)malloc(BUFFER_SIZE_BYTES);

		DM35424_Check_Result(buffer == NULL, "Error allocating space for buffer.");

		result = DM35424_Dac_Volts_To_Conv(2.5f, &max_value);

		DM35424_Check_Result(result,
			     "Error converting value to conversion counts.");

		result = DM35424_Dac_Volts_To_Conv(-2.5f, &min_value);

		DM35424_Check_Result(result,
			     "Error converting value to conversion counts.");


		result = DM35424_Generate_Signal_Data(DM35424_SINE_WAVE,
						      buffer,
						      BUFFER_SIZE_SAMPLES,
						      max_value,
						      min_value,
						      offset,
						      0x0000FFFF);

		DM35424_Check_Result(result,
			     "Error trying to generate data for the DAC.");


		for (channel = 0; channel < my_dac[dac_num].num_dma_channels;
		     channel++) {
			fprintf(stdout,
				"Initializing and configuring DMA Channel %d....",
				channel);

			result =
			    DM35424_Dma_Clear(board, &my_dac[dac_num], channel);

			DM35424_Check_Result(result, "Error clearing DMA");


			result = DM35424_Dma_Write(board,
						   &my_dac[dac_num],
						   channel,
						   BUFFER_0,
						   BUFFER_SIZE_BYTES,
						   buffer);

			DM35424_Check_Result(result, "Writing to DMA buffer failed");

			fprintf(stdout, "Starting DMA Channel %d......",
				channel);

			result =
			    DM35424_Dma_Start(board, &my_dac[dac_num], channel);

			DM35424_Check_Result(result, "Error starting DMA");

			printf("success.\n");

		}

		free(buffer);

		fprintf(stdout, "Starting DAC.\n");

		result = DM35424_Dac_Set_Start_Trigger(board,
						       &my_dac[dac_num],
						       DM35424_CLK_SRC_IMMEDIATE);

		DM35424_Check_Result(result, "Error setting start trigger for DAC.");

		result = DM35424_Dac_Set_Stop_Trigger(board,
						      &my_dac[dac_num],
						      DM35424_CLK_SRC_NEVER);

		DM35424_Check_Result(result, "Error setting stop trigger for DAC.");

		result = DM35424_Dac_Start(board, &my_dac[dac_num]);

		DM35424_Check_Result(result, "Error starting DAC");

	}


	for (channel = 0; channel < my_adc.num_dma_channels; channel++) {

		result = DM35424_Adc_Channel_Setup(board,
							&my_adc,
							channel,
							DM35424_ADC_RNG_UNIPOLAR_5V,
							DM35424_ADC_INPUT_SINGLE_ENDED_POS);

		DM35424_Check_Result(result, "Error setting up channel.");

		if (channel % 2 == 1) {

			printf("Setting ADC Channel %d Filter to Order 7.\n", channel);
			result = DM35424_Adc_Channel_Set_Filter(board,
								&my_adc,
								channel,
								DM35424_ADC_CHAN_FILTER_ORDER7);

			DM35424_Check_Result(result, "Error setting filter order to 7.");
		}

	}


	printf("Starting ADC......\nADC in SINGLE-ENDED POSITIVE mode, using DAC outputs "
		"via physical loopback.\n");

	result = DM35424_Adc_Start(board, &my_adc);

	DM35424_Check_Result(result, "Error starting ADC");


	printf("\nPress Ctrl-C for next mode.\n\n");

	printf("Sample Count");
	for (channel = 0; channel < my_adc.num_dma_channels; channel++) {

		printf("\tVoltage %u.", channel);

	}
	printf("\n");
	printf("============");
	for (channel = 0; channel < my_adc.num_dma_channels; channel++) {

		printf("\t==========");

	}
	printf("\n");

	while (!exit_program) {
		if (last_int_count < interrupt_count) {

			result = DM35424_Adc_Interrupt_Get_Status(board,
								  &my_adc,
								  &int_status);
			DM35424_Check_Result(result, "Error getting interrupt status");

			result = DM35424_Adc_Get_Sample_Count(board,
							      &my_adc,
							      &sample_counts);

			DM35424_Check_Result(result, "Error getting sample count.");

			printf("%12d", sample_counts);

			for (channel = 0; channel < my_adc.num_dma_channels;
			     channel++) {

				result =
				    DM35424_Adc_Channel_Get_Last_Sample(board,
									&my_adc,
									channel,
									&my_value);

				DM35424_Check_Result(result,
					     "Error getting ADC value.");

				result =
				    DM35424_Adc_Sample_To_Volts(DM35424_ADC_RNG_BIPOLAR_2_5V,
				    	    			my_value,
								&volts);

				DM35424_Check_Result(result,
					     "Error converting ADC sample to volts.");

				printf("    %+2.5f    ", volts);

			}
			printf("\r");

			result = DM35424_Adc_Interrupt_Clear_Status(board,
								    &my_adc,
								    int_status);
			DM35424_Check_Result(result, "Error clearing interrupt status");

			last_int_count++;

		}

		DM35424_Micro_Sleep(100);
	}

	exit_program = 0;
	printf("\n\nStopping Adc 0............");

	result = DM35424_Adc_Reset(board, &my_adc);

	DM35424_Check_Result(result, "Error starting ADC");

	printf("success!\n");

	/*********************************************************************
	* SINGLE-ENDED NEGATIVE MODE SETUP
	*********************************************************************/
	for (dac_num = 0; dac_num < DM35424_NUM_DAC_ON_BOARD; dac_num++) {
		result = DM35424_Dac_Reset(board, &my_dac[dac_num]);

		DM35424_Check_Result(result, "Could not reset DAC");

		printf("Resetting DAC%d.\n", dac_num);


		for (channel = 0; channel < my_dac[dac_num].num_dma_channels;
		     channel++) {
			fprintf(stdout,
				"Initializing and configuring DMA Channel %d....",
				channel);

			fprintf(stdout, "Starting DMA Channel %d......",
				channel);

			result =
			    DM35424_Dma_Start(board, &my_dac[dac_num], channel);

			DM35424_Check_Result(result, "Error starting DMA");

			printf("success.\n");

		}

		fprintf(stdout, "Starting DAC.\n");

		result = DM35424_Dac_Set_Start_Trigger(board,
						       &my_dac[dac_num],
						       DM35424_CLK_SRC_IMMEDIATE);

		DM35424_Check_Result(result, "Error setting start trigger for DAC.");

		result = DM35424_Dac_Set_Stop_Trigger(board,
						      &my_dac[dac_num],
						      DM35424_CLK_SRC_NEVER);

		DM35424_Check_Result(result, "Error setting stop trigger for DAC.");

		result = DM35424_Dac_Start(board, &my_dac[dac_num]);

		DM35424_Check_Result(result, "Error starting DAC");


	}


	for (channel = 0; channel < my_adc.num_dma_channels; channel++) {

		result = DM35424_Adc_Channel_Setup(board,
							&my_adc,
							channel,
							DM35424_ADC_RNG_UNIPOLAR_5V,
							DM35424_ADC_INPUT_SINGLE_ENDED_NEG);

		DM35424_Check_Result(result, "Error setting up channel.");

	}


	printf("Starting ADC......\nADC in SINGLE-ENDED NEGATIVE mode, using DAC outputs "
		"via physical loopback.\n");

	result = DM35424_Adc_Start(board, &my_adc);

	DM35424_Check_Result(result, "Error starting ADC");


	printf("\nPress Ctrl-C for next mode.\n\n");

	printf("Sample Count");
	for (channel = 0; channel < my_adc.num_dma_channels; channel++) {

		printf("\tVoltage %u.", channel);

	}
	printf("\n");
	printf("============");
	for (channel = 0; channel < my_adc.num_dma_channels; channel++) {

		printf("\t==========");

	}
	printf("\n");

	while (!exit_program) {
		if (last_int_count < interrupt_count) {

			result = DM35424_Adc_Interrupt_Get_Status(board,
								  &my_adc,
								  &int_status);
			DM35424_Check_Result(result, "Error getting interrupt status");

			result = DM35424_Adc_Get_Sample_Count(board,
							      &my_adc,
							      &sample_counts);

			DM35424_Check_Result(result, "Error getting sample count.");

			printf("%12d", sample_counts);

			for (channel = 0; channel < my_adc.num_dma_channels;
			     channel++) {

				result =
				    DM35424_Adc_Channel_Get_Last_Sample(board,
									&my_adc,
									channel,
									&my_value);

				DM35424_Check_Result(result,
					     "Error getting ADC value.");

				result =
				    DM35424_Adc_Sample_To_Volts(DM35424_ADC_RNG_BIPOLAR_2_5V,
				    	    			my_value,
								&volts);

				DM35424_Check_Result(result,
					     "Error converting ADC sample to volts.");

				printf("    %+2.5f    ", volts);

			}
			printf("\r");

			result = DM35424_Adc_Interrupt_Clear_Status(board,
								    &my_adc,
								    int_status);
			DM35424_Check_Result(result, "Error clearing interrupt status");

			last_int_count++;

		}

		DM35424_Micro_Sleep(100);
	}

	exit_program = 0;
	printf("\n\nStopping ADC 0............");

	result = DM35424_Adc_Reset(board, &my_adc);

	DM35424_Check_Result(result, "Error starting ADC");

	printf("success!\n");

	/*********************************************************************
	* DAC LOOPBACK MODE SETUP
	*********************************************************************/
	for (dac_num = 0; dac_num < DM35424_NUM_DAC_ON_BOARD; dac_num++) {
		result = DM35424_Dac_Reset(board, &my_dac[dac_num]);

		DM35424_Check_Result(result, "Could not reset DAC");

		printf("Resetting DAC%d.\n", dac_num);


		for (channel = 0; channel < my_dac[dac_num].num_dma_channels;
		     channel++) {
			fprintf(stdout,
				"Initializing and configuring DMA Channel %d....",
				channel);

			fprintf(stdout, "Starting DMA Channel %d......",
				channel);

			result =
			    DM35424_Dma_Start(board, &my_dac[dac_num], channel);

			DM35424_Check_Result(result, "Error starting DMA");

			printf("success.\n");

		}

		fprintf(stdout, "Starting DAC.\n");

		result = DM35424_Dac_Set_Start_Trigger(board,
						       &my_dac[dac_num],
						       DM35424_CLK_SRC_IMMEDIATE);

		DM35424_Check_Result(result, "Error setting start trigger for DAC.");

		result = DM35424_Dac_Set_Stop_Trigger(board,
						      &my_dac[dac_num],
						      DM35424_CLK_SRC_NEVER);

		DM35424_Check_Result(result, "Error setting stop trigger for DAC.");

		result = DM35424_Dac_Start(board, &my_dac[dac_num]);

		DM35424_Check_Result(result, "Error starting DAC");


	}


	for (channel = 0; channel < my_adc.num_dma_channels; channel++) {

		result = DM35424_Adc_Channel_Setup(board,
							&my_adc,
							channel,
							DM35424_ADC_RNG_UNIPOLAR_5V,
							DM35424_ADC_INPUT_DAC_LOOPBACK);

		DM35424_Check_Result(result, "Error setting up channel.");

	}


	printf("Starting ADC......\nADC in DAC INTERNAL LOOPBACK mode.  Please remove "
		"physical loopbacks.\n");

	result = DM35424_Adc_Start(board, &my_adc);

	DM35424_Check_Result(result, "Error starting ADC");


	printf("\nPress Ctrl-C to exit program.\n\n");

	printf("Sample Count");
	for (channel = 0; channel < my_adc.num_dma_channels; channel++) {

		printf("\tVoltage %u.", channel);

	}
	printf("\n");
	printf("============");
	for (channel = 0; channel < my_adc.num_dma_channels; channel++) {

		printf("\t==========");

	}
	printf("\n");

	while (!exit_program) {
		if (last_int_count < interrupt_count) {

			result = DM35424_Adc_Interrupt_Get_Status(board,
								  &my_adc,
								  &int_status);
			DM35424_Check_Result(result, "Error getting interrupt status");

			result = DM35424_Adc_Get_Sample_Count(board,
							      &my_adc,
							      &sample_counts);

			DM35424_Check_Result(result, "Error getting sample count.");

			printf("%12d", sample_counts);

			for (channel = 0; channel < my_adc.num_dma_channels;
			     channel++) {

				result =
				    DM35424_Adc_Channel_Get_Last_Sample(board,
									&my_adc,
									channel,
									&my_value);

				DM35424_Check_Result(result,
					     "Error getting ADC value.");

				result =
				    DM35424_Adc_Sample_To_Volts(DM35424_ADC_RNG_BIPOLAR_2_5V,
				    	    			my_value,
								&volts);

				DM35424_Check_Result(result,
					     "Error converting ADC sample to volts.");

				printf("    %+2.5f    ", volts);

			}
			printf("\r");

			result = DM35424_Adc_Interrupt_Clear_Status(board,
								    &my_adc,
								    int_status);
			DM35424_Check_Result(result, "Error clearing interrupt status");

			last_int_count++;

		}

		DM35424_Micro_Sleep(100);

	}

	printf("\n\nStopping ADC 0............");

	result = DM35424_Adc_Reset(board, &my_adc);

	DM35424_Check_Result(result, "Error starting ADC");

	printf("success!\n");

	printf("Disabling interrupt.....");

	result = DM35424_Adc_Interrupt_Set_Config(board,
						  &my_adc,
						  DM35424_ADC_INT_SAMPLE_TAKEN_MASK,
						  INTERRUPT_DISABLE);

	DM35424_Check_Result(result, "Error removing interrupt.");

	printf("success!\nRemoving ISR......");
	result = DM35424_General_RemoveISR(board);

	DM35424_Check_Result(result, "Error removing ISR.");

	printf("success.\n");

	result = DM35424_Gbc_Board_Reset(board);
	printf("Closing Board\n");
	result = DM35424_Board_Close(board);

	DM35424_Check_Result(result, "Error closing board.");
	printf("Example program successfully completed.\n");
	return 0;

}
