#ifndef RUNTIME_H
#define RUNTIME_H

// Runtime API (extern declarations for asm functions)
extern void runtime_init(void);
extern void runtime_display_string(const char *str);
extern void runtime_display_numeric(const void *var, int size, int decimals);
extern void runtime_move_alphanum(void *dest, const char *src, int src_len, int dest_size);
extern void runtime_move_numeric(void *dest, const char *src_digits, int src_len, int dest_size);
extern void runtime_exit(int code);

#endif
