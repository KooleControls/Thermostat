#include "HomeScreen.h"
#include "BleManager/BleManager.h"
#include "ClimateManager/ClimateManager.h"
#include "OpenThermManager/OpenThermManager.h"
#include "RoomTemperatureManager/RoomTemperatureManager.h"

void HomeScreen::Build(lv_obj_t* root)
{
    face_.Build(root, IntentTrampoline, this);
    lv_timer_create(RefreshTimerCb, kRefreshMs, this);
}

void HomeScreen::OnShow()
{
    Refresh();   // never load the face with whatever was on it minutes ago
}

void HomeScreen::Refresh()
{
    HomeView view;

    view.roomValid = serviceProvider_.getRoomTemperatureManager()
                         .GetRoomTemperature(view.roomTemp);
    view.setpoint = serviceProvider_.getClimateManager().GetUserSetpoint();

    switch (serviceProvider_.getClimateManager().GetMode())
    {
    case ClimateMode::Heat: view.mode = HomeMode::Heat; break;
    case ClimateMode::Cool: view.mode = HomeMode::Cool; break;
    default:                view.mode = HomeMode::Off;  break;
    }

    // Activity is what the boiler reports (OT ID 0 status bits), not what we
    // asked for — the difference between the two is exactly the "ramping"
    // state, so the arrow means "called for, not running yet".
    OtBoilerState boiler = serviceProvider_.getOpenThermManager().GetState();
    OtDemand      demand = serviceProvider_.getOpenThermManager().GetDemand();

    bool heating = boiler.chActive && boiler.flame;   // flame alone could be DHW
    bool cooling = boiler.coolingActive;

    if (heating)      view.activity = HomeActivity::Heating;
    else if (cooling) view.activity = HomeActivity::Cooling;
    else              view.activity = HomeActivity::Idle;

    // Only meaningful while the OT link is up; with no boiler talking to us we
    // have no idea whether it caught up, so we claim nothing.
    view.ramping = boiler.linked &&
                   ((demand.chEnable && !heating) || (demand.coolEnable && !cooling));

    view.linked = serviceProvider_.getBleManager().GetLinkState() ==
                  BleManager::LinkState::Ready;

    face_.Apply(view);
}

void HomeScreen::OnIntent(HomeIntent intent)
{
    switch (intent)
    {
    case HomeIntent::NudgeDown:
        serviceProvider_.getClimateManager().NudgeSetpoint(-kNudgeStep);
        break;
    case HomeIntent::NudgeUp:
        serviceProvider_.getClimateManager().NudgeSetpoint(+kNudgeStep);
        break;
    case HomeIntent::SetHeat:
        serviceProvider_.getClimateManager().SetMode(ClimateMode::Heat);
        break;
    case HomeIntent::SetCool:
        serviceProvider_.getClimateManager().SetMode(ClimateMode::Cool);
        break;
    case HomeIntent::SetOff:
        serviceProvider_.getClimateManager().SetMode(ClimateMode::Off);
        break;
    case HomeIntent::SetAuto:
        // No auto mode behind the tile yet — the face draws it inert.
        return;
    case HomeIntent::OpenSettings:
        navigator_.Go(ScreenId::Pin);   // shell skips the pad when no PIN is set
        return;
    }

    // Setpoint and mode both take effect on the control loop's next step, but
    // the face must answer the finger now, not a second from now.
    Refresh();
}

void HomeScreen::RefreshTimerCb(lv_timer_t* t)
{
    auto* self = static_cast<HomeScreen*>(lv_timer_get_user_data(t));
    if (!self->IsActive()) return;   // built, but a menu is on the panel
    self->Refresh();
}

void HomeScreen::IntentTrampoline(void* user, HomeIntent intent)
{
    static_cast<HomeScreen*>(user)->OnIntent(intent);
}
