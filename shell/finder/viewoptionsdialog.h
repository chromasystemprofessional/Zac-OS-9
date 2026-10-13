#pragma once

class FinderView;

/*
 * View > View Options... (Missing Manual pp. 24-25): the front window's
 * options for the view it is in, or the desktop's icon arrangement; Set
 * to Standard Views copies Preferences' for that view. OK applies them.
 */
void showViewOptions(FinderView *view);

/*
 * Edit > Preferences... (Missing Manual p. 413): General (spring-loaded
 * folders and their delay, grid spacing), Views (the standard views new
 * folders start with) and Labels (their names and colours). Changes
 * apply at once; there is one window.
 */
void showPreferences();
