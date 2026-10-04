/*---------------------------------------------------------*\
| AK820Plugin.h                                            |
|                                                           |
|   OpenRGB plugin for the Ajazz AK820 Max Plus             |
|                                                           |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include "OpenRGBPluginInterface.h"

#include <QObject>

class AK820Plugin : public QObject, public OpenRGBPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OpenRGBPluginInterface_IID FILE "AK820Plugin.json")   // 1.0 reads the API version and Id from here
    Q_INTERFACES(OpenRGBPluginInterface)

public:
    OpenRGBPluginInfo   GetPluginInfo() override;
    unsigned int        GetPluginAPIVersion() override;
    void                Load(OpenRGBPluginAPIInterface* plugin_api_ptr) override;
    QWidget*            GetWidget() override;
    QMenu*              GetTrayMenu() override;
    void                Unload() override;

    void                OnProfileAboutToLoad() override {}
    void                OnProfileLoad(nlohmann::json) override {}
    nlohmann::json      OnProfileSave() override { return nlohmann::json(); }
    unsigned char*      OnSDKCommand(unsigned int, unsigned char*, unsigned int*) override { return nullptr; }

    void                ProfileManagerUpdated(unsigned int) override {}
    void                ResourceManagerUpdated(unsigned int) override {}
    void                SettingsManagerUpdated(unsigned int) override {}
};
