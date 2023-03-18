// "Copyright [2022] <alcor exchange>"
#ifndef INCLUDE_HELP_HPP_
#define INCLUDE_HELP_HPP_

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>


static void assertTokens(eosio::extended_asset tokenA, eosio::extended_asset tokenB) {
  eosio::check(tokenA.contract.value <= tokenB.contract.value, "Contracts must be sorted");
  if (tokenA.contract.value == tokenB.contract.value) {
    eosio::check(tokenA.quantity.symbol.code().raw() < tokenB.quantity.symbol.code().raw(),
                 "Symbols must be unique and sorted");
  }
}

static bool isTokensSorted(eosio::extended_asset& tokenA, eosio::extended_asset& tokenB) {
  return (tokenA.contract.value < tokenB.contract.value ||
          tokenA.contract.value == tokenB.contract.value &&
              tokenA.quantity.symbol.code().raw() < tokenB.quantity.symbol.code().raw());
}

static eosio::checksum256 makePoolKey(eosio::extended_asset tokenA, eosio::extended_asset tokenB) {
  assertTokens(tokenA, tokenB);
  return eosio::checksum256::make_from_word_sequence<uint64_t>(
      tokenA.contract.value, tokenA.quantity.symbol.code().raw(), tokenB.contract.value,
      tokenB.quantity.symbol.code().raw());
}

static uint128_t extended_to_id(eosio::extended_asset tokenAsset) {
  return uint128_t(tokenAsset.contract.value) << 64 | tokenAsset.quantity.symbol.code().raw();
}

static std::vector<std::string> splitMemoParams(std::string s, std::string delimiter = "#") {
  std::vector<std::string> vect;

  while (s.length()) {
    size_t pos = s.find(delimiter);
    if (pos != std::string::npos) {
      auto param_str = s.substr(0, pos);
      vect.push_back(param_str);
      s.erase(0, pos + delimiter.length());
    } else {
      vect.push_back(s);
      break;
    }
  }

  return vect;
}

static std::vector<uint64_t> getPoolIds(std::string s) {
  std::vector<std::string> stringPoolIds = splitMemoParams(s, ",");
  std::vector<uint64_t> poolIds;
  for(auto i = 0; i < stringPoolIds.size(); i++){
    const uint64_t poolId = std::stoll(stringPoolIds[i]);
    poolIds.push_back(poolId);
  }
  return poolIds;
}

#endif  // INCLUDE_HELP_HPP_
