package android.text.format;

import java.util.Calendar;
import java.util.Date;
import java.util.GregorianCalendar;
import java.util.TimeZone;
import libcore.util.GuestZoneInfo;
import libcore.util.ZoneInfo;

/** Guest implementation of the native methods on the original Time class. */
final class TimeNative {
    private static final long INVALID = Long.MIN_VALUE;
    private static final long DAY = 86400000L;

    private TimeNative() {}

    private static void validate(Time time) {
        if (time.allDay && (time.hour != 0 || time.minute != 0 || time.second != 0)) {
            throw new IllegalArgumentException("allDay is true but sec, min, hour are not 0.");
        }
    }

    private static GregorianCalendar calendar() {
        return new GregorianCalendar(TimeZone.getTimeZone("UTC"));
    }

    private static long wallMillis(Time time) {
        GregorianCalendar calendar = calendar();
        calendar.clear();
        calendar.set(time.year, time.month, time.monthDay, time.hour, time.minute, time.second);
        return calendar.getTimeInMillis();
    }

    private static int dst(TimeZone zone, long millis) {
        return zone.inDaylightTime(new Date(millis)) ? 1 : 0;
    }

    private static boolean inRange(long millis) {
        return millis >= Integer.MIN_VALUE * 1000L && millis <= Integer.MAX_VALUE * 1000L;
    }

    private static long resolve(Time time, TimeZone zone, boolean ignoreDst) {
        long wall = wallMillis(time);
        if (wall < Integer.MIN_VALUE * 1000L - DAY || wall > Integer.MAX_VALUE * 1000L + DAY) {
            return INVALID;
        }
        int wanted = ignoreDst ? -1 : Math.min(time.isDst, 1);

        // As in API 19 mktime_tz, search signed 32-bit epoch seconds by local time.
        // This also picks the same side of ordinary DST folds when no hint was supplied.
        long low = Integer.MIN_VALUE;
        long high = Integer.MAX_VALUE;
        while (low <= high) {
            long seconds = Math.max(low, Math.min(high, low / 2 + high / 2));
            long millis = seconds * 1000;
            long local = millis + zone.getOffset(millis);
            if (local == wall) {
                if (wanted < 0 || dst(zone, millis) == wanted) return millis;
                break;
            }
            if (local < wall) low = seconds == low ? low + 1 : seconds;
            else high = seconds == high ? high - 1 : seconds;
        }
        if (wanted < 0) return INVALID;

        // Search the real TZif offsets to recover the other side of a fold.
        // A stale explicit hint instead carries its offset into the new DST state.
        if (!(zone instanceof ZoneInfo)) return INVALID;
        int[] offsets = GuestZoneInfo.offsets(zone.getID(), wanted == 1);
        long adjusted = INVALID;
        for (int offset : offsets) {
            long candidate = wall - offset;
            if (!inRange(candidate)) continue;
            if (dst(zone, candidate) == wanted) {
                if (zone.getOffset(candidate) == offset) return candidate;
            } else if (adjusted == INVALID) {
                adjusted = candidate;
            }
        }
        return adjusted;
    }

    private static void setFields(Time time, TimeZone zone, long millis) {
        int offset = zone.getOffset(millis);
        GregorianCalendar calendar = calendar();
        calendar.setTimeInMillis(millis + offset);
        time.second = calendar.get(Calendar.SECOND);
        time.minute = calendar.get(Calendar.MINUTE);
        time.hour = calendar.get(Calendar.HOUR_OF_DAY);
        time.monthDay = calendar.get(Calendar.DAY_OF_MONTH);
        time.month = calendar.get(Calendar.MONTH);
        time.year = calendar.get(Calendar.YEAR);
        time.weekDay = calendar.get(Calendar.DAY_OF_WEEK) - 1;
        time.yearDay = calendar.get(Calendar.DAY_OF_YEAR) - 1;
        time.isDst = dst(zone, millis);
        time.gmtoff = offset / 1000;
    }

    private static long apiMillis(long millis) {
        return millis == INVALID || millis == -1000 ? -1 : millis;
    }

    static long normalize(Time time, boolean ignoreDst) {
        validate(time);
        TimeZone zone = TimeZone.getTimeZone(time.timezone);
        long millis = resolve(time, zone, ignoreDst);
        if (millis != INVALID) setFields(time, zone, millis);
        else if (ignoreDst) time.isDst = -1;
        return apiMillis(millis);
    }

