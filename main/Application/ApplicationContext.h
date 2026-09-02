#pragma once
#include "ServiceProvider.h"
#include "Board.h"
#include "BleManager/BleManager.h"
#include "CommandManager/CommandManager.h"
#include "ConsoleManager/ConsoleManager.h"
#include "NetworkManager/NetworkManager.h"
#include "OpenThermManager/OpenThermManager.h"
#include "ClimateManager/ClimateManager.h"
#include "HotWaterManager/HotWaterManager.h"
#include "DisplayManager/DisplayManager.h"
#include "RoomSimManager/RoomSimManager.h"
#include "RoomTemperatureManager/RoomTemperatureManager.h"
#include "SettingsManager/SettingsManager.h"
#include "SystemManager/SystemManager.h"
#include "TimeManager/TimeManager.h"
#include "UpdateManager/UpdateManager.h"
#include "WebServerManager/WebServerManager.h"

class ApplicationContext : public ServiceProvider
{
public:
    ApplicationContext() = default;
    ~ApplicationContext() = default;
    ApplicationContext(const ApplicationContext&) = delete;
    ApplicationContext& operator=(const ApplicationContext&) = delete;

    BleManager& getBleManager() override { return m_bleManager; }
    Board& getBoard() override { return m_board; }
    ClimateManager& getClimateManager() override { return m_climateManager; }
    CommandManager& getCommandManager() override { return m_commandManager; }
    ConsoleManager& getConsoleManager() override { return m_consoleManager; }
    DisplayManager& getDisplayManager() override { return m_displayManager; }
    HotWaterManager& getHotWaterManager() override { return m_hotWaterManager; }
    NetworkManager& getNetworkManager() override { return m_networkManager; }
    OpenThermManager& getOpenThermManager() override { return m_openThermManager; }
    RoomSimManager& getRoomSimManager() override { return m_roomSimManager; }
    RoomTemperatureManager& getRoomTemperatureManager() override { return m_roomTemperatureManager; }
    SettingsManager& getSettingsManager() override { return m_settingsManager; }
    SystemManager& getSystemManager() override { return m_systemManager; }
    TimeManager& getTimeManager() override { return m_timeManager; }
    UpdateManager& getUpdateManager() override { return m_updateManager; }
    WebServerManager& getWebServerManager() override { return m_webServerManager; }

private:
    ConsoleManager m_consoleManager{*this};
    SettingsManager m_settingsManager{*this};
    SystemManager m_systemManager{*this};
    NetworkManager m_networkManager{*this};
    TimeManager m_timeManager{*this};
    CommandManager m_commandManager{*this};
    Board m_board{*this};
    RoomTemperatureManager m_roomTemperatureManager{*this};
    OpenThermManager m_openThermManager{*this};
    ClimateManager m_climateManager{*this};
    RoomSimManager m_roomSimManager{*this};
    HotWaterManager m_hotWaterManager{*this};
    DisplayManager m_displayManager{*this};
    UpdateManager m_updateManager{*this};
    WebServerManager m_webServerManager{*this};
    BleManager m_bleManager{*this};
};
