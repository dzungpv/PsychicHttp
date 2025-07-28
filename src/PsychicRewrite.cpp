#include "PsychicRewrite.h"
#include "PsychicRequest.h"
#include <string>

namespace PsychicHttp {

    PsychicRewrite::PsychicRewrite(const char* from, const char* to):
		_fromPath(from),
		_toUri(to),
		_toPath(std::string()),
		_toParams(std::string()),
		_filter(nullptr)
	{
      size_t index = _toUri.find('?');
      if (index != std::string::npos) {
        _toParams = _toUri.substr(index + 1);
        _toPath = _toUri.substr(0, index);
      }
	  else
	  	_toPath = _toUri;
    }
    PsychicRewrite::~PsychicRewrite()
	{
		
	}

    PsychicRewrite* PsychicRewrite::setFilter(PsychicRequestFilterFunction fn)
	{
		_filter = fn; return this;
	}
    
	bool PsychicRewrite::filter(PsychicRequest *request) const
	{
		return _filter == nullptr || _filter(request);
	}
    
	const std::string& PsychicRewrite::from(void) const
	{
		return _fromPath;
	}
    const std::string& PsychicRewrite::toUrl(void) const
	{
		return _toUri;
	}
    
	const std::string& PsychicRewrite::params(void) const
	{
		return _toParams;
	}

    bool PsychicRewrite::match(PsychicRequest *request)
	{
		if (!filter(request))
			return false;

		return _fromPath == request->path();
	}

} // namespace PsychicHttp