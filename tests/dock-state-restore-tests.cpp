#include <QApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <QWidget>

#include <iostream>

namespace {

QDockWidget *addDock(QMainWindow &window, const char *id, Qt::DockWidgetArea area)
{
	auto *dock = new QDockWidget(&window);
	dock->setObjectName(QString::fromLatin1(id));
	dock->setWidget(new QWidget(dock));
	window.addDockWidget(area, dock);
	return dock;
}

QDockWidget *registerShell(QMainWindow &window, const char *id)
{
	// Mirror obs_frontend_add_dock_by_id during module post-load. Runtime
	// content is intentionally absent while OBS restores its saved DockState.
	auto *dock = addDock(window, id, Qt::RightDockWidgetArea);
	dock->setVisible(false);
	dock->setFloating(true);
	return dock;
}

} // namespace

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);
	int failures = 0;
	const auto check = [&failures](bool passed, const char *message) {
		if (!passed) {
			std::cerr << "FAIL: " << message << '\n';
			++failures;
		}
	};

	QByteArray savedState;
	QRect savedFloatingGeometry;
	{
		QMainWindow beforeRestart;
		beforeRestart.resize(800, 600);
		beforeRestart.setCentralWidget(new QWidget(&beforeRestart));
		addDock(beforeRestart, "scenesDock", Qt::LeftDockWidgetArea);
		addDock(beforeRestart, "dsk_horizontal_layout", Qt::RightDockWidgetArea);
		auto *floating = addDock(beforeRestart, "dsk_vertical_layout", Qt::RightDockWidgetArea);
		floating->setFloating(true);
		floating->setGeometry(180, 140, 240, 410);
		auto *hidden = addDock(beforeRestart, "dsk_multistream", Qt::BottomDockWidgetArea);
		hidden->hide();
		beforeRestart.show();
		floating->show();
		app.processEvents();
		savedFloatingGeometry = floating->geometry();
		savedState = beforeRestart.saveState();
	}

	QMainWindow afterRestart;
	afterRestart.resize(800, 600);
	afterRestart.setCentralWidget(new QWidget(&afterRestart));
	auto *standard = addDock(afterRestart, "scenesDock", Qt::LeftDockWidgetArea);
	auto *horizontal = registerShell(afterRestart, "dsk_horizontal_layout");
	auto *vertical = registerShell(afterRestart, "dsk_vertical_layout");
	auto *hidden = registerShell(afterRestart, "dsk_multistream");
	auto *newDock = registerShell(afterRestart, "dsk_new_dock");
	check(afterRestart.restoreState(savedState), "OBS restores the lightweight plugin shells with its standard docks");
	afterRestart.show();
	app.processEvents();
	check(!horizontal->isFloating() && horizontal->isVisible() &&
	      afterRestart.dockWidgetArea(horizontal) == Qt::RightDockWidgetArea,
	      "horizontal shell restores its visible docked placement");
	check(vertical->isFloating() && vertical->isVisible() && vertical->geometry() == savedFloatingGeometry,
	      "vertical shell restores its visible floating placement and geometry");
	check(!hidden->isVisible() && !hidden->isFloating() &&
	      afterRestart.dockWidgetArea(hidden) == Qt::BottomDockWidgetArea,
	      "hidden plugin dock remains hidden and retains its dock area");
	check(!newDock->isVisible() && newDock->isFloating(),
	      "a new dock without saved state is not forced visible or repositioned");
	check(afterRestart.dockWidgetArea(standard) == Qt::LeftDockWidgetArea && standard->isVisible() &&
	      !standard->isFloating(), "restoration also preserves the standard dock's state");

	// Loading the real contents later must not hide, float, or re-register the shell.
	delete horizontal->widget();
	horizontal->setWidget(new QWidget(horizontal));
	app.processEvents();
	check(horizontal->isVisible() && !horizontal->isFloating(), "loading contents preserves the restored dock");
	return failures == 0 ? 0 : 1;
}
