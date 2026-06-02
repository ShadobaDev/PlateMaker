/**
 * \file
 * \brief platemaker-gui Qt entry point.
 *
 * Stage 4 will implement the full GUI (MainWindow, ToolPanel subclasses, etc.).
 * For now this is a compile-check stub.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <QApplication>

// TODO: Stage 4 — construct MainWindow, register ToolPanel subclasses, show window.

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Platemaker"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    return app.exec();
}
