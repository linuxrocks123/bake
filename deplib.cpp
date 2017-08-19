#include "deplib.hpp"
#include "StringFunctions.h"
#include <algorithm>

using std::find;
using std::find_if;
using StringFunctions::peekline;

bool DepSystem::has_symbol(const string& name) const noexcept
{
	 return symbols.count(Symbol{name});
}

string DepSystem::get_value(const string& symbol_name) const throw(const char*)
{
	 if(!symbols.count(Symbol{symbol_name}))
		  throw "get_value() called with nonexistent symbol name!";
	 return symbols.find(Symbol{symbol_name})->value;
}

void DepSystem::add_set_symbol(const string& name, const string& value) throw(const char*)
{
	 Symbol to_add{name,value,VALID};
	 if(!symbols.count(to_add))
	 {
		  if(shadowers.count(name)) //handle case where we "shadow" less specific symbols
		  {
			   auto shadow_range = shadowers.equal_range(name); //all symbols listing us as a shadower
			   for(auto i = shadow_range.first; i!=shadow_range.second; ++i)
			   {
					Symbol affected = *symbols.find(Symbol{i->second}); //a symbol who said we would be a shadower
					for(vector<string> deplist : affected.dependency_list_list)
					{
						 //We find our name in the affected symbol's list so we can find the symbol we shadow, if any
						 auto pos = find_if(deplist.begin(),deplist.end(),[&](const string& val) { return symbols.count(Symbol{val}) || val==name; });
						 if(pos==deplist.end() || *pos!=name)
							  continue;

						 //Okay, we found ourselves: now find the shadowed symbol
						 pos = find_if(pos,deplist.end(), [&](const string& val) { return symbols.count(Symbol{val}); });

						 //There might not be a shadowed symbol.
						 /*If there is, erase its reverse dependency on the affected symbol.
						   We are shadowing it: we take that reverse dependency for ourselves.
						 */
						 if(pos!=deplist.end())
						 {
							  Symbol shadowed = *symbols.find(Symbol{*pos});
							  shadowed.reverse_dependency_list_set.erase(affected.name);
							  symbols.erase(shadowed);
							  symbols.insert(shadowed);
						 }

						 //Add the reverse dependency on the affected symbol to our own revdep list set.
						 to_add.reverse_dependency_list_set.insert(affected.name);
					}
			   }

			   //Since we are being added, we are no longer a (potential, nonexistent at the present time) shadower.
			   shadowers.erase(name);
		  }
		  
		  //We're a new symbol: add ourselves to the symbols set.
		  symbols.insert(to_add);

		  //We may have dependents already due to dependency lists: if we do, we need to invalidate them.
		  invalidate_dependents(name);
	 }
	 else if(symbols.find(to_add)->value==value) //nothing to do: this is a no-op
		  return;
	 else //We're not new, but we changed our value: replace ourselves in symbols set.
	 {
		  //Get to_add from symbols set and make necessary modifications
		  to_add = *symbols.find(to_add);
		  to_add.value = value;

		  //See if our new status should be DISABLED or VALID.
		  //If we have dependents, we need to be DISABLED; otherwise, VALID.
		  if(to_add.dependency_edges.size() || [&]()
			 {    for(auto deplist : to_add.dependency_list_list)
					   for(string depsym : deplist)
							if(symbols.count(Symbol{depsym}))
								 return true;
				  return false; }())
			   to_add.state = DISABLED;
		  else
			   to_add.state = VALID;

		  //Reinsert ourselves into symbols set.
		  symbols.erase(to_add);
		  symbols.insert(to_add);

		  invalidate_dependents(name); //we changed: invalidate our dependents
	 }
}

