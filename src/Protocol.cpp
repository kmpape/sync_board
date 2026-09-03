#include "Protocol.h"

#include <cstdarg>
#include <cstdlib>
#include <cstring>

namespace protocol {
namespace {

char rxBuf[kMaxLineBytes];
size_t rxLen = 0;
bool rxOverflow = false;

// Set by parse(): fields[0] is the tag, fields[1] the command, fields[2..]
// the arguments, all null-terminated in place inside rxBuf.
const char* fields[kMaxFields];
size_t fieldCount = 0;
uint32_t requestTag = 0;

bool hasReplied = false;

char faultBuf[256];
bool faultSet = false;

void writeStr(const char* s) { Serial.write(s); }

void writeFloat(float v) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.7g", (double)v);
  writeStr(buf);
}

// Splits rxBuf on '/' in place. Returns false on structural problems.
bool parse() {
  if (rxLen == 0 || rxBuf[0] != '$') return false;
  fieldCount = 0;
  char* p = rxBuf + 1;  // skip '$'
  while (true) {
    if (fieldCount >= kMaxFields) return false;
    fields[fieldCount++] = p;
    char* slash = strchr(p, '/');
    if (slash == nullptr) break;
    *slash = '\0';
    p = slash + 1;
  }
  if (fieldCount < 2) return false;
  char* end = nullptr;
  const unsigned long tag = strtoul(fields[0], &end, 10);
  if (end == fields[0] || *end != '\0' || tag == 0) return false;
  requestTag = (uint32_t)tag;
  return true;
}

}  // namespace

bool poll() {
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (rxLen < kMaxLineBytes - 1) {
        rxBuf[rxLen++] = c;
      } else {
        rxOverflow = true;
      }
      continue;
    }
    // Full line received.
    rxBuf[rxLen] = '\0';
    const bool overflowed = rxOverflow;
    const bool ok = !overflowed && parse();
    rxLen = 0;
    rxOverflow = false;
    if (ok) return true;
    if (rxBuf[0] == '\0') continue;  // ignore blank lines
    // We may not know the tag, so answer on the reserved tag.
    logf("ERROR: dropped malformed request%s", overflowed ? " (line too long)" : "");
  }
  return false;
}

const char* command() { return fields[1]; }

size_t argCount() { return fieldCount - 2; }

const char* argRaw(size_t i) {
  return (i < argCount()) ? fields[2 + i] : nullptr;
}

int32_t argInt(size_t i) {
  const char* s = argRaw(i);
  if (s == nullptr) {
    fault("missing argument %u for %s", (unsigned)i, command());
    return 0;
  }
  char* end = nullptr;
  const long v = strtol(s, &end, 10);
  if (end == s || *end != '\0') {
    fault("argument %u of %s: expected integer, got '%s'", (unsigned)i, command(), s);
    return 0;
  }
  return (int32_t)v;
}

float argFloat(size_t i) {
  const char* s = argRaw(i);
  if (s == nullptr) {
    fault("missing argument %u for %s", (unsigned)i, command());
    return 0.0f;
  }
  char* end = nullptr;
  const float v = strtof(s, &end);
  if (end == s || *end != '\0') {
    fault("argument %u of %s: expected float, got '%s'", (unsigned)i, command(), s);
    return 0.0f;
  }
  return v;
}

bool argBool(size_t i) {
  const char* s = argRaw(i);
  if (s == nullptr) {
    fault("missing argument %u for %s", (unsigned)i, command());
    return false;
  }
  if (strcmp(s, "0") == 0) return false;
  if (strcmp(s, "1") == 0) return true;
  fault("argument %u of %s: expected 0 or 1, got '%s'", (unsigned)i, command(), s);
  return false;
}

void beginOk() {
  char buf[24];
  snprintf(buf, sizeof(buf), "$%lu/ok", (unsigned long)requestTag);
  writeStr(buf);
  hasReplied = true;
}

void addInt(int32_t v) {
  char buf[16];
  snprintf(buf, sizeof(buf), "/%ld", (long)v);
  writeStr(buf);
}

void addUint(uint32_t v) {
  char buf[16];
  snprintf(buf, sizeof(buf), "/%lu", (unsigned long)v);
  writeStr(buf);
}

void addFloat(float v) {
  writeStr("/");
  writeFloat(v);
}

void addStr(const char* s) {
  writeStr("/");
  writeStr(s);
}

void endReply() {
  // No Serial.flush(): the USB stack drains its buffers from an ISR, and
  // blocking here would stall the main loop (and any running signals) for
  // the whole transmission of a large reply.
  writeStr("\n");
}

void replyOk() {
  beginOk();
  endReply();
}

void replyErr(const char* msg) {
  char buf[24];
  snprintf(buf, sizeof(buf), "$%lu/err/", (unsigned long)requestTag);
  writeStr(buf);
  writeStr(msg);
  endReply();
  hasReplied = true;
}

bool replied() { return hasReplied; }

void beginRequestCycle() {
  hasReplied = false;
  faultSet = false;
}

void fault(const char* fmt, ...) {
  if (faultSet) return;  // keep the first, most specific fault
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(faultBuf, sizeof(faultBuf), fmt, ap);
  va_end(ap);
  faultSet = true;
}

bool faultPending() { return faultSet; }

const char* faultMessage() { return faultBuf; }

void clearFault() { faultSet = false; }

void logf(const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  writeStr("$0/log/");
  writeStr(buf);
  writeStr("\n");
}

}  // namespace protocol
