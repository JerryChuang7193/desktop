// Copyright (c) 2020 Private Internet Access, Inc.
//
// This file is part of the Private Internet Access Desktop Client.
//
// The Private Internet Access Desktop Client is free software: you can
// redistribute it and/or modify it under the terms of the GNU General Public
// License as published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.
//
// The Private Internet Access Desktop Client is distributed in the hope that
// it will be useful, but WITHOUT ANY WARRANTY; without even the implied
// warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with the Private Internet Access Desktop Client.  If not, see
// <https://www.gnu.org/licenses/>.

#include "common.h"
#line HEADER_FILE("posix/posix_daemon.h")

#ifndef POSIX_DAEMON_H
#define POSIX_DAEMON_H
#pragma once

#include "daemon.h"
#include "posix/unixsignalhandler.h"
#include "filewatcher.h"

#if defined(Q_OS_MAC)
#include "mac/kext_client.h"
#include "mac/mac_dns.h"
#elif defined(Q_OS_LINUX)
#include "posix/posix_firewall_iptables.h"
#include "linux/linux_modsupport.h"
#endif

class QSocketNotifier;

class PosixDaemon : public Daemon
{
    Q_OBJECT
    CLASS_LOGGING_CATEGORY("posix.daemon")
public:
    PosixDaemon();
    ~PosixDaemon();

    static PosixDaemon* instance() { return static_cast<PosixDaemon*>(Daemon::instance()); }

    virtual std::shared_ptr<NetworkAdapter> getNetworkAdapter() override;

protected slots:
    void handleSignal(int sig) Q_DECL_NOEXCEPT;

protected:
    virtual void applyFirewallRules(const FirewallParams& params) override;
    virtual QJsonValue RPC_installKext() override;
    virtual void writePlatformDiagnostics(DiagnosticsFile &file) override;

private:
#if defined(Q_OS_LINUX)
    void updateExistingDNS();
#endif
    // On Mac, update the bound route on the physical interface; used for split
    // tunnel and DNS leak protection
    void updateBoundRoute(const FirewallParams &params);
    void toggleSplitTunnel(const FirewallParams &params);
    template <typename T>
    void prepareSplitTunnel()
    {
        _commThread.invokeOnThread([&]()
        {
            auto pSplitTunnelHelper = new T(&_commThread.objectOwner());
            connect(this, &PosixDaemon::startSplitTunnel, pSplitTunnelHelper, &T::initiateConnection);
            connect(this, &PosixDaemon::updateSplitTunnel, pSplitTunnelHelper, &T::updateSplitTunnel);
            // Block the caller for shutdown.  The slot invocation is still
            // queued in the proper order with respect to the other changes, but
            // the caller will wait for it.  This is necessary on Mac to ensure
            // that the kext connection is closed before attempting to unload
            // the kext.
            connect(this, &PosixDaemon::shutdownSplitTunnel, pSplitTunnelHelper, &T::shutdownConnection,
                    Qt::ConnectionType::BlockingQueuedConnection);
        });
    }

#ifdef Q_OS_LINUX
    void checkLinuxModules();
#endif

private:
    UnixSignalHandler _signalHandler;

    // Thread for communicating with split tunnel helpers (kexts, process managers, etc)
    RunningWorkerThread _commThread;

    // The current configuration applied to split tunnel - whether it is
    // enabled, and the network info for bypass apps.
    bool _enableSplitTunnel;

    SubnetBypass _subnetBypass;

#ifdef Q_OS_LINUX
    FileWatcher _resolvconfWatcher;
    IpTablesFirewall _firewall;
    LinuxModSupport _linuxModSupport;
#endif

#ifdef Q_OS_MAC
    KextMonitor _kextMonitor;
    MacDns _macDnsMonitor;
    // Network scan last used to create bound route (see applyFirewallRules())
    OriginalNetworkScan _boundRouteNetScan;
#endif

signals:
    void startSplitTunnel(const FirewallParams &params, QString tunnelDeviceName,
                          QString tunnelDeviceLocalAddress,
                          QVector<QString> excludedApps, QVector<QString> vpnOnlyApps);
    void shutdownSplitTunnel();
    void updateSplitTunnel(const FirewallParams &params, QString tunnelDeviceName,
                           QString tunnelDeviceLocalAddress,
                           QVector<QString> excludedApps, QVector<QString> vpnOnlyApps);
};

void setUidAndGid();

#endif // POSIX_DAEMON_H