    static long toMillis(Time time, boolean ignoreDst) {
        validate(time);
        return apiMillis(resolve(time, TimeZone.getTimeZone(time.timezone), ignoreDst));
    }

    static void set(Time time, long millis) {
        time.allDay = false;
        setFields(time, TimeZone.getTimeZone(time.timezone), (int) (millis / 1000) * 1000L);
    }

    static void setToNow(Time time) {
        set(time, System.currentTimeMillis());
    }

    static void switchTimezone(Time time, String id) {
        validate(time);
        TimeZone target = TimeZone.getTimeZone(id);
        long millis = resolve(time, TimeZone.getTimeZone(time.timezone), false);
        setFields(time, target, millis == INVALID ? -1000 : millis);
        time.timezone = id;
    }

    static int nativeCompare(Time a, Time b) {
        validate(a);
        validate(b);
        if (!a.timezone.equals(b.timezone)) {
            long am = toMillis(a, false);
            long bm = toMillis(b, false);
            return am < bm ? -1 : am > bm ? 1 : 0;
        }
        if (a.year != b.year) return a.year - b.year;
        if (a.month != b.month) return a.month - b.month;
        if (a.monthDay != b.monthDay) return a.monthDay - b.monthDay;
        if (a.hour != b.hour) return a.hour - b.hour;
        if (a.minute != b.minute) return a.minute - b.minute;
        return a.second - b.second;
    }

    private static void resetDerived(Time time) {
        time.weekDay = 0;
        time.yearDay = 0;
        time.isDst = -1;
        time.gmtoff = 0;
    }

    // The API19 TimeFormatException constructor is package-private.
    private static final class ParseFailure extends RuntimeException {
        ParseFailure(String message) { super(message); }
    }

    private static int digits(String text, int start, int count) {
        int result = 0;
        for (int i = start; i < start + count; ++i) {
            if (i >= text.length() || text.charAt(i) < '0' || text.charAt(i) > '9') {
                throw new ParseFailure("Parse error at pos=" + i);
            }
            result = result * 10 + text.charAt(i) - '0';
        }
        return result;
    }

    private static void expect(String text, int at, char value) {
        if (at >= text.length() || text.charAt(at) != value) {
            throw new ParseFailure("Expected " + value + " at pos=" + at);
        }
    }

    static boolean nativeParse(Time time, String text) {
        time.year = digits(text, 0, 4);
        time.month = digits(text, 4, 2) - 1;
        time.monthDay = digits(text, 6, 2);
        time.allDay = text.length() == 8;
        boolean utc = false;
        if (time.allDay) {
            time.hour = time.minute = time.second = 0;
        } else {
            expect(text, 8, 'T');
            time.hour = digits(text, 9, 2);
            time.minute = digits(text, 11, 2);
            time.second = digits(text, 13, 2);
            if (text.length() > 15) {
                expect(text, 15, 'Z');
                utc = true;
            }
        }
        resetDerived(time);
        return utc;
    }

    static boolean nativeParse3339(Time time, String text) {
        time.year = digits(text, 0, 4);
        expect(text, 4, '-');
        time.month = digits(text, 5, 2) - 1;
        expect(text, 7, '-');
        time.monthDay = digits(text, 8, 2);
        time.allDay = text.length() < 19;
        boolean utc = false;
        if (time.allDay) {
            time.hour = time.minute = time.second = 0;
        } else {
            expect(text, 10, 'T');
            int hour = digits(text, 11, 2);
            expect(text, 13, ':');
            int minute = digits(text, 14, 2);
            expect(text, 16, ':');
            time.second = digits(text, 17, 2);
            int at = 19;
            if (at < text.length() && text.charAt(at) == '.') {
                ++at;
                while (at < text.length() && text.charAt(at) >= '0' && text.charAt(at) <= '9') ++at;
            }
            int sign = 0;
            if (at < text.length()) {
                char suffix = text.charAt(at);
                utc = true;
                if (suffix == '+' || suffix == '-') {
                    sign = suffix == '+' ? -1 : 1;
                    hour += sign * digits(text, at + 1, 2);
                    expect(text, at + 3, ':');
                    minute += sign * digits(text, at + 4, 2);
                } else {
                    expect(text, at, 'Z');
                }
            }
            time.hour = hour;
            time.minute = minute;
            if (sign != 0) normalize(time, false);
        }
        resetDerived(time);
        return utc;
    }

