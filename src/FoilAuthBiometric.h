/* Copyright (C) 2026 Jolla Mobile Ltd
 * BSD 3-Clause License, see LICENSE.
 */
#ifndef FOILAUTH_BIOMETRIC_H
#define FOILAUTH_BIOMETRIC_H
#include <QObject>
#include "HarbourSystemState.h"
#include <Secrets/secretmanager.h>
class FoilAuthModel;
class FoilAuthBiometric : public QObject
{
    Q_OBJECT
    Q_PROPERTY(FoilAuthModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    explicit FoilAuthBiometric(QObject *parent = Q_NULLPTR);
    FoilAuthModel *model() const { return m_model; }
    void setModel(FoilAuthModel *model);
    bool ready() const { return m_ready; }
    bool available() const { return m_available; }
    bool enabled() const { return m_enabled; }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    Q_INVOKABLE void enroll(const QString &password);
    Q_INVOKABLE void unlock();
    Q_INVOKABLE void disable();
signals:
    void modelChanged();
    void readyChanged();
    void availableChanged();
    void enabledChanged();
    void busyChanged();
    void errorChanged();
private:
    void readSecret(bool enrolling);
    void setBusy(bool busy);
    void setEnabled(bool enabled);
    void setError(const QString &error);
    Sailfish::Secrets::SecretManager m_manager;
    FoilAuthModel *m_model = Q_NULLPTR;
    bool m_ready = false;
    bool m_available = false;
    bool m_enabled = false;
    bool m_busy = false;
    QString m_error;
    uint m_generation = 0;
    uint m_lockGeneration = 0;
    QSharedPointer<HarbourSystemState> m_systemState;
};
#endif
