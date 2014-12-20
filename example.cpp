#include "mr-vibrating.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  using vibrating::Option;

  bool mr_flag;
  bool display_usage;
  int required_int;
  std::string optional_string = "default";
  std::vector<std::string> positional_arguments;

  const auto opts = std::make_tuple(
      Option<bool>(display_usage, "Display usage string and exit", "usage", 'u'),
      Option<bool>(mr_flag, "Set mr_flag to true", "flag", 'f'),
      Option<int>(required_int, "An required integer parameter", "number", 'n', true),
      Option<std::string>(optional_string, "An optional string", "optional-string", 's', false));

  std::string arg_error = vibrating::parse_arguments(argc, argv, opts, &positional_arguments);

  if (!arg_error.empty()) {
    std::cout << arg_error << "\n";
    std::cout << vibrating::usage_string("ExampleProgram", opts, true);
    return 1;
  }

  if (display_usage) {
    std::cout << vibrating::usage_string("ExampleProgram", opts, true);
    return 0;
  }
}
