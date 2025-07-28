#ifndef PsychicWebParameter_h
#define PsychicWebParameter_h

#include <string>

namespace PsychicHttp {

/*
 * PARAMETER :: Chainable object to hold GET/POST and FILE parameters
 * */

class PsychicWebParameter
{
  private:
    std::string _name;
    std::string _value;
    size_t _size;
    bool _isForm;
    bool _isFile;

  public:
    PsychicWebParameter(const std::string& name, const std::string& value, bool form = false, bool file = false, size_t size = 0) : _name(name), _value(value), _size(size), _isForm(form), _isFile(file) {}
    const std::string& name() const { return _name; }
    const std::string& value() const { return _value; }
    size_t size() const { return _size; }
    bool isPost() const { return _isForm; }
    bool isFile() const { return _isFile; }
};

} // namespace PsychicHttp

#endif // PsychicWebParameter_h