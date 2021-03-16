#ifndef _SIMPLE_CONTRACTS_SWAP
#define _SIMPLE_CONTRACTS_SWAP

#ifdef SIMPLE_CONTRACTS_USE_CONCEPTS
#include <simple_contracts/concepts/swap.hpp>
#else

#include <simple_contracts/common.hpp>

namespace simple_contracts
{
    CONTRACT VariableReserveSwapContract: public contract
    {
        public:
        using contract::contract;

        TABLE tokens
        {
            uint64_t id;
            Token    token;

            auto primary_key() const -> uint64_t  { return id; }
            auto token_key()   const -> uint128_t { return token.value(); }
        };

        TABLE pairs
        {
            uint64_t id;
            uint64_t token1_id;
            uint64_t token2_id;
            uint64_t token1_reserve;
            uint64_t token2_reserve;
            uint64_t liquidity_amount;

            auto primary_key()    const -> uint64_t  { return id; }
            auto token_pair_key() const -> uint128_t { return token1_id * (static_cast<uint128_t>(std::numeric_limits<uint64_t>::max())+1) + token2_id; }
        };

        TABLE liquidity
        {
            uint64_t id;
            uint64_t token1_staged;
            uint64_t token2_staged;
            uint64_t liquidity_amount;

            auto primary_key()    const -> uint64_t  { return id; }
        };
        
        using TokenTable = multi_index<name{"tokens"}, tokens,
            indexed_by<name{"token"}, const_mem_fun<tokens, uint128_t, &tokens::token_key>>
        >;
        using PairTable = multi_index<name{"pairs"}, pairs,
            indexed_by<name{"tokenpair"}, const_mem_fun<pairs, uint128_t, &pairs::token_pair_key>>
        >;
        using LiquidityTable = multi_index<name{"liquidity"}, liquidity>; 

        TokenTable token_table{get_self(), get_self().value};
        PairTable  pair_table {get_self(), get_self().value};

        class TokenPair;

        private:
        friend class VariableReserveSwapContract::TokenPair;

        auto get_token_id(const Token& token) const
        {
            auto token_index = token_table.template get_index<name{"token"}>();
            return token_index.require_find(token.value(), "token not added")->id;
        }

        auto send(name owner, uint64_t token_id, int64_t amount)
        {
            if(amount > 0)
            {
                Token token = token_table.find(token_id)->token;
                action{
                    permission_level{get_self(), name{"active"}}, 
                    token.get_contract(),
                    name{"transfer"},
                    std::make_tuple(get_self(), owner, asset{amount, token.get_symbol()}, string{})
                }.send();
            }
        }

        public:
        class TokenPair
        {
            private:
            VariableReserveSwapContract& outer;
            PairTable::const_iterator    it;

            auto reserve_stake(uint64_t liquidity_amount) const -> std::pair<uint64_t, uint64_t>
            {
                uint128_t prod1 = it->token1_reserve, prod2 = it->token2_reserve;
                prod1 *= liquidity_amount; prod2 *= liquidity_amount;
                prod1 /= it->liquidity_amount; prod2 /= it->liquidity_amount;
                return {static_cast<uint64_t>(prod1), static_cast<uint64_t>(prod2)};
            }

            auto send(name owner, uint64_t amount1, uint64_t amount2) -> void
            {
                outer.send(owner, it->token1_id, amount1);
                outer.send(owner, it->token2_id, amount2);
            }

            public:
            TokenPair(VariableReserveSwapContract& outer, uint64_t token1_id, uint64_t token2_id): 
                outer(outer), 
                it{
                    outer.pair_table.iterator_to(
                        outer.pair_table
                        .template get_index<name{"tokenpair"}>()
                        .get(token1_id * (static_cast<uint128_t>(std::numeric_limits<uint64_t>::max())+1) + token2_id, "token pair not created")
                    )
                }
            {}

            TokenPair(VariableReserveSwapContract& outer, uint64_t pair_id): 
                outer(outer), 
                it{
                    outer.pair_table
                    .require_find(pair_id, "token pair id not exist")
                }
            {}

            auto stage_token(name owner, uint64_t token_id, uint64_t amount) -> void
            {
                LiquidityTable liquidity_table{outer.get_self(), owner.value};
                auto lt_it = liquidity_table.find(it->id);
                if(lt_it==liquidity_table.end())
                {
                    uint64_t last_id = ((liquidity_table.begin()==liquidity_table.end())? 0:liquidity_table.rbegin()->id);
                    liquidity_table.emplace(
                        outer.get_self(),
                        [&](liquidity& row)
                        {
                            row.id = last_id + 1;
                            row.token1_staged = (token_id == it->token1_id? amount:0);
                            row.token2_staged = (token_id == it->token2_id? amount:0);
                            row.liquidity_amount = 0;
                        }
                    );
                }
                else
                {
                    liquidity_table.modify(
                        liquidity_table.find(it->id),
                        same_payer,
                        [&](liquidity& row)
                        {
                            if(token_id == it->token1_id) row.token1_staged += amount;
                            if(token_id == it->token2_id) row.token2_staged += amount;
                        }
                    );
                } 
            }

            auto deposit(name owner, uint64_t amount1, uint64_t amount2) -> void
            {
                uint64_t liquidity_amount = 0;
                uint64_t r_amount1 = amount1, r_amount2 = amount2;
                if(it->liquidity_amount==0) liquidity_amount = std::min(amount1, amount2);
                else
                {
                    uint64_t r_amount1, r_amount2;

                    // make sure we can give liquidity token and that the incoming tokens are in the correct proportion
                    {
                        uint128_t prod1 = amount1, prod2 = amount2;
                        prod1 *= it->liquidity_amount;
                        prod2 *= it->liquidity_amount;
                        liquidity_amount = std::min(prod1/it->token1_reserve, prod2/it->token2_reserve);
                        std::tie(r_amount1, r_amount2) = reserve_stake(liquidity_amount);
                    }
                }

                {
                    LiquidityTable liquidity_table{outer.get_self(), owner.value};
                    liquidity_table.modify(
                        liquidity_table.find(it->id),
                        same_payer,
                        [&](liquidity& row)
                        {
                            row.token1_staged -= amount1;
                            row.token2_staged -= amount2;
                            row.liquidity_amount += liquidity_amount;
                        }
                    );
                }

                // refund excess token
                send(owner, amount1-r_amount1, amount2-r_amount2);

                outer.pair_table.modify(
                    it, 
                    same_payer,
                    [&](pairs& row)
                    {
                        row.token1_reserve += r_amount1;
                        row.token2_reserve += r_amount2;
                        row.liquidity_amount += liquidity_amount;
                    }
                );
            }

            auto withdraw(name owner, uint64_t liquidity_amount) -> void
            {
                auto [amount1, amount2] = reserve_stake(liquidity_amount);

                send(owner, amount1, amount2);

                outer.pair_table.modify(
                    it, 
                    same_payer,
                    [amount1=amount1, amount2=amount2, liquidity_amount](pairs& row)
                    {
                        row.token1_reserve -= amount1;
                        row.token2_reserve -= amount2;
                        row.liquidity_amount -= liquidity_amount;
                    }
                );
            }

            auto swap(name owner, uint64_t token_id, uint64_t amount) -> void
            {
                uint64_t token1_new, token2_new;
                uint64_t swapped_token, swapped_amount;

                if(token_id==it->token1_id)
                {
                    uint64_t token1_reserve = it->token1_reserve, token2_reserve = it->token2_reserve;
                    token1_new = token1_reserve + amount;
                    token2_new = static_cast<uint64_t>((static_cast<uint128_t>(token2_reserve) * token1_reserve) / token1_new);

                    swapped_token = it->token2_id;
                    swapped_amount = token2_reserve - token2_new;
                }
                else if(token_id==it->token2_id)
                {
                    uint64_t token1_reserve = it->token1_reserve, token2_reserve = it->token2_reserve;
                    token2_new = token2_reserve + amount;
                    token1_new = static_cast<uint64_t>((static_cast<uint128_t>(token1_reserve) * token2_reserve) / token2_new);

                    swapped_token = it->token1_id;
                    swapped_amount = token1_reserve - token1_new;
                }
                else check(false, "token is not in the pair");

                outer.send(owner, swapped_token, swapped_amount);

                outer.pair_table.modify(
                    it, 
                    same_payer,
                    [&](pairs& row)
                    {
                        row.token1_reserve = token1_new;
                        row.token2_reserve = token2_new;
                    }
                );
            }
        };

        auto add_token(const Token& token) -> void
        {
            uint64_t last_id = ((token_table.begin()==token_table.end())? 0:token_table.rbegin()->id);
            token_table.emplace(
                get_self(), 
                [&](tokens& row)
                {
                    row.id = last_id+1;
                    row.token = token;
                }
            );
        }

        auto create_pair(const Token& token1, const Token& token2) -> void
        {
            uint64_t token1_id = get_token_id(token1), token2_id = get_token_id(token2);

            uint64_t last_id = ((pair_table.begin()==pair_table.end())? 0:pair_table.rbegin()->id);
            pair_table.emplace(
                get_self(), 
                [&](pairs& row)
                {
                    row.id = last_id+1;
                    row.token1_id = token1_id;
                    row.token2_id = token2_id;
                    row.token1_reserve = 0;
                    row.token2_reserve = 0;
                    row.liquidity_amount = 0;
                }
            );
        }

        auto get_pair(const Token& token1, const Token& token2) -> TokenPair
        {
            uint64_t token1_id = get_token_id(token1), token2_id = get_token_id(token2);

            return TokenPair{*this, token1_id, token2_id};
        }

        auto get_pair(uint64_t pair_id) -> TokenPair
        {
            return TokenPair{*this, pair_id};
        }

        [[eosio::on_notify("*::transfer")]] 
        void on_reserve_transfer(name user, name receiver, asset value, string memo)
        {
            if(user == get_self()) return; // our tx

            name token_contract = get_first_receiver();
            
            if(memo.empty()) return; // assume donation
            if(memo[0] == 'D')
            {
                uint64_t token_id = get_token_id(Token{{value.symbol, token_contract}});

                // deposit
                string rest = memo.substr(1);
                uint64_t pair_id = std::stoull(std::string{rest.begin(), rest.end()}); // TODO: illegal conversion?

                TokenPair pair = get_pair(pair_id);
                pair.stage_token(user, token_id, value.amount);
            }
            if(memo[0] == 'S')
            {
                uint64_t token_id = get_token_id(Token{{value.symbol, token_contract}});

                // deposit
                string rest = memo.substr(1);
                uint64_t pair_id = std::stoull(std::string{rest.begin(), rest.end()}); // TODO: illegal conversion?

                TokenPair pair = get_pair(pair_id);
                pair.swap(user, token_id, value.amount);
            }
        }

        ACTION addtoken(eosio::name contract, eosio::symbol symbol)
        {
            check(symbol.is_valid(), "symbol is invalid");

            add_token(Token{{symbol, contract}});
        }

        ACTION createpair(eosio::name contract1, eosio::symbol symbol1, eosio::name contract2, eosio::symbol symbol2)
        {
            check(symbol1.is_valid(), "symbol1 is invalid");
            check(symbol2.is_valid(), "symbol2 is invalid");
            check(!(symbol1==symbol2 && contract1==contract2), "cannot create pair with the same token");

            create_pair(Token{{symbol1, contract1}}, Token{{symbol2, contract2}});
        }

        ACTION deposit(eosio::name owner, uint64_t pair_id)
        {
            require_auth(owner);

            LiquidityTable liquidity_table{get_self(), owner.value};
            auto lt_it = liquidity_table.require_find(pair_id, "no asset deposited to the pair");
            TokenPair pair = get_pair(pair_id);
            pair.deposit(owner, lt_it->token1_staged, lt_it->token2_staged);
        }

        ACTION withdraw(eosio::name owner, uint64_t pair_id, uint64_t liquidity_amount)
        {
            require_auth(owner);

            LiquidityTable liquidity_table{get_self(), owner.value};
            auto lt_it = liquidity_table.require_find(pair_id, "no asset deposited to the pair");

            check(liquidity_amount <= lt_it->liquidity_amount, "insufficient liquidity");
            
            TokenPair pair = get_pair(pair_id);
            pair.withdraw(owner, liquidity_amount);            
        }
    };
}

#endif
#endif
