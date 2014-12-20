# Mr Vibrating

A library for parsing command line arguments

## Usage

Call `vibrating::parse_argument` with `argc`, `argv` and a tuple of Options.
One can also pass this same tuple to `vibrating::usage_string` to generate a usage string for the 
program
An Option is constructed by passing it a reference to a variable to fill, some help text and a long
and short argument.

