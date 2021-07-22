#pragma once
//#include <charconv>
#include <string>
#include <algorithm>
#include <iterator>

#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include "safe.hpp"

using namespace std;
using namespace eosio;

struct currency_stats {
   asset    supply;
   asset    max_supply;
   name     issuer;

   uint64_t primary_key() const { return supply.symbol.code().raw(); }
};

typedef eosio::multi_index<"stat"_n, currency_stats> stats;

string_view trim(string_view sv) {
    sv.remove_prefix(std::min(sv.find_first_not_of(" "), sv.size())); // left trim
    sv.remove_suffix(std::min(sv.size()-sv.find_last_not_of(" ")-1, sv.size())); // right trim    
    return sv;
}
 
vector<string_view> split(string_view str, string_view delims = " ")
{
    vector<string_view> res;
    std::size_t current, previous = 0;
    current = str.find_first_of(delims);
    while (current != std::string::npos) {
        res.push_back(trim(str.substr(previous, current - previous)));
        previous = current + 1;
        current = str.find_first_of(delims, previous);
    }
    res.push_back(trim(str.substr(previous, current - previous)));
    return res;
}

bool starts_with(string_view sv, string_view s) {
    return sv.size() >= s.size() && sv.compare(0, s.size(), s) == 0;
}

template <class T>
void to_int(string_view sv, T& res) {
    res = 0;
    T p = 1;
    for( auto itr = sv.rbegin(); itr != sv.rend(); ++itr ) {
        check( *itr <= '9' && *itr >= '0', "invalid character");
        res += p * T(*itr-'0');
        p   *= T(10);
    }
}
template <class T>
void precision_from_decimals(int8_t decimals, T& p10)
{
    check(decimals <= 18, "precision should be <= 18");
    p10 = 1;
    T p = decimals;
    while( p > 0  ) {
        p10 *= 10; --p;
    }
}

extended_asset e_asset_from_string(string_view s) {
   auto at_pos = s.find('@');	
   check(at_pos != std::string::npos, "Extended asset's asset and contract should be separated with '@'");	
   
   auto asset_str = trim(s.substr(0, at_pos));
   auto contract_str = trim(s.substr(at_pos + 1));	

   name contract(contract_str);
   check(is_account(contract), (string(contract_str) + " account does not exist").c_str());

   // Find space in order to split amount and symbol
   auto space_pos = asset_str.find(' ');
   check(space_pos != string::npos, "Asset's amount and symbol should be separated with space");
   
   auto symbol_str = trim(asset_str.substr(space_pos + 1));
   auto amount_str = asset_str.substr(0, space_pos);
   
   // Ensure that if decimal point is used (.), decimal fraction is specified
   auto dot_pos = amount_str.find('.');
   if (dot_pos != string::npos) {
      check(dot_pos != amount_str.size() - 1, "Missing decimal fraction after decimal point");
   }
   
   // Parse symbol
   uint8_t precision_digit_str;

   if (dot_pos != string::npos) {
      precision_digit_str = (amount_str.size() - dot_pos - 1);
   } else {
      precision_digit_str = 0;
   }
   
   symbol sym(eosio::symbol_code(symbol_str), precision_digit_str);

   stats statstable(contract, sym.code().raw());
   const auto& st = statstable.get(
         sym.code().raw(),
         (string(contract_str) + " contract " + "have not token " + sym.code().to_string()).c_str()
   );

   check(sym == st.supply.symbol, "symbol precision mismatch");
   
   // Parse amount
   int64_t int_part = 0;
   int64_t fract_part = 0;
   
   if (dot_pos != string::npos) {
      int_part = std::stoll(string(amount_str.substr(0, dot_pos)));
      fract_part = std::stoll(string(amount_str.substr(dot_pos + 1)));
      if (amount_str[0] == '-') fract_part *= -1;
   } else {
      int_part = std::stoll(string(amount_str));
   }
   
   int64_t amount = int_part;
   
   int64_t p = sym.precision();
   while( p > 0  ) {
      amount *= 10; --p;
   }
   
   amount += fract_part;
   asset asset(amount, sym);

   check(asset.is_valid(), "invalid quantity");
   check(asset.amount >= 0, "zero amount not allowed");
   
   return extended_asset(asset, contract);
}
