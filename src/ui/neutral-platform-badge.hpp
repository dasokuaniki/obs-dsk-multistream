#pragma once

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QTransform>

namespace dsk {

inline void drawNeutralBadgeFrame(QPainter &painter)
{
	painter.setPen(QPen(QColor(QStringLiteral("#59646e")), 1));
	painter.setBrush(QColor(QStringLiteral("#151b20")));
	painter.drawRoundedRect(QRectF(2.5, 2.5, 25, 25), 5, 5);
}

inline void drawNeutralMonogram(QPainter &painter, const QString &text)
{
	QFont font(QStringLiteral("Bahnschrift SemiCondensed"));
	font.setWeight(QFont::DemiBold);
	font.setPixelSize(17);
	font.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::PreferAntialias |
							       QFont::NoSubpixelAntialias));

	QPainterPath glyph;
	glyph.addText(QPointF(0, 0), font, text);
	const QRectF glyphBounds = glyph.boundingRect();
	const QRectF targetBounds(4, 3, 22, 23);
	QTransform center;
	center.translate(targetBounds.center().x() - glyphBounds.center().x(),
			 targetBounds.center().y() - glyphBounds.center().y());
	painter.setPen(Qt::NoPen);
	painter.setBrush(QColor(QStringLiteral("#dce2e8")));
	painter.drawPath(center.map(glyph));
}

} // namespace dsk
