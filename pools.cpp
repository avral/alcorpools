#include <algorithm>

#include "pools.hpp"
#include "utils.hpp"
#include "token_functions.cpp"

using namespace evolution;
const name alcor_swap = name("swap.alcor");
const int32_t alcor_swap_pool_fee = 3000; // 0.3% by default
const int32_t MIN_TICK = -443636;
const int32_t MAX_TICK = -MIN_TICK;
void pools::ontransfer(name from, name to, asset quantity, string memo) {
   const string DEPOSIT_TO = "deposit";
   //const string EXCHANGE   = "exchange:";
   //const string WITHDRAW   = "exchange:";

   if (from == _self) return;
   contract_is_maintaining();
   check(to == _self, "This transfer is not for pools");
   check(quantity.amount >= 0, "quantity must be positive");

   auto incoming = extended_asset{quantity, _first_receiver};

   if (memo == DEPOSIT_TO) {
      add_signed_ext_balance(from, incoming);
      check(from != _self, "self donation not accepted");
   } else if (memo.find('@') != string::npos || memo.find(' ') != string::npos) {
      memoexchange(from, incoming, memo);
   } else {
      return;
   }
}

void pools::refundremain(name user, uint64_t pool_id) {
    const auto& pool = _pairs.get(pool_id, "Pool not found from token");

    evodexacnts acnts( _self, user.value );
    auto index = acnts.get_index<"extended"_n>();

    const auto& balance1 = index.find(make128key(pool.pool1.contract.value, pool.pool1.quantity.symbol.raw()));
    const auto& balance2 = index.find(make128key(pool.pool2.contract.value, pool.pool2.quantity.symbol.raw()));

	 if (balance1 != index.end()) {
		 action(permission_level{ _self, "active"_n }, balance1->balance.contract, "transfer"_n,
			std::make_tuple( _self, user, balance1->balance.quantity, string("refund liquidity slippage"))).send();

		 add_signed_ext_balance(user, -balance1->balance);
	 }

	 if (balance2 != index.end()) {
		 action(permission_level{ _self, "active"_n }, balance2->balance.contract, "transfer"_n,
			std::make_tuple( _self, user, balance2->balance.quantity, string("refund liquidity slippage"))).send();

		 add_signed_ext_balance(user, -balance2->balance);
	 }
}

void pools::addliquidity(name user, asset to_buy) {
   contract_is_maintaining();

    require_auth(user);
    check(to_buy.amount > 0, "to_buy amount must be positive");

    add_signed_liq(user, to_buy, true);

    stats statstable( _self, to_buy.symbol.code().raw() );
    const auto& pool_token = statstable.get( to_buy.symbol.code().raw(), "pool token does not exist" );

    refundremain(user, pool_token.pool_id);
}

void pools::remliquidity(name user, asset to_sell) {
   contract_is_maintaining();

    require_auth(user);
    check(to_sell.amount > 0, "to_sell amount must be positive");

    stats statstable(_self, to_sell.symbol.code().raw());
    const auto& pool_token = statstable.get(to_sell.symbol.code().raw(), "pool token does not exist" );
    const auto& pool = _pairs.get(pool_token.pool_id, "Pool not found from token");
	 
    add_signed_liq(user, -to_sell, false);

    evodexacnts acnts( _self, user.value );
    auto index = acnts.get_index<"extended"_n>();

    const auto& balance1 = index.find(make128key(pool.pool1.contract.value, pool.pool1.quantity.symbol.raw()));
    const auto& balance2 = index.find(make128key(pool.pool2.contract.value, pool.pool2.quantity.symbol.raw()));

	 if (balance1 != index.end()) {
		 action(permission_level{ _self, "active"_n }, balance1->balance.contract, "transfer"_n,
			std::make_tuple( _self, user, balance1->balance.quantity, string("liquidity withdraw"))).send();

		 add_signed_ext_balance(user, -balance1->balance);
	 }

	 if (balance2 != index.end()) {
		 action(permission_level{ _self, "active"_n }, balance2->balance.contract, "transfer"_n,
			std::make_tuple( _self, user, balance2->balance.quantity, string("liquidity withdraw"))).send();

		 add_signed_ext_balance(user, -balance2->balance);
	 }
}

