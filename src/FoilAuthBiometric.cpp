/* Copyright (C) 2026 Jolla Mobile Ltd
 * BSD 3-Clause License, see LICENSE.
 */
#include "FoilAuthBiometric.h"
#include "FoilAuthModel.h"
#include <Secrets/plugininforequest.h>
#include <Secrets/storesecretrequest.h>
#include <Secrets/storedsecretrequest.h>
#include <Secrets/deletesecretrequest.h>
#include <QSettings>
using namespace Sailfish::Secrets;
namespace {
Secret::Identifier identifier()
{
    return Secret::Identifier(QStringLiteral("harbour-foilauth-biometric-unlock-v1"), QString(),
                              QStringLiteral("org.sailfishos.secrets.plugin.storage.sqlite"));
}
const char enabledSetting[] = "biometricUnlock/enabled";
}
FoilAuthBiometric::FoilAuthBiometric(QObject *parent) : QObject(parent),
    m_systemState(HarbourSystemState::sharedInstance())
{
    connect(m_systemState.data(), &HarbourSystemState::lockedChanged, this, [this] {
        if (m_systemState->locked()) ++m_lockGeneration;
    });
    QSettings settings(QStringLiteral("harbour-foilauth"), QStringLiteral("biometric"));
    m_enabled = settings.value(QLatin1String(enabledSetting), false).toBool();
    PluginInfoRequest *request = new PluginInfoRequest(this);
    request->setManager(&m_manager);
    connect(request, &Request::statusChanged, this, [this, request] {
        if (request->status() != Request::Finished) return;
        if (request->result().code() == Result::Succeeded) {
            for (const PluginInfo &plugin : request->authenticationPlugins()) {
                if (plugin.name() == QLatin1String("org.sailfishos.secrets.plugin.authentication.deviceauth")) {
                    m_available = true;
                    emit availableChanged();
                    break;
                }
            }
        }
        m_ready = true;
        emit readyChanged();
        request->deleteLater();
    });
    request->startRequest();
}
void FoilAuthBiometric::setModel(FoilAuthModel *model)
{
    if (m_model == model) return;
    if (m_model) disconnect(m_model, Q_NULLPTR, this, Q_NULLPTR);
    m_model = model;
    if (model) {
        connect(model, &FoilAuthModel::passwordChanged, this, &FoilAuthBiometric::disable);
        connect(model, &FoilAuthModel::keyGenerated, this, &FoilAuthBiometric::disable);
        connect(model, &QObject::destroyed, this, [this] { m_model = Q_NULLPTR; ++m_generation; });
    }
    emit modelChanged();
}
void FoilAuthBiometric::setBusy(bool busy)
{
    if (m_busy != busy) { m_busy = busy; emit busyChanged(); }
}
void FoilAuthBiometric::setEnabled(bool enabled)
{
    QSettings settings(QStringLiteral("harbour-foilauth"), QStringLiteral("biometric"));
    settings.setValue(QLatin1String(enabledSetting), enabled);
    if (m_enabled != enabled) { m_enabled = enabled; emit enabledChanged(); }
}
void FoilAuthBiometric::setError(const QString &error)
{
    if (m_error != error) { m_error = error; emit errorChanged(); }
}
void FoilAuthBiometric::enroll(const QString &password)
{
    if (m_busy || !m_available || !m_model) return;
    setError(QString());
    if (!m_model->checkPassword(password)) {
        //% "Incorrect Foil password"
        setError(qtTrId("foilauth-biometric-incorrect-password"));
        return;
    }
    setBusy(true);
    const uint generation = ++m_generation;
    // Standalone secrets cannot be overwritten. Remove only our own record.
    DeleteSecretRequest *remove = new DeleteSecretRequest(this);
    remove->setManager(&m_manager);
    remove->setIdentifier(identifier());
    remove->setUserInteractionMode(SecretManager::PreventInteraction);
    connect(remove, &Request::statusChanged, this, [this, remove, password, generation] {
        if (remove->status() != Request::Finished) return;
        remove->deleteLater();
        if (generation != m_generation) return;
        if (remove->result().code() != Result::Succeeded
                && remove->result().errorCode() != Result::InvalidSecretError) {
            setError(remove->result().errorMessage());
            setBusy(false);
            return;
        }
        setEnabled(false);
        StoreSecretRequest *store = new StoreSecretRequest(this);
        store->setManager(&m_manager);
        store->setSecretStorageType(StoreSecretRequest::StandaloneDeviceLockSecret);
        Secret secret(identifier());
        secret.setData(password.toUtf8());
        store->setSecret(secret);
        store->setEncryptionPluginName(QStringLiteral("org.sailfishos.secrets.plugin.encryption.openssl"));
        store->setDeviceLockUnlockSemantic(SecretManager::DeviceLockAccessRelock);
        store->setAccessControlMode(SecretManager::ExactApplicationOwnerMode);
        store->setUserInteractionMode(SecretManager::SystemInteraction);
        connect(store, &Request::statusChanged, this, [this, store, generation] {
            if (store->status() != Request::Finished) return;
            store->deleteLater();
            if (generation != m_generation) {
                disable();
                return;
            }
            if (store->result().code() == Result::Succeeded) {
                // Verify that fresh device authentication works before enabling it.
                readSecret(true);
            } else {
                setError(store->result().errorMessage());
                setBusy(false);
            }
        });
        store->startRequest();
    });
    remove->startRequest();
}
void FoilAuthBiometric::unlock()
{
    if (m_busy || !m_available || !m_enabled || !m_model || m_systemState->locked()) return;
    readSecret(false);
}
void FoilAuthBiometric::readSecret(bool enrolling)
{
    if (m_systemState->locked()) {
        if (enrolling) disable();
        else setBusy(false);
        return;
    }
    const uint lockGeneration = m_lockGeneration;
    setError(QString());
    setBusy(true);
    const uint generation = ++m_generation;
    StoredSecretRequest *request = new StoredSecretRequest(this);
    request->setManager(&m_manager);
    request->setIdentifier(identifier());
    request->setUserInteractionMode(SecretManager::SystemInteraction);
    connect(request, &Request::statusChanged, this, [this, request, generation, enrolling, lockGeneration] {
        if (request->status() != Request::Finished) return;
        request->deleteLater();
        if (generation != m_generation) return;
        // A result from before a device lock must never reopen the model,
        // even if the phone has since been unlocked again.
        if (m_systemState->locked() || lockGeneration != m_lockGeneration) {
            if (enrolling) disable();
            else setBusy(false);
            return;
        }
        if (request->result().code() == Result::Succeeded) {
            if (!m_model || !m_model->unlock(QString::fromUtf8(request->secret().data()))) {
                //% "The Foil password has changed. Set up fingerprint unlock again."
                setError(qtTrId("foilauth-biometric-password-changed"));
                disable();
                return;
            }
            if (enrolling) setEnabled(true);
        } else {
            if (request->result().errorCode() != Result::InteractionViewUserCanceledError) {
                setError(request->result().errorMessage());
            }
            if (enrolling) {
                disable();
                return;
            }
        }
        setBusy(false);
    });
    request->startRequest();
}
void FoilAuthBiometric::disable()
{
    ++m_generation;
    setEnabled(false);
    setBusy(true);
    DeleteSecretRequest *request = new DeleteSecretRequest(this);
    request->setManager(&m_manager);
    request->setIdentifier(identifier());
    request->setUserInteractionMode(SecretManager::PreventInteraction);
    connect(request, &Request::statusChanged, this, [this, request] {
        if (request->status() != Request::Finished) return;
        if (request->result().code() != Result::Succeeded
                && request->result().errorCode() != Result::InvalidSecretError) {
            setError(request->result().errorMessage());
        }
        request->deleteLater();
        setBusy(false);
    });
    request->startRequest();
}