void DepSystem::delete_symbol(const string& name) throw(const char*)
{
	 if(!symbols.count(Symbol{name}))
		  throw "delete_symbol() called with nonexistent symbol name!";

	 auto ptr_erase = symbols.find(Symbol{name});
	 Symbol to_delete = *ptr_erase;

	 //First, delete ourselves from the symbols array.
	 symbols.erase(to_delete);

	 //Delete ourselves from the reverse dependency lists of our dependencies
	 for(const string& dependency : to_delete.dependency_edges)
	 {
		  Symbol to_modify = *symbols.find(Symbol{dependency});
		  to_modify.reverse_dependency_edges.erase(name);
		  symbols.erase(to_modify);
		  symbols.insert(to_modify);
	 }

	 //Now delete ourselves from the dependency lists of our reverse dependencies
	 for(string revdep : to_delete.reverse_dependency_edges)
	 {
		  Symbol to_modify = *symbols.find(Symbol{revdep});
		  to_modify.dependency_edges.erase(name);
		  symbols.erase(to_modify);
		  symbols.insert(to_modify);
	 }	 

	 //Now populate the shadowers array with our lower-priority surrogates.
	 //Since we already deleted ourselves from the symbols set,
	 //this code will also serve to add ourselves to the shadowers array.
	 for(string deplist_owner_ : to_delete.reverse_dependency_list_set)
	 {
		  Symbol deplist_owner = *symbols.find(Symbol{deplist_owner_});
		  for(vector<string> deplist : deplist_owner.dependency_list_list)
			   for(auto i = find(deplist.begin(),deplist.end(),name); i!=deplist.end(); ++i)
					if(!symbols.count(Symbol{*i}))
						 shadowers.emplace(*i,deplist_owner_);
					else
						 break;
	 }
}

void DepSystem::clear()
{
	 symbols.clear();
	 shadowers.clear();
}

DepSystem::Symbol_State DepSystem::get_state(const string& symbol_name) const throw(const char*)
{
	 auto symbol = symbols.find(Symbol{symbol_name});
	 if(symbol==symbols.end())
		  throw "get_state() called with nonexistent symbol.";

	 return symbol->state;
}

vector<string> DepSystem::select_syms_with_states(const vector<string>& buildlist, const initializer_list<Symbol_State>& states) const
{
	 bool state_table[VALID+1] = {false};
	 for(Symbol_State state : states)
		  state_table[state] = true;

	 vector<string> to_return;
	 for(string x : buildlist)
		  if(state_table[symbols.find(Symbol{x})->state])
			   to_return.push_back(x);
	 
	 return to_return;
}

void DepSystem::set_state(const string& symbol_name, Symbol_State new_state) throw(const char*)
{
	 auto symbol = symbols.find(Symbol{symbol_name});
	 if(symbol==symbols.end())
		  throw "set_state() called with nonexistent symbol.";

	 Symbol to_modify = *symbol;
	 to_modify.state = new_state;
	 symbols.erase(to_modify);
	 symbols.insert(to_modify);
}

void DepSystem::set_callback(const string& symbol_name, function<void(string,string)> callback)
{
	 auto symbol_ = symbols.find(Symbol{symbol_name});
	 if(symbol_==symbols.end())
		  throw "set_callback() called with nonexistent symbol.";

	 Symbol symbol = *symbol_;
	 symbol.callback = callback;

	 symbols.erase(symbol);
	 symbols.insert(symbol);
}

bool DepSystem::detect_cycle(const string& detect_from, const string& cycle_member) const
{
     if(detect_from==cycle_member)
          return true;

     auto from_symbol_ = symbols.find(Symbol{detect_from});
     Symbol from_symbol = *from_symbol_;

     for(const string& dep_sym_name : from_symbol.dependency_edges)
          if(dep_sym_name == cycle_member)
               return true;
          else if(detect_cycle(dep_sym_name,cycle_member))
               return true;

     for(const vector<string>& dep_list : from_symbol.dependency_list_list)
          for(const string& list_sym_name : dep_list)
               if(symbols.count(Symbol{list_sym_name}))
               {
                    if(detect_cycle(list_sym_name,cycle_member))
                         return true;
                    break;
               }

     return false;
}

