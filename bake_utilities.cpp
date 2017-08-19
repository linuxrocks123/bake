#include "bake_utilities.hpp"
#include <ctime>
#include <ext/stdio_filebuf.h>
#include <queue>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using std::queue;
using __gnu_cxx::stdio_filebuf;
using std::time;

namespace bake_utilities
{
     static void scan_line(vector<string>& tokens, queue<string>& sentinels, const string& line) throw(const char*)
     {
          bool backslash_escape = false;
          bool inside_double_quote = false;
          string current_token, sentinel_mark;
          for(int i=0; i<line.size(); i++)
          {
               switch(line[i])
               {
               case '<':
                    //Check if it's a sentinel.
                    if(backslash_escape || i+1==line.size() || line[i+1]!='<')
                    {
                         backslash_escape=false;
                         break;
                    }

                    //Check if it's not preceded by whitespace
                    if(i!=0 && line[i-1]!=' ' && line[i-1]!='\t')
                         throw "Sentinel not preceded by whitespace.";

                    //Construct sentinel mark.
                    sentinel_mark="";
                    for(i+=2; i<line.size(); i++)
                         if(line[i]=='<' || line[i]=='"' || line[i]=='\\')
                              throw "Invalid character in sentinel";
                         else if(line[i]==' ' || line[i]=='\t')
                         {
                              while(i<line.size() && (line[i]==' ' || line[i]=='\t'))
                                   i++;
                              i--;
                              break;
                         }
                         else
                              sentinel_mark+=line[i];

                    if(sentinel_mark=="")
                         throw "Empty sentinel";
                    sentinels.push(sentinel_mark);
                    tokens.push_back(string("\n")+sentinel_mark);
                    continue;
               case '\\':
                    backslash_escape=!backslash_escape;
                    continue;
               case '"':
                    if(!backslash_escape)
                    {
                         inside_double_quote=!inside_double_quote;
                         continue;
                    }
                    else
                         backslash_escape=false;
                    break;
               case ' ': case '\t':
                    if(backslash_escape)
                         throw "Invalid backslash escape.";
                    if(!inside_double_quote)
                    {
                         if(i!=0)
                              tokens.push_back(current_token);
                         current_token="";
                         while(i<line.size() && (line[i]==' ' || line[i]=='\t')) i++;
                         i--;
                         continue;
                    }
                    break;
               default:
                    if(backslash_escape)
                         throw "Invalid backslash escape.";
               }

               current_token+=line[i];
          }

          if(current_token!="")
               tokens.push_back(current_token);
          return;
     }

     string get_command(istream& din) throw(const char*)
     {
          queue<string> sentinels;
          vector<string> discard;
          string to_return;
          string line;
          getline(din,line);
          to_return+=line+"\n";
          scan_line(discard,sentinels,line);
          while(din.good() && sentinels.size())
          {
               getline(din,line);
               to_return+=line+"\n";
               if(sentinels.size() && sentinels.front()==line)
                    sentinels.pop();
          }

          if(din.bad() && to_return!="\n")
               throw "EOF reached while reading sentinel.";

          return to_return;
     }

