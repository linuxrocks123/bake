#include "bakelib.hpp"
#include <iostream>

using std::cin;

namespace bakelib
{
     void construct_depsystem(DepSystem& to_construct, function<string(string)> mutator)
     {
          bake_utilities::augment_depsystem(cin,to_construct,mutator);
     }
}
