#include "logmodel.h"
#include <QColor>

LogModel::LogModel(QObject *parent) : QAbstractListModel(parent) {}

int LogModel::rowCount(const QModelIndex &) const {
    return m_entries.size();
}

QVariant LogModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};

    const auto &entry = m_entries.at(index.row());

    switch (role) {
    case LevelRole:
        return static_cast<int>(entry.level);
    case MessageRole:
        return entry.message;
    case TimestampRole:
        return entry.timestamp.toString("hh:mm:ss.zzz");
    case DisplayRole:
        return QString("[%1] %2")
            .arg(entry.timestamp.toString("hh:mm:ss"), entry.message);
    case Qt::ForegroundRole:
        switch (entry.level) {
        case LogEntry::Info:  return QColor(100, 180, 255);
        case LogEntry::Ok:    return QColor(100, 220, 100);
        case LogEntry::Warn:  return QColor(255, 220, 80);
        case LogEntry::Error: return QColor(255, 80, 80);
        }
        return {};
    case Qt::DisplayRole:
        return QString("[%1] %2")
            .arg(entry.timestamp.toString("hh:mm:ss"), entry.message);
    }
    return {};
}

void LogModel::addEntry(LogEntry::Level level, const QString &message) {
    beginInsertRows(QModelIndex(), m_entries.size(), m_entries.size());
    m_entries.append({level, message, QDateTime::currentDateTime()});
    endInsertRows();
    emit entryAdded(m_entries.size() - 1);
}

void LogModel::clear() {
    beginResetModel();
    m_entries.clear();
    endResetModel();
}

QString LogModel::plainText() const {
    QString text;
    for (const auto &e : m_entries) {
        QString prefix;
        switch (e.level) {
        case LogEntry::Info:  prefix = "[INFO]";  break;
        case LogEntry::Ok:    prefix = "[OK]";    break;
        case LogEntry::Warn:  prefix = "[WARN]";  break;
        case LogEntry::Error: prefix = "[ERROR]"; break;
        }
        text += QString("%1 %2 - %3\n")
            .arg(prefix, e.timestamp.toString("hh:mm:ss"), e.message);
    }
    return text;
}
