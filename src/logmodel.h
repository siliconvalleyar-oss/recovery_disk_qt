#ifndef LOGMODEL_H
#define LOGMODEL_H

#include <QAbstractListModel>
#include <QList>
#include <QDateTime>

struct LogEntry {
    enum Level { Info, Ok, Warn, Error };
    Level level;
    QString message;
    QDateTime timestamp;
};

class LogModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        LevelRole = Qt::UserRole + 1,
        MessageRole,
        TimestampRole,
        DisplayRole
    };

    explicit LogModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    void addEntry(LogEntry::Level level, const QString &message);
    void clear();
    QString plainText() const;

signals:
    void entryAdded(int index);

private:
    QList<LogEntry> m_entries;
};

#endif
