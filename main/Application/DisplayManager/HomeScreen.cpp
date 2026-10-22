#include "HomeScreen.h"
#include "BleManager/BleManager.h"
#include "ClimateManager/ClimateManager.h"
#include "OpenThermManager/OpenThermManager.h"
#include "RoomTemperatureManager/RoomTemperatureManager.h"
#include <cmath>

namespace {

// The two enums are ordered for different reasons — ClimateMode by the wire
// value the gateway reads, HomeMode by where the tile sits on the row — so
// they are mapped, never cast.
HomeMode ToHomeMode(ClimateMode m)
{
    switch (m)
    {
    case ClimateMode::Heat: return HomeMode::Heat;
    case ClimateMode::Cool: return HomeMode::Cool;
    case ClimateMode::Off:  return HomeMode::Off;
    case ClimateMode::Auto: return HomeMode::Auto;
    }
    return HomeMode::Auto;
}

}   // namespace

void HomeScreen::Build(lv_obj_t* root)
{
    face_.Build(root, IntentTrampoline, this);

    // Once for the life of the screen, not once per build: an lv_timer is not a
    // child of the tree, so a rebuild would leave the previous one running.
    // Its callback keys off IsActive(), which is false while the tree is gone.
    if (refreshTimer_ == nullptr)
        refreshTimer_ = lv_timer_create(RefreshTimerCb, kRefreshMs, this);
}

// The commit timer is the one piece of state that can outlive the tree with a
// reason to touch it: it is armed by a nudge and survives a walk into the menu,
// so a rebuild from there would land it on a deleted face. A rebuild ends the
// edit instead — handing over what was dialled, since the guest saw it there —
// and re-enters through OnShow().
void HomeScreen::OnDestroy()
{
    FinishEdit();
}

void HomeScreen::OnShow()
{
    // Coming back from the menu always lands on the room temperature, never on
    // a setpoint left on the dial. An edit still open from before the walk into
    // the menu ends here, the same way the timer would have ended it.
    FinishEdit();
    Refresh();
}

void HomeScreen::Refresh()
{
    HomeView view;

    view.roomValid = serviceProvider_.getRoomTemperatureManager()
                         .GetRoomTemperature(view.roomTemp);
    view.setpoint     = editing_ ? editSetpoint_
                                 : serviceProvider_.getClimateManager().GetUserSetpoint();
    view.showSetpoint = editing_;

    view.mode = ToHomeMode(serviceProvider_.getClimateManager().GetMode());

    // The badge reads the slave's own ID 0 status bits, and reads them as the
    // spec defines them:
    //
    //   bit 1 (chActive)      the CH stage is on        — heating
    //   bit 4 (coolingActive) the cooling stage is on   — cooling
    //   bit 3 (flame)         it is burning right now
    //
    // The stage bits and the flame bit answer different questions, which is
    // what makes standby visible: CH on with no flame is a system in heating
    // that is not currently burning, and the same shape holds for cooling. An
    // earlier version folded the two together and could only ever say "on" or
    // "off", which is why a system sitting in cooling standby drew a flame.
    //
    // Nothing KC-specific here — a real boiler sets the same bits with the same
    // meanings; this is just no longer throwing the distinction away.
    OtBoilerState boiler = serviceProvider_.getOpenThermManager().GetState();

    // Both from the slave's config (ID 3). Not read yet is not "no cooling":
    // dimming the tile until the first answer greyed out a mode the guest had
    // picked, on every boot.
    view.coolingAvailable = !boiler.configRead || boiler.coolingSupported;
    view.modeChoice       = boiler.heatCoolByMaster;

    if (boiler.chActive && !boiler.coolingActive)
    {
        view.stage   = HomeStage::Heating;
        view.running = boiler.flame;   // flame alone could be DHW; with CH on it is not
    }
    else if (boiler.coolingActive && !boiler.chActive)
    {
        view.stage = HomeStage::Cooling;
        // Bit 4 says the plant is in cooling, standby included: the gateway
        // sets it on entering cooling standby and holds it through the run.
        // OT has no cooling counterpart of the flame bit, so nothing on the
        // wire says the cooler is running. The nearest fact is our own
        // request — in cooling, and asked to cool. The gateway's minimum
        // on/off times can hold the cooler a little either side of that, so
        // heating colours exactly and cooling closely.
        view.running = serviceProvider_.getClimateManager().GetStatus().coolEnable;
    }
    else if (boiler.flame)
    {
        // Both stage bits set, or neither, and yet it is burning. Seen at
        // startup before the gateway has picked a mode. The flame is the one
        // fact not in doubt.
        view.stage   = HomeStage::Heating;
        view.running = true;
    }
    else
    {
        // Nothing running and no stage claimed. The guest's mode is still a
        // fact, not a guess: in Heat or Cool the system is standing by in that
        // direction, so it gets that stage's glyph in grey. Auto and Off have
        // no direction to stand by in and keep the power glyph.
        if      (view.mode == HomeMode::Heat) view.stage = HomeStage::Heating;
        else if (view.mode == HomeMode::Cool) view.stage = HomeStage::Cooling;
        else                                  view.stage = HomeStage::None;
        view.running = false;
    }

    // No changeover is claimed, so the badge stays a single icon.
    //
    // This was inferred once, by comparing our demand against the slave's
    // reported state and calling any direction mismatch a changeover. The two
    // disagree constantly for reasons that are not transitions at all, and the
    // badge then asserted a mode change that was not happening.
    //
    // A changeover is something only the side running the timers knows it is
    // doing, and OT ID 0 has no bit for "about to swap stages" — both stage
    // bits are simply set to the new one when it happens. HomeFace keeps the
    // three-glyph rendering ready for a signal that actually carries it.
    view.movingTo = view.stage;

    view.linked = serviceProvider_.getBleManager().GetLinkState() ==
                  BleManager::LinkState::Ready;

    face_.Apply(view);
}

