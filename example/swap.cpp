#include <simple_contracts/swap.hpp>

namespace sc = simple_contracts;

CONTRACT simpleswap: public sc::VariableReserveContractMixin<eosio::contract>
{
    public:
    using contract_t::contract_t;

    TABLE tokentable     : public contract_t::TokenTable_row     {};
    TABLE pairtable      : public contract_t::PairTable_row      {};
    TABLE liquiditytable : public contract_t::LiquidityTable_row {};

    [[eosio::on_notify("*::transfer")]] 
    void on_reserve_transfer(eosio::name user, eosio::name receiver, eosio::asset value, eosio::string memo)
    {
        contract_t::on_reserve_transfer(user, receiver, value, memo);
    }

    ACTION addtoken(eosio::name contract, eosio::symbol symbol)
    {
        contract_t::addtoken(contract, symbol);
    }

    ACTION createpair(eosio::name contract1, eosio::symbol symbol1, eosio::name contract2, eosio::symbol symbol2)
    {
        contract_t::createpair(contract1, symbol1, contract2, symbol2);
    }

    ACTION deposit(eosio::name owner, uint64_t pair_id)
    {
        contract_t::deposit(owner, pair_id);
    }

    ACTION withdraw(eosio::name owner, uint64_t pair_id, uint64_t liquidity_amount)
    {
        sc::VariableReserveContractMixin<eosio::contract>::withdraw(owner, pair_id, liquidity_amount);          
    }
};