    private static String fixedDigits(int number, int width) {
        char[] result = new char[width];
        for (int i = width - 1; i >= 0; --i) {
            int digit = i == 0 ? number : number % 10;
            result[i] = digit >= 0 && digit <= 9 ? (char) ('0' + digit) : ' ';
            number /= 10;
        }
        return new String(result);
    }

    static String format2445(Time time) {
        validate(time);
        String date = fixedDigits(time.year, 4) + fixedDigits(time.month + 1, 2)
                + fixedDigits(time.monthDay, 2);
        if (time.allDay) return date;
        return date + 'T' + fixedDigits(time.hour, 2) + fixedDigits(time.minute, 2)
                + fixedDigits(time.second, 2) + ("UTC".equals(time.timezone) ? "Z" : "");
    }

    static String describe(Time time) {
        validate(time);
        return number(time.year, 4, '0', ' ') + number(time.month + 1, 2, '0', ' ')
                + number(time.monthDay, 2, '0', ' ') + 'T' + number(time.hour, 2, '0', ' ')
                + number(time.minute, 2, '0', ' ') + number(time.second, 2, '0', ' ')
                + time.timezone + "(" + time.weekDay + "," + time.yearDay + "," + time.gmtoff
                + "," + time.isDst + "," + toMillis(time, false) / 1000 + ")";
    }

    private static String number(int value, int width, char padding, char modifier) {
        String text = Integer.toString(value);
        if (modifier == '-') return text;
        if (modifier == '_') padding = ' ';
        if (modifier == '0') padding = '0';
        StringBuilder result = new StringBuilder();
        if (value < 0 && padding == '0') {
            result.append('-');
            text = text.substring(1);
        }
        while (result.length() + text.length() < width) result.append(padding);
        return result.append(text).toString();
    }

    private static String name(String[] names, int index) {
        return index < 0 || index >= names.length ? "?" : names[index];
    }

    private static String letterCase(String text, char modifier) {
        if (modifier != 'P' && modifier != '^' && modifier != '#') return text;
        StringBuilder result = new StringBuilder(text.length());
        for (int i = 0; i < text.length(); ++i) {
            char ch = text.charAt(i);
            if (ch >= 'A' && ch <= 'Z' && modifier != '^') ch += 'a' - 'A';
            else if (ch >= 'a' && ch <= 'z' && modifier != 'P') ch -= 'a' - 'A';
            result.append(ch);
        }
        return result.toString();
    }

    static String format1(Time time, String format,
            String[] shortMonths, String[] longMonths, String[] standaloneMonths,
            String[] shortWeekdays, String[] longWeekdays, String timePattern,
            String datePattern, String dateTimePattern, String am, String pm, String commandPattern) {
        validate(time);
        String[][] names = {shortMonths, longMonths, standaloneMonths, shortWeekdays, longWeekdays};
        String[] patterns = {timePattern, datePattern, dateTimePattern, am, pm, commandPattern};
        String output = format(time, format == null ? "%c" : format, names, patterns);
        return output.getBytes(java.nio.charset.Charset.forName("UTF-8")).length <= 256 ? output : "";
    }

    private static String format(Time time, String pattern, String[][] names, String[] patterns) {
        StringBuilder result = new StringBuilder();
        for (int i = 0; i < pattern.length(); ++i) {
            char ch = pattern.charAt(i);
            if (ch != '%') {
                if (result.length() <= 256) result.append(ch);
                continue;
            }
            char modifier = ' ';
            do {
                if (++i == pattern.length()) {
                    throw new UnsupportedOperationException("Incomplete Time.format conversion");
                }
                ch = pattern.charAt(i);
                if ("_-0^#".indexOf(ch) >= 0) modifier = ch;
                else if (ch != 'E' && ch != 'O') break;
            } while (true);
            String part = convert(time, ch, modifier, names, patterns);
            if (result.length() <= 256) result.append(part);
        }
        return result.toString();
    }

