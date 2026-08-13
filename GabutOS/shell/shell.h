#ifndef SHELL_H
#define SHELL_H

#define SHELL_MAX_ARGS 8
#define SHELL_LINE_LEN 80

struct shell_command {
    int argc;
    char* argv[SHELL_MAX_ARGS];
};

void shell_tokenize(char* line, struct shell_command* out);
void shell_run(void);

#endif
