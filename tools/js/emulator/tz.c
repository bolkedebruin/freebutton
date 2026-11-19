/*
 * Local time for fb-emulator.wasm.
 *
 * The device sets its time zone from config.json "core.timezone", a POSIX TZ
 * string ("CET-1CEST,M3.5.0,M10.5.0/3"; the firmware's default, NtpClient.h),
 * with setenv("TZ") + tzset(): newlib then applies the rule in localtime().
 * The wasm's libc has no time zone data and no tzset() of that kind, so this
 * file is the same rule: it parses the POSIX string and provides
 * localtime_r() / localtime(), which the script engine (Date#getHours ...)
 * and the firmware's clock use.
 *
 * Supported, as newlib: "std offset", "std offset dst [offset]" with the
 * rules "Mm.w.d[/time]" (month, week 1..5 where 5 = last, weekday 0 =
 * Sunday), "Jn[/time]" (day 1..365, 29 February not counted) and "n[/time]"
 * (day 0..365). Names are letters or <...>. Without a rule after a dst name
 * the US default "M3.2.0,M11.1.0" applies. A string that does not parse
 * gives UTC.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    char kind;   /* 'M', 'J' or 'N' (zero-based day) */
    int month;   /* 1..12 ('M') */
    int week;    /* 1..5 ('M') */
    int day;     /* weekday 0..6 ('M'), day of the year ('J' 1..365, 'N' 0..365) */
    long time;   /* seconds after local midnight (default 02:00) */
} tz_rule_t;

static struct {
    bool has_dst;
    long std_offset; /* seconds to ADD to UTC for standard time */
    long dst_offset;
    tz_rule_t start; /* to daylight saving time */
    tz_rule_t end;
    char std_name[12];
    char dst_name[12];
} zone = {false, 0, 0, {0}, {0}, "UTC", ""};

static const char *parse_name(const char *p, char *out, size_t cap)
{
    size_t n = 0;
    if (*p == '<') {
        p++;
        while (*p && *p != '>') {
            if (n + 1 < cap)
                out[n++] = *p;
            p++;
        }
        if (*p != '>')
            return NULL;
        p++;
    } else {
        while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
            if (n + 1 < cap)
                out[n++] = *p;
            p++;
        }
    }
    out[n] = '\0';
    return n >= 3 ? p : NULL;
}

static const char *parse_number(const char *p, int *out)
{
    if (*p < '0' || *p > '9')
        return NULL;
    int v = 0;
    while (*p >= '0' && *p <= '9')
        v = v * 10 + (*p++ - '0');
    *out = v;
    return p;
}

/* [+-]hh[:mm[:ss]] -> seconds */
static const char *parse_time(const char *p, long *out)
{
    int sign = 1;
    if (*p == '-') {
        sign = -1;
        p++;
    } else if (*p == '+') {
        p++;
    }
    int h = 0, m = 0, s = 0;
    p = parse_number(p, &h);
    if (!p)
        return NULL;
    if (*p == ':') {
        p = parse_number(p + 1, &m);
        if (!p)
            return NULL;
        if (*p == ':') {
            p = parse_number(p + 1, &s);
            if (!p)
                return NULL;
        }
    }
    *out = sign * (h * 3600L + m * 60L + s);
    return p;
}

static const char *parse_rule(const char *p, tz_rule_t *rule)
{
    rule->time = 2 * 3600L;
    if (*p == 'M') {
        rule->kind = 'M';
        p = parse_number(p + 1, &rule->month);
        if (!p || *p != '.')
            return NULL;
        p = parse_number(p + 1, &rule->week);
        if (!p || *p != '.')
            return NULL;
        p = parse_number(p + 1, &rule->day);
        if (!p || rule->month < 1 || rule->month > 12 || rule->week < 1 || rule->week > 5 || rule->day > 6)
            return NULL;
    } else if (*p == 'J') {
        rule->kind = 'J';
        p = parse_number(p + 1, &rule->day);
        if (!p || rule->day < 1 || rule->day > 365)
            return NULL;
    } else {
        rule->kind = 'N';
        p = parse_number(p, &rule->day);
        if (!p || rule->day > 365)
            return NULL;
    }
    if (*p == '/') {
        p = parse_time(p + 1, &rule->time);
        if (!p)
            return NULL;
    }
    return p;
}

