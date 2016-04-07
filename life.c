/*
Copyright (c) 2009 Mats Rauhala <mats.rauhala@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

/* XXX: Toggle the cells */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curses.h>
#include <locale.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>


#include "macros.h"

#define ALIVE_CELL_CHAR ACS_CKBOARD
#define DEAD_CELL_CHAR ' '

/* Array to hold the cells */
static int *cells = NULL;
/* Array to hold the cells of the next round */
static int *buffer = NULL;

/* Size of the 'game area' */
static int lifecols = 0;
static int lifelines = 0;
static long cellsAlive = 0;

/* Default tick size. How many iterations are made in a tick */
static int ticksize = 1;

/* Information window */
static WINDOW *info = NULL;
/* Game area */
static WINDOW *life = NULL;

/* Holds the sleep time */
static struct timespec sleeptime = {0};

/* Prints the game area, but is not used since the curses interface. Obsolete
 * */
void print();

/* Prints status information about the game. Window size, game size, tick size
 * etc. Populates the info-window */
void status();

/* Initialize the game */
void init();

/* Initialize curses */
void initcurses();

/* Clean up the mess */
void finish();

/* Go forward a tick (ticksize iterations) */
void tick();
void spawn_glider(int y,int x);
/* Activate a cell and the visual representation of it */
void activateCell(int y, int x);
/* Deactivate a cell and the visual representation of it */
void deactivateCell(int y, int x);

/* A helper function for creating new windows */
WINDOW *createwin(int height, int width, int begy, int begx);

/* Keyboard utilities */
bool kbd(int ch,int *reset);

void randomize();

/* Calculate the live neighbours for a cell */
int neighbours(int y, int x);
int kbhit(void)
{
    struct timeval        timeout;
    fd_set                readfds;
    int                how;
    /* look only at stdin (fd = 0) */
    FD_ZERO(&readfds);
    FD_SET(0, &readfds);
    
    /* poll: return immediately */
    timeout.tv_sec = 0L;
    timeout.tv_usec = 0L;
    
    how = select(1, &readfds, (fd_set *)NULL, (fd_set *)NULL, &timeout);
    /* Change "&timeout" above to "(struct timeval *)0"       ^^^^^^^^
     * if you want to wait until a key is hit
     */
    
    if ((how > 0) && FD_ISSET(0, &readfds))
        return 1;
    else
        return 0;
}
int gol_main()
{
    int ch=0; /* Holds the keyboard char */
    init();
    randomize();
    int reset=0;
    do
    { /* Keyboard loop */
        ch=0;
        while(kbhit()){
            ch = getch();
        }
        if(ch==0&&reset==2){tick();}
    }
    while(kbd(ch,&reset)); /* When the kbd returns false, we stop */
    
    finish();
    return reset;
}
int main()
{
    int res=0;
    do
    { /* Keyboard loop */
        res=gol_main();
    }
    while(res==1);
    return 0;
}
void randomize()
{
    cellsAlive=0;
    int x,y;
    for(x = 0; x < lifecols; x++)
    {
        for(y = 0; y < lifelines; y++)
        {
            if(rand()%2==0)
            {
                deactivateCell(y,x);
            }
            else
            {
                activateCell(y,x);
            }
        }
    }
}
void clearCells()
{
    int x,y;
    for(x = 0; x < lifecols; x++)
    {
        for(y = 0; y < lifelines; y++)
        {
            deactivateCell(y,x);
        }
    }
    cellsAlive=0;
}
bool kbd(int ch,int *reset)
{
    int x,y; /* Holds the coordinates */
    getyx(life, y, x); /* Get the coordinates from curses life window */
    switch(ch)
    { /* hjkl + q + space + enter */
        case 'q':
            return false;
            break;
        case 'r':
            randomize();
            break;
        case 'c':
            clearCells();
            break;
        case KEY_RIGHT: /* Go right */
            x=(x+1)%CMAX;
            break;
        case KEY_LEFT: /* Go left */
            x=(x-1)%CMAX;
            break;
        case KEY_DOWN: /* Go down */
            y=(y+1)%LMAX;
            break;
        case KEY_UP: /* Go up */
            y=(y-1)%LMAX;
            break;
        case ' ': /* Activate a cell */
        case 'a':
            activateCell(y, x);
            break;
        case KEY_BACKSPACE:
        case 'd': /* Deactivate a cell */
            deactivateCell(y, x);
            break;
        case 10: /* Enter.
                  Start the tick */
            tick();
            break;
        case 'g':
            spawn_glider(y, x);
            break;
        case '+': /* Increase tick size */
        case '=': /* Increase tick size */
            ticksize++;
            status();
            break;
        case '-': /* Reduce tick size */
        case '_': /* Reduce tick size */
            ticksize--;
            status();
            break;
        case '0':
        case ')':
            (*reset)=1;
            return false;
            break;
        case '9':
        case '(':
            (*reset)=2;
            break;
        case '8':
        case '*':
            (*reset)=0;
            break;
    }
    wmove(life, y, x); /* Move the coordinates to the new location and refresh
                          the game area */
    wrefresh(life);
    if(ch!=0){status();}
    return true;
}