// computes x * y / z plus the fee
int64_t pools::compute(int64_t x, int64_t y, int64_t z, int fee) {
    check( (x != 0) && (y > 0) && (z > 0), "invalid parameters");
    int128_t prod = int128_t(x) * int128_t(y);
    int128_t tmp = 0;
    int128_t tmp_fee = 0;
    if (x > 0) {
        tmp = 1 + (prod - 1) / int128_t(z);
        check( (tmp <= MAX), "computation overflow" );
        tmp_fee = (tmp * fee + 9999) / 10000;
    } else {
        tmp = prod / int128_t(z);
        check( (tmp >= -MAX), "computation underflow" );
        tmp_fee =  (-tmp * fee + 9999) / 10000;
    }
    tmp += tmp_fee;
    return int64_t(tmp);
}

void pools::add_signed_liq(name user, asset to_add, bool is_adding) {
   check( to_add.is_valid(), "invalid asset");

   stats statstable( _self, to_add.symbol.code().raw() );
   const auto& pool_token = statstable.get( to_add.symbol.code().raw(), "pool token does not exist" );

   const auto& pair = _pairs.get(pool_token.pool_id, "Pool not found from token");

   auto A = pool_token.supply.amount;
   auto P1 = pair.pool1.quantity.amount;
   auto P2 = pair.pool2.quantity.amount;

   int fee = is_adding ? ADD_LIQUIDITY_FEE : 0;

   auto to_pay1 = extended_asset{ asset{compute(to_add.amount, P1, A, fee),
     pair.pool1.quantity.symbol}, pair.pool1.contract};
   auto to_pay2 = extended_asset{ asset{compute(to_add.amount, P2, A, fee),
     pair.pool2.quantity.symbol}, pair.pool2.contract};

   //check(false, "to_pay1(" + to_pay1.quantity.to_string() + "), to_pay2(" + to_pay2.quantity.to_string() + ")");

   add_signed_ext_balance(user, -to_pay1, is_adding);
   add_signed_ext_balance(user, -to_pay2, is_adding);

   (to_add.amount > 0) ? add_balance(user, to_add, user) : sub_balance(user, -to_add);

   //if (pair.fee_contract) require_recipient(pair.fee_contract);

   statstable.modify(pool_token, same_payer, [&]( auto& a ) {
     a.supply += to_add;
   });

   _pairs.modify(pair, same_payer, [&]( auto& a ) {
     a.supply += to_add;
     a.pool1 += to_pay1;
     a.pool2 += to_pay2;
   });

   // Log
   action(
      permission_level{_self, "active"_n},
      _self,
      "liquiditylog"_n,
      make_tuple(liquidityrecord{
         .pair_id = pair.id,
         .lp_token = to_add,
         .owner = user,
         .liquidity1 = to_pay1.quantity,
         .liquidity2 = to_pay2.quantity,
         .pool1 = pair.pool1.quantity,
         .pool2 = pair.pool2.quantity,
         .supply = pair.supply,
      })
   ).send();

   if (pool_token.supply.amount == 0) {
      _pairs.erase(pair);
   }
}

