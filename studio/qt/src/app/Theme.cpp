#include "Theme.h"

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>

namespace Theme {

void apply(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette p;
    // Blender's default theme
    const QColor window(0x30, 0x30, 0x30), base(0x2b, 0x2b, 0x2b), text(0xe5, 0xe5, 0xe5), mid(0x54, 0x54, 0x54);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, QColor(0x30, 0x30, 0x30));
    p.setColor(QPalette::ToolTipBase, QColor(0x18, 0x18, 0x18));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, mid);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, QColor(0x47, 0x72, 0xb3));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(0x6e, 0xb2, 0xff));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x74, 0x74, 0x74));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x74, 0x74, 0x74));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x74, 0x74, 0x74));
    app.setPalette(p);
    QFont f(QStringLiteral("Segoe UI"));
    f.setPixelSize(12);
    app.setFont(f);
    // areas sit on a near-black background with thin gaps, like Blender's
    app.setStyleSheet(QStringLiteral(R"(
QMainWindow { background: #161616; }
QMainWindow::separator { background: #161616; width: 3px; height: 3px; }
QMainWindow::separator:hover { background: #4772b3; }
QMenuBar { background: #1d1d1d; color: #e5e5e5; border: none; padding: 2px 6px; }
QMenuBar::item { padding: 4px 9px; background: transparent; border-radius: 4px; }
QMenuBar::item:selected { background: #4772b3; }
QMenu { background: #181818; color: #e5e5e5; border: 1px solid #0c0c0c; padding: 4px 0; border-radius: 4px; }
QMenu::item { padding: 5px 28px 5px 24px; border-radius: 3px; margin: 0 4px; }
QMenu::item:selected { background: #4772b3; color: white; }
QMenu::item:disabled { color: #6e6e6e; }
QMenu::separator { height: 1px; background: #303030; margin: 4px 8px; }
QMenu::indicator { width: 12px; height: 12px; left: 6px; }
QDockWidget { color: #e5e5e5; }
QDockWidget::title { background: #303030; padding: 5px 8px; border-bottom: 1px solid #161616; text-align: left; }
QTabBar { background: #1d1d1d; }
QTabBar::tab { background: #282828; color: #a5a5a5; padding: 5px 14px; border: none; margin-right: 2px;
               border-top-left-radius: 4px; border-top-right-radius: 4px; min-width: 60px; }
QTabBar::tab:selected { background: #303030; color: #ffffff; }
QTabBar::tab:hover:!selected { background: #353535; color: #e5e5e5; }
QTabBar#workspaces { background: transparent; }
QTabBar#workspaces::tab { background: transparent; color: #a5a5a5; padding: 4px 12px; margin: 2px 1px; border-radius: 4px; min-width: 0; }
QTabBar#workspaces::tab:selected { background: #303030; color: #ffffff; }
QTabBar#workspaces::tab:hover:!selected { background: #282828; color: #e5e5e5; }
QStatusBar { background: #1d1d1d; color: #a5a5a5; border: none; }
QStatusBar QLabel { color: #a5a5a5; padding: 0 8px; }
QToolTip { background: #181818; color: #e5e5e5; border: 1px solid #3d3d3d; padding: 4px 6px; }
QScrollBar:vertical { background: #2b2b2b; width: 10px; }
QScrollBar::handle:vertical { background: #545454; border-radius: 4px; min-height: 24px; }
QMessageBox, QDialog { background: #303030; }
QPushButton { background: #545454; color: #e5e5e5; border: 1px solid #3d3d3d; border-radius: 4px; padding: 5px 14px; }
QPushButton:hover { background: #656565; }
QPushButton:default { background: #4772b3; }
QLineEdit, QComboBox, QSpinBox { background: #1d1d1d; color: #e5e5e5; border: 1px solid #3d3d3d; border-radius: 4px; padding: 3px 6px; }
QListWidget { background: #2b2b2b; color: #e5e5e5; border: 1px solid #161616; }
)"));
}

} // namespace Theme