    private static String convert(Time t, char ch, char m, String[][] names, String[] patterns) {
        int hour12 = t.hour % 12 == 0 ? 12 : t.hour % 12;
        int monday = (t.weekDay + 6) % 7;
        switch (ch) {
            case 'A': return letterCase(name(names[4], t.weekDay + 1), m);
            case 'a': return letterCase(name(names[3], t.weekDay + 1), m);
            case 'B': return letterCase(name(names[m == '-' ? 2 : 1], t.month), m);
            case 'b': case 'h': return letterCase(name(names[0], t.month), m);
            case 'p': return letterCase(patterns[t.hour >= 12 ? 4 : 3], m);
            case 'P': return letterCase(patterns[t.hour >= 12 ? 4 : 3], 'P');
            case 'c': return format(t, patterns[2], names, patterns);
            case 'x': return format(t, patterns[1], names, patterns);
            case 'X': return format(t, patterns[0], names, patterns);
            case '+': return format(t, patterns[5], names, patterns);
            case 'D': return format(t, "%m/%d/%y", names, patterns);
            case 'F': return format(t, "%Y-%m-%d", names, patterns);
            case 'R': return format(t, "%H:%M", names, patterns);
            case 'r': return format(t, "%I:%M:%S %p", names, patterns);
            case 'T': return format(t, "%H:%M:%S", names, patterns);
            case 'v': return format(t, "%e-%^b-%Y", names, patterns);
            case 'C': return number(t.year / 100, 2, '0', m);
            case 'Y': return number(t.year, 4, '0', m);
            case 'y': return number(Math.abs(t.year % 100), 2, '0', m);
            case 'm': return number(t.month + 1, 2, '0', m);
            case 'd': return number(t.monthDay, 2, '0', m);
            case 'e': return number(t.monthDay, 2, ' ', m);
            case 'H': return number(t.hour, 2, '0', m);
            case 'I': return number(hour12, 2, '0', m);
            case 'k': return number(t.hour, 2, ' ', m);
            case 'l': return number(hour12, 2, ' ', m);
            case 'M': return number(t.minute, 2, '0', m);
            case 'S': return number(t.second, 2, '0', m);
            case 'j': return number(t.yearDay + 1, 3, '0', m);
            case 'U': return number((t.yearDay + 7 - t.weekDay) / 7, 2, '0', m);
            case 'W': return number((t.yearDay + 7 - monday) / 7, 2, '0', m);
            case 'u': return Integer.toString(monday + 1);
            case 'w': return Integer.toString(t.weekDay);
            case 'G': case 'g': case 'V': {
                int year = t.year;
                int thursday = t.yearDay + 3 - monday;
                GregorianCalendar calendar = calendar();
                int days = calendar.isLeapYear(year) ? 366 : 365;
                if (thursday < 0) thursday += calendar.isLeapYear(--year) ? 366 : 365;
                else if (thursday >= days) {
                    thursday -= days;
                    ++year;
                }
                if (ch == 'G') return number(year, 4, '0', m);
                if (ch == 'g') return number(Math.abs(year % 100), 2, '0', m);
                return number(1 + thursday / 7, 2, '0', m);
            }
            case 's': {
                long millis = resolve(t, TimeZone.getTimeZone(t.timezone), false);
                return Long.toString(millis == INVALID ? -1 : millis / 1000);
            }
            case 'z': {
                if (t.isDst < 0) return "";
                long minutes = Math.abs(t.gmtoff / 60);
                return (t.gmtoff < 0 ? "-" : "+") + number((int) (minutes / 60 * 100 + minutes % 60), 4, '0', m);
            }
            case 'Z':
                if (t.isDst < 0) return "";
                if ("UTC".equals(t.timezone) || "GMT".equals(t.timezone)) return letterCase(t.timezone, m);
                TimeZone zone = TimeZone.getTimeZone(t.timezone);
                long millis = resolve(t, zone, false);
                if (millis == INVALID) return "";
                String zoneName = zone instanceof ZoneInfo
                        ? GuestZoneInfo.abbreviation(zone.getID(), millis) : zone.getID();
                return letterCase(zoneName, m);
            case 'n': return "\n";
            case 't': return "\t";
            case '%': return "%";
            default: throw new UnsupportedOperationException("Unsupported Time.format conversion %" + ch);
        }
    }
}