extended_asset pools::process_exch(const pairs_struct& pool, extended_asset ext_asset_in, asset min_expected) {
    bool in_first;
    if ((pool.pool1.get_extended_symbol() == ext_asset_in.get_extended_symbol()) && 
        (pool.pool2.quantity.symbol == min_expected.symbol)) {
        in_first = true;
    } else if ((pool.pool1.quantity.symbol == min_expected.symbol) &&
               (pool.pool2.get_extended_symbol() == ext_asset_in.get_extended_symbol())) {
        in_first = false;
    }
    else check(false, "extended_symbol mismatch");
    int64_t P_in, P_out;
    if (in_first) { 
      P_in = pool.pool1.quantity.amount;
      P_out = pool.pool2.quantity.amount;
    } else {
      P_in = pool.pool2.quantity.amount;
      P_out = pool.pool1.quantity.amount;
    }
    auto A_in = ext_asset_in.quantity.amount;
    int64_t A_out = compute(-A_in, P_out, P_in + A_in, pool.fee);
    extended_asset ext_asset1, ext_asset2, ext_asset_out;
    if (in_first) { 
      ext_asset1 = ext_asset_in;
      ext_asset2 = extended_asset{A_out, pool.pool2.get_extended_symbol()};
      ext_asset_out = -ext_asset2;
    } else {
      ext_asset1 = extended_asset{A_out, pool.pool1.get_extended_symbol()};
      ext_asset2 = ext_asset_in;
      ext_asset_out = -ext_asset1;
    }

    check(min_expected.amount <= -A_out, "available(" + ext_asset_out.quantity.to_string() +  ") is less than expected");

    _pairs.modify(pool, same_payer, [&]( auto& a ) {
      a.pool1 += ext_asset1;
      a.pool2 += ext_asset2;
    });

    return ext_asset_out;
}

void pools::memoexchange(name user, extended_asset ext_asset_in, string_view details) {
   auto parts = split(details, "|");
   check(parts.size() >= 1, "Expected format 'min_expected_asset|optional memo'");

   extended_asset min_expected = e_asset_from_string(parts[0]);

   auto index = _pairs.get_index<"extended"_n>();
   const auto& pair = index.get(
      make256key(
         ext_asset_in.contract.value, ext_asset_in.quantity.symbol.code().raw(),
         min_expected.contract.value, min_expected.quantity.symbol.code().raw()
      ), "Pool not exists!"
   );

   check(min_expected.quantity.amount >= 0, "min_expected must be expressed with a positive or zero amount");

   name send_to = parts.size() > 1 ? name(parts[1]) : user;
   auto memo = (parts.size() > 2) ? parts[2] : "";

   auto ext_asset_out = process_exch(pair, ext_asset_in, min_expected.quantity);

   // TODO Multipath swap here

   action(permission_level{ _self, "active"_n }, ext_asset_out.contract, "transfer"_n,
     std::make_tuple( _self, send_to, ext_asset_out.quantity, std::string(memo)) ).send();

   // Log
   action(
      permission_level{_self, "active"_n},
      _self,
      "exchangelog"_n,
      make_tuple(exchangerecord{
         .pair_id = pair.id,
         .maker = user,
         .quantity_in = ext_asset_in.quantity,
         .quantity_out = ext_asset_out.quantity,
         .pool1 = pair.pool1.quantity,
         .pool2 = pair.pool2.quantity,
      })
   ).send();
}

symbol_code pools::get_free_symbol(string new_symbol) {
   static const char* sAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
   std::vector<char> vAlphabet(sAlphabet, sAlphabet + 26);

   symbol_code sym = symbol_code(new_symbol);

   stats statstable(_self, sym.raw() );
   const auto& token = statstable.find( sym.raw() );

   if (token == statstable.end()) {
      return sym;
   } else {
      for(auto const& value: vAlphabet) {
         if (new_symbol.length() == 6) {
            sym = symbol_code(new_symbol + value);
         } else {
            sym = symbol_code(new_symbol.substr(0, 6) + value);
         }

         stats statstable( _self, sym.raw() );
         const auto& token = statstable.find( sym.raw() );
         if (token == statstable.end()) return sym;
      }

      check(false, "all token names are existed");
   }
}