// The first touch only puts the setpoint on the dial; the touches after it move
// it. Every touch restarts the quiet time that ends the edit.
//
// The face shows whole degrees, so the setpoint has to move in whole degrees —
// a half-degree step would leave every other press with nothing to show for
// itself. A setpoint already carrying a fraction (an older stored value, or a
// remote override adopted from the gateway) snaps to the next whole degree in
// the direction of travel, so a press still moves the number by one.
void HomeScreen::Nudge(float direction)
{
    if (!editing_)
    {
        editing_      = true;
        editStart_    = serviceProvider_.getClimateManager().GetUserSetpoint();
        editSetpoint_ = editStart_;
    }
    else
    {
        float target = (direction > 0.0f) ? floorf(editSetpoint_) + kNudgeStep
                                          : ceilf(editSetpoint_)  - kNudgeStep;
        if (target < ClimateManager::kSetpointMin) target = ClimateManager::kSetpointMin;
        if (target > ClimateManager::kSetpointMax) target = ClimateManager::kSetpointMax;
        editSetpoint_ = target;
    }

    if (commitTimer_) lv_timer_reset(commitTimer_);
    else commitTimer_ = lv_timer_create(CommitTimerCb, kCommitMs, this);
}

// Ends an edit. Only now does the dialled value reach the climate loop, and
// from there OpenTherm — and only if it moved: a guest who touched -/+ just to
// look changes nothing, not even an override that landed meanwhile.
void HomeScreen::FinishEdit()
{
    if (commitTimer_)
    {
        lv_timer_delete(commitTimer_);
        commitTimer_ = nullptr;
    }
    if (!editing_) return;
    editing_ = false;

    if (fabsf(editSetpoint_ - editStart_) > 0.001f)
        serviceProvider_.getClimateManager().SetUserSetpoint(editSetpoint_);
}

void HomeScreen::OnIntent(HomeIntent intent)
{
    switch (intent)
    {
    case HomeIntent::NudgeDown: Nudge(-1.0f); break;
    case HomeIntent::NudgeUp:   Nudge(+1.0f); break;

    // The mode is the guest's choice. ClimateManager turns it into which
    // requests go out; the gateway's timers still decide when it runs.
    case HomeIntent::SetAuto:
        serviceProvider_.getClimateManager().SetMode(ClimateMode::Auto);
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

void HomeScreen::CommitTimerCb(lv_timer_t* t)
{
    auto* self = static_cast<HomeScreen*>(lv_timer_get_user_data(t));
    self->FinishEdit();   // deletes t
    self->Refresh();
}

void HomeScreen::IntentTrampoline(void* user, HomeIntent intent)
{
    static_cast<HomeScreen*>(user)->OnIntent(intent);
}
