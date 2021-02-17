#include "evolutiondex.hpp"
#include "utils.hpp"

using namespace evolution;

void evolutiondex::ontransfer(name from, name to, asset quantity, string memo) {
    constexpr string DEPOSIT_TO = "deposit to:";
    constexpr string EXCHANGE   = "exchange:";

    if (from == _self) return;
    check(to == _self, "This transfer is not for evolutiondex");
    check(quantity.amount >= 0, "quantity must be positive");

    auto incoming = extended_asset{quantity, _first_receiver};
    string_view memosv(memo);
    if ( starts_with(memosv, EXCHANGE) ) {
      memoexchange(from, incoming, memosv.substr(EXCHANGE.size()) );
    } else {
      if ( starts_with(memosv, DEPOSIT_TO) ) {
          from = name(trim(memosv.substr(DEPOSIT_TO.size())));
          check(from != _self, "Donation not accepted");
      }
      add_signed_ext_balance(from, incoming);
    }
}

void evolutiondex::withdraw(name user, name to, extended_asset to_withdraw, string memo) {
    require_auth( user );
    check(to_withdraw.quantity.amount > 0, "quantity must be positive");
    add_signed_ext_balance(user, -to_withdraw);
    action(permission_level{ _self, "active"_n }, to_withdraw.contract, "transfer"_n,
      std::make_tuple( _self, to, to_withdraw.quantity, memo) ).send();
}

void evolutiondex::addliquidity(name user, asset to_buy, 
  asset max_asset1, asset max_asset2) {
    require_auth(user);
    check( (to_buy.amount > 0), "to_buy amount must be positive");
    check( (max_asset1.amount >= 0) && (max_asset2.amount >= 0), "assets must be nonnegative");
    add_signed_liq(user, to_buy, true, max_asset1, max_asset2);
}

void evolutiondex::remliquidity(name user, asset to_sell,
  asset min_asset1, asset min_asset2) {
    require_auth(user);
    check(to_sell.amount > 0, "to_sell amount must be positive");
    check( (min_asset1.amount >= 0) && (min_asset2.amount >= 0), "assets must be nonnegative");
    add_signed_liq(user, -to_sell, false, -min_asset1, -min_asset2);
}

