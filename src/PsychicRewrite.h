#ifndef PsychicRewrite_h
#define PsychicRewrite_h

#include "PsychicCore.h"
#include <string>

namespace PsychicHttp {

/*
 * REWRITE :: One instance can be handle any Request (done by the Server)
 * */

class PsychicRewrite {
  protected:
    std::string _fromPath;
    std::string _toUri;
    std::string _toPath;
    std::string _toParams;
    PsychicRequestFilterFunction _filter;

  public:
    PsychicRewrite(const char* from, const char* to);
    virtual ~PsychicRewrite();

    PsychicRewrite* setFilter(PsychicRequestFilterFunction fn);
    bool filter(PsychicRequest *request) const;
    const std::string& from(void) const;
    const std::string& toUrl(void) const;
    const std::string& params(void) const;
    virtual bool match(PsychicRequest *request);
};

} // namespace PsychicHttp

#endif