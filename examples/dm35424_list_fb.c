/**
    @file

    @brief
        Example program which demonstrates use of the library to
        open a function block for use.

    @verbatim

        This example program uses the board library to query all
        function blocks on the board.  When a function block is opened
        that has a valid function type, then the number of DMA channels
        and buffers is printed to the screen.  In this way, the example
        program shows an inventory of the function blocks on a given
        board.

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

    $Id: dm35424_list_fb.c 141531 2024-03-06 21:05:25Z lfrankenfield $
*/

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <error.h>
#include <unistd.h>
#include <limits.h>
#include <getopt.h>

#include "dm35424_gbc_library.h"
#include "dm35424_ioctl.h"
#include "dm35424_examples.h"
#include "dm35424_util_library.h"

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

	unsigned long int minor = 0;
	int result;
	int fb_num = 0;
	struct DM35424_Board_Descriptor *board;
	struct DM35424_Function_Block my_func_block;
	char errMsg[300];
	uint8_t rev_num;
	uint32_t pdp_num, fpga_num;
	int help_option_given = 0;

	int status;

	char *invalid_char_p;
	struct option options[] = {
		{"help", 0, 0, 1},
		{"minor", 1, 0, 2},
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

	printf("Opening board.....");
	result = DM35424_Board_Open(minor, &board);

	DM35424_Check_Result(result, "Could not open board");

	printf("success.\nResetting board.....");
	result = DM35424_Gbc_Board_Reset(board);

	DM35424_Check_Result(result, "Could not reset board");
	printf("success.\n\n");

	result = DM35424_Gbc_Get_Revision(board, &rev_num);
	DM35424_Check_Result(result, "Error getting board revision number");

	result = DM35424_Gbc_Get_Fpga_Build(board,
						&fpga_num);
	DM35424_Check_Result(result, "Error getting FPGA build number.");

	result = DM35424_Gbc_Get_Pdp_Number(board,
						&pdp_num);
	DM35424_Check_Result(result, "Error getting PDP number.");

	printf("FPGA Build: %u\n", fpga_num);
	printf("PDP Number: %u rev %c\n\n", pdp_num, 'A' + (rev_num - 1));

	printf
	    ("\nListing Function Blocks\n====================================================\n");

	fb_num = 0;
	while ((fb_num < DM35424_MAX_FB) && (errno != ERANGE)) {
		result = DM35424_Function_Block_Open(board,
						     fb_num, &my_func_block);

		/*
		 * Because the GBC size is allowed to be less than what it
		 * would take to define all possible function blocks, we
		 * need to capture that error case and not report it.
		 * It just means we're done finding function blocks.
		 */
		if (errno != ERANGE) {

			sprintf(errMsg, "Could not open function block %d.",
				fb_num);
			DM35424_Check_Result(result, errMsg);

			switch (my_func_block.type) {

			case DM35424_FUNC_BLOCK_ADC:
				printf
				    ("  FB%d: ADC (Type 0x%x Subtype %d), with %d DMA Channels (%d buffers each)\n",
				     my_func_block.fb_num,
				     my_func_block.type,
				     my_func_block.sub_type,
				     my_func_block.num_dma_channels,
				     my_func_block.num_dma_buffers);
				break;
			case DM35424_FUNC_BLOCK_DAC:
				printf
				    ("  FB%d: DAC (Type 0x%x Subtype %d), with %d DMA Channels (%d buffers each)\n",
				     my_func_block.fb_num,
				     my_func_block.type,
				     my_func_block.sub_type,
				     my_func_block.num_dma_channels,
				     my_func_block.num_dma_buffers);
				break;
			case DM35424_FUNC_BLOCK_DIO:
				printf
				    ("  FB%d: DIO (Type 0x%x Subtype %d), with %d DMA Channels (%d buffers each)\n",
				     my_func_block.fb_num,
				     my_func_block.type,
				     my_func_block.sub_type,
				     my_func_block.num_dma_channels,
				     my_func_block.num_dma_buffers);
				break;
			case DM35424_FUNC_BLOCK_REF_ADJUST:
				printf
				    ("  FB%d: Ref. Adj. Module (Type 0x%x Subtype %d), with %d DMA Channels (%d buffers each)\n",
				     my_func_block.fb_num,
				     my_func_block.type,
				     my_func_block.sub_type,
				     my_func_block.num_dma_channels,
				     my_func_block.num_dma_buffers);
				break;
			case DM35424_FUNC_BLOCK_TEMPERATURE_SENSOR:
				printf
				    ("  FB%d: Temp. Sensor Module (Type 0x%x Subtype %d), with %d DMA Channels (%d buffers each)\n",
				     my_func_block.fb_num,
				     my_func_block.type,
				     my_func_block.sub_type,
				     my_func_block.num_dma_channels,
				     my_func_block.num_dma_buffers);
				break;
			case DM35424_FUNC_BLOCK_INVALID:
				// Do nothing
				break;
			default:
				printf("  FB%d: **Unknown module type (0x%x)\n",
				       my_func_block.fb_num,
				       my_func_block.type);
				break;
			}

			fb_num++;
		}

	}

	result = DM35424_Gbc_Board_Reset(board);
	printf("\nClosing Board\n");
	result = DM35424_Board_Close(board);

	DM35424_Check_Result(result, "Error closing board.");
	printf("Example program successfully completed.\n");
	return 0;

}
