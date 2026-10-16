#include "ui/canvas-settings-panels.hpp"

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QPixmap>
#include <QScrollArea>

#include <iostream>

namespace {
int failures = 0;
QString renderDirectory;
void check(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}

void testPanels(int fontSize, int previewMinimumWidth)
{
	QWidget editor;
	QFont font = editor.font();
	font.setPointSize(fontSize);
	editor.setFont(font);
	auto *root = new QVBoxLayout(&editor);
	root->setContentsMargins(4, 4, 4, 4);
	root->setSpacing(4);
	auto *preview = new QWidget(&editor);
	preview->setMinimumSize(previewMinimumWidth, 110);
	root->addWidget(preview, 1);
	std::array<QSpinBox *, 4> axes;
	for (int i = 0; i < 4; ++i) {
		axes[i] = new QSpinBox(&editor);
		axes[i]->setRange(i < 2 ? -4000 : 1, 4000);
		axes[i]->setMinimumWidth(74);
	}
	auto *fit = new QComboBox(&editor);
	fit->addItems({"Fit", "Fill", "Stretch"});
	auto *visible = new QCheckBox("Visible", &editor);
	std::array<QPushButton *, 3> transformActions{
		new QPushButton("Center", &editor), new QPushButton("Fit Canvas", &editor),
		new QPushButton("Fill Canvas", &editor)};
	auto *transform = dsk::createCanvasTransformPanel(&editor, axes, fit, visible, transformActions);
	root->addWidget(transform);
	auto *follow = new QCheckBox("Follow OBS scene", &editor);
	auto *status = new QLabel("Scene -> Default", &editor);
	auto *obsScene = new QComboBox(&editor);
	auto *canvasScene = new QComboBox(&editor);
	obsScene->addItem("Scene");
	canvasScene->addItem("Default");
	std::array<QPushButton *, 3> linkActions{
		new QPushButton("Link", &editor), new QPushButton("Unlink", &editor),
		new QPushButton("Refresh", &editor)};
	auto *links = new QListWidget(&editor);
	links->setMaximumHeight(68);
	auto *obsLinks = dsk::createCanvasObsLinksPanel(&editor, follow, status, obsScene, canvasScene, linkActions, links);
	root->addWidget(obsLinks);
	obsLinks->hide();
	editor.ensurePolished();
	root->activate();
	check(editor.minimumSizeHint().width() <= previewMinimumWidth + 8,
	      "opening Transform must not widen the preview dock");
	check(transform->minimumSizeHint().height() <= 80,
	      "Transform must not impose its full form height on the dock");
	check(qobject_cast<QScrollArea *>(transform) != nullptr, "Transform remains reachable through scrolling");
	for (auto *axis : axes)
		check(transform->isAncestorOf(axis), "Transform owns and retains all four axis controls");
	transform->hide();
	obsLinks->setHidden(false);
	for (const QString &name : {QStringLiteral("Scene"), QString(120, QChar('A')), QString(120, QChar(0x914D))}) {
		status->setText(name + QStringLiteral(" -> Default"));
		obsScene->setItemText(0, name);
		canvasScene->setItemText(0, name);
		root->invalidate();
		root->activate();
		check(editor.minimumSizeHint().width() <= previewMinimumWidth + 8,
		      "OBS Link status and combo names must not widen the preview dock");
	}
	check(obsLinks->minimumSizeHint().height() <= 80, "OBS Link must have a bounded minimum height");
	check(qobject_cast<QScrollArea *>(obsLinks) != nullptr, "all OBS Link controls remain reachable through scrolling");
	for (auto *action : linkActions)
		check(obsLinks->isAncestorOf(action), "OBS Link retains every action");
	check(status->textFormat() == Qt::PlainText, "scene names are displayed as plain text");
	for (int i = 0; i < 6; ++i) {
		transform->setHidden(i % 2 == 0);
		obsLinks->setHidden(i % 2 != 0);
		root->invalidate();
		root->activate();
		check(editor.minimumSizeHint().width() <= previewMinimumWidth + 8,
		      "repeated panel toggles preserve a compact minimum width");
	}
	transform->setHidden(false);
	obsLinks->setHidden(false);
	root->invalidate();
	root->activate();
	check(editor.minimumSizeHint().width() <= previewMinimumWidth + 8,
	      "opening both panels together must not widen the dock");
	editor.resize(previewMinimumWidth + 8, 440);
	for (auto *panel : {transform, obsLinks}) {
		transform->setHidden(panel != transform);
		obsLinks->setHidden(panel != obsLinks);
		root->activate();
		const QPixmap rendered = editor.grab();
		check(!rendered.isNull(), "production settings panels render without showing a top-level window");
		auto *scroll = qobject_cast<QScrollArea *>(panel);
		if (scroll) {
			auto *lastAction = panel == transform ? transformActions.back() : linkActions.back();
			scroll->ensureWidgetVisible(lastAction, 0, 0);
			const QRect actionRect(lastAction->mapTo(scroll->viewport(), QPoint()), lastAction->size());
			check(scroll->viewport()->rect().intersects(actionRect), "scrolling reaches the last action");
			check(scroll->height() <= 220, "the settings viewport remains bounded when rendered");
			check(scroll->horizontalScrollBarPolicy() != Qt::ScrollBarAlwaysOff,
			      "large theme controls retain a horizontal scrolling fallback");
		}
		check(editor.width() == previewMinimumWidth + 8, "rendering must not expand the parent editor");
		check(!editor.isVisible(), "the isolated test never shows its top-level window");
	}
	obsLinks->hide();
	transform->hide();
	root->invalidate();
	root->activate();
	check(editor.minimumSizeHint().width() == previewMinimumWidth + 8,
	      "closing both settings panels restores the preview-only width");
	std::cout << "font=" << fontSize << " previewMinimum=" << previewMinimumWidth
		  << " transformMinimum=" << transform->minimumSizeHint().width()
		  << " linksMinimum=" << obsLinks->minimumSizeHint().width() << '\n';
	if (!renderDirectory.isEmpty()) {
		editor.resize(previewMinimumWidth + 8, 440);
		for (auto *panel : {transform, obsLinks}) {
			transform->setHidden(panel != transform);
			obsLinks->setHidden(panel != obsLinks);
			root->activate();
			const QString name = QStringLiteral("font%1-width%2-%3.png")
				.arg(fontSize).arg(previewMinimumWidth).arg(panel == transform ? "transform" : "links");
			check(editor.grab().save(QDir(renderDirectory).filePath(name)), "optional panel snapshot saves successfully");
		}
	}
}
} // namespace

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	app.setStyle("Fusion");
	const QStringList arguments = app.arguments();
	const int renderArgument = arguments.indexOf(QStringLiteral("--render-directory"));
	if (renderArgument >= 0 && renderArgument + 1 < arguments.size()) {
		renderDirectory = arguments[renderArgument + 1];
		check(QDir().mkpath(renderDirectory), "optional snapshot directory is available");
	}
	std::cout << "Qt=" << qVersion() << "; isolated production panels; no windows shown; no OBS\n";
	for (int font : {10, 12, 16})
		for (int width : {110, 195})
			testPanels(font, width);
	return failures == 0 ? 0 : 1;
}