// computes x * y / z plus the fee
int64_t evolutiondex::compute(int64_t x, int64_t y, int64_t z, int fee) {
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

void evolutiondex::add_signed_liq(name user, asset to_add, bool is_buying,
  asset max_asset1, asset max_asset2) {
    check( to_add.is_valid(), "invalid asset");
    stats statstable( _self, to_add.symbol.code().raw() );
    const auto& token = statstable.find( to_add.symbol.code().raw() );
    check ( token != statstable.end(), "pair token does not exist" );
    auto A = token-> supply.amount;
    auto P1 = token-> pool1.quantity.amount;
    auto P2 = token-> pool2.quantity.amount;

    int fee = is_buying? ADD_LIQUIDITY_FEE : 0;
    auto to_pay1 = extended_asset{ asset{compute(to_add.amount, P1, A, fee),
      token->pool1.quantity.symbol}, token->pool1.contract};
    auto to_pay2 = extended_asset{ asset{compute(to_add.amount, P2, A, fee),
      token->pool2.quantity.symbol}, token->pool2.contract};
    check( (to_pay1.quantity.symbol == max_asset1.symbol) && 
           (to_pay2.quantity.symbol == max_asset2.symbol), "incorrect symbol");
    check( (to_pay1.quantity.amount <= max_asset1.amount) && 
           (to_pay2.quantity.amount <= max_asset2.amount), "available is less than expected");

    add_signed_ext_balance(user, -to_pay1);
    add_signed_ext_balance(user, -to_pay2);
    (to_add.amount > 0)? add_balance(user, to_add, user) : sub_balance(user, -to_add);
    if (token->fee_contract) require_recipient(token->fee_contract);
    statstable.modify( token, same_payer, [&]( auto& a ) {
      a.supply += to_add;
      a.pool1 += to_pay1;
      a.pool2 += to_pay2;
    });
    check(token->supply.amount != 0, "the pool cannot be left empty");
}

void evolutiondex::exchange( name user, symbol_code pair_token, 
  extended_asset ext_asset_in, asset min_expected) {
    require_auth(user);
    check( ((ext_asset_in.quantity.amount > 0) && (min_expected.amount >= 0)) ||
           ((ext_asset_in.quantity.amount < 0) && (min_expected.amount <= 0)), 
           "ext_asset_in must be nonzero and min_expected must have same sign or be zero");
    auto ext_asset_out = process_exch(pair_token, ext_asset_in, min_expected);
    add_signed_ext_balance(user, -ext_asset_in);
    add_signed_ext_balance(user, ext_asset_out);
}

extended_asset evolutiondex::process_exch(symbol_code pair_token,
  extended_asset ext_asset_in, asset min_expected){
    stats statstable( _self, pair_token.raw() );
    const auto token = statstable.find( pair_token.raw() );
    check ( token != statstable.end(), "pair token does not exist" );
    bool in_first;
    if ((token->pool1.get_extended_symbol() == ext_asset_in.get_extended_symbol()) && 
        (token->pool2.quantity.symbol == min_expected.symbol)) {
        in_first = true;
    } else if ((token->pool1.quantity.symbol == min_expected.symbol) &&
               (token->pool2.get_extended_symbol() == ext_asset_in.get_extended_symbol())) {
        in_first = false;
    }
    else check(false, "extended_symbol mismatch");
    int64_t P_in, P_out;
    if (in_first) { 
      P_in = token-> pool1.quantity.amount;
      P_out = token-> pool2.quantity.amount;
    } else {
      P_in = token-> pool2.quantity.amount;
      P_out = token-> pool1.quantity.amount;
    }
    auto A_in = ext_asset_in.quantity.amount;
    int64_t A_out = compute(-A_in, P_out, P_in + A_in, token->fee);
    check(min_expected.amount <= -A_out, "available is less than expected");
    extended_asset ext_asset1, ext_asset2, ext_asset_out;
    if (in_first) { 
      ext_asset1 = ext_asset_in;
      ext_asset2 = extended_asset{A_out, token-> pool2.get_extended_symbol()};
      ext_asset_out = -ext_asset2;
    } else {
      ext_asset1 = extended_asset{A_out, token-> pool1.get_extended_symbol()};
      ext_asset2 = ext_asset_in;
      ext_asset_out = -ext_asset1;
    }
    statstable.modify( token, same_payer, [&]( auto& a ) {
      a.pool1 += ext_asset1;
      a.pool2 += ext_asset2;
    });
    return ext_asset_out;
}

void evolutiondex::memoexchange(name user, extended_asset ext_asset_in, string details) {
   auto parts = split(details, "|");
   check(parts.size() >= 1, "Expected format 'min_expected_asset|optional memo'");

   extended_asset min_expected = e_asset_from_string(parts[0]);

   auto index = _pools.get_index<"extended"_n>();
   const auto& pool = index.get(
      make256key(
         ext_asset_in.contract.value, ext_asset_in.quantity.symbol.code().raw(),
         min_expected.contract.value, min_expected.quantity.symbol.code().raw()
      ), "Pool not exists!"
   );

    // TODO Тут вычисляем из двух ассетов какой это пул

   auto second_comma_pos = details.find(",", 1 + details.find(","));
   auto memo = (second_comma_pos == string::npos) ? "" : details.substr(1 + second_comma_pos);

   check(min_expected.quantity.amount >= 0, "min_expected must be expressed with a positive amount");

   auto ext_asset_out = process_exch(pool, ext_asset_in, min_expected);

   action(permission_level{ _self, "active"_n }, ext_asset_out.contract, "transfer"_n,
     std::make_tuple( _self, user, ext_asset_out.quantity, std::string(memo)) ).send();
}

void evolutiondex::inittoken(name user, extended_asset initial_pool1,
extended_asset initial_pool2, int initial_fee, name fee_contract)
{
   require_auth( user );
   require_auth( _self );

   check((initial_pool1.quantity.amount > 0) && (initial_pool2.quantity.amount > 0), "Both assets must be positive");
   check((initial_pool1.quantity.amount < INIT_MAX) && (initial_pool2.quantity.amount < INIT_MAX), "Initial amounts must be less than 10^15");
   check( initial_pool1.get_extended_symbol() != initial_pool2.get_extended_symbol(), "extended symbols must be different");

   // Check if pool exists
   auto index = _pools.get_index<"extended"_n>();
   const auto& pool_exists = index.find(make256key(
      initial_pool1.contract.value, initial_pool1.quantity.symbol.code().raw(),
      initial_pool2.contract.value, initial_pool2.quantity.symbol.code().raw()));
   check(pool_exists == index.end(), "Pool already exists");

   // Generate pool token


   uint8_t new_precision = ( initial_pool1.quantity.symbol.precision() + initial_pool2.quantity.symbol.precision() ) / 2;
   int128_t geometric_mean = sqrt(int128_t(initial_pool1.quantity.amount) * int128_t(initial_pool2.quantity.amount));
   auto new_token = asset{int64_t(geometric_mean), new_symbol};



               //return make256key(
               //   pool1.contract.value, pool1.quantity.symbol.code().raw(),
               //   pool2.contract.value, pool2.quantity.symbol.code().raw()
               //);


   stats statstable( _self, new_symbol.code().raw() );
   const auto& token = statstable.find( new_symbol.code().raw() );
   check ( token == statstable.end(), "token symbol already exists" );
   check( initial_fee == DEFAULT_FEE, "initial_fee must be 10");
   check( fee_contract == "wevotethefee"_n, "fee_contract must be wevotethefee");

   const uint64_t new_pool_id = _pools.emplace( user, [&]( auto& a ) {
      a.id = _pools.available_primary_key();
      a.pool1 = initial_pool1;
      a.pool2 = initial_pool2;
      a.fee = initial_fee;
      a.fee_contract = fee_contract;
   });

   statstable.emplace( user, [&]( auto& a ) {
       a.supply = new_token;
       a.max_supply = asset{MAX,new_symbol};
       a.issuer = _self;
       a.pool_id = new_pool_id;
   } );

   placeindex(user, new_symbol, initial_pool1, initial_pool2 );
   add_balance(user, new_token, user);
   add_signed_ext_balance(user, -initial_pool1);
   add_signed_ext_balance(user, -initial_pool2);
}

void evolutiondex::changefee(symbol_code pair_token, int newfee) {
    stats statstable( _self, pair_token.raw() );
    const auto& token = statstable.find( pair_token.raw() );
    check ( token != statstable.end(), "pair token does not exist" );
    require_auth(token->fee_contract);
    statstable.modify( token, same_payer, [&]( auto& a ) {
      a.fee = newfee;
    } );
}

uint128_t evolutiondex::make128key(uint64_t a, uint64_t b) {
    uint128_t aa = a;
    uint128_t bb = b;
    return (aa << 64) + bb;
}

checksum256 evolutiondex::make256key(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    if (make128key(a,b) < make128key(c,d))
      return checksum256::make_from_word_sequence<uint64_t>(a,b,c,d);
    else
      return checksum256::make_from_word_sequence<uint64_t>(c,d,a,b);
}

void evolutiondex::add_signed_ext_balance( const name& user, const extended_asset& to_add )
{
    check( to_add.quantity.is_valid(), "invalid asset" );
    evodexacnts acnts( _self, user.value );
    auto index = acnts.get_index<"extended"_n>();
    const auto& acnt_balance = index.find( make128key(to_add.contract.value, to_add.quantity.symbol.raw() ) );

    if (acnt_balance == index.end()) {
       index.emplace(same_payer, [&]( auto& a ) {
           a.balance = to_add;
           check( a.balance.quantity.amount >= 0, "insufficient funds");
       });
    } else {
       index.modify( acnt_balance, same_payer, [&]( auto& a ) {
           a.balance += to_add;
           check( a.balance.quantity.amount >= 0, "insufficient funds");
       });
    }
}
