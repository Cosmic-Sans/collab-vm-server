#pragma once
#include <iomanip>
#include <sstream>
#include <string>

// Taken from GNU Cgicc
/*
   From the HTML standard:
   <http://www.w3.org/TR/html4/interact/forms.html#h-17.13.4.1>
   application/x-www-form-urlencoded
   This is the default content type. Forms submitted with this content
   type must be encoded as follows:
   1. Control names and values are escaped. Space characters are
   replaced by `+', and then reserved characters are escaped as
   described in [RFC1738], section 2.2: Non-alphanumeric characters
   are replaced by `%HH', a percent sign and two hexadecimal digits
   representing the ASCII code of the character. Line breaks are
   represented as "CR LF" pairs (i.e., `%0D%0A').
   2. The control names/values are listed in the order they appear in
   the document. The name is separated from the value by `=' and
   name/value pairs are separated from each other by `&'.
   Note RFC 1738 is obsoleted by RFC 2396.  Basically it says to
   escape out the reserved characters in the standard %xx format.  It
   also says this about the query string:
   
   query         = *uric
   uric          = reserved | unreserved | escaped
   reserved      = ";" | "/" | "?" | ":" | "@" | "&" | "=" | "+" |
   "$" | ","
   unreserved    = alphanum | mark
   mark          = "-" | "_" | "." | "!" | "~" | "*" | "'" |
   "(" | ")"
   escaped = "%" hex hex */

std::string form_urlencode(const std::string& src) {
  std::ostringstream result;
  result.fill('0');
  result << std::hex;

  for (const auto& c : src) {
    switch (c) {
      case ' ':
        result << '+';
        break;
        // alnum
      case 'A':
      case 'B':
      case 'C':
      case 'D':
      case 'E':
      case 'F':
      case 'G':
      case 'H':
      case 'I':
      case 'J':
      case 'K':
      case 'L':
      case 'M':
      case 'N':
      case 'O':
      case 'P':
      case 'Q':
      case 'R':
      case 'S':
      case 'T':
      case 'U':
      case 'V':
      case 'W':
      case 'X':
      case 'Y':
      case 'Z':
      case 'a':
      case 'b':
      case 'c':
      case 'd':
      case 'e':
      case 'f':
      case 'g':
      case 'h':
      case 'i':
      case 'j':
      case 'k':
      case 'l':
      case 'm':
      case 'n':
      case 'o':
      case 'p':
      case 'q':
      case 'r':
      case 's':
      case 't':
      case 'u':
      case 'v':
      case 'w':
      case 'x':
      case 'y':
      case 'z':
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        // mark
      case '-':
      case '_':
      case '.':
      case '!':
      case '~':
      case '*':
      case '\'':
      case '(':
      case ')':
        result << c;
        break;
        // escape
      default:
        result << std::uppercase;
        result << '%' << std::setw(2) << int((unsigned char)c);
        result << std::nouppercase;
        break;
    }
  }

  return result.str();
}