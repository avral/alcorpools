#include "wevotethefee.hpp"

int wevotethefee::median(symbol_code token){
    feetables tables( get_self(), token.raw() );
    auto table = tables.find( token.raw());
    check( table != tables.end(), "fee table nonexistent, run openfeetable" );
    auto votes = table->votes;
    int64_t sum = 0, partial_sum = 0;
    int index = 0;
    for (int i = 0; i < fee_vector.size(); i++) {
        sum += votes.at(i);
    }
    for (int i = 0; 2 * partial_sum < sum; i++) {
        partial_sum += votes.at(i);
        index = i;
    }
    return fee_vector.at(index);
}

void wevotethefee::updatefee(symbol_code token) {
    action(permission_level{ get_self(), "active"_n }, "evolutiondex"_n, "changefee"_n,
      std::make_tuple( token, median(token))).send(); 
}

void wevotethefee::addliquidity(name user, asset to_buy, extended_asset max_ext_asset1, extended_asset max_ext_asset2){
    add_balance(user, to_buy);
}

void wevotethefee::remliquidity(name user, asset to_sell, extended_asset min_ext_asset1, extended_asset min_ext_asset2){
    add_balance(user, -to_sell);
}

void wevotethefee::transfer(const name& from, const name& to, const asset& quantity, const string&  memo ){
    add_balance(from, -quantity);
    add_balance(to, quantity);
};

asset wevotethefee::bring_balance(name user, symbol_code token) {
    accounts table( "evolutiondex"_n, user.value );
    const auto& user_balance = table.find( token.raw() );
    check ( user_balance != table.end(), "token does not exist" );
    return user_balance->balance;
}

void wevotethefee::votefee(name user, symbol_code token, int fee_voted){
    int fee_index_voted = get_index(fee_voted);
    require_auth(user);
    feeaccounts acnts( get_self(), user.value );
    auto acnt = acnts.find( token.raw());
    auto balance = bring_balance(user, token);
    if ( acnt == acnts.end() ) {
        acnts.emplace( user, [&]( auto& a ){
          a.token = token;
          a.fee_index_voted = fee_index_voted;
        });
    } else {
        addvote(token, acnt->fee_index_voted, -balance.amount );
        acnts.modify( acnt, user, [&]( auto& a ){
          a.fee_index_voted = fee_index_voted;
        });
    }
    addvote(token, fee_index_voted, balance.amount);
}

void wevotethefee::closevote(name user, symbol_code token) {
    require_auth(user);
    feeaccounts acnts( get_self(), user.value );
    auto acnt = acnts.find( token.raw());
    check( acnt != acnts.end(), "user is not voting" );
    auto balance = bring_balance(user, token);    
    addvote( token, acnt->fee_index_voted, -balance.amount );
    acnts.erase(acnt);
}

void wevotethefee::closefeetable(name user, symbol_code token) {
    require_auth(user);
    feetables tables( get_self(), token.raw() );
    auto table = tables.find( token.raw());
    check( table != tables.end(), "table does not exist" );
    auto votes = table->votes;
    bool empty = true;
    for (int i = 0; i < votes.size(); i++) {
        empty &= !votes.at(i);
    }
    if (empty) tables.erase(table);
}

void wevotethefee::openfeetable(name user, symbol_code token) {
    feetables tables( get_self(), token.raw() );
    auto table = tables.find( token.raw());
    check( table == tables.end(), "already opened" );
    vector<int64_t> zeros( fee_vector.size());
    tables.emplace( user, [&]( auto& a ){
      a.token = token;  
      a.votes = zeros;
    });
}

void wevotethefee::add_balance(name user, asset to_add) {
    feeaccounts acnts( get_self(), user.value );
    auto acnt = acnts.find( to_add.symbol.code().raw());
    if (acnt != acnts.end()) addvote( acnt->token, acnt->fee_index_voted, to_add.amount);
}

void wevotethefee::addvote(symbol_code token, int fee_index, int64_t amount) {
    feetables tables( get_self(), token.raw() );
    auto table = tables.find( token.raw());
    check( table != tables.end(), "fee table nonexistent, run openfeetable" );
    tables.modify(table, ""_n, [&]( auto& a ){
      a.votes.at(fee_index) += amount;
    });
}

int wevotethefee::get_index(int fee_value){
    int index;
    for (int i = 0; (i < fee_vector.size() ) && (fee_vector.at(i) <= fee_value); i++){
        index = i;
    }
    return index;
}