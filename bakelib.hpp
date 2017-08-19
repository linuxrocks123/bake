#ifndef BAKELIB_HPP
#define BAKELIB_HPP

#include "bake_utilities.hpp"

namespace bakelib
{
     //Wrapper for bake_utilities::augment_depsystem
     void construct_depsystem(DepSystem& to_construct, function<string(string)> mutator = [](string symname) noexcept { return symname; });

     //Direct importation
     inline void output_depsystem(ostream& dout, const DepSystem& to_output, function<string(string)> mutator = [](string symname) noexcept { return symname; }) { bake_utilities::output_depsystem(dout,to_output,mutator); }
}

#endif
