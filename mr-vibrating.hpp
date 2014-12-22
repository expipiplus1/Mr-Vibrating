#pragma once

#include <array>
#include <cassert>
#include <functional>
#include <string>
#include <sstream>
#include <tuple>
#include <type_traits>
#include <vector>

namespace vibrating {

// Forward declaration
template <class>
struct Option;

//--------------------------------------------------------------------------------------------------
// parse_options takes argc and argv as given to main and a tuple of different Options and an
// optional vector of strings to fill with any positional arguments.
// It returns an empty string if it was able to find all the required options. If for some reason it
// can't do this then it returns a string describing the error.
// Any arguments after "--" are treated as positional arguments
//--------------------------------------------------------------------------------------------------
template <class... OptionTypes>
std::string parse_arguments(const unsigned argc, const char* const* const argv,
                            const std::tuple<Option<OptionTypes>...>& option_specifiers,
                            std::vector<std::string>* positional_arguments = nullptr);

//--------------------------------------------------------------------------------------------------
// usage_string returns a string which describes the program usage for a set of Options
// positional_argument_type is the string used to represent positional options
//--------------------------------------------------------------------------------------------------
template <class... OptionTypes>
std::string usage_string(const std::string program_name,
                         const std::tuple<Option<OptionTypes>...>& option_specifiers,
                         const bool positional_arguments_enabled = false,
                         const std::string positional_argument_type = "file");

//--------------------------------------------------------------------------------------------------
// Option is a struct used to represent a command line switch or value.
// Option is specialized for bool to not require a value to be written as an option
// Boolean options are true iff the flag is present among the options
//--------------------------------------------------------------------------------------------------

template <class T>
struct Option {
  Option(T& value, std::string help_string, std::string long_opt, char short_opt, bool required)
      : Value(value),
        HelpString(std::move(help_string)),
        LongOpt(std::move(long_opt)),
        ShortOpt(short_opt),
        Required(required) {}

  bool ReadValue(const std::string& s) const { return detail::from_string(s, Value); };

  static const bool ReadsValue = true;
  T& Value;
  std::string HelpString;
  std::string LongOpt;
  char ShortOpt;
  bool Required;
};

template <>
struct Option<bool> {
  Option(bool& value, std::string help_string, std::string long_opt, char short_opt)
      : Value(value),
        HelpString(std::move(help_string)),
        LongOpt(std::move(long_opt)),
        ShortOpt(short_opt) {
    // Initialize the value to false, it's only set to true if we see its flag
    Value = false;
  }

  static const bool ReadsValue = false;
  bool& Value;
  std::string HelpString;
  std::string LongOpt;
  char ShortOpt;
  bool Required = false;
};

//--------------------------------------------------------------------------------------------------
// Mr Vibrating Implementation:
//--------------------------------------------------------------------------------------------------

// All the functionality used by the library
namespace detail {

//
// Execute a functor on every element in the tuple
//
template <class F, std::size_t N = 0, class... Ts,
          class = typename std::enable_if<N == sizeof...(Ts)>::type>
void traverse_tuple(F&, const std::tuple<Ts...>&) {}

template <class F, std::size_t N = 0, class... Ts,
          class = typename std::enable_if<(N < sizeof...(Ts))>::type, class = void>
void traverse_tuple(F& f, const std::tuple<Ts...>& ts) {
  f(std::get<N>(ts), N);
  traverse_tuple<F, N + 1, Ts...>(f, ts);
}

//
// The struct with information on how an option was matched, and a function to fill it's value
//
struct OptionMatch {
  bool Matched;
  bool ReadsValue;
  unsigned Index;
  std::function<bool(const std::string&)> FillValue;
};

template <class T, class = typename std::enable_if<Option<T>::ReadsValue>::type>
OptionMatch create_option_match(const Option<T>& opt, const unsigned index) {
  return OptionMatch{true, true, index,
                     std::bind(&Option<T>::ReadValue, &opt, std::placeholders::_1)};
}

template <class T, class = typename std::enable_if<!Option<T>::ReadsValue>::type, class = void>
OptionMatch create_option_match(const Option<T>& opt, const unsigned index) {
  return OptionMatch{true, false, index};
}

//
// Return an OptionMatch for the first option in the tuple to match, or return a false OptionMatch
// if none could be matched
//
template <unsigned N = 0, class... OptionTypes,
          class Z = typename std::enable_if<(N >= sizeof...(OptionTypes))>::type>
OptionMatch find_match(const std::tuple<Option<OptionTypes>...>&, const std::string&) {
  return OptionMatch{false};
}

template <unsigned N = 0, class... OptionTypes,
          class A = typename std::enable_if<(N < sizeof...(OptionTypes))>::type, class = void>
OptionMatch find_match(const std::tuple<Option<OptionTypes>...>& option_specifiers,
                       const std::string& opt) {
  auto& a = std::get<N>(option_specifiers);
  using at = typename std::decay<decltype(a)>::type;
  if ((opt.size() == 1 && a.ShortOpt == opt[0]) || a.LongOpt == opt)
    return create_option_match(a, N);
  return find_match<N + 1>(option_specifiers, opt);
}

//
// A functor to make sure we have all the required options
//
template <unsigned NumOptions>
struct CheckRequiredOptions {
  CheckRequiredOptions(const std::array<bool, NumOptions>& found_options,
                       std::stringstream& error_accumulator)
      : FoundOptions(found_options), ErrorAccumulator(error_accumulator) {}