void DepSystem::add_dependency(const string& from_name, const string& to_name) throw(const char*)
{
	 auto from_symbol_ = symbols.find(Symbol{from_name});
	 if(from_symbol_==symbols.end())
		  throw "add_dependency() called with nonexistent from symbol name.";

	 auto to_symbol_ = symbols.find(Symbol{to_name});
	 if(to_symbol_==symbols.end())
		  throw "add_dependency() called with nonexistent to symbol name.";

	 Symbol from_symbol = *from_symbol_;
	 from_symbol.dependency_edges.insert(to_name);

	 Symbol to_symbol = *to_symbol_;
	 to_symbol.reverse_dependency_edges.insert(from_name);

	 for(auto symbol : {from_symbol,to_symbol})
	 {
		  symbols.erase(symbol);
		  symbols.insert(symbol);
	 }

     if(detect_cycle(to_name,from_name))
     {
          delete_dependency(from_name,to_name);
          throw (string("Attempted to add cyclic dependency: ")+from_name+" / "+to_name).c_str();
     }
}

bool DepSystem::has_dependency(const string& from_name, const string& to_name) const throw(const char*)
{
	 auto from_symbol_ = symbols.find(Symbol{from_name});
	 if(from_symbol_==symbols.end())
		  throw "has_dependency() called with nonexistent from symbol name.";

	 auto to_symbol_ = symbols.find(Symbol{to_name});
	 if(to_symbol_==symbols.end())
		  throw "has_dependency() called with nonexistent to symbol name.";

	 Symbol from_symbol = *from_symbol_;
	 return from_symbol.dependency_edges.count(to_name);
}

void DepSystem::delete_dependency(const string& from_name, const string& to_name) throw(const char*)
{
	 auto from_symbol_ = symbols.find(Symbol{from_name});
	 if(from_symbol_==symbols.end())
		  throw "delete_dependency() called with nonexistent from symbol name.";

	 auto to_symbol_ = symbols.find(Symbol{to_name});
	 if(to_symbol_==symbols.end())
		  throw "delete_dependency() called with nonexistent to symbol name.";

	 Symbol from_symbol = *from_symbol_;
	 from_symbol.dependency_edges.erase(to_name);

	 Symbol to_symbol = *to_symbol_;
	 to_symbol.reverse_dependency_edges.erase(from_name);

	 for(auto symbol : {from_symbol,to_symbol})
	 {
		  symbols.erase(symbol);
		  symbols.insert(symbol);
	 }
}

void DepSystem::add_dependency_list(const vector<string>& deplist, const string& to_symbol_name) throw(const char*)
{
	 auto to_symbol_ = symbols.find(Symbol{to_symbol_name});
	 if(to_symbol_==symbols.end())
		  throw "add_dependency_list() called with nonexistent symbol name.";
	 Symbol to_symbol = *to_symbol_;

	 //Add necessary symbols to shadowers map
	 string first_existing_symbol = ""; //so no, you can't use the empty string as a symbol name
	 for(string i : deplist)
		  if(!symbols.count(Symbol{i}))
			   shadowers.emplace(i,to_symbol_name);
		  else
		  {
			   first_existing_symbol = i;
			   break;
		  }

	 //Add deplist to to_symbol's deplist_list
	 to_symbol.dependency_list_list.push_back(deplist);

	 //If list is satisfied by a symbol, create appropriate entry in satisfying symbol's revdep_list_set
	 if(first_existing_symbol!="")
	 {
		  Symbol sym = *symbols.find(Symbol{first_existing_symbol});
		  sym.reverse_dependency_list_set.insert(to_symbol_name);
		  symbols.erase(sym);
		  symbols.insert(sym);
	 }

	 //Update to_symbol's entry in symbols set.
	 symbols.erase(to_symbol);
	 symbols.insert(to_symbol);
}

