#ifndef MALLOG_H
#define MALLOG_H

extern "C" {
void add_filter(void);
void log_info(char const * const string, size_t len);
}

#endif
