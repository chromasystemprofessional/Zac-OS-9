#pragma once

#include <QString>

/*
 * The graphics hardware as one line for About This Computer:
 * "GA104 [GeForce RTX 3070] (nvidia)". More than one GPU is joined with
 * commas. root puts another directory in place of / for tests.
 */
QString gpuDescription(const QString &root = QString());