void DepSystem::delete_dependency_list(int index, const string& to_name)
{
	 auto sym_ = symbols.find(Symbol{to_name});
	 if(sym_==symbols.end())
		  throw "delete_dependency_list() called with nonexistent sym name.";

	 Symbol sym = *sym_;
	 if(index < 0 || index >= sym.dependency_list_list.size())
		  throw "delete_dependency_list() called with invalid index.";

	 //Get list to delete, delete from sym.
	 vector<string> list_to_delete = sym.dependency_list_list[index];
	 sym.dependency_list_list.erase(sym.dependency_list_list.begin()+index);

	 //Find active symbol, if any.
	 string active_symbol = "";
	 for(string i : list_to_delete)
		  if(symbols.count(Symbol{i}))
		  {
			   active_symbol = i;
			   break;
		  }

	 //Check to see if we should delete ourselves from active_symbol's revdep list set.
	 //We should do so iff active_symbol is not also the active symbol for another of our deplist.
	 if(active_symbol!="")
	 {
		  bool delete_revdep = true;
		  for(vector<string> deplist : sym.dependency_list_list)
			   for(string i : deplist)
					if(symbols.count(Symbol{i}))
					{
						 delete_revdep = false;
						 break;
					}

		  if(delete_revdep)
		  {
			   Symbol revdep = *symbols.find(Symbol{active_symbol});
			   revdep.reverse_dependency_list_set.erase(to_name);
			   symbols.erase(revdep);
			   symbols.insert(revdep);
		  }
	 }

	 //Replace sym in symbols set
	 symbols.erase(sym);
	 symbols.insert(sym);
}

vector<vector<string>> DepSystem::get_dependency_lists(const string& to_symbol) const throw(const char*)
{
	 auto sym = symbols.find(Symbol{to_symbol});
	 if(sym==symbols.end())
		  throw "get_dependency_lists() called with nonexistent sym name.";

	 return sym->dependency_list_list;
}


vector<string> DepSystem::get_dependencies_recursive(const Symbol& symbol, unordered_set<string> considered_symbols) const throw(const char*)
{
	 vector<string> to_return;

	 //If we've already considered this symbol, return empty vector
	 if(considered_symbols.count(symbol.name))
		  return vector<string>{};

	 //Deal with dependencies
	 for(const string& dep_sym_name : symbol.dependency_edges)
	 {
		  vector<string> dep_deps = get_dependencies_recursive(*symbols.find(Symbol{dep_sym_name}),considered_symbols);

		  //Only insert into to_return vector if not in considered_symbols
		  for(string x : dep_deps)
			   if(!considered_symbols.count(x))
					to_return.push_back(x);

		  //Add dep_deps to considered_symbols set
		  considered_symbols.insert(dep_deps.begin(),dep_deps.end());
	 }

	 //Deal with dependency lists
	 for(vector<string> dep_list : symbol.dependency_list_list)
		  for(string list_sym_name : dep_list)
			   if(symbols.count(Symbol{list_sym_name}))
			   {
					vector<string> dep_deps = get_dependencies_recursive(*symbols.find(Symbol{list_sym_name}),considered_symbols);

					//Only insert into to_return vector if not in considered_symbols
					for(string x : dep_deps)
						 if(!considered_symbols.count(x))
							  to_return.push_back(x);

					//Add dep_deps to considered_symbols set
					considered_symbols.insert(dep_deps.begin(),dep_deps.end());
					break;
			   }

	 //Add ourselves to the end of the to_return list (base case!)
	 to_return.push_back(symbol.name);

	 return to_return;
}

