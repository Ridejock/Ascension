#include "Ascension/Input/Controls.h"

#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "Emerald/Input/Input.h"
#include "Emerald/Input/InputBindings.h"

namespace Ascension {

bool BoundKeyNeedsPlayerTurn(const Emerald::Input& input, Emerald::Key key)
{
    const std::vector<std::string_view> actions = input.FindActionsUsing(key);
    for (const std::string_view action : actions) {
        if (ActionNeedsPlayerTurn(action))
            return true;
    }
    return false;
}

const ControlBind* FindControl(const ControlRules& rules, std::string_view name)
{
    for (const ControlBind& bind : rules.Actions) {
        if (bind.Name == name)
            return &bind;
    }
    return nullptr;
}

namespace {

bool ResolveKeys(const ControlBind& bind, std::vector<Emerald::Key>& keys, std::string& error)
{
    keys.clear();
    for (const std::string& name : bind.Keys) {
        const std::optional<Emerald::Key> key = Emerald::FindKey(name);
        if (!key) {
            error = "input.json: action '" + bind.Name + "': unknown key '" + name + "'";
            return false;
        }
        keys.push_back(*key);
    }
    return true;
}

bool ResolveButtons(const ControlBind& bind, std::vector<Emerald::GamepadButton>& buttons,
                    std::string& error)
{
    buttons.clear();
    for (const std::string& name : bind.Buttons) {
        const std::optional<Emerald::GamepadButton> button = Emerald::FindGamepadButton(name);
        if (!button) {
            error = "input.json: action '" + bind.Name + "': unknown button '" + name + "'";
            return false;
        }
        buttons.push_back(*button);
    }
    return true;
}

} // namespace

bool ApplyControls(Emerald::Input& input, const ControlRules& rules, std::string& error)
{
    struct Resolved {
        std::string Name;
        std::vector<Emerald::Key> Keys;
        std::vector<Emerald::GamepadButton> Buttons;
    };
    std::vector<Resolved> resolved;
    resolved.reserve(rules.Actions.size());
    for (const ControlBind& bind : rules.Actions) {
        Resolved row;
        row.Name = bind.Name;
        if (!ResolveKeys(bind, row.Keys, error) || !ResolveButtons(bind, row.Buttons, error))
            return false;
        resolved.push_back(std::move(row));
    }
    for (const Resolved& row : resolved) {
        input.RebindAction(row.Name, std::span<const Emerald::Key>(row.Keys));
        input.RebindAction(row.Name, std::span<const Emerald::GamepadButton>(row.Buttons));
    }
    return true;
}

} // namespace Ascension