void pools::inittoken(name user, extended_asset initial_pool1,
extended_asset initial_pool2, int initial_fee, name fee_contract)
{
   contract_is_maintaining();
   require_auth( user );

   check((initial_pool1.quantity.amount > 0) && (initial_pool2.quantity.amount > 0), "Both assets must be positive");
   check((initial_pool1.quantity.amount < INIT_MAX) && (initial_pool2.quantity.amount < INIT_MAX), "Initial amounts must be less than 10^15");
   check( initial_pool1.get_extended_symbol() != initial_pool2.get_extended_symbol(), "extended symbols must be different");

   // Check if pool exists
   auto index = _pairs.get_index<"extended"_n>();
   const auto& pool_exists = index.find(make256key(
      initial_pool1.contract.value, initial_pool1.quantity.symbol.code().raw(),
      initial_pool2.contract.value, initial_pool2.quantity.symbol.code().raw()));
   check(pool_exists == index.end(), "Pool already exists");

   string new_symbol_str;
   uint8_t new_precision = ( initial_pool1.quantity.symbol.precision() + initial_pool2.quantity.symbol.precision() ) / 2;
   int128_t geometric_mean = sqrt(int128_t(initial_pool1.quantity.amount) * int128_t(initial_pool2.quantity.amount));

   symbol_code first_code = initial_pool1.get_extended_symbol().get_symbol().code();
   symbol_code second_code = initial_pool2.get_extended_symbol().get_symbol().code();

   new_symbol_str = first_code.to_string() + second_code.to_string(); 

   if (first_code.length() + second_code.length() > 7) {
      const string first_path = first_code.to_string().substr(0, 4);
      new_symbol_str = first_path + second_code.to_string().substr(0, 7 - first_path.length());
   }

   auto new_token = asset{int64_t(geometric_mean), symbol(get_free_symbol(new_symbol_str), new_precision)};

   // Generate pool token
   stats statstable( _self, new_token.symbol.code().raw() );
   const auto& token = statstable.find( new_token.symbol.code().raw() );
   check ( token == statstable.end(), "token symbol already exists" );
   check( initial_fee == DEFAULT_FEE, "initial_fee must be 30");

   uint64_t new_pool_id = _pairs.available_primary_key();
   _pairs.emplace( user, [&]( auto& a ) {
      a.id = new_pool_id;
      a.pool1 = initial_pool1;
      a.pool2 = initial_pool2;
      a.fee = initial_fee;
      a.fee_contract = fee_contract;
		a.supply = new_token;
   });

   statstable.emplace( user, [&]( auto& a ) {
       a.supply = new_token;
       a.max_supply = asset{ MAX, new_token.symbol };
       a.issuer = _self;
       a.pool_id = new_pool_id;
   } );

   add_balance(user, new_token, user);
   add_signed_ext_balance(user, -initial_pool1);
   add_signed_ext_balance(user, -initial_pool2);

   // Log
   action(
      permission_level{_self, "active"_n},
      _self,
      "liquiditylog"_n,
      make_tuple(liquidityrecord{
         .pair_id = new_pool_id,
         .lp_token = new_token,
         .owner = user,
         .liquidity1 = initial_pool1.quantity,
         .liquidity2 = initial_pool2.quantity,
         .pool1 = initial_pool1.quantity,
         .pool2 = initial_pool2.quantity,
         .supply = new_token
      })
   ).send();
}

// price should input from script because it more complex to add sqrt function and handle foat number inside smart contract
void pools::migratepool(uint64_t poolId, uint128_t sqrtPriceX64){
   require_auth( get_self() );

   auto pool_itr = _pairs.require_find(poolId, "poolId not found");
   check((pool_itr->pool1.quantity.amount  > 0)&& (pool_itr->pool2.quantity.amount>0), "Both token must be positive");
   auto tokenA = extended_asset(0, pool_itr->pool1.get_extended_symbol());
   auto tokenB = extended_asset(0, pool_itr->pool2.get_extended_symbol());;
   
   if(!isTokensSorted(tokenA, tokenB)){
      tokenA = extended_asset(0, pool_itr->pool2.get_extended_symbol());
      tokenB = extended_asset(0, pool_itr->pool1.get_extended_symbol());
   }
   action(permission_level{get_self(), name("active")}, alcor_swap, name("createpool"),
         std::make_tuple(get_self(), tokenA, tokenB, sqrtPriceX64, alcor_swap_pool_fee))
      .send();
}