vector<string> DepSystem::get_dependencies(const string& symbol, function<bool(string,string,Symbol_State)> selector) const throw(const char*)
{
	 auto sym_ = symbols.find(Symbol{symbol});
	 if(sym_==symbols.end())
		  throw "get_dependencies() called with nonexistent sym name.";

	 //Make recursive call
	 vector<string> to_return = get_dependencies_recursive(*sym_,{});

	 //Per our API, delete ourselves from the end of the dependency list
	 to_return.pop_back();

	 for(int i=0; i<to_return.size(); i++)
	 {
		  const Symbol& deplist_sym = *symbols.find(Symbol{to_return[i]});
		  if(!selector(deplist_sym.name,deplist_sym.value,deplist_sym.state))
		  {
			   to_return.erase(to_return.begin()+i);
			   i--;
		  }
	 }

	 return to_return;
}

unordered_set<string> DepSystem::get_dependency_edges(const string& symbol) const
{
     auto sym_ = symbols.find(Symbol{symbol});
	 if(sym_==symbols.end())
		  throw "get_dependency_edges() called with nonexistent sym name.";

     return sym_->dependency_edges;
}

vector<string> DepSystem::get_symbols(function<bool(string,string,Symbol_State)> selector) const throw(const char*)
{
	 vector<string> to_return;
	 unordered_set<string> considered_symbols;
	 for(const Symbol& x : symbols)
	 {
		  vector<string> to_add = get_dependencies_recursive(x,considered_symbols);
		  to_return.insert(to_return.end(),to_add.begin(),to_add.end());
		  considered_symbols.insert(to_add.begin(),to_add.end());
	 }

	 for(int i=0; i<to_return.size(); i++)
	 {
		  const Symbol& sym = *symbols.find(Symbol{to_return[i]});
		  if(!selector(sym.name,sym.value,sym.state))
		  {
			   to_return.erase(to_return.begin()+i);
			   i--;
		  }
	 }

	 return to_return;
}

unordered_set<string> DepSystem::get_dependents_recursive(const string& symname) const throw(const char*)
{
	 unordered_set<string> to_return;

	 //Add ourselves to to_return and to the considered_symbols set.
	 to_return.insert(symname);

	 //Get symbol for this name
	 Symbol symbol = *symbols.find(Symbol{symname});

	 //Deal with reverse dependencies
	 for(const string& revdep : symbol.reverse_dependency_edges)
	 {
		  unordered_set<string> revdep_revdeps = get_dependents_recursive(revdep);
		  to_return.insert(revdep_revdeps.begin(),revdep_revdeps.end());
	 }

	 //Deal with reverse dependency list set
	 for(const string& revdep : symbol.reverse_dependency_list_set)
	 {
		  unordered_set<string> revdep_revdeps = get_dependents_recursive(revdep);
		  to_return.insert(revdep_revdeps.begin(),revdep_revdeps.end());
	 }

	 return to_return;
}

vector<string> DepSystem::get_dependents(const string& symbol, function<bool(string,string,Symbol_State)> selector) const throw(const char*)
{
	 auto sym_ = symbols.find(Symbol{symbol});
	 if(sym_==symbols.end())
		  throw "get_dependents() called with nonexistent sym name.";

	 //Make recursive call
	 unordered_set<string> dependents = get_dependents_recursive(symbol);

	 //We have the dependents now: we just need to put them in order.

	 //Construct to_return by getting build plan for each dependent and appending new values
	 vector<string> to_return;
	 for(const string& symname : dependents)
	 {
		  vector<string> build_plan = get_dependencies_recursive(Symbol{symname},{});

		  for(const string& x : build_plan)
			   //This is a linear search.  If it ever gets too slow, just make a parallel unordered_map.
			   if(find(to_return.begin(),to_return.end(),x)==to_return.end())
					to_return.push_back(x);
	 }

	 //Per our API, delete ourselves from the dependency list
	 to_return.erase(find(to_return.begin(),to_return.end(),symbol));

	 //Now, we have all our dependents in the right order in to_return.
	 //The problem is there's a lot of junk in-between them.
	 //We deal with this by deleting the junk while simultaneously applying the user-supplied selector.
	 for(int i=0; i<to_return.size(); i++)
	 {
		  const Symbol& revdeplist_sym = *symbols.find(Symbol{to_return[i]});
		  if(!dependents.count(to_return[i]) || !selector(revdeplist_sym.name,revdeplist_sym.value,revdeplist_sym.state))
		  {
			   to_return.erase(to_return.begin()+i);
			   i--;
		  }
	 }

	 return to_return;
}

