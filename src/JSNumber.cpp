#include "JSNumber.h"

#include <charconv>
#include <cstdlib>
#include <cstring>
#include <system_error>

namespace protojs {

std::string numberToString(double d) {
    if (std::isnan(d)) return "NaN";
    if (d == 0.0) return "0";                     // +0 and -0 (step 2)
    if (std::isinf(d)) return d < 0 ? "-Infinity" : "Infinity";

    // The shortest round-trip digits, and among those the closest to d
    // (std::to_chars, scientific form "d.ddde+XX"): the s and k of step 5.
    // Plain integer formatting (to_string of the integral value) is not
    // this algorithm beyond 2^53: 2147483647 * 2147483647 is the double
    // 4611686014132420608, whose shortest digits are 46116860141324206.
    char buf[64];
    const std::to_chars_result r =
        std::to_chars(buf, buf + sizeof(buf), d, std::chars_format::scientific);
    if (r.ec != std::errc()) return "NaN";        // never: 64 bytes always fit
    *r.ptr = '\0';

    std::string out;
    const char* p = buf;
    if (*p == '-') { out.push_back('-'); ++p; }
    std::string digits;
    for (; *p && *p != 'e'; ++p)
        if (*p != '.') digits.push_back(*p);
    const int e = (*p == 'e') ? std::atoi(p + 1) : 0;
    const int k = static_cast<int>(digits.size());
    const int n = e + 1;                          // d = 0.digits * 10^n

    if (k <= n && n <= 21) {                      // step 6: integer, padded with zeros
        out += digits;
        out.append(static_cast<size_t>(n - k), '0');
    } else if (0 < n && n <= 21) {                // step 7: point inside the digits
        out.append(digits, 0, static_cast<size_t>(n));
        out.push_back('.');
        out.append(digits, static_cast<size_t>(n), std::string::npos);
    } else if (-6 < n && n <= 0) {                // step 8: 0.000ddd
        out += "0.";
        out.append(static_cast<size_t>(-n), '0');
        out += digits;
    } else {                                      // steps 9-10: exponential form
        out.push_back(digits[0]);
        if (k > 1) {
            out.push_back('.');
            out.append(digits, 1, std::string::npos);
        }
        out.push_back('e');
        out.push_back(n - 1 < 0 ? '-' : '+');
        out += std::to_string(n - 1 < 0 ? 1 - n : n - 1);
    }
    return out;
}

std::string numberToString(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (proto::isSmallInt(v)) return std::to_string(proto::asSmallInt(v));
    double d = 0.0;
    return numberToDouble(ctx, v, d) ? numberToString(d) : std::string("NaN");
}

}  // namespace protojs
