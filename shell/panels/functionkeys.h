#pragma once

class QWidget;

/*
 * The Keyboard panel's Function Keys dialog (Mac OS 9's, Missing Manual
 * p. 105): an application, document or folder for each of F1 to F12,
 * chosen or dropped onto its row; zacos9-wm opens it when the key is
 * pressed (compositor/src/fkeys.c). OK keeps the changes.
 */
void showFunctionKeys(QWidget *parent);
