#pragma once

class Board;
class ClimateManager;
class CommandManager;
class ConsoleManager;
class HotWaterManager;
class NetworkManager;
class OpenThermManager;
class RoomTemperatureManager;
class SettingsManager;
class SystemManager;
class TimeManager;
class UpdateManager;
class WebServerManager;

class ServiceProvider
{
public:
    virtual Board& getBoard() = 0;
    virtual ClimateManager& getClimateManager() = 0;
    virtual CommandManager& getCommandManager() = 0;
    virtual ConsoleManager& getConsoleManager() = 0;
    virtual HotWaterManager& getHotWaterManager() = 0;
    virtual NetworkManager& getNetworkManager() = 0;
    virtual OpenThermManager& getOpenThermManager() = 0;
    virtual RoomTemperatureManager& getRoomTemperatureManager() = 0;
    virtual SettingsManager& getSettingsManager() = 0;
    virtual SystemManager& getSystemManager() = 0;
    virtual TimeManager& getTimeManager() = 0;
    virtual UpdateManager& getUpdateManager() = 0;
    virtual WebServerManager& getWebServerManager() = 0;
};
