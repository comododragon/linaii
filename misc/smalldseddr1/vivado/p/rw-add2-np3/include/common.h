/* ********************************************************************************************* */
/* * Common macros for nice codes.                                                             * */
/* * Author: André Bannwart Perina                                                             * */
/* ********************************************************************************************* */
/* * Copyright (c) 2017 André B. Perina                                                        * */
/* *                                                                                           * */
/* * Permission is hereby granted, free of charge, to any person obtaining a copy of this      * */
/* * software and associated documentation files (the "Software"), to deal in the Software     * */
/* * without restriction, including without limitation the rights to use, copy, modify,        * */
/* * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to        * */
/* * permit persons to whom the Software is furnished to do so, subject to the following       * */
/* * conditions:                                                                               * */
/* *                                                                                           * */
/* * The above copyright notice and this permission notice shall be included in all copies     * */
/* * or substantial portions of the Software.                                                  * */
/* *                                                                                           * */
/* * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,       * */
/* * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR  * */
/* * PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE * */
/* * FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR      * */
/* * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER    * */
/* * DEALINGS IN THE SOFTWARE.                                                                 * */
/* ********************************************************************************************* */

#ifndef COMMON_H
#define COMMON_H

/**
 * @brief If coloured mode is activated, these constants will be effective.
 */
#ifdef COMMON_COLOURED_PRINTS

/**
 * @brief Colour constants. If used before a string, it changes its color on a
 * colour-enabled terminal.
 */
#define ANSI_COLOUR_BLACK   "\x1b[30m"
#define ANSI_COLOUR_RED     "\x1b[31m"
#define ANSI_COLOUR_GREEN   "\x1b[32m"
#define ANSI_COLOUR_YELLOW  "\x1b[33m"
#define ANSI_COLOUR_BLUE    "\x1b[34m"
#define ANSI_COLOUR_MAGENTA "\x1b[35m"
#define ANSI_COLOUR_CYAN    "\x1b[36m"
#define ANSI_COLOUR_WHITE   "\x1b[37m"
#define ANSI_COLOUR_RESET   "\x1b[0m"

#else

#define ANSI_COLOUR_BLACK   ""
#define ANSI_COLOUR_RED     ""
#define ANSI_COLOUR_GREEN   ""
#define ANSI_COLOUR_YELLOW  ""
#define ANSI_COLOUR_BLUE    ""
#define ANSI_COLOUR_MAGENTA ""
#define ANSI_COLOUR_CYAN    ""
#define ANSI_COLOUR_WHITE   ""
#define ANSI_COLOUR_RESET   ""

#endif

/**
 * @brief Assert a condition.
 * @param cond Condition to be asserted.
 * @note This macro will jump to a label called _err if @p cond fails. Make sure to declare this label and use it wisely to handle post-error procedures.
 */
#define ASSERT(cond) {\
	if(!(cond)) {\
		goto _err;\
	}\
}

/**
 * @brief Assert a condition. If it's false, run callback statements.
 * @param cond Condition to be asserted.
 * @param callbacks Statements to be executed if @p cond is false. Statements might be separated by semicolon.
 * @note This macro will jump to a label called _err if @p cond fails. Make sure to declare this label and use it wisely to handle post-error procedures.
 */
#define ASSERT_CALL(cond, callbacks) {\
	if(!(cond)) {\
		callbacks;\
		goto _err;\
	}\
}

/**
 * @brief Print a nice line for indicating steps. E.g.: [    ] Your formatted text goes here.
 * @param ... Standard arguments for printf.
 */
#define PRINT_STEP(...) {\
	printf("[    ] " __VA_ARGS__);\
}

/**
 * @brief Indicate success on a previous PRINT_STEP call. E.g.: [ OK ] Your formatted text goes here.
 */
#define PRINT_SUCCESS() {\
	printf("\r[ " ANSI_COLOUR_GREEN "OK" ANSI_COLOUR_RESET "\n");\
}

/**
 * @brief Indicate failure on a previous PRINT_STEP call. E.g.: [FAIL] Your formatted text goes here.
 */
#define PRINT_FAIL() {\
	printf("\r[" ANSI_COLOUR_RED "FAIL" ANSI_COLOUR_RESET "\n");\
}

#endif