#ifndef RUN_ID_PARSER_H_
#define RUN_ID_PARSER_H_
#include <array>
#include <regex>
#include <utility>

class RunIdParser {
public:
  RunIdParser(const std::string &name);
  size_t runId() const { return rundId_; }
  size_t fileId() const { return fileId_; }
  // AsAd id
  // returns -1 if no information
  int AsadId() const { return AsAdId_; };
  // CoBoid
  // returns -1 if no information
  int CoBoId() const { return CoBoId_; };

private:
  size_t rundId_;
  size_t fileId_;
  int AsAdId_ = -1;
  int CoBoId_ = -1;

  class Positions {
  public:
    Positions(size_t year, size_t month, size_t day, size_t hour,
              size_t minutes, size_t seconds, size_t fileId, size_t cobo, size_t asad);
    const size_t year;
    const size_t month;
    const size_t day;
    const size_t hour;
    const size_t minutes;
    const size_t seconds;
    const size_t fileId;
    const size_t cobo;
    const size_t asad;
    size_t max() const { return max_; }

  private:
    size_t max_;
  };
  static const std::array<std::pair<std::regex, Positions>, 2> regexes;
  void matchResults(const std::smatch &match, const Positions &positions);
};

#endif // RUN_ID_PARSER_H_