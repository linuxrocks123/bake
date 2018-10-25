#ifndef BAKE_UTILITIES_HPP
#define BAKE_UTILITIES_HPP

#include "deplib.hpp"
#include <queue>
#include <tuple>
#include <utility>

using std::pair;
using std::queue;
using std::tuple;

namespace bake_utilities
{
     extern queue<tuple<string,pid_t,time_t>> wait_queue;

     //Should only be needed by Baker.
     /*Given input stream, returns possibly multiline string containing the next command present in this stream.  Throws exception for the following conditions:
       1. Invalid backslash escape.
       2. Sentinel definition inside quoted argument.
       3. Backslash, quote, or less-than included inside sentinel, or sentinel mark not preceded by whitespace (sentinel at beginning of line is legal).
       4. Stream termination before sentinel reached.*/
     string get_command(istream& din) throw(const char*);

     //Given input stream, DepSystem reference, and mutator (for use in Bakelib), augment the DepSystem with the data from the input stream, assumed to be in Baker Interchange Format.
     //Sets values of symbols to their build commands, and sets dep_callback as the callback for any symbols with associated commands.
     void augment_depsystem(istream& din, DepSystem& to_construct, function<string(string)> mutator = [](string symname) noexcept { return symname; }) throw(const char*);

     //Given the passed reference to a DepSystem and passed reference to an ostream, outputs the DepSystem to the ostream in Baker Interchange Format.
     //Mutator mutates symbol names before transmittal.
     //Throws exception if ostream is closed on it.
     //Directly imported into Bakelib.
     void output_depsystem(ostream& dout, const DepSystem& to_output, function<string(string)> mutator = [](string symname) noexcept { return symname; });

     //Parses string parameter and executes it as a command using exec.
     //Pipes the referenced DepSystem to the command's standard input.
     //Returns the read end of another pipe and a pid_t with the PID of the child ready for wait() to be called on it.
     //Uses output_depsystem.
     //Used by dep_callback.
     //Also to be used by Baker's main file like this:
     //1.  Call bakery_execute().
     //2.  Create streambuf from file descriptor.
     //3.  Create istream from streambuf.
     //4.  Call augment_depsystem.
     pair<int,pid_t> bakery_execute(const string& command, const DepSystem& cmd_input = DepSystem());
}

#endif
