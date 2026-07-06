/**
    @file

    @brief
        Example program for read the temperature sensor.

    @verbatim

        The program will read the temperature value from the temperature
        function block and print it to the screen.  It will also print the
        maximum and minimum temperature received from the board for the time
        that the example program has been running.

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

    $Id: dm35424_temperature.c 141531 2024-03-06 21:05:25Z lfrankenfield $

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
#include <string.h>

#include "dm35424_gbc_library.h"
#include "dm35424_temperature_library.h"
#include "dm35424_ioctl.h"
#include "dm35424_examples.h"
#include "dm35424_util_library.h"


/**
 * Which temperature function block to open on this board (there
 * is only one).
 */
#define		TEMP_FB_TO_OPEN		0

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
	exit(EXIT_FAILURE);
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
	struct DM35424_Function_Block temp_fb;
	unsigned long int minor = 0;
	int result;

	int help_option_given = 0;

	int status;
	struct sigaction signal_action;

	float temperature;
	float max_temp = 0;
	float min_temp = 1000.0f;
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

	printf("Opening board.....");
	result = DM35424_Board_Open(minor, &board);

	DM35424_Check_Result(result, "Could not open board");
	printf("success.\nResetting board.....");
	result = DM35424_Gbc_Board_Reset(board);

	DM35424_Check_Result(result, "Could not reset board");
	printf("success.\nOpening Temperature Function Block....");

	result = DM35424_Temperature_Open(board,
						TEMP_FB_TO_OPEN,
						&temp_fb);

	DM35424_Check_Result(result, "Could not open temperature function block.");
	printf("success.\n");

	printf("\nPress Ctrl-C to exit.\n\n");
	printf("Temp (C)\t Min (C)\t Max (C)");
	printf("\n");
	printf("========\t========\t========");
	printf("\n");

	while (!exit_program) {
		result = DM35424_Temperature_Read(board, &temp_fb, &temperature);
		DM35424_Check_Result(result, "Could not read temperature");

		if (temperature < min_temp) {
			min_temp = temperature;
		}

		if (temperature > max_temp) {
			max_temp = temperature;
		}

		fprintf(stdout, "   %3.2f\t   %3.2f\t   %3.2f\r", temperature,
			min_temp, max_temp);
		fflush(stdout);
		DM35424_Micro_Sleep(500000);
	}

	printf("\n\n");

	result = DM35424_Gbc_Board_Reset(board);
	printf("Closing Board\n");
	result = DM35424_Board_Close(board);

	DM35424_Check_Result(result, "Error closing board.");
	printf("Example program successfully completed.\n");
	return 0;

}
