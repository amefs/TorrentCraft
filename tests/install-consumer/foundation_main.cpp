#include <torrentutils/core/foundation.hpp>

int main()
try
{
    torrentutils::core::CancellationSource source;
    const auto token = source.token();
    const auto result = torrentutils::core::Result<int>::success(1);

    return !token.is_cancelled() && result.has_value() && result.value() == 1 ? 0 : 1;
}
catch (...)
{
    return 1;
}