  template <class T>
  void operator()(Option<T> a, unsigned i) {
    const bool all_ok = FoundOptions[i] || !a.Required;

    if (!all_ok) {
      ErrorAccumulator << "Missing required option \"";
      if (a.LongOpt.empty()) {
        assert(a.ShortOpt != 0);
        ErrorAccumulator << "-" << a.ShortOpt;
      } else {
        ErrorAccumulator << "--" << a.LongOpt;
      }
      ErrorAccumulator << "\"\n";
      Failed = true;
    }
  }

  const std::array<bool, NumOptions>& FoundOptions;
  bool Failed = false;
  std::stringstream& ErrorAccumulator;
};

//
// A functor to determine the longest option length (Used for aligning the help text in the usage
// string)
//
struct MaxOptionLength {
  MaxOptionLength() : MaxLength(0) {}

  template <class T, class = typename std::enable_if<Option<T>::ReadsValue>::type>
  void operator()(Option<T> a, unsigned) {
    MaxLength = std::max(MaxLength, a.LongOpt.length() + 1 + type_string<T>().length());
  }

  template <class T, class = typename std::enable_if<!Option<T>::ReadsValue>::type, class = void>
  void operator()(Option<T> a, unsigned) {
    MaxLength = std::max(MaxLength, a.LongOpt.length());
  }

  std::size_t MaxLength;
};

//
// A functor to accumulate the help strings of all the options
//
struct UsagePrinter {
  UsagePrinter(std::stringstream& usage_accumulator, const std::size_t max_option_length)
      : UsageAccumulator(usage_accumulator), LengthPad(max_option_length) {}

