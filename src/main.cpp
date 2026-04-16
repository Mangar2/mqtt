#include <iostream>
#include <string_view>

namespace {
constexpr std::string_view k_version = "0.1.0";
}

int main()
{
    std::cout << "mqtt-broker " << k_version << '\n';
    return 0;
}