vector<string> DepSystem::get_build_plan(const string& symbol) const throw(const char*)
{
	 auto sym_ = symbols.find(Symbol{symbol});
	 if(sym_==symbols.end())
		  throw "get_build_plan() called with nonexistent sym name.";

	 vector<string> all_dependencies = get_dependencies_recursive(*sym_,{});

	 //DO _NOT_ INCLUDE DISABLED SYMBOLS HERE!
	 //It is PERFECTLY OKAY to build a symbol with a disabled symbol in its build plan!
	 //"DISABLED" means "valid, but unable to be regenerated from its dependencies."
	 vector<string> unbuildable_dependencies = select_syms_with_states(all_dependencies,{INVALID});

	 if(unbuildable_dependencies.size())
		  throw "get_build_plan() called with unbuildable symbol.";

	 return select_syms_with_states(all_dependencies,{NONBUILT,STALE});
}

void DepSystem::build_symbol(const string& symbol) throw(const char*)
{
	 vector<string> buildlist = get_build_plan(symbol);

	 for(string x_ : buildlist)
	 {
		  const Symbol& x = *symbols.find(Symbol{x_});
		  if(x.callback)
			   x.callback(x.name,x.value);
          set_state(x.name,VALID);
	 }
}

void DepSystem::invalidate_dependents(const string& symbol) throw(const char*)
{
	 get_dependents(symbol, [&](string symname, string symval_ignored, Symbol_State symstate)
					{
						 Symbol sym = *symbols.find(Symbol{symname});
						 bool symchanged = false;
						 switch(symstate)
						 {
						 case DISABLED:
							  sym.state = INVALID;
							  symchanged = true;
							  break;

						 case VALID:
							  sym.state = STALE;
							  symchanged = true;
							  break;
						 }

						 if(symchanged)
						 {
							  symbols.erase(sym);
							  symbols.insert(sym);
						 }

						 return false;
					}
		  );
}

ostream& operator<<(ostream& sout, const DepSystem& x)
{
	 for(const DepSystem::Symbol& sym : x.symbols)
		  sout << sym;
	 sout << "%%%ENDSYMBOLS%%%\n";

	 for(const auto& shadow_pair : x.shadowers)
	 {
		  sout << shadow_pair.first << endl;
		  sout << "%%%ENDSHADOWER%%%\n";
		  sout << shadow_pair.second << endl;
		  sout << "%%%ENDSHADOWEE%%%\n";
	 }
	 sout << "%%%ENDSHADOWERS%%%\n";

	 return sout;
}

istream& operator>>(istream& sin, DepSystem& x)
{
	 while(peekline(sin)!="%%%ENDSYMBOLS%%%")
	 {
		  DepSystem::Symbol to_insert;
		  sin >> to_insert;
		  x.symbols.insert(to_insert);
	 }

	 string shadower,shadowee;
	 getline(sin,shadower); //swallow "%%%ENDSYMBOLS%%%"
	 getline(sin,shadower);
	 while(shadower!="%%%ENDSHADOWERS%%%")
	 {
		  string temp;
		  getline(sin,temp);
		  while(temp!="%%%ENDSHADOWER%%%")
		  {
			   shadower = shadower + "\n" + temp;
			   getline(sin,temp);
		  }

		  getline(sin,shadowee);
		  getline(sin,temp);
		  while(temp!="%%%ENDSHADOWEE%%%")
		  {
			   shadowee = shadowee + "\n" + temp;
			   getline(sin,temp);
		  }
		  
		  x.shadowers.emplace(shadower,shadowee);
		  getline(sin,shadower);
	 }

	 return sin;
}

