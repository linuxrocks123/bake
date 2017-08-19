#ifndef DEPLIB_HPP
#define DEPLIB_HPP

#include <functional>
#include <initializer_list>
#include <iostream>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "StringFunctions.h"

using std::function;
using std::getline;
using std::initializer_list;
using std::endl;
using std::istream;
using std::ostream;
using std::stoi;
using std::string;
using std::to_string;
using std::unordered_map;
using std::unordered_multimap;
using std::unordered_set;
using std::vector;

class DepSystem
{
	 friend ostream& operator<<(ostream& sout, const DepSystem& x);
	 friend istream& operator>>(istream& sin, DepSystem& x);
public:
	 //NOTE: "VALID" must always be the last state!
	 enum Symbol_State { NONBUILT, DISABLED, STALE, INVALID /*conceptually the same as STALE+DISABLED*/, VALID };

	 //Returns whether symbol exists
	 bool has_symbol(const string& name) const noexcept;

	 //Gets value of symbol; throws exception if symbol nonexistent
	 string get_value(const string& symbol_name) const throw(const char*);

	 /**Adds symbol if nonexistent, setting state to "VALID".
		If symbol does exist, and the value given is not the current value of the symbol, this function:
		 - Sets its value to given parameter
		 - Changes symbol state to "VALID", unless symbol has dependencies, in which case symbol is marked "DISABLED".
		 - If symbol existed and had its value changed, marks all "VALID" symbols which depend on this one "STALE"
		   (and all "DISABLED" symbols which depend on this one as "INVALID").
	 */
	 void add_set_symbol(const string& name, const string& value) throw(const char*);

	 //Deletes symbol.  Deletes all edges associated with symbol.  Correctly handles dependency lists.  Throws exception if cyclic graph would result.
	 void delete_symbol(const string& name) throw(const char*);

	 //Deletes all symbols
	 void clear();

	 //Returns current state of symbol; throws exception if symbol nonexistent
	 Symbol_State get_state(const string& symbol_name) const throw(const char*);

	 //Sets state of symbol.  This does NO PROCESSING of any dependencies of the modified symbol!  Throws exception on nonexistent symbol name.
	 void set_state(const string& symbol_name, Symbol_State new_state) throw(const char*);

	 //Returns all syms in passed vector which are of the given states
	 vector<string> select_syms_with_states(const vector<string>& symlist, const initializer_list<Symbol_State>& states) const;

	 //Sets callback for symbol
	 void set_callback(const string& symbol_name, function<void(string,string)> callback);

	 //Adds or sets dependency between symbols.  Throws exception -- and does not add dependency -- if added dependency would make dependency graph cyclic.
	 void add_dependency(const string& from_symbol, const string& to_symbol) throw(const char*);

	 //Returns whether dependency exists from symbol from to symbol to.
	 bool has_dependency(const string& from, const string& to) const throw(const char*);

	 //Deletes dependency between symbols
	 void delete_dependency(const string& from, const string& to) throw(const char*);

     /*A symbol may have a set of ordered dependency lists in addition to a set of dependency edges.
	   An ordered dependency list is interpreted as creating a dependency edge from a symbol in the
	   ordered list to the symbol associated with the list iff that symbol is the first symbol in
	   the list which exists.*/
     //This code is NOT to handle the local-global symbol merging that can occur in SEPM.
     //That is an application-specific problem.  However, functions to make that code simpler should exist.

	 //Adds dependency list to symbol.  Throws exception if this would make graph cyclic.
	 void add_dependency_list(const vector<string>& deplist, const string& to_symbol) throw(const char*);

	 //Gets dependency lists of symbol.  Throws if symbol does not exist.
	 vector<vector<string>> get_dependency_lists(const string& to_symbol) const throw(const char*);

	 //Deletes dependency list from symbol.  Throws exception if specified dependency list out of range for symbol or symbol does not exist.
	 void delete_dependency_list(int index, const string& to_symbol);

	 //In what would be a buildable order if all root dependencies were valid and all nonroot dependencies were nonbuilt, return the dependencies of this symbol which satisfy the passed selector, not including this symbol itself.
	 vector<string> get_dependencies(const string& symbol, function<bool(string,string,Symbol_State)> selector = [](string symbol, string value, Symbol_State state) noexcept { return true; }) const throw(const char*);

     //Returns the direct dependency edges of the given symbol in an arbitrary order.  Does not handle dependency lists.
     unordered_set<string> get_dependency_edges(const string& symbol) const;

	 //In what would be a buildable order if all root dependencies were valid and all nonroot dependencies were nonbuilt, return a vector of all symbols.
	 vector<string> get_symbols(function<bool(string,string,Symbol_State)> selector = [](string symbol, string value, Symbol_State state) noexcept { return true; }) const throw(const char*);

	 //In what would be a buildable order if this symbol were valid and all of its dependents were nonbuilt, return the dependents of this symbol which satisfy the passed selector, not including this symbol itself.
	 vector<string> get_dependents(const string& symbol, function<bool(string,string,Symbol_State)> selector = [](string symbol, string value, Symbol_State state) noexcept { return true; }) const throw(const char*);

	 //Returns stale symbols on which passed symbol depends in a buildable order, including this symbol itself.  Throws exception if symbol nonexistent or if no way to build symbol.
     vector<string> get_build_plan(const string& symbol) const throw(const char*);

	 //Invokes dependency build functions on all stale or nonbuilt dependencies of symbol in buildable order and marks affected symbols valid.
	 void build_symbol(const string& symbol) throw(const char*);

	 //Marks all valid symbols which depend on this symbol as stale (and all disabled symbols invalid).  Throws exception for nonexistent symbols.
	 void invalidate_dependents(const string& symbol) throw(const char*);

private:
	 //Internal symbol structure
	 struct Symbol
	 {
		  string name;
		  string value;
		  Symbol_State state;
		  function<void(string,string)> callback;
		  unordered_set<string> dependency_edges;
		  unordered_set<string> reverse_dependency_edges;
		  vector<vector<string>> dependency_list_list;
		  unordered_set<string> reverse_dependency_list_set;
	 };
	 friend ostream& operator<<(ostream& sout, const DepSystem::Symbol& x);
	 friend istream& operator>>(istream& sin, DepSystem::Symbol& x);

	 //Symbol Hash
	 class Symbol_hash
	 {
	 public:
		  size_t operator()(const Symbol& symbol) const { return std::hash<string>()(symbol.name); }
	 };

	 //Symbol Comparator
	 class Symbol_equivalence
	 {
	 public:
		  bool operator()(const Symbol& left, const Symbol& right) const { return left.name == right.name; }
	 };


	 //Private helper functions
	 vector<string> get_dependencies_recursive(const Symbol& symbol, unordered_set<string> considered_symbols) const throw(const char*);

	 unordered_set<string> get_dependents_recursive(const string& symbol) const throw(const char*);

     bool detect_cycle(const string& detect_from, const string& cycle_member) const;

	 //Set of all Symbols
	 unordered_set<Symbol,Symbol_hash,Symbol_equivalence> symbols;

	 //Set of nonexistent symbols which may shadow other symbols
	 unordered_multimap<string,string> shadowers;
};

//I/O functions
ostream& operator<<(ostream& sout, const DepSystem& x);
istream& operator>>(istream& sin, DepSystem& x);
ostream& operator<<(ostream& sout, const DepSystem::Symbol& x);
istream& operator>>(istream& sin, DepSystem::Symbol& x);

#endif
