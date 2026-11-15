#include "Log.h"
#include <stdarg.h>

namespace Log {

static Entry entries[CAPACITY];
static uint32_t lastSeq = 0;   // line 'seq' lives in entries[(seq - 1) % CAPACITY]
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

void write(Level level, const char *fmt, ...) {
  // Format outside the critical section; only the copy into the buffer is locked.
  char text[LINE_LENGTH];
  va_list args;
  va_start(args, fmt);
  vsnprintf(text, sizeof(text), fmt, args);
  va_end(args);
  uint32_t now = millis();

  portENTER_CRITICAL(&mux);
  Entry &e = entries[lastSeq % CAPACITY];
  e.seq = ++lastSeq;
  e.ms = now;
  e.level = level;
  memcpy(e.text, text, sizeof(text));
  portEXIT_CRITICAL(&mux);
}

uint32_t newest() {
  portENTER_CRITICAL(&mux);
  uint32_t seq = lastSeq;
  portEXIT_CRITICAL(&mux);
  return seq;
}

bool read(uint32_t seq, Entry &out) {
  if (seq == 0) return false;
  portENTER_CRITICAL(&mux);
  const Entry &e = entries[(seq - 1) % CAPACITY];
  bool found = e.seq == seq;
  if (found) out = e;
  portEXIT_CRITICAL(&mux);
  return found;
}

}  // namespace Log
