source_path=$(PWD)
contract_name=pools

#cleos_path=cleos -u https://api.jungle.alohaeos.com
#cleos_path=cleos -u https://api.main.alohaeos.com
#cleos_path=cleos -u https://coffe.io:8888

# LOCAL DEV MODE
# contract_account=dex TODO migrate old contract
#cleos_path=cleos
#contract_account=pools

# WAX
#cleos_path=cleos -u https://wax.greymass.com
#contract_account=alcordexmain

# TELOS
#cleos_path=cleos -u https://api.telos.alohaeos.com
#contract_account=eostokensdex

# EOS mainnet
#cleos_path=cleos -u https://api.main.alohaeos.com:443
#contract_account=eostokensdex

# PROTON
#cleos_path=cleos -u https://proton.cryptolions.io
#contract_account=alcor

# JUNGLE TESTNET
#cleos_path=cleos -u https://api.jungle3.alohaeos.com
#contract_account=alcorpoolsex

# WAX TESTNET
cleos_path=cleos -u https://waxtestnet.greymass.com
contract_account=alcordexswap

unlock_wallet:
	$(cleos_path) wallet unlock --password $(wallet_pasword)

import_keys:
	$(cleos_path) wallet import --private-key 5Jg7y...

build:
	eosio-cpp -w --abigen $(source_path)/$(contract_name).cpp -o $(source_path)/$(contract_name).wasm

buy_ram:
	$(cleos_path) system buyram $(contract_account) $(contract_account) --kbytes 800

deploy:
	$(cleos_path) set contract $(contract_account) $(source_path) $(contract_name).wasm $(contract_name).abi -p $(contract_account)@active
	$(cleos_path) set account permission $(contract_account) active --add-code

#deploy:
#	$(cleos_path) set contract ex $(source_path) $(contract).wasm $(contract).abi -p ex@active
#	$(cleos_path) set account permission ex active --add-code