static bool parse_zone(const char *p)
{
    memset(&zone, 0, sizeof(zone));
    long offset = 0;
    p = parse_name(p, zone.std_name, sizeof(zone.std_name));
    if (!p)
        return false;
    p = parse_time(p, &offset);
    if (!p)
        return false;
    /* POSIX: the offset is what to add to local time to get UTC. */
    zone.std_offset = -offset;
    if (*p == '\0')
        return true;

    p = parse_name(p, zone.dst_name, sizeof(zone.dst_name));
    if (!p)
        return false;
    zone.has_dst = true;
    zone.dst_offset = zone.std_offset + 3600L;
    if (*p != '\0' && *p != ',') {
        p = parse_time(p, &offset);
        if (!p)
            return false;
        zone.dst_offset = -offset;
    }
    if (*p == '\0') {
        parse_rule("M3.2.0", &zone.start);
        parse_rule("M11.1.0", &zone.end);
        return true;
    }
    if (*p != ',')
        return false;
    p = parse_rule(p + 1, &zone.start);
    if (!p || *p != ',')
        return false;
    p = parse_rule(p + 1, &zone.end);
    return p != NULL && *p == '\0';
}

/* The POSIX TZ string of the device (config.json "core.timezone"). */
void fbe_tz_set(const char *posix)
{
    if (posix == NULL || !parse_zone(posix)) {
        memset(&zone, 0, sizeof(zone));
        strcpy(zone.std_name, "UTC");
    }
}

static bool leap(const int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

/* Days since 1970-01-01 of year-01-01. */
static int64_t days_to_year(const int year)
{
    const int64_t y = year - 1;
    return 365 * (int64_t)(year - 1970) + (y / 4 - 1969 / 4) - (y / 100 - 1969 / 100) + (y / 400 - 1969 / 400);
}

/* Seconds since the epoch, in local wall time, at which `rule` falls in `year`. */
static int64_t rule_local_seconds(const tz_rule_t *rule, const int year)
{
    static const int month_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int64_t day = days_to_year(year);
    if (rule->kind == 'J') {
        day += rule->day - 1;
        if (leap(year) && rule->day >= 60)
            day += 1;
    } else if (rule->kind == 'N') {
        day += rule->day;
    } else {
        for (int m = 1; m < rule->month; m++)
            day += month_days[m - 1] + (m == 2 && leap(year) ? 1 : 0);
        const int in_month = month_days[rule->month - 1] + (rule->month == 2 && leap(year) ? 1 : 0);
        /* 1970-01-01 was a Thursday (4). */
        const int first_weekday = (int)(((day % 7) + 7 + 4) % 7);
        int d = (rule->day - first_weekday + 7) % 7 + (rule->week - 1) * 7;
        while (d >= in_month)
            d -= 7;
        day += d;
    }
    return day * 86400 + rule->time;
}

static bool is_dst(const int64_t utc)
{
    if (!zone.has_dst)
        return false;
    time_t standard = (time_t)(utc + zone.std_offset);
    struct tm tm;
    gmtime_r(&standard, &tm);
    const int year = tm.tm_year + 1900;
    /* The change to DST is given in standard time, the change back in DST. */
    const int64_t start = rule_local_seconds(&zone.start, year) - zone.std_offset;
    const int64_t end = rule_local_seconds(&zone.end, year) - zone.dst_offset;
    if (start < end)
        return utc >= start && utc < end;
    return utc >= start || utc < end; /* southern hemisphere */
}

struct tm *localtime_r(const time_t *restrict t, struct tm *restrict out)
{
    const bool dst = is_dst((int64_t)*t);
    const long offset = dst ? zone.dst_offset : zone.std_offset;
    const time_t local = *t + offset;
    if (!gmtime_r(&local, out))
        return NULL;
    out->tm_isdst = dst ? 1 : 0;
    out->tm_gmtoff = offset;
    out->tm_zone = dst ? zone.dst_name : zone.std_name;
    return out;
}

struct tm *localtime(const time_t *t)
{
    static struct tm tm;
    return localtime_r(t, &tm);
}

/* For the host: seconds east of UTC that local time is at `utc_seconds`. */
__attribute__((export_name("fbe_utc_offset"), used)) int fbe_utc_offset(const double utc_seconds)
{
    return (int)(is_dst((int64_t)utc_seconds) ? zone.dst_offset : zone.std_offset);
}