  template <class T>
  void operator()(Option<T> a, unsigned) {
    const bool has_short_opt = a.ShortOpt != '\0';
    const bool has_long_opt = !a.LongOpt.empty();

    // Indent the options
    UsageAccumulator << "  ";

    // Print the short option if we have one
    if (has_short_opt)
      UsageAccumulator << "-" << a.ShortOpt;
    else
      UsageAccumulator << "  ";

    // If we have a long option print it
    if (has_long_opt)
      UsageAccumulator << " --" << a.LongOpt;

    if (Option<T>::ReadsValue)
      UsageAccumulator << " " << type_string<T>();

    // Pad to
    MaxOptionLength m;
    m(a, 0);
    UsageAccumulator << std::string(LengthPad + (has_long_opt ? 2 : 5) - m.MaxLength, ' ');

    // Print the help string
    UsageAccumulator << a.HelpString;

    // If the value isn't required print the given default
    if (!a.Required && !std::is_same<T, bool>::value)
      if (std::is_same<T, std::string>::value)
        UsageAccumulator << " (default: \"" << a.Value << "\")";
      else
        UsageAccumulator << " (default: " << a.Value << ")";

    UsageAccumulator << "\n";
  }

private:
  std::stringstream& UsageAccumulator;
  std::size_t LengthPad;
};

//
// A function to return the string for a particular type, not the mangled stuff from typeinfo
//
template <class T>
inline std::string type_string() {
  return "unknown";
}

#define VIBRATING_TYPE_STRING(type, name)                                                          \
  template <>                                                                                      \
  inline std::string type_string<type>() {                                                         \
    return #name;                                                                                  \
  }
VIBRATING_TYPE_STRING(int, int);
VIBRATING_TYPE_STRING(long, int);
VIBRATING_TYPE_STRING(long long, int);
VIBRATING_TYPE_STRING(unsigned, uint);
VIBRATING_TYPE_STRING(unsigned long, uint);
VIBRATING_TYPE_STRING(unsigned long long, uint);
VIBRATING_TYPE_STRING(float, float);
VIBRATING_TYPE_STRING(double, double);
VIBRATING_TYPE_STRING(long double, double);
VIBRATING_TYPE_STRING(std::string, string);
#undef VIBRATING_TYPE_STRING

//
// A function to act as the inverse of std::to_string
//
template <class T>
bool from_string(const std::string& s, T& t);

template <>
inline bool from_string(const std::string& s, std::string& t) {
  t = s;
  return true;
}

#define VIBRATING_FROM_STRING_INTEGRAL(type, function)                                             \
  template <>                                                                                      \
  inline bool from_string(const std::string& s, type& t) {                                         \
    char* e;                                                                                       \
    t = function(s.c_str(), &e, 0);                                                                \
    return e == &(*s.end());                                                                       \
  }

#define VIBRATING_FROM_STRING_FLOATING(type, function)                                             \
  template <>                                                                                      \
  inline bool from_string(const std::string& s, type& t) {                                         \
    char* e;                                                                                       \
    t = function(s.c_str(), &e);                                                                   \
    return e == &(*s.end());                                                                       \
  }

VIBRATING_FROM_STRING_INTEGRAL(int, std::strtol)
VIBRATING_FROM_STRING_INTEGRAL(long, std::strtol)
VIBRATING_FROM_STRING_INTEGRAL(long long, std::strtoll)
VIBRATING_FROM_STRING_INTEGRAL(unsigned int, std::strtoul)
VIBRATING_FROM_STRING_INTEGRAL(unsigned long, std::strtoul)
VIBRATING_FROM_STRING_INTEGRAL(unsigned long long, std::strtoull)
VIBRATING_FROM_STRING_FLOATING(float, std::strtof)
VIBRATING_FROM_STRING_FLOATING(double, std::strtod)
VIBRATING_FROM_STRING_FLOATING(long double, std::strtold)

#undef VIBRATING_FROM_STRING_FLOATING
#undef VIBRATING_FROM_STRING_INTEGRAL
}

//------------------------------------------------------------------------------
// The main functions
//------------------------------------------------------------------------------

template <class... OptionTypes>
std::string parse_arguments(const unsigned argc, const char* const* const argv,
                            const std::tuple<Option<OptionTypes>...>& option_specifiers,
                            std::vector<std::string>* positional_arguments) {
  // Have we found an option for this member of the tuple?
  std::array<bool, sizeof...(OptionTypes)> found_options{0};

  // Have we passed a "--" argument
  bool end_of_options_reached = false;

  unsigned i = 1;
  while (i < argc) {
    std::string arg(argv[i]);

    //
    // -- : End of options
    // -[^-] : Short option
    // --.+ : Long option
    // Anything else : Positional Argument
    //

    if (arg == "--") {
      end_of_options_reached = true;
      continue;
    }

    std::string opt; // The long or short option if it was found

    // If we've passed the '--' marker nothing is treated as an option
    if (!end_of_options_reached) {
      // -[^-] : Match a short option
      if (arg.size() == 2 && arg[0] == '-' && arg[1] != '-')
        opt = std::string(arg.begin() + 1, arg.end());

      // --.+ Match a long option
      if (arg.size() >= 3 && arg[0] == '-' && arg[1] == '-')
        opt = std::string(arg.begin() + 2, arg.end());
    }

    // If we didn't find an option then this must be a positional argument
    if (opt.empty()) {
      if (positional_arguments)
        positional_arguments->emplace_back(std::move(arg));
      else
        return "Bare option found!";
    } else {
      const detail::OptionMatch match = detail::find_match(option_specifiers, opt);

      if (!match.Matched)
        return "Unrecognized option found: " + opt;

      if (found_options[match.Index])
        return "Duplicate option found: " + opt;
      found_options[match.Index] = true;

      if (match.ReadsValue) {
        // Try to read the next argument as a value
        ++i;
        if (i >= argc)
          return "No value for option " + opt;
        const std::string value(argv[i]);
        if (!match.FillValue(value))
          return "Unable to parse value \"" + value + "\" for option " + opt;
      }
    }

    ++i;
  }

  // Check to see if we missed anything important
  std::stringstream required_options_errors;
  detail::CheckRequiredOptions<sizeof...(OptionTypes)> check_required_options(
      found_options, required_options_errors);
  detail::traverse_tuple(check_required_options, option_specifiers);
  if (check_required_options.Failed)
    return required_options_errors.str();

  // Everything was ok!
  return "";
}

template <class... OptionTypes>
std::string usage_string(const std::string program_name,
                         const std::tuple<Option<OptionTypes>...>& option_specifiers,
                         const bool positional_arguments_enabled,
                         const std::string positional_argument_type) {
  std::stringstream usage;
  usage << "Usage: " << program_name << " [option]...";
  if (positional_arguments_enabled)
    usage << " [--] [" << positional_argument_type << "]...";
  usage << "\n";

  detail::MaxOptionLength m;
  detail::traverse_tuple(m, option_specifiers);
  detail::traverse_tuple(detail::UsagePrinter(usage, m.MaxLength), option_specifiers);

  return usage.str();
}
}