void tick()
{
    struct timespec *remaining; /* Holds the remaining time. Not used */
    /* t = current tick iteration
     * x = x coordinate
     * y = y coordinate
     * n = neighbours for a coordinate
     */
    int t, x, y, n;
    /* Sync the buffer with the game area
     * buffer <- cells.
     * The buffer and cells are separated so that the calculations from this
     * round won't affect this round. For example let's imagine that coordinate
     * (y,x) (1,1) is active but is deemed dead after visiting it. Then we
     * check the coordinate (y,x) (2,1) which now has only 1 neighbour and also
     * dies. By separating the cells and buffer we keep the cells untouchable
     * during the round, but after we have finished the round, we can sync
     * them. */
    COPYC;
    for(t = 0; t < ticksize; t++)
    { /* Iterations */
        cellsAlive=0;
        for(x = 0; x < lifecols; x++)
        {
            for(y = 0; y < lifelines; y++)
            {
                /* Get the neighbours for the coordinate */
                n = neighbours(y, x);
                /* If a cell has less than two neighbours it dies, no matter
                 * what. If a cell has more than three neighbours it's
                 * overcrowded and dies too. */
                if(n < 2 || n > 3)
                {
                    ABCPR(y,x);
                    mvwaddch(life, y, x, DEAD_CELL_CHAR);
                }
                /* If a cell has 3 neighbours it revives */
                if(n == 3)
                {
                    BCPR(y, x);
                    mvwaddch(life, y, x, ALIVE_CELL_CHAR);
                }
                /* The only possibility left is that the cell has two
                 * neighbours, which means that the cell stays alive if it's
                 * alive, if it's dead it stays dead. */
                if(BALIVE(y, x)){cellsAlive++;}
            }
        }
        /* Sync the cells with the buffer */
        COPYB;
        /* Refresh the screen */
        wrefresh(life);
        /* Sleep for while */
        nanosleep(&sleeptime, remaining);
    }
}

int neighbours(int y, int x)
{
    int n = 0; /* Alive neighbours */

    /* The curses coordinate system puts y=0 to the top, so y-1 means checking
     * for above and y+1 means the bottom */

    /* Check for boundaries and then sum up the alive cells */
    if(y < LMAX)
    {
        n += ALIVE(y+1,x); /* Bottom */
    }
    if(y > 0)
    {
        n += ALIVE(y-1,x); /* Top */
    }
    if(x < CMAX)
    {
        n += ALIVE(y, x+1); /* Right */
        if(y > 0)
        {
            n += ALIVE(y-1, x+1); /* Top right */
        }
        if(y < LMAX)
        {
            n += ALIVE(y+1, x+1); /* Bottom right */
        }
    }
    if(x > 0)
    {
        n += ALIVE(y, x-1); /* Left */
        if(y > 0)
        {
            n += ALIVE(y-1, x-1); /* Top left */
        }
        if(y < LMAX)
        {
            n += ALIVE(y+1, x-1); /* Bottom left */
        }
    }

    return n;
}

void finish()
{
    /* Free the cells and buffer */
    free(cells);
    free(buffer);
    /* Delete the two windows we made */
    delwin(info);
    delwin(life);
    /* And end curses mode. It's bad if this is forgotten. Vey very bad */
    endwin();
}

