#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace dsk {

enum class YouTubeBroadcastSelectionState {
	NoActiveBroadcast,
	Selected,
	MultipleActiveBroadcasts,
	NoStreamKeyMatch,
	MultipleStreamKeyMatches,
	PreferredBroadcastUnavailable,
};

enum class YouTubeBroadcastSelectionMode {
	ActiveSignal,
	Preflight,
};

struct YouTubeBroadcastSelection {
	YouTubeBroadcastSelectionState state = YouTubeBroadcastSelectionState::NoActiveBroadcast;
	QJsonObject broadcast;
	QVector<QJsonObject> candidates;
	QString streamKey;
};

YouTubeBroadcastSelection selectYouTubeBroadcast(const QJsonArray &broadcasts,
						  const QHash<QString, QJsonObject> &streamsById,
						  const QString &targetStreamKey,
						  const QString &preferredBroadcastId = {},
						  YouTubeBroadcastSelectionMode mode =
							  YouTubeBroadcastSelectionMode::ActiveSignal);

bool youtubeHasConflictingAutoStart(const QVector<QJsonObject> &candidates,
				    const QString &selectedBroadcastId);

} // namespace dsk
