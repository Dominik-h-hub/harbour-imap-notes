#ifndef NETWORKMONITOR_H
#define NETWORKMONITOR_H

#include <QObject>

// Watches Connman state via DBus and reports whether we are online and on a
// metered (mobile) connection. The full Connman wiring is added together with
// the sync engine — this scaffold exposes the API the QML and sync layers will
// consume so we do not have to rewire callers later.
class NetworkMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)
    Q_PROPERTY(bool metered READ metered NOTIFY meteredChanged)

public:
    explicit NetworkMonitor(QObject *parent = nullptr);

    bool online() const { return m_online; }
    bool metered() const { return m_metered; }

signals:
    void onlineChanged();
    void meteredChanged();

private:
    bool m_online = true;
    bool m_metered = false;
};

#endif
