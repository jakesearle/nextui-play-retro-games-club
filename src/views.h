#ifndef VIEWS_H
#define VIEWS_H

#include <stdbool.h>

/* Loads the club page and shows the current picks until the user quits or
   chooses Play. returning is set when launch.sh reopens the app after a
   game: the page fetched before it is reused, and the game is reselected. */
void MainView(bool returning);

#endif
