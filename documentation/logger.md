Loggers
=======

**Note: this feature is not yet completely implemented.**

1. Whenever you want to log a message, you should specify a tag and severity, for example "pci" and logger_error. The tag describes the module that is responsible for the output.
   
   Filtering of messages is done by configuring the logger on load time (`logger::instance()->parse_configuration`), the configuration specifies the threshold severity level per each tag.

   Messages pass the filter only if their severity level is above or equal what's configured.
   
   Severities: debug, info, warn, error.
   *Please see pci.cc and pci.hh for usage example.*

2. The following are notes about the logger:

  1. If a tag is not configured, the default severity is used: logger_error.
  2. suppressing messages - configure a tag with logger_none.

3. What's left to do?

  1. Wire sprintf
  
  2. Configure line format (show time, show thread id, etc...)
  
  3. Configure using a file (change parse_configuration())

4. Examples
  1. Log a message with the "pci" tag and debug severity:
  
    ```c++  
    logger::instance()->log("pci", logger::logger_debug, fmt("Message"));
    ```
