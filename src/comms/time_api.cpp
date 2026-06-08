#include "time_api.h"

#include "../config.h"
#include <sys/time.h>
#include <time.h>

static bool s_time_valid = false;

static bool epoch_is_valid(time_t value)
{
    struct tm tm_now;
    localtime_r(&value, &tm_now);
    return tm_now.tm_year + 1900 >= 2024;
}

void time_api_init()
{
    setenv("TZ", TIMEZONE, 1);
    tzset();
    s_time_valid = false;
}

bool time_api_set_epoch(long epoch)
{
    if (epoch < 1704067200L) {
        return false;
    }

    struct timeval tv = {static_cast<time_t>(epoch), 0};
    settimeofday(&tv, nullptr);
    s_time_valid = true;
    return true;
}

bool time_api_is_valid()
{
    time_t now = time(nullptr);
    if (s_time_valid && !epoch_is_valid(now)) {
        s_time_valid = false;
    }
    return s_time_valid;
}

void time_api_get_strings(char *time_buf, size_t time_size, char *date_buf, size_t date_size)
{
    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    if (time_api_is_valid()) {
        snprintf(time_buf, time_size, "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
        snprintf(date_buf, date_size, "%02d/%02d", tm_now.tm_mon + 1, tm_now.tm_mday);
    } else {
        snprintf(time_buf, time_size, "--:--");
        snprintf(date_buf, date_size, "--/--");
    }
}

long time_api_epoch()
{
    return static_cast<long>(time(nullptr));
}
