#ifndef PROTOJS_DATEPROTOTYPE_H
#define PROTOJS_DATEPROTOTYPE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * Install the Date constructor and Date.prototype with all spec methods
 * onto the global root.  Idempotent — checks for an existing "Date" entry
 * before reinstalling.
 *
 * Built per ECMA-262 §21.4.  The Date instance carries a single internal
 * slot, [[DateValue]], stored as the own attribute "__date_value__" with
 * a double value (milliseconds since the Unix epoch, or NaN).
 *
 * Every Date.prototype method reads __date_value__ from the receiver
 * (throwing TypeError when the receiver is not a Date instance, per
 * §21.4.4.1 thisTimeValue), decomposes the time value into broken-down
 * components via the spec's §21.4.1 abstract operations, and either
 * returns a component (getters), composes a new time value (setters),
 * or formats a string (stringifiers).
 */
void ensureDateConstructor(proto::ProtoContext* ctx,
                           const proto::ProtoObject** globalRoot);

} // namespace protojs

#endif // PROTOJS_DATEPROTOTYPE_H