WINDOW *createwin(int height, int width, int begy, int begx)
{
    /* Hold the window */
    WINDOW *local_win = NULL;

    /* Create the window */
    local_win = newwin(height, width, begy, begx);
    if(local_win == NULL) /* Check that it succeeded */
        exit(EXIT_FAILURE);

    wmove(local_win, 1, 1); /* Looks better if the cursor is not on
                               top of lines */

    wrefresh(local_win); /* And refresh */

    return local_win;
}

void initcurses()
{
    /* Initialize curses */
    initscr();
    /* Don't echo keys */
    noecho();
    raw();
    /* Allow arrows and other weird keys */
    keypad(stdscr, true);
    /* Refresh the standard screen */
    refresh();

    lifecols = COLS-ICOLS-1; /* Leave one column empty */
    lifelines = LINES; /* Full height */

    /* Create the two windows */
    life = createwin(lifelines, lifecols, 0, 0);
    info = createwin(ILINES, ICOLS, 0, lifecols+1);

    /* Make a default box around the info-window */
    box(info, 0, 0);
    /* Move, window, printw; Write to window info and move the cursor to 1,1
     * before writing. */
    mvwprintw(info, 1,1, "INFO");

    /* Move the cursor back to game area. The user don't need to see the cursor
     * jumping to the info-window */
    wmove(life, 1,1);

    /* Refresh both of the windows */
    wrefresh(info);
    wrefresh(life);
}

void init()
{
    /* Set the sleep time */
    sleeptime.tv_nsec = SLEEP;
    /* Initialize the curses. Curses initialises few variables that we need
     * further on */
    initcurses();
    /* Locale */
    setlocale(LC_ALL, "");
    /* Initialize arrays. SIZE is dependent on the life-window */
    cells = malloc(SIZE);
    if(cells == NULL)
        exit(1);
    memset(cells, 0, SIZE); /* Fill it with 0 */

    /* The buffer is identical to cells */
    buffer = malloc(SIZE);
    if(buffer == NULL)
        exit(1);
    memset(buffer, 0, SIZE);

    /* Write the first status information */
    status();
}

void print()
{
    int x, y;
    printf("\n");
    for(x = 0; x < COLS+2; x++)
        printf("-");
    printf("\n");
    for(y = 0; y < LINES; y++)
    {
        printf("|");
        for(x = 0; x < COLS; x++)
            printf("%d", CELL(y, x));
        printf("|\n");
    }
    for(x = 0; x < COLS+2; x++)
        printf("-");
    printf("\n");
}

void status()
{
    int y,x;
    mvwprintw(info, 2, 1, "Total sizes");
    mvwprintw(info, 3, 1, " Columns: %d", COLS);
    mvwprintw(info, 4, 1, " Lines: %d", LINES);
    mvwprintw(info, 5, 1, "Life sizes");
    mvwprintw(info, 6, 1, " Columns: %d", lifecols);
    mvwprintw(info, 7, 1, " Lines: %d", lifelines);
    mvwprintw(info, 8, 1, "Array size: %d", ASIZE);
    mvwprintw(info, 9, 1, "Tick size: %d", ticksize);
    mvwprintw(info, 9, 1, "Cells alive: %d", cellsAlive);
    getyx(life, y, x);
    wmove(life, y, x);
    wrefresh(info);
    wrefresh(life);
}
void spawn_glider(int y,int x)
{
    if(x==0){return;}
    if(y==0){return;}
    if(x+4>CMAX){return;}
    if(y+4>LMAX){return;}
    for(int _x=x-1;_x<x+4;_x++)
    {
        for(int _y=y-1;_y<y+4;_y++)
        {
            deactivateCell(_y, _x);
        }
    }
    activateCell(y, x+1);
    activateCell(y+1, x+2);
    activateCell(y+2, x);
    activateCell(y+2, x+1);
    activateCell(y+2, x+2);
}
void activateCell(int y, int x)
{
    /* Set the cell alive in the array */
    if(! ALIVE(y, x)){cellsAlive++;}
    CPR(y,x);
    /* And show it visually too */
    mvwaddch(life, y, x, ALIVE_CELL_CHAR);
}
void deactivateCell(int y, int x)
{
    if(ALIVE(y, x)){cellsAlive--;}
    /* Set the cell alive in the array */
    ACPR(y,x);
    /* And show it visually too */
    mvwaddch(life, y, x, DEAD_CELL_CHAR);
}
