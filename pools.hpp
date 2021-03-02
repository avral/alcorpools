#pragma once

#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/system.hpp>
#include <eosio/print.hpp>
#include <cmath>

using namespace eosio;
using namespace std;

typedef __uint128_t uint128_t;
typedef __int128_t int128_t;


struct exchangerecord {
   uint64_t pair_id;

   name maker;

   asset quantity_in;
   asset quantity_out;

   asset pool1;
   asset pool2;
};

struct liquidityrecord {
   uint64_t pair_id;

   asset lp_token;
   name owner;

   asset liquidity1;
   asset liquidity2;

   asset pool1;
   asset pool2;
   asset supply;
};

namespace evolution {
   class [[eosio::contract]] pools : public contract {
      public:
         const int64_t MAX = eosio::asset::max_amount;
         const int64_t INIT_MAX = 1000000000000000;  // 10^15 
         const int ADD_LIQUIDITY_FEE = 1;
         const int DEFAULT_FEE = 30;

         using contract::contract;

         pools(name receiver, name code, datastream<const char*> ds)
            :contract(receiver, code, ds),
            _pairs(receiver, receiver.value){};

         // Logs
         [[eosio::action]] void exchangelog(exchangerecord record) { require_auth(_self); }
         [[eosio::action]] void liquiditylog(liquidityrecord record) { require_auth(_self); }

         // Pools
         [[eosio::on_notify("*::transfer")]] void ontransfer(name from, name to, asset quantity, string memo);
         [[eosio::action]] void addliquidity(name user, asset to_buy);
         [[eosio::action]] void remliquidity(name user, asset to_sell);
         [[eosio::action]] void changefee(uint64_t pool_id, int newfee);
         [[eosio::action]] void inittoken(
            name user,
            extended_asset initial_pool1,
            extended_asset initial_pool2, 
            int initial_fee, name fee_contract
         );

         // Token
         [[eosio::action]] void transfer(const name& from, const name& to, const asset& quantity, const string&  memo );
         [[eosio::action]] void open( const name& owner, const symbol& symbol, const name& ram_payer );
         [[eosio::action]] void close( const name& owner, const symbol& symbol );

      private:
         struct [[eosio::table]] account {
            asset    balance;
            uint64_t primary_key()const { return balance.symbol.code().raw(); }
         };

         struct [[eosio::table]] evodexaccount {
            extended_asset   balance;
            uint64_t id;
            uint64_t primary_key()const { return id; }
            uint128_t secondary_key()const { return 
              make128key(balance.contract.value, balance.quantity.symbol.raw() ); }
         };

         struct [[eosio::table]] pairs_struct {
            uint64_t id;

            asset             supply;
            extended_asset    pool1;
            extended_asset    pool2;

            int fee;
            name fee_contract;

            uint64_t primary_key() const { return id; }
            checksum256 secondary_key() const {
               return make256key(
                  pool1.contract.value, pool1.quantity.symbol.code().raw(),
                  pool2.contract.value, pool2.quantity.symbol.code().raw()
               );
            }
         };

         struct [[eosio::table]] currency_stats {
            asset    supply;
            asset    max_supply;
            name     issuer;

            uint64_t pool_id;

            uint64_t primary_key()const { return supply.symbol.code().raw(); }
            uint64_t secondary_key() const { return pool_id; }
         };

         typedef eosio::multi_index< "pairs"_n, pairs_struct,
         indexed_by<"extended"_n, const_mem_fun<pairs_struct, checksum256, 
           &pairs_struct::secondary_key>> > pairs_index;

         typedef eosio::multi_index< "deposits"_n, evodexaccount,
         indexed_by<"extended"_n, const_mem_fun<evodexaccount, uint128_t, 
           &evodexaccount::secondary_key>> > evodexacnts;

         typedef eosio::multi_index< "stat"_n, currency_stats,
            indexed_by<"pool"_n, const_mem_fun<currency_stats, uint64_t, &currency_stats::secondary_key>>
         > stats;

         typedef eosio::multi_index< "accounts"_n, account > accounts;

         pairs_index _pairs;

         static uint128_t make128key(uint64_t a, uint64_t b);
         static checksum256 make256key(uint64_t a, uint64_t b, uint64_t c, uint64_t d);
         symbol_code get_free_symbol(string new_symbol);

         void add_signed_ext_balance( const name& owner, const extended_asset& value );
         void add_signed_liq(name user, asset to_buy, bool is_buying);
			//void remliquidity(name user, asset to_sell);
         void memoexchange(name user, extended_asset ext_asset_in, string_view details);
         extended_asset process_exch(const pairs_struct& pool, extended_asset paying, asset min_expected);
         int64_t compute(int64_t x, int64_t y, int64_t z, int fee);
         asset string_to_asset(string input);
         void add_balance( const name& owner, const asset& value, const name& ram_payer );
         void sub_balance( const name& owner, const asset& value );
   };
}
