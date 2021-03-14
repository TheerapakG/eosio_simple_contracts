#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/string.hpp>

namespace simple_contracts
{
    using namespace eosio;

    struct Token: public extended_symbol
    {        
        auto value() const -> uint128_t
        {
            return get_contract().value * (static_cast<uint128_t>(std::numeric_limits<uint64_t>::max())+1) + get_symbol().raw();
        }
    };
}
