#ifndef CONSOLE_H
#define CONSOLE_H

/* The demos' console. On the host: stdin and stdout. On bare metal: HTIF (printstr, output only),
   or with CONSOLE_UART the chip's UART, which FireSim connects to the driver's terminal both ways.
   An HTIF write waits for the host, about 20M cycles per call on FireSim; the UART doesn't. */

void con_put(const char *s);

/* The next typed character, or -1 at the end of input. Only the host and the UART have input. */
int con_getc(void);

/* Whether typed characters must be echoed by the program (the UART: the terminal is raw). */
int con_echoes(void);

/* Let pending output leave before the program exits. */
void con_drain(void);

/* A story's text, token by token, wrapped at word boundaries before column 96. */
void con_new_story(void);
void con_put_token(int token);

#endif
