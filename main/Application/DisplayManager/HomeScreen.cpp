#include "HomeScreen.h"
#include "BleManager/BleManager.h"
#include "ClimateManager/ClimateManager.h"
#include "OpenThermManager/OpenThermManager.h"
#include "RoomTemperatureManager/RoomTemperatureManager.h"
#include <cmath>

void HomeScreen::Build(lv_obj_t* root)
{
    face_.Build(root, IntentTrampoline, this);
    lv_timer_create(RefreshTimerCb, kRefreshMs, this);
}

void HomeScreen::OnShow()
{
    // Coming back from the menu always lands on the room temperature, never on
    // a setpoint left on the dial minutes ago.
    if (revertTimer_)
    {
        lv_timer_delete(revertTimer_);
        revertTimer_ = nullptr;
    }
    showingSetpoint_ = false;
    Refresh();
}

void HomeScreen::Refresh()
{
    HomeView view;

    view.roomValid = serviceProvider_.getRoomTemperatureManager()
                         .GetRoomTemperature(view.roomTemp);
    view.setpoint     = serviceProvider_.getClimateManager().GetUserSetpoint();
    view.showSetpoint = showingSetpoint_;

    // The gateway owns the heat/cool decision and only offers automatic mode,
    // so the face reports Auto regardless of what ClimateManager has stored.
    // When the gateway grows real modes, this reads GetMode() again.
    view.mode = HomeMode::Auto;

    // Activity is what the boiler reports (OT ID 0 status bits), not what we
    // asked for — but only for a stage we actually enabled. We are the master:
    // a stage we never switched on cannot be running, so a slave reporting it
    // active is reporting a bit it does not maintain rather than a fact about
    // the system. Bench case: a gateway that advertises cooling-active
    // permanently, having never written that bit in its life, would otherwise
    // paint a blue snowflake on a house that is heating.
    //
    // The cost is that a stage reads idle the moment we stop asking for it,
    // while the boiler is still winding down. That is a second or two of
    // understatement, against a permanent falsehood.
    OtBoilerState boiler = serviceProvider_.getOpenThermManager().GetState();
    OtDemand      demand = serviceProvider_.getOpenThermManager().GetDemand();

    // flame alone could be DHW, so heating needs the CH bit with it.
    bool heating = demand.chEnable   && boiler.chActive && boiler.flame;
    bool cooling = demand.coolEnable && boiler.coolingActive;

    if (heating)      view.activity = HomeActivity::Heating;
    else if (cooling) view.activity = HomeActivity::Cooling;
    else              view.activity = HomeActivity::Idle;

    // No changeover is claimed, so the badge stays a single icon.
    //
    // This was inferred once, by comparing our demand against the boiler's
    // reported state and calling any direction mismatch a changeover. That is
    // wrong. The two disagree constantly for reasons that are not transitions
    // at all — a demand raised before the other end acts on it, a status bit
    // the other end never updates — and the badge then asserts a mode change
    // that is not happening. Seen on the bench within minutes: cooling-active
    // reported while we were demanding heat, and the face drew snowflake →
    // flame at a system that was doing neither.
    //
    // A changeover is something only the side running the timers knows it is
    // doing. Standard OpenTherm has no data-ID that says so, so until the
    // gateway tells us over a KC extension there is nothing to draw — and
    // guessing is worse than staying quiet. HomeFace keeps the three-glyph
    // rendering ready for the day that signal exists.
    view.movingTo = view.activity;

    view.linked = serviceProvider_.getBleManager().GetLinkState() ==
                  BleManager::LinkState::Ready;

    face_.Apply(view);
}

// The face shows whole degrees, so the setpoint has to move in whole degrees —
// a half-degree step would leave every other press with nothing to show for
// itself. A setpoint already carrying a fraction (an older stored value, or a
// remote override adopted from the gateway) snaps to the next whole degree in
// the direction of travel, so the first press still moves the number by one.
void HomeScreen::Nudge(float direction)
{
    auto& climate = serviceProvider_.getClimateManager();
    float current = climate.GetUserSetpoint();
    float target  = (direction > 0.0f) ? floorf(current) + kNudgeStep
                                       : ceilf(current)  - kNudgeStep;
    climate.NudgeSetpoint(target - current);   // clamped inside

    showingSetpoint_ = true;
    if (revertTimer_) lv_timer_reset(revertTimer_);
    else revertTimer_ = lv_timer_create(RevertTimerCb, kRevertMs, this);
}

void HomeScreen::OnIntent(HomeIntent intent)
{
    switch (intent)
    {
    case HomeIntent::NudgeDown: Nudge(-1.0f); break;
    case HomeIntent::NudgeUp:   Nudge(+1.0f); break;

    // Mode selection is the gateway's today, and the face draws all four tiles
    // locked, so no mode intent can reach here. They stay wired for the day it
    // unlocks.
    case HomeIntent::SetAuto: return;
    case HomeIntent::SetHeat:
        serviceProvider_.getClimateManager().SetMode(ClimateMode::Heat);
        break;
    case HomeIntent::SetCool:
        serviceProvider_.getClimateManager().SetMode(ClimateMode::Cool);
        break;
    case HomeIntent::SetOff:
        serviceProvider_.getClimateManager().SetMode(ClimateMode::Off);
        break;

    case HomeIntent::OpenSettings:
        navigator_.Go(ScreenId::Pin);   // shell skips the pad when no PIN is set
        return;
    }

    // The change takes effect on the control loop's next step, but the face
    // must answer the finger now, not a second from now.
    Refresh();
}

void HomeScreen::RefreshTimerCb(lv_timer_t* t)
{
    auto* self = static_cast<HomeScreen*>(lv_timer_get_user_data(t));
    if (!self->IsActive()) return;   // built, but a menu is on the panel
    self->Refresh();
}

void HomeScreen::RevertTimerCb(lv_timer_t* t)
{
    auto* self = static_cast<HomeScreen*>(lv_timer_get_user_data(t));
    lv_timer_delete(t);
    self->revertTimer_ = nullptr;
    self->showingSetpoint_ = false;
    self->Refresh();
}

void HomeScreen::IntentTrampoline(void* user, HomeIntent intent)
{
    static_cast<HomeScreen*>(user)->OnIntent(intent);
}
