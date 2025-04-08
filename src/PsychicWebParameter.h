#ifndef PsychicWebParameter_h
#define PsychicWebParameter_h

/*
 * PARAMETER :: Chainable object to hold GET/POST and FILE parameters
 * */

class PsychicWebParameter {
  private:
    const char* _name;
    const char* _value;
    size_t _size;
    bool _isForm;
    bool _isFile;

  public:
    PsychicWebParameter(const char* name, const char* value, bool form=false, bool file=false, size_t size=0): _name(name), _value(value), _size(size), _isForm(form), _isFile(file){}
    const char* name() const { return _name; }
    const char* value() const { return _value; }
    size_t size() const { return _size; }
    bool isPost() const { return _isForm; }
    bool isFile() const { return _isFile; }
};

#endif //PsychicWebParameter_h