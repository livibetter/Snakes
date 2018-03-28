#include <config.h>

#include <langinfo.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <curses.h>
#include <signal.h>
#include <time.h>
#include <locale.h>
#include <getopt.h>
#include <errno.h>

#include "pipe.h"
#include "render.h"

// Use noreturn on die() if possible
#ifdef HAVE_STDNORETURN_H
#   include <stdnoreturn.h>
#else
#   define noreturn /* noreturn */
#endif

void interrupt_signal(int param);
void parse_options(int argc, char **argv);
float parse_float_opt(const char *optname);
int parse_int_opt(const char *optname);
noreturn void die(void);
void usage_msg(int exitval);
void render(unsigned int width, unsigned int height, void *data);
int init_chars(void);

//If set >= zero, this initial state is used.
int initial_state = -1;

const char *usage =
    "Usage: cpipes [OPTIONS]\n"
    "Options:\n"
    "    -p, --pipes=N   Number of pipes.                  (Default: 20    )\n"
    "    -f, --fps=F     Frames per second.                (Default: 60.0  )\n"
    "    -a, --ascii     ASCII mode.                       (Default: no    )\n"
    "    -l, --length=N  Minimum length of pipe.           (Default: 2     )\n"
    "    -r, --prob=N    Probability of changing direction.(Default: 0.1   )\n"
    "    -i, --init=N    Initial state (0,1,2,3 => R,D,L,U)(Default: random)\n"
    "    -h, --help      This help message.\n";

static struct option opts[] = {
    {"pipes",   required_argument, 0,   'p'},
    {"fps",     required_argument, 0,   'f'},
    {"ascii",   no_argument,       0,   'a'},
    {"length",  required_argument, 0,   'l'},
    {"prob",    required_argument, 0,   'r'},
    {"help",    no_argument,       0,   'h'},
    {0,         0,                 0,    0 }
};


//All pipes
struct pipe *pipes;

//Signal flag for interrupts
volatile sig_atomic_t interrupted = 0;

//Width and height of terminal (in chars and lines)
unsigned int screen_width, screen_height;

unsigned int num_pipes = 20;
float fps = 60;
float prob = 0.1;
unsigned int min_len = 2;

char *trans[16];
char *pipe_chars[2];

// If/when we supply user-configurable characters, this will need to be changed
// to point to the user's terminal encoding.
const char *source_charset = "UTF-8";

const char *ASCII_CHARS = "-|++++";
const char *UNICODE_CHARS = "━┃┓┛┗┏";
const char *selected_chars = NULL;

char pipe_char_buf[CHAR_BUF_SZ];

// Convenience macro for bailing in init_chars
#define X(a) do { \
        if( ((a)) == -1 ) { \
            fprintf(stderr, "Error initialising pipe characters.\n"); \
            exit(1); \
        } \
    } while(0);

int init_chars(void) {
    if(!selected_chars)
        selected_chars = UNICODE_CHARS;
    if(strlen(selected_chars) >= CHAR_BUF_SZ)
        return -1;

    char *term_charset = nl_langinfo(CODESET);
    char inbuf[CHAR_BUF_SZ];
    char utf8buf[CHAR_BUF_SZ];
    strncpy(inbuf, selected_chars, CHAR_BUF_SZ);

    X(locale_to_utf8(inbuf, utf8buf, source_charset, CHAR_BUF_SZ));
    X(utf8_to_locale(utf8buf, pipe_char_buf, CHAR_BUF_SZ, term_charset));
    X(assign_matrices(pipe_char_buf, trans, pipe_chars));
    X(multicolumn_adjust(pipe_chars));
    return 0;
}

int main(int argc, char **argv){
    srand(time(NULL));
    setlocale(LC_ALL, "");
    //Set a flag upon interrupt to allow proper cleaning
    signal(SIGINT, interrupt_signal);

    parse_options(argc, argv);
    init_chars();

    //Initialise ncurses, hide the cursor and get width/height.
    initscr();
    curs_set(0);
    cbreak();
    nodelay(stdscr, true);
    getmaxyx(stdscr, screen_height, screen_width);
    init_colours();

    //Init pipes. Use predetermined initial state, if any.
    pipes = malloc(num_pipes * sizeof(struct pipe));
    for(unsigned int i=0; i<num_pipes;i++)
        init_pipe(&pipes[i], COLORS, initial_state,
            screen_width, screen_height);

    animate(fps, render, &screen_width, &screen_height, &interrupted, NULL);

    curs_set(1);
    endwin();
    free(pipes);
    return 0;
}

void render(unsigned int width, unsigned int height, void *data){
    for(size_t i=0; i<num_pipes && !interrupted; i++){
        move_pipe(&pipes[i]);
        if(wrap_pipe(&pipes[i], width, height))
            random_pipe_colour(&pipes[i], COLORS);

        char old_state = pipes[i].state;
        if(should_flip_state(&pipes[i], min_len, prob)){
            old_state = flip_pipe_state(&pipes[i]);
        }
        render_pipe(&pipes[i], trans, pipe_chars, old_state, pipes[i].state);
    }
    refresh();
}

void interrupt_signal(int param){
    interrupted = 1;
}

void usage_msg(int exitval){
    fprintf(exitval == 0 ? stdout : stderr, "%s",   usage);
}

noreturn void die(void){
    usage_msg(1);
    exit(1);
}

int parse_int_opt(const char *optname){
    errno = 0;
    int i_res = strtol(optarg, NULL, 10);
    if(errno || i_res < 1){
        fprintf(stderr, "%s must be a positive integer.\n", optname);
        die();
    }
    return i_res;
}

float parse_float_opt(const char *optname){
    errno = 0;
    float f_res = strtof(optarg, NULL);
    if(errno || f_res < 0){
        fprintf(stderr, "%s must be a real number (>= 0).\n", optname);
        die();
    }
    return f_res;
}

void parse_options(int argc, char **argv){
    int c;
    while((c = getopt_long(argc, argv, "p:f:al:r:i:h", opts, NULL)) != -1){
        switch(c){
            errno = 0;
            case 'p':
                num_pipes = parse_int_opt("--pipes");
                break;
            case 'f':
                fps = parse_float_opt("--fps");
                break;
            case 'a':
                selected_chars = ASCII_CHARS;
                break;
            case 'l':
                min_len = parse_int_opt("--length");
                break;
            case 'r':
                prob = parse_float_opt("--prob");
                if(prob > 1){
                    fprintf(stderr, "%s\n",
                            "--prob must be less than 1");
                    usage_msg(1);
                    exit(1);
                }
                break;
            case 'i':
                initial_state = strtol(optarg, NULL, 10);
                if(initial_state < 0 || initial_state > 3){
                    fprintf(stderr, "%s\n",
                            "--init must be between 0 and 3 (inclusive).");
                    usage_msg(1);
                    exit(1);
                }
                break;
            case 'h':
                usage_msg(0);
                exit(0);
            case '?':
            default:
                usage_msg(1);
                exit(1);
        }
    }
}