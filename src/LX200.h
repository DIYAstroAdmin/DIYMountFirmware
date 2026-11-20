// Meade LX200 command set over a serial stream.
#pragma once
#include <Arduino.h>

class LX200 {
 public:
  explicit LX200(Stream &stream) : _s(stream) {}
  void poll();

 private:
  void execute(const char *cmd);   // runs a command and logs "<command> => <response>"
  void dispatch(const char *cmd);
  void cmdGet(const char *cmd);
  void cmdSet(const char *cmd);
  void cmdMove(const char *cmd);
  double moveRate() const;
  void beginReply();
  void send(const char *text);
  void send(char c);
  void sendf(const char *fmt, ...);
  double localNow() const;

  Stream &_s;
  char _buf[48];
  size_t _len = 0;
  char _reply[80] = {0};           // copy of the reply to the current command, for the log
  size_t _replyLen = 0;
  bool _inCmd = false;
  char _rate = 'M';              // G, C, M or S
  double _utcOffset = 0;         // hours to add to local time to get UTC (LX200 convention)
};