void pools::migrateuser(uint64_t poolId, std::vector<name> users){
   require_auth( get_self() );

   auto pool_itr = _pairs.require_find(poolId, "poolId not found");
   for(auto user : users ){
      accounts from_acnts( _self, user.value );

      auto liqudity_token_itr = from_acnts.find(pool_itr->supply.symbol.code().raw());
      if(liqudity_token_itr != from_acnts.end()){
         if(liqudity_token_itr->balance.amount > 0){
            // remove liquidity: 
            auto to_sell = liqudity_token_itr->balance;
            check(to_sell.amount > 0, "to_sell amount must be positive");

            stats statstable(_self, to_sell.symbol.code().raw());
            const auto& pool_token = statstable.get(to_sell.symbol.code().raw(), "pool token does not exist" );
            // double check
            check(pool_token.pool_id == poolId, "sanity check");
            
            add_signed_liq(user, -to_sell, false);

            evodexacnts acnts( _self, user.value );
            auto index = acnts.get_index<"extended"_n>();

            const auto& balance1_itr = index.find(make128key(pool_itr->pool1.contract.value, pool_itr->pool1.quantity.symbol.raw()));
            const auto& balance2_itr = index.find(make128key(pool_itr->pool2.contract.value, pool_itr->pool2.quantity.symbol.raw()));

            auto tokenADesired = extended_asset(0, pool_itr->pool1.get_extended_symbol());
            auto tokenBDesired = extended_asset(0, pool_itr->pool2.get_extended_symbol());
            if (balance1_itr != index.end() && (balance1_itr->balance.quantity.amount > 0)) {
               action(permission_level{ _self, "active"_n }, balance1_itr->balance.contract, "transfer"_n,
                  std::make_tuple( _self, alcor_swap, balance1_itr->balance.quantity, string("deposit-") + user.to_string())).send();

               add_signed_ext_balance(user, -balance1_itr->balance);
               check(tokenADesired.quantity.symbol == balance1_itr->balance.quantity.symbol, "sanity check");
               tokenADesired.quantity = balance1_itr->balance.quantity; 
            }

            if (balance2_itr != index.end() && (balance2_itr->balance.quantity.amount > 0)) {
               action(permission_level{ _self, "active"_n }, balance2_itr->balance.contract, "transfer"_n,
                  std::make_tuple( _self, alcor_swap, balance2_itr->balance.quantity, string("deposit-") + user.to_string())).send();

               add_signed_ext_balance(user, -balance2_itr->balance);
               check(tokenBDesired.quantity.symbol == balance2_itr->balance.quantity.symbol, "sanity check");
               tokenBDesired.quantity = balance2_itr->balance.quantity;
            }

            if((tokenADesired.quantity.amount > 0) || (tokenBDesired.quantity.amount > 0)){
   
               if(!isTokensSorted(tokenADesired, tokenBDesired)){
                  auto swapToken = tokenADesired;
                  tokenADesired = tokenBDesired;
                  tokenBDesired = swapToken;
               }
               auto tokenAMin = extended_asset(0, tokenADesired.get_extended_symbol());
               auto tokenBMin = extended_asset(0, tokenBDesired.get_extended_symbol());

               auto [isExist, pool] = _getPool(tokenADesired, tokenBDesired, alcor_swap_pool_fee);
               check(isExist, "Pool not found on swap.alcor");
               check(tokenADesired.get_extended_symbol() == pool.tokenA.get_extended_symbol(), "sanity check");
               check(tokenBDesired.get_extended_symbol() == pool.tokenB.get_extended_symbol(), "sanity check");
               // create liquidity on alcorswap
               action(permission_level{get_self(), name("active")}, alcor_swap, name("addliquid"),
                     std::make_tuple(pool.id, user, tokenADesired, tokenBDesired, MIN_TICK, MAX_TICK, tokenAMin,tokenBDesired, 0))
                  .send();

               action(permission_level{get_self(), name("active")}, alcor_swap, name("logmigration"),
                     std::make_tuple(poolId, user, std::string("Your liqudity was migrated to AlcorV2 for better capital efficiency and accuracy. Please visit https://alcor.exchange to see your updated liquidity")))
                  .send();
            }
         }
         check(liqudity_token_itr->balance.amount == 0, "sanity check");
         from_acnts.erase(liqudity_token_itr);
      }
   }
}

