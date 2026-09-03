#pragma once
#include <Arduino.h>

// Serial wire protocol, v2.
//
// Every message is one ASCII line terminated by '\n' (a trailing '\r' is
// ignored). Fields are separated by '/'.
//
//   request : $<tag>/<command>[/<arg>...]
//   reply   : $<tag>/ok[/<value>...]        exactly one reply per request
//             $<tag>/err/<message>          message may itself contain '/'
//   async   : $0/log/<text>                 unsolicited; informational only
//
// <tag> is a positive integer chosen by the host and echoed verbatim, so the
// host can always match a reply to its request. Tag 0 is reserved for
// unsolicited output.
//
// Argument conventions: integers in decimal, booleans as 0/1, floats in any
// strtof-compatible form (replies use %.7g).

namespace protocol {

// Sized for the largest request: a full signal upload of 2000 value/timing
// pairs is ~50 kB. Static allocation; the Teensy 4.1 has RAM to spare.
constexpr size_t kMaxLineBytes = 64 * 1024;
constexpr size_t kMaxFields = 2 + 2 * 2000 + 8;

// ---- Receiving -------------------------------------------------------------

// Pump the serial port. Returns true once a complete request line has been
// assembled and parsed; the accessors below are then valid until the next
// call. Malformed lines are answered with an err reply internally.
bool poll();

const char* command();
size_t argCount();

// Argument accessors. On a missing or malformed argument they record a fault
// (see below) and return a dummy value, so handlers can read all arguments
// first and check faultPending() once.
int32_t argInt(size_t i);
float argFloat(size_t i);
bool argBool(size_t i);
const char* argRaw(size_t i);  // no validation; nullptr if missing

// ---- Replying --------------------------------------------------------------

// Values are streamed straight to the port; call beginOk / add* / endReply,
// or the one-shot helpers. The dispatcher uses replied() to guarantee the
// one-reply-per-request invariant.
void beginOk();
void addInt(int32_t v);
void addUint(uint32_t v);
void addFloat(float v);
void addStr(const char* s);
void endReply();

void replyOk();                 // "$tag/ok"
void replyErr(const char* msg); // "$tag/err/msg"
bool replied();
void beginRequestCycle();       // clears replied + fault state before dispatch

// ---- Faults ----------------------------------------------------------------

// Deep code reports problems with fault(); the dispatcher turns a pending
// fault into the err reply for the current request. Only the first fault per
// request is kept. Outside a request cycle, use logf for async errors.
void fault(const char* fmt, ...);
bool faultPending();
const char* faultMessage();
void clearFault();

// ---- Async output ----------------------------------------------------------

// "$0/log/<text>". The only legitimate way to print anything that is not a
// reply; never use Serial.print directly elsewhere.
void logf(const char* fmt, ...);

}  // namespace protocol
