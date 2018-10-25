#include "bakelib.hpp"
#include "bake_utilities.hpp"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <ext/stdio_filebuf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using std::cerr;
using std::cout;
using std::cin;
using std::endl;
using std::function;
using std::getenv;
using std::ifstream;
//using std::setenv;
using std::strcmp;
using std::strlen;

using __gnu_cxx::stdio_filebuf;

//Usage: bake, bake -sub dir, bake target
int main(int argc, char** argv)
{
     //Command line parameters
     string target = "";
     string subdir = "";
     string filename = "Bakefile";

     //Parse our command line
     int i=1;
     try
     {
          while(i<argc)
          {
               if(strcmp(argv[i],"-f")==0)
               {
                    if(i+1==argc || filename!="Bakefile") throw i;
                    i++;
                    filename=argv[i];
               }
               else if(strcmp(argv[i],"-sub")==0 || subdir!="")
               {
                    if(i+1==argc) throw i;
                    i++;
                    subdir=argv[i];
               }
               else
               {
                    if(target!="") throw i;
                    target=argv[i];
               }

               i++;
          }
     }
     catch(int x)
     {
          cerr << argv[0] << ": Invalid invocation at parameter " << x << endl;
          return 1;
     }

try {
     /*Okay, we've parsed our command line.
       Now, let's get cracking.*/

     //Create our DepSystem
     DepSystem dep_tree;

     //If we were called with -sub, do initial sub processing.
     if(subdir!="")
     {
          //Check that the subdir actually exists and is a directory.
          struct stat subdir_info;
          if(stat(subdir.c_str(),&subdir_info)==-1)
          {
               cerr << argv[0] << ": Error accessing directory " << subdir << endl;
               return 1;
          }

          //Check that the file we got is actually a directory.
          if(!S_ISDIR(subdir_info.st_mode))
          {
               cerr << argv[0] << ": " << subdir << ": Not a directory.\n";
               return 1;
          }
          
          //Change our working directory to subdir
          /*Note that any use of setenv causes a memory leak, but it's okay
            since we only change our directory once.  Use putenv if you
            want to avoid memory leaks.*/
          chdir((string(getenv("PWD"))+"/"+subdir).c_str());

          //Construct initial depsystem by reading dep_tree from standard in
          //Mutate dep_tree by prefacing symbols with "../"
          bakelib::construct_depsystem(dep_tree,[](string symname) noexcept { return string("../")+symname; });
     }

     //Open our Bakefile
     ifstream fin(filename);

     //Iteratively augment dep_tree by executing commands in Bakefile
     while(fin.good())
     {
          string next_command = bake_utilities::get_command(fin);
          if(next_command=="\n" || next_command[0]=='#')
               continue;
          pair<int,pid_t> cmd_result = bake_utilities::bakery_execute(next_command,dep_tree);

          //Read our pipe
          stdio_filebuf<char> child_in_(cmd_result.first,std::ios::in);
          istream child_in(&child_in_);
          bake_utilities::augment_depsystem(child_in,dep_tree);

          //Ensure command completed normally by waiting on child.
          siginfo_t child_status;
          waitid(P_PID,cmd_result.second,&child_status,WEXITED);
          if(child_status.si_code!=CLD_EXITED)
          {
               cerr << next_command << ": terminated by signal " << child_status.si_status << endl;
               return 1;
          }
          else if(child_status.si_status!=0)
          {
               cerr << next_command  << ": exited with abnormal status " << child_status.si_status << endl;
               return 1;
          }
     }

     if(subdir=="")
     {
          //Start valid, then set to other status later if required.
          for(const string& symname : dep_tree.get_symbols())
               dep_tree.set_state(symname,DepSystem::VALID);

          //Go through and stat every target, setting dep_tree symbol statuses accordingly.
          for(const string& symname : dep_tree.get_symbols())
          {
               struct stat statbuf;
               int retval = stat(symname.c_str(),&statbuf);
               if(retval!=0)
               {
                    dep_tree.set_state(symname,DepSystem::NONBUILT);
                    dep_tree.invalidate_dependents(symname);
                    continue;
               }

               //See if any of our dependencies was modified after us.
               time_t sym_mtime = statbuf.st_mtime;
               for(const string& depname : dep_tree.get_dependency_edges(symname))
               {
                    retval = stat(depname.c_str(),&statbuf);
                    if(retval==0 && sym_mtime < statbuf.st_mtime)
                    {
                         dep_tree.set_state(symname,DepSystem::STALE);
                         dep_tree.invalidate_dependents(symname);
                         break;
                    }
               }
          }

          //Actually execute build plan
          vector<string> symbols_remaining;
          if(target!="")
               symbols_remaining = dep_tree.get_build_plan(target);
          else
               symbols_remaining = dep_tree.get_symbols();

          while(symbols_remaining.size())
          {
               DepSystem temp_deptree = dep_tree;
               for(auto i = symbols_remaining.begin(); i!=symbols_remaining.end();)
               {
                    vector<string> build_plan = temp_deptree.get_build_plan(*i);
                    if(build_plan.size()==1)
                         dep_tree.build_symbol(*i);
                    if(build_plan.size()==0)
                         i = symbols_remaining.erase(i);
                    else
                         ++i;
               }

               while(bake_utilities::wait_queue.size())
               {
                    tuple<string,pid_t,time_t> build_result = bake_utilities::wait_queue.front();
                    string& symname = std::get<0>(build_result);
                    pid_t& child_pid = std::get<1>(build_result);
                    time_t& before_build = std::get<2>(build_result);
                    bake_utilities::wait_queue.pop();

                    siginfo_t child_status;
                    waitid(P_PID,child_pid,&child_status,WEXITED);
                    if(child_status.si_code!=CLD_EXITED || child_status.si_status!=0)
                         throw StringFunctions::permanent_c_str(symname+": build failure.");
               
                    //Okay, build exited normally.  Check if file modified.
                    /*Note: If this behavior is found to sometimes be undesirable, perhaps a global option could disable it.
                      Then again, if this behavior is found by someone to be undesirable, perhaps that person is doing it wrong.*/
                    struct stat status;
                    stat(symname.c_str(),&status);
                    if(status.st_mtime < before_build)
                         throw StringFunctions::permanent_c_str(symname+": build appeared to complete successfully but did not modify file.");
               }
          }
     }
     else //We were invoked with -sub, so output dep_tree to handler
     {
          //TODO: need to parrot back anything from standard in unmodified, right?
          
          auto output_mutator = [&subdir](string symname) noexcept
          {
               if(symname.find("../")==0)
                    return symname.substr(strlen("../"));
               else
                    return subdir+"/"+symname;
          };

          bakelib::output_depsystem(cout,dep_tree,output_mutator);
     }
}
catch(const char* e)
{
     cerr << e << endl;
     return 1;
}

     return 0;
}
