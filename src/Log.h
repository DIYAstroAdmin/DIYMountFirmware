// In-memory log shown on the web page. The USB serial port is reserved for LX200,
// so log messages go to a ring buffer that the page reads through /api/log.
//
//   LOGI("Goto RA %.4f h, Dec %.3f deg", ra, dec);
//
// The newest CAPACITY lines are kept; older ones are overwritten. Safe to call from
// any task, but not from an interrupt handler.
//
// Messages below the current level (setLevel) are not written at all. The LOGx macros
// check the level first, so a suppressed message's arguments are not even evaluated.
//
// Protocol traffic can also be switched off by source: LOG_LX200 and LOG_ALPACA write at
// Info, but only while that source is enabled (setSourceEnabled).
#pragma once
#include <Arduino.h>

namespace Log {

enum class Level : uint8_t { Debug, Info, Warn, Error };

static const size_t CAPACITY = 80;     // lines kept
static const size_t LINE_LENGTH = 120;    // longest message including the terminator; longer ones are cut

struct Entry {
  uint32_t seq;          // 1 for the first line since boot, then counting up
  uint32_t ms;           // millis() when the line was written
  Level level;
  char text[LINE_LENGTH];
};

void write(Level level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

void setLevel(Level level);   // lowest level that is written; Info after boot
Level level();
inline bool enabled(Level l) { return l >= level(); }

enum class Source : uint8_t { Lx200, Alpaca };
void setSourceEnabled(Source s, bool on);   // both disabled until enabled (main applies the saved settings)
bool sourceEnabled(Source s);

uint32_t newest();                      // seq of the latest line, 0 if nothing has been logged
bool read(uint32_t seq, Entry &out);    // false if that line does not exist or has been overwritten

}  // namespace Log

#define LOG_AT(lvl, ...)                                \
  do {                                                  \
    if (Log::enabled(lvl)) Log::write(lvl, __VA_ARGS__); \
  } while (0)

#define LOGD(...) LOG_AT(Log::Level::Debug, __VA_ARGS__)
#define LOGI(...) LOG_AT(Log::Level::Info, __VA_ARGS__)
#define LOGW(...) LOG_AT(Log::Level::Warn, __VA_ARGS__)
#define LOGE(...) LOG_AT(Log::Level::Error, __VA_ARGS__)

// True if a message from this source would be written; use it to skip building the text.
#define LOG_SOURCE_ON(src) (Log::sourceEnabled(src) && Log::enabled(Log::Level::Info))

#define LOG_SOURCE(src, ...)                                           \
  do {                                                                 \
    if (LOG_SOURCE_ON(src)) Log::write(Log::Level::Info, __VA_ARGS__); \
  } while (0)

#define LOG_LX200(...) LOG_SOURCE(Log::Source::Lx200, __VA_ARGS__)
#define LOG_ALPACA(...) LOG_SOURCE(Log::Source::Alpaca, __VA_ARGS__)
