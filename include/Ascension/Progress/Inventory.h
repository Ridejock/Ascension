#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Data/Database.h"

namespace Ascension {

// One stack in the bag. An empty stack has a blank id and a count of 0, and it keeps its place
// so the number keys in a fight stay on the same consumable.
struct ItemStack {
    std::string Id;
    i32 Count = 0;

    [[nodiscard]] bool Empty() const { return Id.empty() || Count <= 0; }
};

// The hero's gold, 12-slot bag, and worn gear. Item ids are stored, not pointers, so a reload
// of items.json does not dangle. Worn gear is applied by the battle as a stat bonus. A
// two-handed weapon blocks the offhand. Consumables are not worn; a fight drinks them.
class Inventory {
public:
    // An empty bag and no gold. Fails if shop.json did not load a bag.
    bool Start(const Database& data, std::string& error);

    // Keeps gold, worn gear, and stacks. A smaller bag drops the stacks that no longer fit. A
    // smaller stack size clamps each count. If the worn weapon is now two-handed, the offhand
    // piece is moved into the bag so its bonus stops; a full bag sets the last stack down to
    // make a slot, and that note is left in `error` while this still succeeds. Missing item
    // ids stay put and add no stats.
    bool ApplyData(std::string& error);

    [[nodiscard]] bool IsReady() const { return m_Data != nullptr; }
    [[nodiscard]] i32 Gold() const { return m_Gold; }
    [[nodiscard]] i32 BagSlots() const { return static_cast<i32>(m_Bag.size()); }

    // Out of range is an empty stack.
    [[nodiscard]] const ItemStack& BagAt(i32 index) const;
    // Weapon, offhand, armor, or accessory. Empty when nothing is worn. A consumable slot is
    // never worn.
    [[nodiscard]] const std::string& Equipped(GearSlot slot) const;
    [[nodiscard]] bool OffhandBlocked() const;

    // Sum of the worn pieces' bonuses, including negatives. A two-handed weapon skips the
    // offhand piece, so a shield left there adds nothing. Unknown ids add nothing.
    [[nodiscard]] StatBlock GearBonus() const;

    // Bag indexes of every consumable stack, in bag order. The fight's number keys use the
    // ones that fit after the skills. The item picker uses the whole list.
    [[nodiscard]] std::vector<i32> ConsumableSlots() const;

    void GrantGold(i32 amount);
    // Replaces the purse. Negative gold is refused, and so is a purse above the same cap
    // GrantGold stops at.
    bool SetGold(i32 amount, std::string& error);

    // Puts `count` of `itemId` into the bag, stacking into an open stack of the same id first.
    // Unknown ids fail and the message names the id. Nothing is added when the bag cannot hold
    // the whole count.
    bool Give(std::string_view itemId, i32 count, std::string& error);

    // Buys one from Pell's stock into the first stack that has room, or an empty slot.
    bool Buy(std::string_view itemId, std::string& error);
    // Sells one from a bag stack. Worn gear has to come off first.
    bool Sell(i32 bagIndex, std::string& error);
    // Moves one from the bag onto its slot. A two-handed weapon sends the offhand back to the
    // bag, and refuses if that piece would not fit.
    bool Equip(i32 bagIndex, std::string& error);
    // Takes a worn piece off. The offhand always comes off when something is there, including
    // while a two-handed weapon is worn. A full bag sets the last stack down to make room, and
    // that note is left in `error` while this still succeeds. Other slots refuse when the bag
    // is full.
    bool Unequip(GearSlot slot, std::string& error);
    // Removes one consumable after the fight has accepted the drink.
    bool Take(i32 bagIndex, std::string& error);

private:
    [[nodiscard]] const ShopDef* Shop() const;
    [[nodiscard]] i32 StackCap() const;
    // Puts one of `id` into `bag`. False when every stack is full.
    bool TryGive(std::vector<ItemStack>& bag, std::string_view id) const;
    // Puts one of `id` into `bag`. When every stack is full, the last occupied stack is cleared
    // first. `dropped` is set when that happens. False only when `bag` has no slots.
    bool GiveOrDropLast(std::vector<ItemStack>& bag, std::string_view id, bool& dropped) const;

    const Database* m_Data = nullptr;
    i32 m_Gold = 0;
    std::string m_Weapon;
    std::string m_Offhand;
    std::string m_Armor;
    std::string m_Accessory;
    std::vector<ItemStack> m_Bag;
};

} // namespace Ascension