ostream& operator<<(ostream& sout, const DepSystem::Symbol& x)
{
	 auto output_sym = [&sout](const string& field, const char* terminus)
		  {
			   sout << field << endl;
			   sout << terminus << endl;
		  };

	 output_sym(x.name,"%%%ENDSYMNAME%%%");
	 output_sym(x.value,"%%%ENDSYMVALUE%%%");
	 output_sym(to_string(x.state),"%%%ENDSYMSTATE%%%");

	 //NOTE: Callbacks are *NOT* serialized for obvious reasons.
			   
	 for(const string& edge : x.dependency_edges)
		  output_sym(edge,"%%%ENDDEPEDGE%%%");
	 sout << "%%%ENDDEPEDGES%%%\n";

	 for(const string& edge : x.reverse_dependency_edges)
		  output_sym(edge,"%%%ENDREVDEPEDGE%%%");
	 sout << "%%%ENDREVDEPEDGES%%%\n";

	 for(const vector<string>& deplist : x.dependency_list_list)
	 {
		  for(const string& depname : deplist)
			   output_sym(depname,"%%%ENDDEPLISTITEM%%%");
		  sout << "%%%ENDDEPLIST%%%\n";
	 }
	 sout << "%%%ENDDEPLISTLIST%%%\n";

	 for(const string& revdep : x.reverse_dependency_list_set)
		  output_sym(revdep,"%%%ENDREVDEP%%%");
	 sout << "%%%ENDREVDEPLIST%%%\n";

	 sout << "%%%ENDSYMBOL%%%\n";
	 return sout;
}

istream& operator>>(istream& sin, DepSystem::Symbol& x)
{
	 auto getsym = [&sin](const char* terminus)
		  {
			   string to_return;
			   string line;
			   getline(sin,line);
			   while(line!=terminus)
			   {
					to_return += to_return=="" ? line : string("\n")+line;
					getline(sin,line);
			   }

			   return to_return;
		  };

	 auto getsymlist = [&sin,&getsym](unordered_set<string>& target, const char* item_terminus, const char* list_terminus)
		  {
			   string temp;
			   getline(sin,temp);
			   while(temp!=list_terminus)
			   {
					temp += getsym(item_terminus);
					target.insert(temp);
					getline(sin,temp);
			   }
		  };


	 x.name = getsym("%%%ENDSYMNAME%%%");
	 x.value = getsym("%%%ENDSYMVALUE%%%");
	 x.state = static_cast<DepSystem::Symbol_State>(stoi(getsym("%%%ENDSYMSTATE%%%")));

	 getsymlist(x.dependency_edges,"%%%ENDDEPEDGE%%%","%%%ENDDEPEDGES%%%");
	 getsymlist(x.reverse_dependency_edges,"%%%ENDREVDEPEDGE%%%","%%%ENDREVDEPEDGES%%%");

	 string temp = getsym("%%%ENDDEPLISTLIST%%%");
	 vector<string> lines;
	 StringFunctions::tokenize(lines,temp,"\n");
	 temp.clear();
	 vector<string> to_push;
	 for(string item : lines)
		  if(item=="%%%ENDDEPLIST%%%")
		  {
			   x.dependency_list_list.push_back(to_push);
			   to_push.clear();
		  }
		  else if(item=="%%%ENDDEPLISTITEM%%%")
		  {
			   to_push.push_back(temp);
			   temp.clear();
		  }
		  else
			   temp = temp=="" ? item : temp+"\n"+item;

	 getsymlist(x.reverse_dependency_list_set,"%%%ENDREVDEP%%%","%%%ENDREVDEPLIST%%%");

	 getline(sin,temp); //Swallow "%%%ENDSYMBOL%%%\n"
	 return sin;
}
