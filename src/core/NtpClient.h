#pragma once

#include <string>

namespace FreeButton {

class NtpClient {
public:
    static void initialize(const std::string& ntpServer, const std::string& timezone);
    static bool syncTime();
    static bool isTimeSynchronized();

private:
    static constexpr auto TAG = "ntp";
    static constexpr auto DEFAULT_NTP_SERVER = "pool.ntp.org";
    static constexpr auto DEFAULT_TIMEZONE = "CET-1CEST,M3.5.0,M10.5.0/3";

    static bool initialized;
};

} // namespace FreeButton