void pools::changefee(uint64_t pool_id, int newfee) {
   contract_is_maintaining();
    const auto& pool = _pairs.get(pool_id, "does not exist");
    require_auth(pool.fee_contract);
    _pairs.modify(pool, same_payer, [&]( auto& a ) {
      a.fee = newfee;
    });
}

uint128_t pools::make128key(uint64_t a, uint64_t b) {
    uint128_t aa = a;
    uint128_t bb = b;
    return (aa << 64) + bb;
}

checksum256 pools::make256key(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    if (make128key(a,b) < make128key(c,d))
      return checksum256::make_from_word_sequence<uint64_t>(a,b,c,d);
    else
      return checksum256::make_from_word_sequence<uint64_t>(c,d,a,b);
}

void pools::add_signed_ext_balance(const name& user, const extended_asset& to_add, bool user_paying) {
   check(to_add.quantity.is_valid(), "invalid asset");
   evodexacnts acnts( _self, user.value );
   auto index = acnts.get_index<"extended"_n>();
   const auto& acnt_balance = index.find(make128key(to_add.contract.value, to_add.quantity.symbol.raw()));

   auto payer = user_paying ? user : _self;
   if (acnt_balance == index.end()) {
	   acnts.emplace(payer, [&]( auto& a ) {

	  	a.id = acnts.available_primary_key();
        a.balance = to_add;
		  check( a.balance.quantity.amount > 0, "insufficient funds: " + to_add.quantity.to_string());
      });
   } else {
		if ((acnt_balance->balance.quantity.amount + to_add.quantity.amount) == 0) {
			index.erase(acnt_balance);
	   } else {
			index.modify( acnt_balance, payer, [&]( auto& a ) {
				a.balance += to_add;
		      check( a.balance.quantity.amount > 0, "insufficient funds: " + to_add.quantity.to_string());
			});
		}
	}
}

void pools::contract_is_maintaining(){
   check(false, "contract is maintaining ...");
}

std::tuple<bool, pools::PoolS> pools::_getPool(extended_asset tokenA, extended_asset tokenB, uint32_t fee) {
   pools_t pools_(alcor_swap, alcor_swap.value);
  auto _pools_by_poolkey = pools_.get_index<"bypoolkey"_n>();
  auto pools_key = makePoolKey(tokenA, tokenB);
  auto pools_itr = _pools_by_poolkey.lower_bound(pools_key);
  PoolS _pool;
  bool isExist = false;
  while (pools_itr != _pools_by_poolkey.end() && makePoolKey(pools_itr->tokenA, pools_itr->tokenB) == pools_key) {
    if (pools_itr->fee == fee) {
      _pool = *pools_itr;
      isExist = true;
    }
    ++pools_itr;
  }
  if (isExist) {
    check(makePoolKey(_pool.tokenA, _pool.tokenB) == pools_key && _pool.fee == fee, "Sanity check");
  }
  return {isExist, _pool};
}