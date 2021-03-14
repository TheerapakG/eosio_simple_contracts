#include <simple_contracts/common.hpp>
#include <concepts>

namespace simple_contracts
{
    template<typename T>
    concept Reserve = requires(T reserve, Token token) 
    {
        { reserve.add_token(token) } -> std::same_as<void>;
        { reserve.get_reserve(token, token) } -> std::convertible_to<std::pair<int64_t, int64_t>>;
    };
}