     //Function for use as DepSystem callback.
     //Executes string value symval as command using exec, waits on it, and throws an exception if the command exited with error.  Also uses stat() to check that the file symname has been built successfully (and that the modification time has changed to near the present).
     static void dep_callback(string symname, string symval)
     {
          //Throw exception immediately if symval is the empty string
          if(symval=="")
               throw StringFunctions::permanent_c_str(symname+": No rule to build target.");

          time_t before_build = time(NULL);
          pair<int,pid_t> build_result = bakery_execute(symval);
          siginfo_t child_status;
          waitid(P_PID,build_result.second,&child_status,WEXITED);
          close(build_result.first); //we'll never need this
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

     void augment_depsystem(istream& din, DepSystem& to_construct, function<string(string)> mutator) throw(const char*)
     {
          auto add_if_not_present = [&to_construct](string symname, string symval)
          {
               if(!to_construct.has_symbol(symname))
               {
                    if(symname.substr(0,3)=="../")
                         throw "Attempted to add symbol outside working directory.";

                    to_construct.add_set_symbol(symname,symval);
                    to_construct.set_callback(symname,dep_callback);
               }
          };

          //Note: this code doesn't yet handle really bad filenames which need sentinels to represent
          string line = get_command(din);
          while(line!="\n") //It's \n when we read EOF
          {
               vector<string> tokens;
               StringFunctions::tokenize(tokens,line);
               if(!tokens.size())
                    continue;
               if(tokens.size()==1 || tokens[1]!="/")
               {
                    to_construct.add_set_symbol(mutator(tokens[0]),line.substr(tokens[0].size()+(tokens.size()==1 ? 0 : 1)));
                    to_construct.set_callback(mutator(tokens[0]),dep_callback);
               }
               else
               {
                    if(tokens.size()!=3)
                         throw "Invalid dependency specification.";
                    add_if_not_present(mutator(tokens[0]), "");
                    add_if_not_present(mutator(tokens[2]), "");
                    if(mutator(tokens[2]).substr(0,3)=="../" && !to_construct.has_dependency(mutator(tokens[2]),mutator(tokens[0])))
                         throw "Attempted to add dependency to symbol outside working directory.";
                    to_construct.add_dependency(mutator(tokens[2]),mutator(tokens[0])); //yes the order is right
               }
               line = get_command(din);
          }
     }

     void output_depsystem(ostream& dout, const DepSystem& to_output, function<string(string)> mutator)
     {
          vector<string> symbols = to_output.get_symbols();
          for(const string& sym : symbols)
          {
               dout << mutator(sym) << ' ' << to_output.get_value(sym) << endl;
               for(const string& depsym : to_output.get_dependency_edges(sym))
                    dout << mutator(depsym) << " / " << mutator(sym) << endl;
          }
     }

     static int exec_wrapper(const vector<string>& tokens)
     {
          char* filename = StringFunctions::permanent_c_str(tokens[0]);
          char** args = new char*[tokens.size()+1];
          for(int i=0; i<tokens.size(); i++)
               args[i] = StringFunctions::permanent_c_str(tokens[i]);
          args[tokens.size()]=NULL;
          return execvp(filename,args);
     }

     pair<int,pid_t> bakery_execute(const string& command, const DepSystem& cmd_input)
     {
          //Get the arguments for exec in tokens[]
          vector<string> lines;
          vector<string> tokens;
          StringFunctions::strsplit(lines,command,"\n");
          queue<string> discard;
          scan_line(tokens,discard,lines[0]);
          for(int i=0,j=1; i<tokens.size(); i++)
               if(tokens[i][0]=='\n') //indicates sentinel
               {
                    string sentinel = tokens[i].substr(1);
                    tokens[i]="";
                    while(lines[j]!=sentinel)
                    {
                         if(j>=lines.size())
                              throw "Unterminated sentinel: "+sentinel;
                         tokens[i]+=lines[j]+"\n";
                         j++;
                    }
                    j++;
                    tokens[i] = tokens[i].substr(0,tokens[i].size()-1);
               }

          //Now we create pipes and do fork/exec
          int to_child, to_parent;
          int parent_writes[2];
          int child_writes[2];
          pipe(parent_writes);
          pipe(child_writes);
          pid_t child_id = fork();

          if(child_id!=0) //we are the parent
          {
               //Set up pipe communication
               to_child = parent_writes[1];
               to_parent = child_writes[0];
               close(parent_writes[0]);
               close(child_writes[1]);

               //Set up ostream to child; call output_depsystem.
               //This will block if the child does not read its pipe.
               stdio_filebuf<char> child_out_(to_child,std::ios::out);
               ostream child_out(&child_out_);
               output_depsystem(child_out,cmd_input);

               //Flush ostream, then close corresponding file descriptor
               child_out.flush();
               close(to_child);

               //Return child's ID and read end of child's pipe
               return pair<int,pid_t>(to_parent,child_id);
          }
          else //we are the child
          {
               //Set up pipe communication
               to_child = parent_writes[0];
               to_parent = child_writes[1];
               close(parent_writes[1]);
               close(child_writes[0]);

               //Redirect pipes to standard in and standard out
               dup2(to_child,STDIN_FILENO);
               dup2(to_parent,STDOUT_FILENO);

               //Execute child: exec_wrapper() does not return if the child's main process is able to be executed.
               exec_wrapper(tokens);

               //If exec_wrapper() returns, it means something went wrong, most likely a typo in the Bakefile.  Exit with error.
               exit(1);
          }
     }
}
